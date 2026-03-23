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
}

// Build a GMCP telnet sub-negotiation frame: IAC SB GMCP <payload> IAC SE
inline std::string buildGmcpFrame(const std::string& payload) {
    std::string frame;
    frame.reserve(payload.size() + 5);
    frame.push_back(static_cast<char>(telnet::IAC));
    frame.push_back(static_cast<char>(telnet::SB));
    frame.push_back(static_cast<char>(telnet::GMCP));
    frame.append(payload);
    frame.push_back(static_cast<char>(telnet::IAC));
    frame.push_back(static_cast<char>(telnet::SE));
    return frame;
}

// Build a NAWS telnet sub-negotiation frame: IAC SB NAWS w_hi w_lo h_hi h_lo IAC SE
inline std::string buildNawsFrame(uint16_t width, uint16_t height) {
    std::string frame;
    frame.reserve(9);
    frame.push_back(static_cast<char>(telnet::IAC));
    frame.push_back(static_cast<char>(telnet::SB));
    frame.push_back(static_cast<char>(telnet::NAWS));
    frame.push_back(static_cast<char>((width >> 8) & 0xFF));
    frame.push_back(static_cast<char>(width & 0xFF));
    frame.push_back(static_cast<char>((height >> 8) & 0xFF));
    frame.push_back(static_cast<char>(height & 0xFF));
    frame.push_back(static_cast<char>(telnet::IAC));
    frame.push_back(static_cast<char>(telnet::SE));
    return frame;
}

#endif // HYDRA_TELNET_UTILS_H
