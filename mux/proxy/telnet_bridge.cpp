#include "telnet_bridge.h"

// Override color_ops.h fallback LBUF_SIZE (8000) to match the engine's
// alloc.h value (32768).  The proxy doesn't include alloc.h directly,
// but color_ops render functions internally cap output at LBUF_SIZE.
#define LBUF_SIZE 32768

#include <color_ops.h>
#include <cstring>
#include <vector>

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
        size_t bufCap = std::max(utf8Str.size(), static_cast<size_t>(LBUF_SIZE));
        std::vector<unsigned char> buf(bufCap);
        size_t n = co_render_ascii(buf.data(),
            reinterpret_cast<const unsigned char*>(utf8Str.data()),
            utf8Str.size());
        return std::string(reinterpret_cast<char*>(buf.data()), n);
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
    // PUA encoding can expand slightly (3-byte PUA per SGR), but the
    // visible text is roughly 1:1.  Use generous heap buffer.
    size_t bufCap = std::max(utf8Str.size() * 2, static_cast<size_t>(LBUF_SIZE));
    std::vector<unsigned char> puaBuf(bufCap);
    size_t puaLen = co_parse_ansi(
        reinterpret_cast<const unsigned char*>(utf8Str.data()),
        utf8Str.size(),
        puaBuf.data(), bufCap);

    return std::string(reinterpret_cast<char*>(puaBuf.data()), puaLen);
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
    // Truecolor SGR can expand ~4x; use heap buffer.
    size_t bufCap = std::max(srcLen * 4 + 256, static_cast<size_t>(LBUF_SIZE));
    std::vector<unsigned char> ansiBuf(bufCap);
    size_t ansiLen = 0;

    switch (colorDepth) {
    case ColorDepth::TrueColor:
        ansiLen = co_render_truecolor(ansiBuf.data(), src, srcLen, 0);
        break;
    case ColorDepth::Ansi256:
        ansiLen = co_render_ansi256(ansiBuf.data(), src, srcLen, 0);
        break;
    case ColorDepth::Ansi16:
        ansiLen = co_render_ansi16(ansiBuf.data(), src, srcLen, 0);
        break;
    case ColorDepth::None:
        ansiLen = co_strip_color(ansiBuf.data(), src, srcLen);
        break;
    }

    // Step 2: Charset-encode if client is not UTF-8.
    if (clientEncoding != ganl::EncodingType::Utf8 &&
        clientEncoding != ganl::EncodingType::Ascii) {
        std::string rendered(reinterpret_cast<char*>(ansiBuf.data()), ansiLen);
        return charsetEncodeFromUtf8(rendered, clientEncoding);
    }

    return std::string(reinterpret_cast<char*>(ansiBuf.data()), ansiLen);
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
