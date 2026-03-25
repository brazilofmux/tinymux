#ifndef HYDRA_WEBSOCKET_H
#define HYDRA_WEBSOCKET_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// WebSocket opcodes (RFC 6455)
constexpr uint8_t WS_OP_CONTINUATION = 0x0;
constexpr uint8_t WS_OP_TEXT         = 0x1;
constexpr uint8_t WS_OP_BINARY       = 0x2;
constexpr uint8_t WS_OP_CLOSE        = 0x8;
constexpr uint8_t WS_OP_PING         = 0x9;
constexpr uint8_t WS_OP_PONG         = 0xA;

constexpr size_t WS_MAX_PAYLOAD = 65536;

// Per-connection WebSocket state.
struct WsState {
    // Handshake accumulation
    std::string handshakeBuf;
    bool handshakeComplete{false};
    bool handshakeOk{false};

    // Frame parser state
    enum ParseState {
        Header1, Header2, LenExt16, LenExt64, MaskKey, Payload
    };
    ParseState parseState{Header1};

    uint8_t opcode{0};
    bool fin{false};
    bool masked{false};
    uint64_t payloadLen{0};
    uint8_t maskKey[4]{};
    size_t maskIdx{0};
    size_t lenBytesRead{0};
    uint8_t lenBuf[8]{};
    std::string frameBuf;       // payload accumulation

    // Fragment reassembly
    std::string fragBuf;
    uint8_t fragOpcode{0};

    // Subprotocol negotiation (set during handshake)
    bool isGameSession{false};       // true if hydra-gamesession subprotocol
};

// A decoded WebSocket message.
struct WsMessage {
    uint8_t opcode;
    std::string payload;
};

// Process the HTTP upgrade handshake. Appends data to ws.handshakeBuf.
// Returns the response to send (101 or 400), or empty if more data needed.
// Sets ws.handshakeComplete and ws.handshakeOk.
std::string wsProcessHandshake(WsState& ws, const char* data, size_t len);

// Decode WebSocket frames from data. Returns decoded messages.
// Control frames (ping/pong/close) are handled internally; pong/close
// responses are appended to 'responses'.
std::vector<WsMessage> wsDecodeFrames(WsState& ws, const char* data,
                                       size_t len, std::string& responses);

// Encode a payload as a WebSocket frame (server→client, unmasked).
std::string wsEncodeFrame(const std::string& payload,
                          uint8_t opcode = WS_OP_TEXT);

// Build a WebSocket close frame.
std::string wsCloseFrame(uint16_t code);

#endif // HYDRA_WEBSOCKET_H
