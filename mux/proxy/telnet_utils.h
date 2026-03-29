#ifndef HYDRA_TELNET_UTILS_H
#define HYDRA_TELNET_UTILS_H

#include <cstdint>
#include <string>

// Telnet protocol constants used by the Hydra proxy.
namespace telnet {
    constexpr uint8_t IAC  = 255;
    constexpr uint8_t SB   = 250;
    constexpr uint8_t SE   = 240;
    constexpr uint8_t WILL = 251;
    constexpr uint8_t WONT = 252;
    constexpr uint8_t DO   = 253;
    constexpr uint8_t DONT = 254;
    constexpr uint8_t GMCP = 201;
    constexpr uint8_t NAWS = 31;
    constexpr uint8_t TTYPE = 24;
    constexpr uint8_t CHARSET = 42;
    constexpr uint8_t TELQUAL_IS = 0;
    constexpr uint8_t TELQUAL_SEND = 1;
    constexpr uint8_t TELQUAL_REQUEST = 1;
    constexpr uint8_t TELQUAL_ACCEPTED = 2;
    constexpr uint8_t TELQUAL_REJECTED = 3;
}

// Escape IAC bytes in a telnet sub-negotiation payload.
// Any 0xFF byte becomes 0xFF 0xFF per RFC 854.
inline std::string telnetEscapeIAC(const std::string& data) {
    std::string out;
    out.reserve(data.size());
    for (unsigned char ch : data) {
        out.push_back(static_cast<char>(ch));
        if (ch == telnet::IAC) {
            out.push_back(static_cast<char>(telnet::IAC));
        }
    }
    return out;
}

// Build a GMCP telnet sub-negotiation frame: IAC SB GMCP <payload> IAC SE
// Payload bytes are IAC-escaped per RFC 854.
inline std::string buildGmcpFrame(const std::string& payload) {
    std::string escaped = telnetEscapeIAC(payload);
    std::string frame;
    frame.reserve(escaped.size() + 5);
    frame.push_back(static_cast<char>(telnet::IAC));
    frame.push_back(static_cast<char>(telnet::SB));
    frame.push_back(static_cast<char>(telnet::GMCP));
    frame.append(escaped);
    frame.push_back(static_cast<char>(telnet::IAC));
    frame.push_back(static_cast<char>(telnet::SE));
    return frame;
}

// Escape a single byte for NAWS: if 0xFF, emit 0xFF 0xFF.
inline void nawsAppendByte(std::string& frame, uint8_t byte) {
    frame.push_back(static_cast<char>(byte));
    if (byte == telnet::IAC) {
        frame.push_back(static_cast<char>(telnet::IAC));
    }
}

// Build a NAWS telnet sub-negotiation frame: IAC SB NAWS w_hi w_lo h_hi h_lo IAC SE
// Width/height bytes are IAC-escaped per RFC 854.
inline std::string buildNawsFrame(uint16_t width, uint16_t height) {
    std::string frame;
    frame.reserve(13);  // worst case: all 4 data bytes are 0xFF
    frame.push_back(static_cast<char>(telnet::IAC));
    frame.push_back(static_cast<char>(telnet::SB));
    frame.push_back(static_cast<char>(telnet::NAWS));
    nawsAppendByte(frame, static_cast<uint8_t>((width >> 8) & 0xFF));
    nawsAppendByte(frame, static_cast<uint8_t>(width & 0xFF));
    nawsAppendByte(frame, static_cast<uint8_t>((height >> 8) & 0xFF));
    nawsAppendByte(frame, static_cast<uint8_t>(height & 0xFF));
    frame.push_back(static_cast<char>(telnet::IAC));
    frame.push_back(static_cast<char>(telnet::SE));
    return frame;
}

inline std::string buildTelnetCommandFrame(uint8_t command, uint8_t option) {
    std::string frame;
    frame.reserve(3);
    frame.push_back(static_cast<char>(telnet::IAC));
    frame.push_back(static_cast<char>(command));
    frame.push_back(static_cast<char>(option));
    return frame;
}

inline std::string buildTtypeIsFrame(const std::string& terminalType) {
    std::string escaped = telnetEscapeIAC(terminalType);
    std::string frame;
    frame.reserve(escaped.size() + 6);
    frame.push_back(static_cast<char>(telnet::IAC));
    frame.push_back(static_cast<char>(telnet::SB));
    frame.push_back(static_cast<char>(telnet::TTYPE));
    frame.push_back(static_cast<char>(telnet::TELQUAL_IS));
    frame.append(escaped);
    frame.push_back(static_cast<char>(telnet::IAC));
    frame.push_back(static_cast<char>(telnet::SE));
    return frame;
}

inline std::string buildCharsetAcceptedFrame(const std::string& charsetName) {
    std::string escaped = telnetEscapeIAC(charsetName);
    std::string frame;
    frame.reserve(escaped.size() + 6);
    frame.push_back(static_cast<char>(telnet::IAC));
    frame.push_back(static_cast<char>(telnet::SB));
    frame.push_back(static_cast<char>(telnet::CHARSET));
    frame.push_back(static_cast<char>(telnet::TELQUAL_ACCEPTED));
    frame.append(escaped);
    frame.push_back(static_cast<char>(telnet::IAC));
    frame.push_back(static_cast<char>(telnet::SE));
    return frame;
}

inline std::string buildCharsetRejectedFrame() {
    std::string frame;
    frame.reserve(6);
    frame.push_back(static_cast<char>(telnet::IAC));
    frame.push_back(static_cast<char>(telnet::SB));
    frame.push_back(static_cast<char>(telnet::CHARSET));
    frame.push_back(static_cast<char>(telnet::TELQUAL_REJECTED));
    frame.push_back(static_cast<char>(telnet::IAC));
    frame.push_back(static_cast<char>(telnet::SE));
    return frame;
}

#endif // HYDRA_TELNET_UTILS_H
