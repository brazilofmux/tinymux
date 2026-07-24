/*! \file websocket.cpp
 * \brief WebSocket protocol support (RFC 6455).
 *
 * Handles HTTP upgrade handshake, frame encoding/decoding, and control
 * frames (ping/pong/close).  All functions operate on DESC structs
 * in the driver layer.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "driver_log.h"  // STARTLOG via g_pILog (not mudconf)

#include "interface.h"
#include "websocket.h"
#include "ganl_adapter.h"  // ganl_close_connection (RFC 6455 close handling)
#include "sha1.h"

#include <cstring>
#include <algorithm>

// RFC 6455 Section 4.2.2: the WebSocket GUID appended to client key.
//
static const char WS_GUID[] = "258EAFA5-E914-47DA-95CA-5AB5AA98CA57";

// Path we accept for WebSocket upgrade.
//
static const char WS_PATH[] = "/wsclient";

// --------------------------------------------------------------------------
// Base64 encoding (minimal, for handshake only)
// --------------------------------------------------------------------------

static const char b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const uint8_t *in, size_t len, char *out)
{
    size_t i = 0;
    size_t o = 0;
    while (i + 2 < len)
    {
        uint32_t v = (in[i] << 16) | (in[i+1] << 8) | in[i+2];
        out[o++] = b64[(v >> 18) & 0x3F];
        out[o++] = b64[(v >> 12) & 0x3F];
        out[o++] = b64[(v >>  6) & 0x3F];
        out[o++] = b64[(v      ) & 0x3F];
        i += 3;
    }
    if (i + 1 == len)
    {
        uint32_t v = in[i] << 16;
        out[o++] = b64[(v >> 18) & 0x3F];
        out[o++] = b64[(v >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    }
    else if (i + 2 == len)
    {
        uint32_t v = (in[i] << 16) | (in[i+1] << 8);
        out[o++] = b64[(v >> 18) & 0x3F];
        out[o++] = b64[(v >> 12) & 0x3F];
        out[o++] = b64[(v >>  6) & 0x3F];
        out[o++] = '=';
    }
    out[o] = '\0';
}

// --------------------------------------------------------------------------
// HTTP header parsing helpers
// --------------------------------------------------------------------------

// Case-insensitive header value lookup.
// Returns pointer to the value (after ": "), or nullptr.
//
static const char *find_header(const char *headers, const char *name)
{
    size_t nlen = strlen(name);
    const char *p = headers;
    while (*p)
    {
        // Find line start.
        //
#if defined(WIN32)
        if (  0 == _strnicmp(p, name, nlen)
#else
        if (  0 == strncasecmp(p, name, nlen)
#endif
           && p[nlen] == ':')
        {
            const char *v = p + nlen + 1;
            while (*v == ' ' || *v == '\t')
            {
                v++;
            }
            return v;
        }
        // Skip to next line.
        //
        while (*p && *p != '\n')
        {
            p++;
        }
        if (*p == '\n')
        {
            p++;
        }
    }
    return nullptr;
}

// Extract a header value up to \r or \n.
//
static std::string get_header_value(const char *headers, const char *name)
{
    const char *v = find_header(headers, name);
    if (!v)
    {
        return {};
    }
    const char *end = v;
    while (*end && *end != '\r' && *end != '\n')
    {
        end++;
    }
    return std::string(v, end - v);
}

// --------------------------------------------------------------------------
// Detection
// --------------------------------------------------------------------------

bool ws_is_upgrade_request(const char *data, size_t len)
{
    // Minimum: "GET / HTTP/1.1\r\n" = 16 bytes.
    // But we just need to see "GET " at the start.
    //
    return len >= 4
        && data[0] == 'G'
        && data[1] == 'E'
        && data[2] == 'T'
        && data[3] == ' ';
}

// --------------------------------------------------------------------------
// Handshake
// --------------------------------------------------------------------------

// #1082: reject HTTP upgrade — queue response, flush, close.  Always
// returns true so the caller treats the handshake as finished (failed)
// and does not re-enter with DS_WEBSOCKET_HS.  After this returns, d
// may be freed; the caller must not touch it.
//
static bool ws_handshake_reject(DESC *d, const char *reject)
{
    queue_write_LEN(d, reinterpret_cast<const UTF8 *>(reject), strlen(reject));
    process_output(d, false);
    ganl_close_connection(d, R_QUIT);
    return true;
}

bool ws_process_handshake(DESC *d, const char *data, size_t len)
{
    // Accumulate data until we have the complete HTTP header
    // (terminated by \r\n\r\n).
    //
    d->ws->handshake_buf.append(data, len);

    // Safety: don't let the handshake buffer grow unbounded.
    //
    if (d->ws->handshake_buf.size() > 4096)
    {
        // Reject: headers too large.
        //
        return ws_handshake_reject(d,
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n");
    }

    // Check for end of headers.
    //
    size_t hdrEnd = d->ws->handshake_buf.find("\r\n\r\n");
    if (hdrEnd == std::string::npos)
    {
        return false; // need more data
    }

    const std::string &hdr = d->ws->handshake_buf;

    // Validate request line: "GET /wsclient HTTP/1.1\r\n"
    //
    bool pathOk = false;
    if (hdr.compare(0, 4, "GET ") == 0)
    {
        size_t pathEnd = hdr.find(' ', 4);
        if (pathEnd != std::string::npos)
        {
            std::string path = hdr.substr(4, pathEnd - 4);
            pathOk = (path == WS_PATH || path == "/");
        }
    }

    if (!pathOk)
    {
        return ws_handshake_reject(d,
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n");
    }

    // Check required headers.
    //
    std::string upgrade = get_header_value(hdr.c_str(), "Upgrade");
    std::string connection = get_header_value(hdr.c_str(), "Connection");
    std::string wsKey = get_header_value(hdr.c_str(), "Sec-WebSocket-Key");
    std::string wsVersion = get_header_value(hdr.c_str(), "Sec-WebSocket-Version");

    // Upgrade must contain "websocket" (case-insensitive).
    //
    bool upgradeOk = false;
    {
        std::string lower = upgrade;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        upgradeOk = (lower.find("websocket") != std::string::npos);
    }

    // Connection must contain "Upgrade" (case-insensitive).
    //
    bool connectionOk = false;
    {
        std::string lower = connection;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        connectionOk = (lower.find("upgrade") != std::string::npos);
    }

    if (!upgradeOk || !connectionOk || wsKey.empty())
    {
        return ws_handshake_reject(d,
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n");
    }

    // Version must be 13 (RFC 6455).
    //
    if (wsVersion != "13")
    {
        return ws_handshake_reject(d,
            "HTTP/1.1 426 Upgrade Required\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n");
    }

    // Compute accept key: SHA1(client_key + GUID), then base64.
    //
    std::string acceptInput = wsKey + WS_GUID;
    const UTF8 *parts[1] = {
        reinterpret_cast<const UTF8 *>(acceptInput.c_str())
    };
    size_t lens[1] = { acceptInput.size() };
    uint8_t digest[20];
    unsigned int digest_len = 0;
    if (!mux_sha1_digest(parts, lens, 1, digest, &digest_len) || digest_len != 20)
    {
        // #1082: crypto failure is fatal, not "need more data".
        //
        return ws_handshake_reject(d,
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n");
    }

    char acceptKey[29]; // ceil(20 * 4/3) + 1 = 29
    base64_encode(digest, digest_len, acceptKey);

    // Send 101 Switching Protocols.
    //
    char response[256];
    int n = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        acceptKey);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(response))
    {
        // Formatting failed or would have been truncated. Given the
        // fixed format and the 28-character acceptKey this should be
        // impossible, but fail closed rather than sending a malformed
        // or unterminated 101 response — or casting a negative
        // snprintf return to a huge size_t and reading past the
        // stack buffer.
        //
        return ws_handshake_reject(d,
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n");
    }
    queue_write_LEN(d, reinterpret_cast<const UTF8 *>(response),
                    static_cast<size_t>(n));

    // Mark as fully upgraded.
    //
    d->flags |= DS_WEBSOCKET;

    // Check for trailing data after \r\n\r\n (WebSocket frames sent
    // before we finished reading the headers — unusual but valid).
    //
    size_t remainder = hdrEnd + 4;
    std::string trailing;
    if (remainder < d->ws->handshake_buf.size())
    {
        trailing = d->ws->handshake_buf.substr(remainder);
    }

    // Clear the handshake buffer — no longer needed.
    //
    d->ws->handshake_buf.clear();
    d->ws->handshake_buf.shrink_to_fit();

    // Now send the welcome screen.
    //
    welcome_user(d);

    // Process any trailing data as WebSocket frames.
    //
    if (!trailing.empty())
    {
        ws_process_input(d, trailing.data(), trailing.size());
    }

    return true; // handshake complete
}

// --------------------------------------------------------------------------
// Frame encoding (server → client)
// --------------------------------------------------------------------------

void ws_queue_frame(DESC *d, const uint8_t *data, size_t len, uint8_t opcode)
{
    // Server frames are never masked (RFC 6455 Section 5.1).
    //
    // Frame header: 2-10 bytes.
    //
    uint8_t hdr[10];
    size_t hdrlen = 0;

    hdr[0] = 0x80 | (opcode & 0x0F); // FIN + opcode
    hdrlen++;

    if (len < 126)
    {
        hdr[1] = static_cast<uint8_t>(len);
        hdrlen = 2;
    }
    else if (len <= 65535)
    {
        hdr[1] = 126;
        hdr[2] = static_cast<uint8_t>((len >> 8) & 0xFF);
        hdr[3] = static_cast<uint8_t>((len     ) & 0xFF);
        hdrlen = 4;
    }
    else
    {
        // 64-bit extended length (RFC 6455 §5.2): 8-byte big-endian
        // unsigned, high bit MUST be 0. Promote to uint64_t so the
        // high-word shifts are well-defined regardless of size_t
        // width (on 32-bit size_t, `len >> 32` would be undefined).
        //
        const uint64_t len64 = len;
        hdr[1] = 127;
        hdr[2] = static_cast<uint8_t>((len64 >> 56) & 0xFF);
        hdr[3] = static_cast<uint8_t>((len64 >> 48) & 0xFF);
        hdr[4] = static_cast<uint8_t>((len64 >> 40) & 0xFF);
        hdr[5] = static_cast<uint8_t>((len64 >> 32) & 0xFF);
        hdr[6] = static_cast<uint8_t>((len64 >> 24) & 0xFF);
        hdr[7] = static_cast<uint8_t>((len64 >> 16) & 0xFF);
        hdr[8] = static_cast<uint8_t>((len64 >>  8) & 0xFF);
        hdr[9] = static_cast<uint8_t>((len64      ) & 0xFF);
        hdrlen = 10;
    }

    // Queue header + payload as a single write.  #1083: enforce the same
    // output_limit / drop-oldest policy as queue_write_LEN, accounting
    // framed size (hdr + payload) so control-frame floods cannot grow
    // the queue without bound.  Skip when output_limit is unset (0).
    //
    const size_t framed = hdrlen + len;
    if (g_dc.output_limit > 0)
    {
        if (static_cast<size_t>(g_dc.output_limit) < d->output_size + framed)
        {
            process_output(d, false);
        }
        while (  static_cast<size_t>(g_dc.output_limit) < d->output_size + framed
              && !d->output_queue.empty())
        {
            const size_t nchars = d->output_queue.front().size();
            STARTLOG(LOG_NET, "NET", "WRITE");
            UTF8 *buf = alloc_lbuf("ws_queue_frame.LOG");
            mux_sprintf(buf, LBUF_SIZE,
                T("[%u/%s] Output buffer overflow, %zu chars discarded by "),
                d->socket, d->addr, nchars);
            g_pILog->log_text(buf);
            free_lbuf(buf);
            if (d->flags & DS_CONNECTED)
            {
                g_pILog->log_name(d->player);
            }
            ENDLOG;
            d->output_size -= nchars;
            d->output_lost += nchars;
            d->output_queue.pop_front();
        }
    }

    std::string frame(reinterpret_cast<const char *>(hdr), hdrlen);
    frame.append(reinterpret_cast<const char *>(data), len);

    d->output_queue.emplace_back(std::move(frame));
    d->output_size += framed;
}

void ws_send_close(DESC *d, uint16_t code)
{
    uint8_t payload[2];
    payload[0] = static_cast<uint8_t>((code >> 8) & 0xFF);
    payload[1] = static_cast<uint8_t>((code     ) & 0xFF);
    ws_queue_frame(d, payload, 2, WS_OPCODE_CLOSE);
}

// --------------------------------------------------------------------------
// Frame decoding (client → server)
// --------------------------------------------------------------------------

// Frame parser states.
//
enum {
    WS_PARSE_HEADER1 = 0,  // reading first 2 bytes
    WS_PARSE_LEN16,        // reading 2-byte extended length
    WS_PARSE_LEN64,        // reading 8-byte extended length
    WS_PARSE_MASK,         // reading 4-byte mask key
    WS_PARSE_PAYLOAD       // reading payload bytes
};

// Fail the connection per RFC 6455 §7.1.7: send a Close frame with the given
// status code, flush it toward the client, then close the connection.  Every
// caller MUST return from ws_process_input immediately afterward without
// touching d or ws: closing is safe here because the GANL event-dispatch loop
// holds a shared_ptr that outlives close() (ganl_adapter.cpp), but the close
// path can free d / d->ws synchronously, so the parser must not continue.
//
static void ws_fail(DESC *d, uint16_t code)
{
    ws_send_close(d, code);
    process_output(d, false);
    ganl_close_connection(d, R_QUIT);
}

// Strict UTF-8 well-formedness check (RFC 3629) for RFC 6455 §8.1 TEXT-frame
// validation.  Rejects overlong encodings, surrogates, code points above
// U+10FFFF, stray/short continuation bytes, and invalid lead bytes.  The whole
// assembled message is validated at once, so a multibyte sequence split across
// fragments (already concatenated into frag_buf before this runs) validates
// correctly.
//
static bool ws_utf8_valid(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n)
    {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t extra;
        unsigned int cp;
        unsigned int lo;

        if (c < 0x80)
        {
            i++;
            continue;
        }
        else if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; lo = 0x80; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; lo = 0x800; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; lo = 0x10000; }
        else { return false; } // stray continuation byte or invalid lead

        if (i + extra >= n)
        {
            return false; // truncated multibyte sequence
        }
        for (size_t k = 1; k <= extra; k++)
        {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80)
            {
                return false; // not a continuation byte
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (  cp < lo                          // overlong
           || cp > 0x10FFFF                    // out of range
           || (cp >= 0xD800 && cp <= 0xDFFF))  // UTF-16 surrogate
        {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

void ws_process_input(DESC *d, const char *data, size_t len)
{
    ws_state *ws = d->ws;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(data);
    const uint8_t *end = p + len;

    // A frame transitions into WS_PARSE_PAYLOAD on the same iteration that
    // consumes its last header/length/mask byte.  A zero-length payload
    // needs no further input, but with a plain `p < end` loop it is only
    // dispatched on the *next* iteration — which never comes if the frame
    // ended exactly at the read boundary (p == end).  A zero-payload CLOSE
    // (or PING) would then stall: the client waits for our echo while we
    // wait for bytes that never arrive.  Keep looping while the current
    // payload is already complete so the trailing zero-length frame is
    // dispatched.  This is a no-op for non-empty payloads (their completing
    // iteration consumes bytes and dispatches in the same pass) and cannot
    // spin: dispatch resets parse_state to WS_PARSE_HEADER1.
    //
    while (p < end
           || (WS_PARSE_PAYLOAD == ws->parse_state
               && ws->frame_buf.size() >= ws->frame_expected))
    {
        switch (ws->parse_state)
        {
        case WS_PARSE_HEADER1:
            {
                ws->frame_buf.push_back(static_cast<char>(*p++));
                if (ws->frame_buf.size() < 2)
                {
                    continue;
                }

                uint8_t b0 = static_cast<uint8_t>(ws->frame_buf[0]);
                uint8_t b1 = static_cast<uint8_t>(ws->frame_buf[1]);

                ws->frame_fin = (b0 & 0x80) != 0;
                ws->frame_opcode = b0 & 0x0F;
                ws->frame_masked = (b1 & 0x80) != 0;

                uint8_t lenByte = b1 & 0x7F;
                ws->frame_buf.clear();

                // RFC 6455 §5.1: client→server frames MUST be masked; the
                // server MUST fail the connection otherwise (#1081).
                //
                if (!ws->frame_masked)
                {
                    ws_fail(d, WS_CLOSE_PROTOCOL_ERR);
                    return;
                }

                // RFC 6455 §5.2: the reserved bits RSV1/2/3 MUST be 0 unless an
                // extension that defines them has been negotiated.  We negotiate
                // no extensions, so any set reserved bit fails the connection.
                //
                if ((b0 & 0x70) != 0)
                {
                    ws_fail(d, WS_CLOSE_PROTOCOL_ERR);
                    return;
                }

                // RFC 6455 §5.5: control frames (opcodes 0x8-0xF)
                // MUST have FIN=1 and payload length ≤ 125. Enforce
                // before entering any extended-length state so an
                // adversarial control frame cannot pollute the
                // continuation buffer or trigger echo amplification
                // (a fragmented PING or 2 MiB CLOSE was previously
                // accepted and echoed back verbatim via
                // ws_queue_frame).
                //
                const bool isControl = (ws->frame_opcode & 0x08) != 0;
                if (isControl && (!ws->frame_fin || lenByte >= 126))
                {
                    ws_fail(d, WS_CLOSE_PROTOCOL_ERR);
                    return;
                }

                if (lenByte < 126)
                {
                    ws->frame_expected = lenByte;
                    ws->parse_state = ws->frame_masked ? WS_PARSE_MASK : WS_PARSE_PAYLOAD;
                }
                else if (lenByte == 126)
                {
                    ws->parse_state = WS_PARSE_LEN16;
                }
                else
                {
                    ws->parse_state = WS_PARSE_LEN64;
                }
            }
            break;

        case WS_PARSE_LEN16:
            {
                ws->frame_buf.push_back(static_cast<char>(*p++));
                if (ws->frame_buf.size() < 2)
                {
                    continue;
                }
                ws->frame_expected =
                    (static_cast<size_t>(static_cast<uint8_t>(ws->frame_buf[0])) << 8) |
                     static_cast<size_t>(static_cast<uint8_t>(ws->frame_buf[1]));
                ws->frame_buf.clear();
                ws->parse_state = ws->frame_masked ? WS_PARSE_MASK : WS_PARSE_PAYLOAD;
            }
            break;

        case WS_PARSE_LEN64:
            {
                ws->frame_buf.push_back(static_cast<char>(*p++));
                if (ws->frame_buf.size() < 8)
                {
                    continue;
                }
                // Accumulate into an explicit uint64_t and bound-check BEFORE
                // narrowing to frame_expected (size_t).  On a 32-bit build
                // size_t is 32 bits, so shifting all 8 bytes through it would
                // discard the top 4 bytes first: a length like
                // 0x0000000100000005 would truncate to 5, pass the cap, and the
                // parser would mis-frame the rest of the stream (F5).
                //
                uint64_t len64 = 0;
                for (int i = 0; i < 8; i++)
                {
                    len64 = (len64 << 8) |
                        static_cast<uint64_t>(static_cast<uint8_t>(ws->frame_buf[i]));
                }
                ws->frame_buf.clear();

                if (len64 > WS_MAX_PAYLOAD)
                {
                    ws_fail(d, WS_CLOSE_PROTOCOL_ERR);
                    return;
                }
                ws->frame_expected = static_cast<size_t>(len64);
                ws->parse_state = ws->frame_masked ? WS_PARSE_MASK : WS_PARSE_PAYLOAD;
            }
            break;

        case WS_PARSE_MASK:
            {
                ws->frame_buf.push_back(static_cast<char>(*p++));
                if (ws->frame_buf.size() < 4)
                {
                    continue;
                }
                for (int i = 0; i < 4; i++)
                {
                    ws->frame_mask[i] = static_cast<uint8_t>(ws->frame_buf[i]);
                }
                ws->frame_buf.clear();
                ws->parse_state = WS_PARSE_PAYLOAD;
            }
            break;

        case WS_PARSE_PAYLOAD:
            {
                // Consume as many payload bytes as available.
                //
                size_t need = ws->frame_expected - ws->frame_buf.size();
                size_t avail = static_cast<size_t>(end - p);
                size_t take = (std::min)(need, avail);

                ws->frame_buf.append(reinterpret_cast<const char *>(p), take);
                p += take;

                if (ws->frame_buf.size() < ws->frame_expected)
                {
                    continue; // need more data
                }

                // Unmask the payload.
                //
                if (ws->frame_masked)
                {
                    for (size_t i = 0; i < ws->frame_buf.size(); i++)
                    {
                        ws->frame_buf[i] ^= ws->frame_mask[i & 3];
                    }
                }

                // Bound assembled fragmented messages at
                // WS_MAX_PAYLOAD. Each individual frame is already
                // capped there, but without this check a client
                // could send thousands of small non-FIN fragments
                // and push frag_buf arbitrarily large — a memory-
                // exhaustion DoS from a single connection.
                //
                auto frag_would_overflow = [&](size_t add) {
                    return ws->frag_buf.size() + add > WS_MAX_PAYLOAD;
                };

                switch (ws->frame_opcode)
                {
                case WS_OPCODE_TEXT:
                case WS_OPCODE_BINARY:
                    // RFC 6455 §5.4: a TEXT/BINARY frame begins a new message,
                    // so receiving one while a fragmented message is still in
                    // progress is a protocol error — the continuation must use
                    // opcode 0.  Validate the state rather than silently
                    // discarding the in-progress fragment (F2).
                    //
                    if (0 != ws->frag_opcode)
                    {
                        ws->frag_buf.clear();
                        ws->frag_opcode = 0;
                        ws_fail(d, WS_CLOSE_PROTOCOL_ERR);
                        return;
                    }
                    if (ws->frame_fin)
                    {
                        // Complete single-frame message.  RFC 6455 §8.1: a TEXT
                        // message must be valid UTF-8 (F4); BINARY is exempt.
                        //
                        if (  WS_OPCODE_TEXT == ws->frame_opcode
                           && !ws_utf8_valid(ws->frame_buf.c_str(), ws->frame_buf.size()))
                        {
                            ws_fail(d, WS_CLOSE_BAD_DATA);
                            return;
                        }
                        save_command(d,
                            reinterpret_cast<const UTF8 *>(ws->frame_buf.c_str()),
                            ws->frame_buf.size());
                    }
                    else
                    {
                        // First fragment. frame_buf is already bounded to
                        // WS_MAX_PAYLOAD by the header parser, so this is safe.
                        //
                        ws->frag_opcode = ws->frame_opcode;
                        ws->frag_buf = ws->frame_buf;
                    }
                    break;

                case WS_OPCODE_CONTINUATION:
                    // RFC 6455 §5.4: a CONTINUATION frame is only valid while a
                    // fragmented message is in progress (F2).
                    //
                    if (0 == ws->frag_opcode)
                    {
                        ws_fail(d, WS_CLOSE_PROTOCOL_ERR);
                        return;
                    }
                    if (frag_would_overflow(ws->frame_buf.size()))
                    {
                        ws->frag_buf.clear();
                        ws->frag_opcode = 0;
                        ws_fail(d, WS_CLOSE_MESSAGE_TOO_BIG);
                        return;
                    }
                    ws->frag_buf.append(ws->frame_buf);
                    if (ws->frame_fin)
                    {
                        // §8.1: validate the assembled message if it began as a
                        // TEXT message (F4).  The whole frag_buf is checked at
                        // once, so a multibyte sequence split across fragments
                        // validates correctly.
                        //
                        if (  WS_OPCODE_TEXT == ws->frag_opcode
                           && !ws_utf8_valid(ws->frag_buf.c_str(), ws->frag_buf.size()))
                        {
                            ws->frag_buf.clear();
                            ws->frag_opcode = 0;
                            ws_fail(d, WS_CLOSE_BAD_DATA);
                            return;
                        }
                        save_command(d,
                            reinterpret_cast<const UTF8 *>(ws->frag_buf.c_str()),
                            ws->frag_buf.size());
                        ws->frag_buf.clear();
                        ws->frag_opcode = 0;
                    }
                    break;

                case WS_OPCODE_PING:
                    // Reply with pong containing the same payload.
                    //
                    ws_queue_frame(d,
                        reinterpret_cast<const uint8_t *>(ws->frame_buf.c_str()),
                        ws->frame_buf.size(),
                        WS_OPCODE_PONG);
                    break;

                case WS_OPCODE_PONG:
                    // Unsolicited pong — ignore.
                    //
                    break;

                case WS_OPCODE_CLOSE:
                    // RFC 6455 §5.5.1: respond to the client's Close with a
                    // Close frame, then close the connection — do not keep
                    // processing subsequent frames (F1).  Echo the received
                    // close payload (status code + optional reason), already
                    // bounded to <=125 bytes by the control-frame check.  Safe
                    // to close here (see ws_fail): return immediately and touch
                    // neither d nor ws afterward.
                    //
                    ws_queue_frame(d,
                        reinterpret_cast<const uint8_t *>(ws->frame_buf.c_str()),
                        ws->frame_buf.size(),
                        WS_OPCODE_CLOSE);
                    process_output(d, false);
                    ganl_close_connection(d, R_QUIT);
                    return;

                default:
                    // Unknown opcode — protocol error.
                    //
                    ws_fail(d, WS_CLOSE_PROTOCOL_ERR);
                    return;
                }

                // Reset for next frame.
                //
                ws->frame_buf.clear();
                ws->frame_expected = 0;
                ws->parse_state = WS_PARSE_HEADER1;
            }
            break;
        }
    }
}
