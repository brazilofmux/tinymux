#include "websocket.h"
#include "base64.h"
#include "utf8_utils.h"
#include <openssl/sha.h>
#include <cstring>
#include <algorithm>

// RFC 6455 GUID
static const char WS_GUID[] = "258EAFA5-E914-47DA-95CA-5AB5AA98CA57";

// ---- HTTP header helpers ----

static const char* findHeader(const char* headers, const char* name) {
    size_t nlen = strlen(name);
    const char* p = headers;
    while (*p) {
        if (strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            const char* v = p + nlen + 1;
            while (*v == ' ' || *v == '\t') v++;
            return v;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return nullptr;
}

static std::string headerValue(const char* v) {
    if (!v) return "";
    std::string s;
    while (*v && *v != '\r' && *v != '\n') {
        s.push_back(*v++);
    }
    return s;
}

// ---- Handshake ----

std::string wsProcessHandshake(WsState& ws, const char* data, size_t len) {
    ws.handshakeBuf.append(data, len);

    // Look for end of HTTP headers
    auto pos = ws.handshakeBuf.find("\r\n\r\n");
    if (pos == std::string::npos) {
        if (ws.handshakeBuf.size() > 4096) {
            ws.handshakeComplete = true;
            ws.handshakeOk = false;
            return "HTTP/1.1 400 Bad Request\r\n\r\n";
        }
        return "";  // need more data
    }

    ws.handshakeComplete = true;

    // Null-terminate for header parsing
    std::string hdrs = ws.handshakeBuf.substr(0, pos + 4);

    // Validate required headers
    const char* upgrade = findHeader(hdrs.c_str(), "Upgrade");
    const char* connection = findHeader(hdrs.c_str(), "Connection");
    const char* key = findHeader(hdrs.c_str(), "Sec-WebSocket-Key");
    const char* version = findHeader(hdrs.c_str(), "Sec-WebSocket-Version");

    std::string upgradeVal = headerValue(upgrade);
    std::string connectionVal = headerValue(connection);
    std::string keyVal = headerValue(key);
    std::string versionVal = headerValue(version);

    // Case-insensitive check
    std::string upgradeLower = upgradeVal;
    std::transform(upgradeLower.begin(), upgradeLower.end(),
                   upgradeLower.begin(), ::tolower);

    if (upgradeLower != "websocket" || keyVal.empty() || versionVal != "13") {
        ws.handshakeOk = false;
        return "HTTP/1.1 400 Bad Request\r\n\r\n";
    }

    // Compute accept hash: SHA1(key + GUID), base64
    std::string accept_input = keyVal + WS_GUID;
    uint8_t sha[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const uint8_t*>(accept_input.data()),
         accept_input.size(), sha);
    std::string accept = base64Encode(sha, SHA_DIGEST_LENGTH);

    // Check for hydra-gamesession subprotocol
    const char* subproto = findHeader(hdrs.c_str(), "Sec-WebSocket-Protocol");
    std::string subprotoVal = headerValue(subproto);
    ws.isGameSession = (subprotoVal.find("hydra-gamesession") != std::string::npos);

    ws.handshakeOk = true;

    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n";

    if (ws.isGameSession) {
        response += "Sec-WebSocket-Protocol: hydra-gamesession\r\n";
    }

    response += "\r\n";

    // Any trailing data after headers needs to be fed back as frames
    // (stored in handshakeBuf for caller to re-process)
    ws.handshakeBuf = ws.handshakeBuf.substr(pos + 4);

    return response;
}

// ---- Frame decoding ----

std::vector<WsMessage> wsDecodeFrames(WsState& ws, const char* data,
                                       size_t len, std::string& responses) {
    std::vector<WsMessage> messages;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
    const uint8_t* pe = p + len;

    // Also enter when a zero-length payload is already complete after the
    // mask key (or unmasked header): otherwise empty CLOSE/PING never run.
    while (p < pe
           || (ws.parseState == WsState::Payload
               && ws.frameBuf.size() == ws.payloadLen)) {
        switch (ws.parseState) {
        case WsState::Header1: {
            // #1095: RFC 6455 §5.2 — RSV1–3 must be 0 (no extensions negotiated).
            if ((*p & 0x70) != 0) {
                responses += wsCloseFrame(1002);
                messages.push_back({WS_OP_CLOSE, {}});
                ws.parseState = WsState::Header1;
                return messages;
            }
            ws.fin = (*p & 0x80) != 0;
            ws.opcode = *p & 0x0F;
            ws.parseState = WsState::Header2;
            p++;
        } break;

        case WsState::Header2: {
            ws.masked = (*p & 0x80) != 0;
            uint8_t len7 = *p & 0x7F;
            p++;

            // Client-to-server frames must be masked.
            if (!ws.masked) {
                // #1094: surface CLOSE so the session path tears down the FD.
                responses += wsCloseFrame(1002);
                messages.push_back({WS_OP_CLOSE, {}});
                ws.parseState = WsState::Header1;
                return messages;
            }

            // #1095: RFC 6455 §5.5 — control frames (0x8–0xF) must have FIN=1
            // and payload length ≤ 125 *before* extended length, so a large
            // masked PING cannot be accepted and echoed as a 64 KiB PONG.
            const bool isControl = (ws.opcode & 0x08) != 0;
            if (isControl && (!ws.fin || len7 >= 126)) {
                responses += wsCloseFrame(1002);
                messages.push_back({WS_OP_CLOSE, {}});
                ws.parseState = WsState::Header1;
                return messages;
            }

            if (len7 < 126) {
                ws.payloadLen = len7;
                ws.frameBuf.clear();
                ws.maskIdx = 0;
                // #1093: MaskKey indexes maskKey[lenBytesRead]; always start at 0.
                ws.lenBytesRead = 0;
                ws.parseState = ws.masked ? WsState::MaskKey : WsState::Payload;
            } else if (len7 == 126) {
                ws.payloadLen = 0;
                ws.lenBytesRead = 0;
                ws.parseState = WsState::LenExt16;
            } else {
                ws.payloadLen = 0;
                ws.lenBytesRead = 0;
                ws.parseState = WsState::LenExt64;
            }
        } break;

        case WsState::LenExt16:
            ws.lenBuf[ws.lenBytesRead++] = *p++;
            if (ws.lenBytesRead == 2) {
                ws.payloadLen = (static_cast<uint64_t>(ws.lenBuf[0]) << 8)
                              | ws.lenBuf[1];
                if (ws.payloadLen > WS_MAX_PAYLOAD) {
                    responses += wsCloseFrame(1009);
                    messages.push_back({WS_OP_CLOSE, {}});
                    return messages;
                }
                ws.frameBuf.clear();
                ws.maskIdx = 0;
                // #1093: reset — lenBytesRead was 2 for the length field.
                ws.lenBytesRead = 0;
                ws.parseState = ws.masked ? WsState::MaskKey : WsState::Payload;
            }
            break;

        case WsState::LenExt64:
            ws.lenBuf[ws.lenBytesRead++] = *p++;
            if (ws.lenBytesRead == 8) {
                ws.payloadLen = 0;
                for (int i = 0; i < 8; i++) {
                    ws.payloadLen = (ws.payloadLen << 8) | ws.lenBuf[i];
                }
                if (ws.payloadLen > WS_MAX_PAYLOAD) {
                    responses += wsCloseFrame(1009);
                    messages.push_back({WS_OP_CLOSE, {}});
                    return messages;
                }
                ws.frameBuf.clear();
                ws.maskIdx = 0;
                // #1093: reset — lenBytesRead was 8 for the length field;
                // without this, MaskKey writes past maskKey[4] (OOB).
                ws.lenBytesRead = 0;
                ws.parseState = ws.masked ? WsState::MaskKey : WsState::Payload;
            }
            break;

        case WsState::MaskKey:
            ws.maskKey[ws.lenBytesRead++] = *p++;
            if (ws.lenBytesRead == 4) {
                ws.lenBytesRead = 0;
                ws.parseState = WsState::Payload;
            }
            break;

        case WsState::Payload: {
            size_t remaining = ws.payloadLen - ws.frameBuf.size();
            size_t avail = static_cast<size_t>(pe - p);
            size_t take = std::min(remaining, avail);

            for (size_t i = 0; i < take; i++) {
                uint8_t ch = p[i];
                if (ws.masked) {
                    ch ^= ws.maskKey[ws.maskIdx % 4];
                    ws.maskIdx++;
                }
                ws.frameBuf.push_back(static_cast<char>(ch));
            }
            p += take;

            if (ws.frameBuf.size() == ws.payloadLen) {
                // Frame complete
                uint8_t op = ws.opcode;

                if (op == WS_OP_PING) {
                    // Send pong
                    responses += wsEncodeFrame(ws.frameBuf, WS_OP_PONG);
                } else if (op == WS_OP_CLOSE) {
                    // #1094: echo close response AND deliver CLOSE so the
                    // session manager closes the connection (was dead code).
                    responses += wsCloseFrame(1000);
                    messages.push_back({WS_OP_CLOSE, ws.frameBuf});
                    ws.parseState = WsState::Header1;
                    return messages;
                } else if (op == WS_OP_PONG) {
                    // Ignore
                } else {
                    // #1886: RFC 6455 §5.4 fragmentation state + §8.1 TEXT UTF-8.
                    // fragOpcode != 0 means a fragmented message is in progress
                    // (engine #792); clear it when reassembly completes or fails.
                    //
                    auto protocolClose = [&](uint16_t code) {
                        responses += wsCloseFrame(code);
                        messages.push_back({WS_OP_CLOSE, {}});
                        ws.fragBuf.clear();
                        ws.fragOpcode = 0;
                        ws.parseState = WsState::Header1;
                    };
                    auto textUtf8Ok = [](const std::string& s) {
                        return !findFirstUtf8Issue(s).hasIssue();
                    };

                    if (op == WS_OP_CONTINUATION) {
                        if (ws.fragOpcode == 0) {
                            // Orphan CONTINUATION — no message in progress.
                            protocolClose(1002);
                            return messages;
                        }
                        if (ws.fragBuf.size() + ws.frameBuf.size() > WS_MAX_PAYLOAD) {
                            protocolClose(1009);
                            return messages;
                        }
                        ws.fragBuf += ws.frameBuf;
                        if (ws.fin) {
                            if (ws.fragOpcode == WS_OP_TEXT
                                && !textUtf8Ok(ws.fragBuf)) {
                                protocolClose(1007);
                                return messages;
                            }
                            messages.push_back({ws.fragOpcode, ws.fragBuf});
                            ws.fragBuf.clear();
                            ws.fragOpcode = 0;
                        }
                    } else if (op == WS_OP_TEXT || op == WS_OP_BINARY) {
                        if (ws.fragOpcode != 0) {
                            // New data frame while reassembly is pending.
                            protocolClose(1002);
                            return messages;
                        }
                        if (ws.fin) {
                            if (op == WS_OP_TEXT && !textUtf8Ok(ws.frameBuf)) {
                                protocolClose(1007);
                                return messages;
                            }
                            messages.push_back({op, ws.frameBuf});
                        } else {
                            ws.fragOpcode = op;
                            ws.fragBuf = ws.frameBuf;
                        }
                    } else {
                        // Unknown non-control opcode — protocol error.
                        protocolClose(1002);
                        return messages;
                    }
                }

                ws.parseState = WsState::Header1;
            }
        } break;
        }
    }

    return messages;
}

// ---- Frame encoding ----

std::string wsEncodeFrame(const std::string& payload, uint8_t opcode) {
    std::string frame;
    size_t plen = payload.size();

    frame.push_back(static_cast<char>(0x80 | opcode));  // FIN + opcode

    if (plen < 126) {
        frame.push_back(static_cast<char>(plen));
    } else if (plen <= 65535) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((plen >> 8) & 0xFF));
        frame.push_back(static_cast<char>(plen & 0xFF));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; i--) {
            frame.push_back(static_cast<char>((plen >> (i * 8)) & 0xFF));
        }
    }

    frame.append(payload);
    return frame;
}

std::string wsCloseFrame(uint16_t code) {
    std::string payload;
    payload.push_back(static_cast<char>((code >> 8) & 0xFF));
    payload.push_back(static_cast<char>(code & 0xFF));
    return wsEncodeFrame(payload, WS_OP_CLOSE);
}
