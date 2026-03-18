// telnet.h -- Telnet protocol constants and definitions.
#ifndef TELNET_H
#define TELNET_H

#include <cstdint>

// Telnet commands
constexpr uint8_t TEL_IAC  = 255;
constexpr uint8_t TEL_DONT = 254;
constexpr uint8_t TEL_DO   = 253;
constexpr uint8_t TEL_WONT = 252;
constexpr uint8_t TEL_WILL = 251;
constexpr uint8_t TEL_SB   = 250;
constexpr uint8_t TEL_GA   = 249;
constexpr uint8_t TEL_EL   = 248;
constexpr uint8_t TEL_EC   = 247;
constexpr uint8_t TEL_AYT  = 246;
constexpr uint8_t TEL_AO   = 245;
constexpr uint8_t TEL_IP   = 244;
constexpr uint8_t TEL_NOP  = 241;
constexpr uint8_t TEL_SE   = 240;

// Telnet options
constexpr uint8_t TELOPT_ECHO     = 1;
constexpr uint8_t TELOPT_SGA      = 3;   // Suppress Go Ahead
constexpr uint8_t TELOPT_TTYPE    = 24;  // Terminal Type
constexpr uint8_t TELOPT_NAWS     = 31;  // Negotiate About Window Size
constexpr uint8_t TELOPT_CHARSET  = 42;
constexpr uint8_t TELOPT_MSSP     = 70;  // MUD Server Status Protocol
constexpr uint8_t TELOPT_MCCP2    = 86;  // MUD Client Compression Protocol v2
constexpr uint8_t TELOPT_GMCP     = 201; // Generic MUD Communication Protocol

// CHARSET subneg
constexpr uint8_t CHARSET_REQUEST  = 1;
constexpr uint8_t CHARSET_ACCEPTED = 2;
constexpr uint8_t CHARSET_REJECTED = 3;

// TTYPE subneg
constexpr uint8_t TTYPE_IS   = 0;
constexpr uint8_t TTYPE_SEND = 1;

#endif // TELNET_H
