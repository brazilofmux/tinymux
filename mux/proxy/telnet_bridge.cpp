#include "telnet_bridge.h"
#include <color_ops.h>
#include <cstring>

// Maximum buffer size for color_ops output (matches LBUF_SIZE).
static constexpr size_t MAX_BUF = LBUF_SIZE;

// Charset lookup tables from libmux (declared in stringutil.h, but that
// header pulls in too much of the engine).  These are LIBMUX_API arrays
// of pointers to null-terminated UTF-8 strings, one per byte value.
extern const unsigned char* cp437_utf8[256];
extern const unsigned char* latin1_utf8[256];
extern const unsigned char* latin2_utf8[256];

// Charset-decode a single-byte encoding to UTF-8.
// Each byte maps to a (possibly multi-byte) UTF-8 sequence via the table.
static std::string charsetDecodeToUtf8(const char* data, size_t len,
                                       const unsigned char* table[256]) {
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = static_cast<unsigned char>(data[i]);
        const unsigned char* utf = table[ch];
        if (utf) {
            out.append(reinterpret_cast<const char*>(utf));
        }
    }
    return out;
}

// Charset-encode UTF-8 to a single-byte encoding.
// For characters outside the target set, approximate via co_dfa_ascii().
// This is a best-effort approach for Phase 1.
static std::string charsetEncodeFromUtf8(const std::string& utf8Str,
                                         ganl::EncodingType encoding) {
    // For ASCII output, use co_render_ascii() which strips to 7-bit
    // with perceptual approximation.
    if (encoding == ganl::EncodingType::Ascii) {
        unsigned char buf[MAX_BUF];
        size_t n = co_render_ascii(buf,
            reinterpret_cast<const unsigned char*>(utf8Str.data()),
            utf8Str.size());
        return std::string(reinterpret_cast<char*>(buf), n);
    }

    // For Latin1/CP437/CP1252: walk UTF-8 code points.
    // Single-byte chars in 0x00-0x7F pass through (same in all encodings).
    // Multi-byte chars get ASCII-approximated via co_dfa_ascii().
    std::string out;
    out.reserve(utf8Str.size());
    const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8Str.data());
    const unsigned char* pe = p + utf8Str.size();

    while (p < pe) {
        if (*p < 0x80) {
            // ASCII byte — pass through
            out.push_back(static_cast<char>(*p));
            p++;
        } else {
            // Multi-byte UTF-8 — approximate to ASCII
            unsigned char approx = co_dfa_ascii(p);
            out.push_back(static_cast<char>(approx));
            // Advance past this code point
            if ((*p & 0xE0) == 0xC0)      p += 2;
            else if ((*p & 0xF0) == 0xE0)  p += 3;
            else if ((*p & 0xF8) == 0xF0)  p += 4;
            else                            p++;  // invalid — skip byte
        }
    }
    return out;
}

std::string TelnetBridge::ingestGameOutput(
    const ganl::ProtocolState& gameState,
    const char* data, size_t len) {

    // Step 1: Charset-decode to UTF-8 if needed.
    std::string utf8Str;
    switch (gameState.encoding) {
    case ganl::EncodingType::Latin1:
        utf8Str = charsetDecodeToUtf8(data, len, latin1_utf8);
        break;
    case ganl::EncodingType::Cp437:
        utf8Str = charsetDecodeToUtf8(data, len, cp437_utf8);
        break;
    default:
        // UTF-8 or ASCII — pass through
        utf8Str.assign(data, len);
        break;
    }

    // Step 2: Parse ANSI SGR escape sequences into PUA color codes.
    unsigned char puaBuf[MAX_BUF];
    size_t puaLen = co_parse_ansi(
        reinterpret_cast<const unsigned char*>(utf8Str.data()),
        utf8Str.size(),
        puaBuf, MAX_BUF);

    return std::string(reinterpret_cast<char*>(puaBuf), puaLen);
}

std::string TelnetBridge::renderForClient(
    ganl::EncodingType clientEncoding,
    ColorDepth colorDepth,
    const std::string& puaUtf8) {

    if (puaUtf8.empty()) return puaUtf8;

    const unsigned char* src =
        reinterpret_cast<const unsigned char*>(puaUtf8.data());
    size_t srcLen = puaUtf8.size();

    // Step 1: Render PUA color codes to ANSI SGR at the client's depth.
    unsigned char ansiBuf[MAX_BUF];
    size_t ansiLen = 0;

    switch (colorDepth) {
    case ColorDepth::TrueColor:
        ansiLen = co_render_truecolor(ansiBuf, src, srcLen, 0);
        break;
    case ColorDepth::Ansi256:
        ansiLen = co_render_ansi256(ansiBuf, src, srcLen, 0);
        break;
    case ColorDepth::Ansi16:
        ansiLen = co_render_ansi16(ansiBuf, src, srcLen, 0);
        break;
    case ColorDepth::None:
        ansiLen = co_strip_color(ansiBuf, src, srcLen);
        break;
    }

    // Step 2: Charset-encode if client is not UTF-8.
    if (clientEncoding != ganl::EncodingType::Utf8 &&
        clientEncoding != ganl::EncodingType::Ascii) {
        std::string rendered(reinterpret_cast<char*>(ansiBuf), ansiLen);
        return charsetEncodeFromUtf8(rendered, clientEncoding);
    }

    return std::string(reinterpret_cast<char*>(ansiBuf), ansiLen);
}

std::string TelnetBridge::convertInput(
    ganl::EncodingType clientEncoding,
    ganl::EncodingType gameEncoding,
    const std::string& clientLine) {

    // If both sides use the same encoding, pass through.
    if (clientEncoding == gameEncoding) return clientLine;

    // Step 1: Decode client bytes to UTF-8 if needed.
    std::string utf8Str;
    switch (clientEncoding) {
    case ganl::EncodingType::Latin1:
        utf8Str = charsetDecodeToUtf8(clientLine.data(), clientLine.size(),
                                      latin1_utf8);
        break;
    case ganl::EncodingType::Cp437:
        utf8Str = charsetDecodeToUtf8(clientLine.data(), clientLine.size(),
                                      cp437_utf8);
        break;
    default:
        utf8Str = clientLine;
        break;
    }

    // Step 2: Encode UTF-8 to game encoding if needed.
    if (gameEncoding != ganl::EncodingType::Utf8 &&
        gameEncoding != ganl::EncodingType::Ascii) {
        return charsetEncodeFromUtf8(utf8Str, gameEncoding);
    }

    return utf8Str;
}
