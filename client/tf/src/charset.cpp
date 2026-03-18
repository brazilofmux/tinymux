#include "charset.h"
#include <algorithm>
#include <cctype>
#include <cstring>

// CP437 high-byte (0x80-0xFF) to Unicode mapping.
//
static const uint16_t cp437_to_unicode[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, // 80-87
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5, // 88-8F
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, // 90-97
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, // 98-9F
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, // A0-A7
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, // A8-AF
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, // B0-B7
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, // B8-BF
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, // C0-C7
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567, // C8-CF
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, // D0-D7
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, // D8-DF
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, // E0-E7
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229, // E8-EF
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, // F0-F7
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0, // F8-FF
};

// Encode a Unicode code point as UTF-8, appending to out.
//
static void utf8_encode(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

static std::string to_upper(const std::string& s) {
    std::string u = s;
    std::transform(u.begin(), u.end(), u.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return u;
}

Charset charset_from_name(const std::string& name) {
    std::string u = to_upper(name);

    // Remove hyphens and underscores for flexible matching.
    std::string norm;
    for (char ch : u) {
        if (ch != '-' && ch != '_') norm += ch;
    }

    if (norm == "UTF8")        return Charset::UTF8;
    if (norm == "USASCII")     return Charset::ASCII;
    if (norm == "ASCII")       return Charset::ASCII;
    if (norm == "ANSIX3.41968") return Charset::ASCII;
    if (norm == "ISO88591")    return Charset::Latin1;
    if (norm == "LATIN1")      return Charset::Latin1;
    if (norm == "ISO885915")   return Charset::Latin1;  // Latin-9, close enough
    if (norm == "CP437")       return Charset::CP437;
    if (norm == "IBM437")      return Charset::CP437;
    if (norm == "CP850")       return Charset::Latin1;  // Western European, close to Latin-1

    return Charset::UTF8;  // Unknown — assume UTF-8
}

const char* charset_name(Charset cs) {
    switch (cs) {
    case Charset::UTF8:   return "UTF-8";
    case Charset::ASCII:  return "US-ASCII";
    case Charset::Latin1: return "ISO-8859-1";
    case Charset::CP437:  return "CP437";
    }
    return "UTF-8";
}

bool charset_supported(const std::string& name) {
    std::string u = to_upper(name);
    std::string norm;
    for (char ch : u) {
        if (ch != '-' && ch != '_') norm += ch;
    }
    return norm == "UTF8" || norm == "USASCII" || norm == "ASCII"
        || norm == "ISO88591" || norm == "LATIN1" || norm == "ISO885915"
        || norm == "CP437" || norm == "IBM437" || norm == "CP850";
}

std::string charset_to_utf8(const std::string& input, Charset from) {
    if (from == Charset::UTF8) return input;

    std::string out;
    out.reserve(input.size() * 2);

    for (unsigned char ch : input) {
        if (ch < 0x80) {
            out += static_cast<char>(ch);
            continue;
        }

        switch (from) {
        case Charset::ASCII:
            out += '?';
            break;

        case Charset::Latin1:
            // ISO-8859-1: byte value == Unicode code point.
            utf8_encode(out, ch);
            break;

        case Charset::CP437:
            utf8_encode(out, cp437_to_unicode[ch - 0x80]);
            break;

        default:
            out += '?';
            break;
        }
    }
    return out;
}

std::string utf8_to_charset(const std::string& input, Charset to) {
    if (to == Charset::UTF8) return input;

    std::string out;
    out.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        unsigned char ch = static_cast<unsigned char>(input[i]);

        // Decode one UTF-8 code point.
        uint32_t cp;
        size_t clen;
        if (ch < 0x80) {
            cp = ch; clen = 1;
        } else if (ch < 0xC0) {
            cp = '?'; clen = 1;  // stray continuation
        } else if (ch < 0xE0) {
            cp = ch & 0x1F; clen = 2;
        } else if (ch < 0xF0) {
            cp = ch & 0x0F; clen = 3;
        } else {
            cp = ch & 0x07; clen = 4;
        }

        for (size_t j = 1; j < clen && i + j < input.size(); j++) {
            cp = (cp << 6) | (static_cast<unsigned char>(input[i + j]) & 0x3F);
        }
        i += clen;

        // Encode to target charset.
        if (cp < 0x80) {
            out += static_cast<char>(cp);
            continue;
        }

        switch (to) {
        case Charset::ASCII:
            out += '?';
            break;

        case Charset::Latin1:
            if (cp <= 0xFF) {
                out += static_cast<char>(cp);
            } else {
                out += '?';
            }
            break;

        case Charset::CP437:
            {
                // Reverse lookup in cp437 table.
                bool found = false;
                for (int k = 0; k < 128; k++) {
                    if (cp437_to_unicode[k] == cp) {
                        out += static_cast<char>(0x80 + k);
                        found = true;
                        break;
                    }
                }
                if (!found) out += '?';
            }
            break;

        default:
            out += '?';
            break;
        }
    }
    return out;
}
