#ifndef TF_CHARSET_H
#define TF_CHARSET_H

#include <string>
#include <cstdint>

// Supported character set encodings for MUD connections.
//
enum class Charset {
    UTF8,       // No conversion needed
    ASCII,      // 7-bit only; bytes > 127 replaced with '?'
    Latin1,     // ISO-8859-1: bytes 0x80-0xFF → U+0080-U+00FF
    CP437,      // IBM Code Page 437: DOS box-drawing, ANSI art
};

// Parse a charset name (case-insensitive) into a Charset enum.
// Returns UTF8 for unrecognized names.
//
Charset charset_from_name(const std::string& name);

// Return the canonical name for a charset.
//
const char* charset_name(Charset cs);

// Can we handle this charset name?
//
bool charset_supported(const std::string& name);

// Convert a byte string from the given charset to UTF-8.
// For UTF8, returns the input unchanged.
//
std::string charset_to_utf8(const std::string& input, Charset from);

// Convert a UTF-8 string to the given charset for sending.
// Characters not representable in the target charset are replaced with '?'.
//
std::string utf8_to_charset(const std::string& input, Charset to);

#endif // TF_CHARSET_H
