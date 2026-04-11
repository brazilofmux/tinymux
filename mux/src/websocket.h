/*! \file websocket.h
 * \brief WebSocket protocol support (RFC 6455).
 *
 * WebSocket connections are auto-detected on the same port as telnet.
 * After the HTTP upgrade handshake, all I/O is framed per RFC 6455.
 */

#pragma once
#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include "copyright.h"

// WebSocket opcodes (RFC 6455 Section 5.2)
//
constexpr uint8_t WS_OPCODE_CONTINUATION = 0x0;
constexpr uint8_t WS_OPCODE_TEXT         = 0x1;
constexpr uint8_t WS_OPCODE_BINARY       = 0x2;
constexpr uint8_t WS_OPCODE_CLOSE        = 0x8;
constexpr uint8_t WS_OPCODE_PING         = 0x9;
constexpr uint8_t WS_OPCODE_PONG         = 0xA;

// WebSocket close status codes (RFC 6455 Section 7.4.1)
//
constexpr uint16_t WS_CLOSE_NORMAL         = 1000;
constexpr uint16_t WS_CLOSE_GOING_AWAY     = 1001;
constexpr uint16_t WS_CLOSE_PROTOCOL_ERR   = 1002;
constexpr uint16_t WS_CLOSE_MESSAGE_TOO_BIG = 1009;

// Maximum WebSocket frame payload we accept (64 KB).
// MUX commands are limited to LBUF_SIZE anyway.
//
constexpr size_t WS_MAX_PAYLOAD = 65536;

// WebSocket frame reassembly state.
//
struct ws_state
{
    // Accumulated partial HTTP header during handshake.
    //
    std::string handshake_buf;

    // Frame reassembly.
    //
    std::string frame_buf;          // partial frame accumulator
    size_t frame_expected;          // bytes expected in current frame
    uint8_t frame_opcode;           // opcode of current frame
    bool frame_fin;                 // FIN bit of current frame
    uint8_t frame_mask[4];          // masking key
    bool frame_masked;              // is frame masked?
    int parse_state;                // frame parser state machine

    // Fragmentation reassembly.
    //
    std::string frag_buf;           // accumulated fragmented payload
    uint8_t frag_opcode;            // opcode from first fragment

    ws_state()
        : frame_expected(0)
        , frame_opcode(0)
        , frame_fin(false)
        , frame_mask{0,0,0,0}
        , frame_masked(false)
        , parse_state(0)
        , frag_opcode(0)
    {}
};

// Forward declaration.
//
struct descriptor_data;

// Detect whether the first bytes of a connection look like an HTTP
// WebSocket upgrade request.
//
bool ws_is_upgrade_request(const char *data, size_t len);

// Process the HTTP upgrade handshake.  Returns true if the handshake
// is complete (success or failure).  Accumulates partial headers across
// multiple onDataReceived calls if needed.
//
bool ws_process_handshake(descriptor_data *d, const char *data, size_t len);

// Process incoming WebSocket frames.  Decodes frames, handles control
// frames (ping/pong/close), and feeds decoded text to save_command().
//
void ws_process_input(descriptor_data *d, const char *data, size_t len);

// Wrap raw output bytes in a WebSocket text frame.
//
void ws_queue_frame(descriptor_data *d, const uint8_t *data, size_t len,
                    uint8_t opcode = WS_OPCODE_TEXT);

// Send a WebSocket close frame.
//
void ws_send_close(descriptor_data *d, uint16_t code);

#endif // !WEBSOCKET_H
