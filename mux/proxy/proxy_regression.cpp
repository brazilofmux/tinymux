#include "telnet_bridge.h"
#include "telnet_stream.h"
#include "telnet_utils.h"
#include "websocket.h"
#include "config.h"
#include "session_manager.h"  // HydraSession::OutputQueue caps (#1266/#1268)
#include "work_queue.h"       // WorkQueue::MAX_PENDING (#1265)
#ifdef GRPC_ENABLED
#include "hydra.pb.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "proxy_regression: " << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

bool isAscii(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](unsigned char ch) {
        return ch < 0x80;
    });
}

std::string bytes(std::initializer_list<unsigned int> values) {
    std::string out;
    out.reserve(values.size());
    for (unsigned int value : values) {
        out.push_back(static_cast<char>(value));
    }
    return out;
}

void testSplitGmcpAcrossReads() {
    TelnetParseState state;
    std::string regular;
    std::vector<TelnetGmcpMessage> gmcp;
    TelnetSignals signals;

    const std::string chunk1 = "look\r\n" + bytes({0xff, 0xfa, 0xc9}) + "Core.Hello ";
    const std::string chunk2 = "{}" + bytes({0xff, 0xf0});

    splitTelnetStream(chunk1.data(), chunk1.size(), state, regular, gmcp, signals);
    expect(regular == "look\r\n", "chunk1 regular text mismatch");
    expect(gmcp.empty(), "chunk1 should not complete GMCP");
    expect(state.state == TelnetParseState::InGmcpSB,
           "chunk1 should leave parser inside GMCP subnegotiation");

    splitTelnetStream(chunk2.data(), chunk2.size(), state, regular, gmcp, signals);
    expect(regular == "look\r\n", "chunk2 should not alter regular text");
    expect(gmcp.size() == 1, "chunk2 should complete one GMCP message");
    expect(gmcp[0].payload == "Core.Hello {}",
           "completed GMCP payload mismatch");
    expect(state.state == TelnetParseState::Normal,
           "parser should return to Normal after GMCP");
}

void testSplitTelnetNegotiationAcrossReads() {
    TelnetParseState state;
    std::string regular;
    std::vector<TelnetGmcpMessage> gmcp;
    TelnetSignals signals;

    const std::string chunk1 = bytes({0xff, 0xfb});
    const std::string chunk2 = bytes({0xc9});

    splitTelnetStream(chunk1.data(), chunk1.size(), state, regular, gmcp, signals);
    expect(!signals.sawWillGmcp, "partial WILL GMCP should not fire early");
    expect(state.state == TelnetParseState::SawCmd,
           "parser should remember partial WILL command");

    splitTelnetStream(chunk2.data(), chunk2.size(), state, regular, gmcp, signals);
    expect(signals.sawWillGmcp, "WILL GMCP should be detected across reads");
    expect(state.state == TelnetParseState::Normal,
           "parser should return to Normal after option byte");
}

void testStripNonGmcpSubnegotiationAcrossReads() {
    TelnetParseState state;
    std::string regular;
    std::vector<TelnetGmcpMessage> gmcp;
    TelnetSignals signals;

    const std::string chunk1 = "A" + bytes({0xff, 0xfa, 0x1f});
    const std::string chunk2 = bytes({0x00, 0x50, 0x00, 0x28, 0xff, 0xf0}) + "B";

    splitTelnetStream(chunk1.data(), chunk1.size(), state, regular, gmcp, signals, true);
    splitTelnetStream(chunk2.data(), chunk2.size(), state, regular, gmcp, signals, true);

    expect(regular == "AB", "stripTelnet should remove split NAWS subnegotiation");
    expect(gmcp.empty(), "NAWS test should not create GMCP messages");
}

void testSplitTtypeSignals() {
    TelnetParseState state;
    std::string regular;
    std::vector<TelnetGmcpMessage> gmcp;
    TelnetSignals signals;

    splitTelnetStream(bytes({0xff, 0xfd}).data(), 2, state, regular, gmcp, signals, true);
    expect(!signals.sawDoTtype, "partial DO TTYPE should not fire early");
    expect(state.state == TelnetParseState::SawCmd,
           "partial DO TTYPE should leave parser in SawCmd");

    std::string ttypeOpt = bytes({0x18});
    splitTelnetStream(ttypeOpt.data(), ttypeOpt.size(), state, regular, gmcp, signals, true);
    expect(signals.sawDoTtype, "DO TTYPE should be detected across reads");

    splitTelnetStream(bytes({0xff, 0xfe, 0x18}).data(), 3, state, regular, gmcp, signals, true);
    expect(signals.sawDontTtype, "DONT TTYPE should be detected");

    const std::string sendChunk1 = bytes({0xff, 0xfa, 0x18, 0x01});
    const std::string sendChunk2 = bytes({0xff, 0xf0});
    splitTelnetStream(sendChunk1.data(), sendChunk1.size(), state, regular, gmcp, signals, true);
    expect(!signals.sawTtypeSend, "partial TTYPE SEND should not fire early");
    splitTelnetStream(sendChunk2.data(), sendChunk2.size(), state, regular, gmcp, signals, true);
    expect(signals.sawTtypeSend, "TTYPE SEND should be detected across reads");

    expect(buildTelnetCommandFrame(telnet::WILL, telnet::TTYPE)
               == bytes({0xff, 0xfb, 0x18}),
           "WILL TTYPE frame encoding mismatch");
    expect(buildTtypeIsFrame("xterm-256color")
               == bytes({0xff, 0xfa, 0x18, 0x00}) + "xterm-256color" + bytes({0xff, 0xf0}),
           "TTYPE IS frame encoding mismatch");

    expect(buildTelnetCommandFrame(telnet::DO, telnet::GMCP)
               == bytes({0xff, 0xfd, 0xc9}),
           "DO GMCP frame encoding mismatch");
    expect(buildTelnetCommandFrame(telnet::WILL, telnet::NAWS)
               == bytes({0xff, 0xfb, 0x1f}),
           "WILL NAWS frame encoding mismatch");
    expect(buildCharsetAcceptedFrame("UTF-8")
               == bytes({0xff, 0xfa, 0x2a, 0x02}) + "UTF-8" + bytes({0xff, 0xf0}),
           "CHARSET ACCEPTED frame encoding mismatch");
    expect(buildCharsetRejectedFrame()
               == bytes({0xff, 0xfa, 0x2a, 0x03, 0xff, 0xf0}),
           "CHARSET REJECTED frame encoding mismatch");
}

void testSplitCharsetSignals() {
    TelnetParseState state;
    std::string regular;
    std::vector<TelnetGmcpMessage> gmcp;
    TelnetSignals signals;

    splitTelnetStream(bytes({0xff, 0xfd}).data(), 2, state, regular, gmcp, signals, true);
    expect(!signals.sawDoCharset, "partial DO CHARSET should not fire early");
    splitTelnetStream(bytes({0x2a}).data(), 1, state, regular, gmcp, signals, true);
    expect(signals.sawDoCharset, "DO CHARSET should be detected across reads");

    const std::string req1 = bytes({0xff, 0xfa, 0x2a, 0x01, '|'});
    const std::string req2 = "UTF-8|US-ASCII" + bytes({0xff, 0xf0});
    splitTelnetStream(req1.data(), req1.size(), state, regular, gmcp, signals, true);
    expect(!signals.sawCharsetRequest, "partial CHARSET REQUEST should not fire early");
    splitTelnetStream(req2.data(), req2.size(), state, regular, gmcp, signals, true);
    expect(signals.sawCharsetRequest, "CHARSET REQUEST should be detected across reads");
    expect(signals.charsetRequestPayload == "|UTF-8|US-ASCII",
           "CHARSET REQUEST payload mismatch");

    const std::string accepted = bytes({0xff, 0xfa, 0x2a, 0x02}) + "ISO-8859-1" + bytes({0xff, 0xf0});
    splitTelnetStream(accepted.data(), accepted.size(), state, regular, gmcp, signals, true);
    expect(signals.sawCharsetAccepted, "CHARSET ACCEPTED should be detected");
    expect(signals.charsetAcceptedPayload == "ISO-8859-1",
           "CHARSET ACCEPTED payload mismatch");

    splitTelnetStream(bytes({0xff, 0xfe, 0x2a}).data(), 3, state, regular, gmcp, signals, true);
    expect(signals.sawDontCharset, "DONT CHARSET should be detected");
}

void testCharsetRequestAndAcceptedInSameRead() {
    TelnetParseState state;
    std::string regular;
    std::vector<TelnetGmcpMessage> gmcp;
    TelnetSignals signals;

    // CHARSET REQUEST followed immediately by CHARSET ACCEPTED in one read.
    const std::string combined =
        bytes({0xff, 0xfa, 0x2a, 0x01, ';'}) + "UTF-8;US-ASCII" + bytes({0xff, 0xf0})
        + bytes({0xff, 0xfa, 0x2a, 0x02}) + "CP437" + bytes({0xff, 0xf0});
    splitTelnetStream(combined.data(), combined.size(), state, regular, gmcp, signals, true);

    expect(signals.sawCharsetRequest, "REQUEST should fire in combined read");
    expect(signals.charsetRequestPayload == ";UTF-8;US-ASCII",
           "REQUEST payload mismatch in combined read");
    expect(signals.sawCharsetAccepted, "ACCEPTED should fire in combined read");
    expect(signals.charsetAcceptedPayload == "CP437",
           "ACCEPTED payload mismatch in combined read");
}

void testSplitEorAcrossReads() {
    TelnetParseState state;
    std::string regular;
    std::vector<TelnetGmcpMessage> gmcp;
    TelnetSignals signals;

    // IAC in first chunk, EOR_CMD in second chunk.
    splitTelnetStream(bytes({0xff}).data(), 1, state, regular, gmcp, signals, true);
    expect(!signals.sawEor, "partial IAC EOR should not fire early");
    expect(state.state == TelnetParseState::SawIAC,
           "parser should be in SawIAC after lone IAC byte");

    splitTelnetStream(bytes({0xef}).data(), 1, state, regular, gmcp, signals, true);
    expect(signals.sawEor, "IAC EOR should be detected across reads");
    expect(state.state == TelnetParseState::Normal,
           "parser should return to Normal after split EOR");
}

void testSplitEorSignals() {
    TelnetParseState state;
    std::string regular;
    std::vector<TelnetGmcpMessage> gmcp;
    TelnetSignals signals;

    splitTelnetStream(bytes({0xff, 0xfd}).data(), 2, state, regular, gmcp, signals, true);
    expect(!signals.sawDoEor, "partial DO EOR should not fire early");
    splitTelnetStream(bytes({0x19}).data(), 1, state, regular, gmcp, signals, true);
    expect(signals.sawDoEor, "DO EOR should be detected across reads");

    splitTelnetStream(bytes({0xff, 0xfe, 0x19}).data(), 3, state, regular, gmcp, signals, true);
    expect(signals.sawDontEor, "DONT EOR should be detected");

    splitTelnetStream(bytes({0xff, 0xfe, 0x1f}).data(), 3, state, regular, gmcp, signals, true);
    expect(signals.sawDontNaws, "DONT NAWS should be detected");

    splitTelnetStream(bytes({0xff, 0xef}).data(), 2, state, regular, gmcp, signals, true);
    expect(signals.sawEor, "standalone EOR should be detected");

    expect(buildTelnetCommandFrame(telnet::DO, telnet::EOR_OPT) == bytes({0xff, 0xfd, 0x19}),
           "DO EOR option frame encoding mismatch");
    expect(buildTelnetTwoByteCommand(telnet::EOR_CMD) == bytes({0xff, 0xef}),
           "IAC EOR frame encoding mismatch");
}

void testAsciiBridgeConversion() {
    TelnetBridge bridge;
    std::string utf8 = "caf\xc3\xa9 \xe2\x98\x83";

    std::string toAscii = bridge.convertInput(
        ganl::EncodingType::Utf8,
        ganl::EncodingType::Ascii,
        utf8);
    expect(isAscii(toAscii), "convertInput should produce pure ASCII for ASCII games");

    std::string rendered = bridge.renderForClient(
        ganl::EncodingType::Ascii,
        ColorDepth::None,
        utf8);
    expect(isAscii(rendered), "renderForClient should produce pure ASCII for ASCII clients");
}

// Build a single masked client→server WebSocket frame.
std::string maskedFrame(uint8_t finOpcode, const std::string& payload,
                        uint8_t m0 = 0x01, uint8_t m1 = 0x02,
                        uint8_t m2 = 0x03, uint8_t m3 = 0x04) {
    expect(payload.size() < 126, "maskedFrame helper is short-payload only");
    std::string frame;
    frame.push_back(static_cast<char>(finOpcode));
    frame.push_back(static_cast<char>(0x80 | payload.size()));  // MASK + len
    frame.push_back(static_cast<char>(m0));
    frame.push_back(static_cast<char>(m1));
    frame.push_back(static_cast<char>(m2));
    frame.push_back(static_cast<char>(m3));
    const uint8_t mask[4] = {m0, m1, m2, m3};
    for (size_t i = 0; i < payload.size(); i++) {
        frame.push_back(static_cast<char>(
            static_cast<uint8_t>(payload[i]) ^ mask[i % 4]));
    }
    return frame;
}

void testWebSocketMaskEnforcement() {
    WsState ws;
    std::string responses;
    const std::string unmasked = bytes({0x81, 0x02}) + "hi";
    auto messages = wsDecodeFrames(ws, unmasked.data(), unmasked.size(), responses);

    // #1094: unmasked frames deliver a synthetic CLOSE so callers close the FD.
    expect(messages.size() == 1 && messages[0].opcode == WS_OP_CLOSE,
           "unmasked client frame should deliver CLOSE for teardown");
    expect(responses == wsCloseFrame(1002),
           "unmasked client frame should trigger protocol-error close");

    WsState maskedWs;
    responses.clear();
    const std::string masked = maskedFrame(0x81, "hi");
    messages = wsDecodeFrames(maskedWs, masked.data(), masked.size(), responses);

    expect(responses.empty(), "masked frame should not trigger close");
    expect(messages.size() == 1, "masked frame should decode to one message");
    expect(messages[0].opcode == WS_OP_TEXT, "masked frame opcode mismatch");
    expect(messages[0].payload == "hi", "masked frame payload mismatch");
}

// #1886: RFC 6455 fragmentation state + TEXT UTF-8 validation.
void testWebSocketFragmentationAndUtf8() {
    // Orphan CONTINUATION (no message in progress) → 1002.
    {
        WsState ws;
        std::string responses;
        const std::string frame = maskedFrame(0x80, "x");  // FIN + CONTINUATION
        auto messages = wsDecodeFrames(ws, frame.data(), frame.size(), responses);
        expect(messages.size() == 1 && messages[0].opcode == WS_OP_CLOSE,
               "orphan CONTINUATION should deliver CLOSE");
        expect(responses == wsCloseFrame(1002),
               "orphan CONTINUATION should close with 1002");
    }

    // TEXT while fragmented message pending → 1002.
    {
        WsState ws;
        std::string responses;
        // First fragment: FIN=0 TEXT "he"
        auto m1 = wsDecodeFrames(ws, maskedFrame(0x01, "he").data(),
                                 maskedFrame(0x01, "he").size(), responses);
        expect(m1.empty() && responses.empty(),
               "first TEXT fragment should not deliver yet");
        // New TEXT while reassembly pending
        const std::string second = maskedFrame(0x81, "no");
        auto m2 = wsDecodeFrames(ws, second.data(), second.size(), responses);
        expect(m2.size() == 1 && m2[0].opcode == WS_OP_CLOSE,
               "TEXT during reassembly should deliver CLOSE");
        expect(responses == wsCloseFrame(1002),
               "TEXT during reassembly should close with 1002");
    }

    // Valid fragmented TEXT reassembles.
    {
        WsState ws;
        std::string responses;
        const std::string f1 = maskedFrame(0x01, "hel");
        const std::string f2 = maskedFrame(0x80, "lo");
        auto m1 = wsDecodeFrames(ws, f1.data(), f1.size(), responses);
        expect(m1.empty(), "partial TEXT fragment holds");
        auto m2 = wsDecodeFrames(ws, f2.data(), f2.size(), responses);
        expect(m2.size() == 1 && m2[0].opcode == WS_OP_TEXT
               && m2[0].payload == "hello",
               "fragmented TEXT should reassemble to hello");
        expect(responses.empty(), "valid fragments should not close");
    }

    // Invalid UTF-8 on complete single-frame TEXT → 1007.
    {
        WsState ws;
        std::string responses;
        // Lone continuation byte 0x80 is invalid UTF-8.
        const std::string frame = maskedFrame(0x81, bytes({0x80}));
        auto messages = wsDecodeFrames(ws, frame.data(), frame.size(), responses);
        expect(messages.size() == 1 && messages[0].opcode == WS_OP_CLOSE,
               "invalid UTF-8 TEXT should deliver CLOSE");
        expect(responses == wsCloseFrame(1007),
               "invalid UTF-8 TEXT should close with 1007");
    }

    // Invalid UTF-8 on assembled fragmented TEXT → 1007.
    // Split multi-byte sequence across fragments: lead C3 in first, missing cont.
    {
        WsState ws;
        std::string responses;
        // First: FIN=0 TEXT with incomplete lead 0xC3 (would need 0xA9 for é)
        const std::string f1 = maskedFrame(0x01, bytes({0xC3}));
        // Second: FIN=1 CONTINUATION with invalid byte 0xFF (not a cont)
        const std::string f2 = maskedFrame(0x80, bytes({0xFF}));
        wsDecodeFrames(ws, f1.data(), f1.size(), responses);
        auto m2 = wsDecodeFrames(ws, f2.data(), f2.size(), responses);
        expect(m2.size() == 1 && m2[0].opcode == WS_OP_CLOSE,
               "invalid UTF-8 across fragments should CLOSE");
        expect(responses == wsCloseFrame(1007),
               "invalid fragmented UTF-8 should close with 1007");
    }

    // Valid multi-byte UTF-8 split across fragments assembles.
    {
        WsState ws;
        std::string responses;
        // U+00E9 é is C3 A9 — split as C3 | A9
        const std::string f1 = maskedFrame(0x01, bytes({0xC3}));
        const std::string f2 = maskedFrame(0x80, bytes({0xA9}));
        wsDecodeFrames(ws, f1.data(), f1.size(), responses);
        auto m2 = wsDecodeFrames(ws, f2.data(), f2.size(), responses);
        expect(m2.size() == 1 && m2[0].opcode == WS_OP_TEXT
               && m2[0].payload == bytes({0xC3, 0xA9}),
               "valid UTF-8 split across fragments should reassemble");
        expect(responses.empty(), "valid split UTF-8 should not close");
    }
}

// #1093: after a 16-bit extended length, MaskKey must start at index 0.
// Frame: FIN+TEXT, mask, len=126, length=0x0002, mask 01 02 03 04, "hi" xored.
void testWebSocketExtLenMaskKey() {
    WsState ws;
    std::string responses;
    const uint8_t m0 = 0x01, m1 = 0x02, m2 = 0x03, m3 = 0x04;
    const std::string frame = bytes({
        0x81, 0xFE, 0x00, 0x02, m0, m1, m2, m3,
        static_cast<uint8_t>('h' ^ m0),
        static_cast<uint8_t>('i' ^ m1)
    });
    auto messages = wsDecodeFrames(ws, frame.data(), frame.size(), responses);
    expect(responses.empty(), "ext-len masked TEXT should not close");
    expect(messages.size() == 1, "ext-len masked TEXT should yield one message");
    expect(messages[0].opcode == WS_OP_TEXT, "ext-len opcode TEXT");
    expect(messages[0].payload == "hi", "ext-len payload unmasked correctly");
}

// #1093: after a 64-bit extended length, MaskKey must start at index 0.
// This is the out-of-bounds case: pre-fix, lenBytesRead was still 8 entering
// MaskKey, so maskKey[8] (past uint8_t maskKey[4]) was written — an OOB write
// that UBSan/ASan traps.  The 16-bit case above only desyncs framing (index 2
// is still in-bounds), so this case is what actually guards the memory-safety
// fix.  Frame: FIN+TEXT, mask, len=127, 64-bit length=0x...0002, mask, "hi".
void testWebSocketExtLen64MaskKey() {
    WsState ws;
    std::string responses;
    const uint8_t m0 = 0x01, m1 = 0x02, m2 = 0x03, m3 = 0x04;
    const std::string frame = bytes({
        0x81, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
        m0, m1, m2, m3,
        static_cast<uint8_t>('h' ^ m0),
        static_cast<uint8_t>('i' ^ m1)
    });
    auto messages = wsDecodeFrames(ws, frame.data(), frame.size(), responses);
    expect(responses.empty(), "ext-len64 masked TEXT should not close");
    expect(messages.size() == 1, "ext-len64 masked TEXT should yield one message");
    expect(messages[0].opcode == WS_OP_TEXT, "ext-len64 opcode TEXT");
    expect(messages[0].payload == "hi", "ext-len64 payload unmasked correctly");
}

// #1094: client CLOSE must be delivered so the session path can close.
void testWebSocketCloseDelivered() {
    WsState ws;
    std::string responses;
    // Empty CLOSE, masked, mask all zero.
    const std::string frame = bytes({0x88, 0x80, 0x00, 0x00, 0x00, 0x00});
    auto messages = wsDecodeFrames(ws, frame.data(), frame.size(), responses);
    expect(messages.size() == 1 && messages[0].opcode == WS_OP_CLOSE,
           "client CLOSE should be delivered");
    expect(responses == wsCloseFrame(1000),
           "client CLOSE should echo close response");
}

// #1095: RSV bits must force protocol close.
void testWebSocketRsvRejected() {
    WsState ws;
    std::string responses;
    // FIN+TEXT with RSV1 set, masked, empty payload.
    const std::string frame = bytes({0xC1, 0x80, 0x00, 0x00, 0x00, 0x00});
    auto messages = wsDecodeFrames(ws, frame.data(), frame.size(), responses);
    expect(messages.size() == 1 && messages[0].opcode == WS_OP_CLOSE,
           "RSV frame should deliver CLOSE");
    expect(responses == wsCloseFrame(1002),
           "RSV frame should trigger protocol-error close");
}

// #1095: control frames cannot use extended length (large PING → PONG).
void testWebSocketLargePingRejected() {
    WsState ws;
    std::string responses;
    // FIN+PING, mask, len=126 (illegal for control).
    const std::string frame = bytes({0x89, 0xFE, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00});
    auto messages = wsDecodeFrames(ws, frame.data(), frame.size(), responses);
    expect(messages.size() == 1 && messages[0].opcode == WS_OP_CLOSE,
           "ext-len PING should deliver CLOSE");
    expect(responses == wsCloseFrame(1002),
           "ext-len PING should trigger protocol-error close");
}

// #1101: unbounded SB reassembly must trip the cap.
void testTelnetSbCap() {
    TelnetParseState state;
    std::string regular;
    std::vector<TelnetGmcpMessage> gmcp;
    TelnetSignals signals;

    // IAC SB GMCP then flood payload without SE.
    std::string flood = bytes({0xff, 0xfa, 0xc9});
    flood.append(TELNET_SB_MAX + 64, 'X');
    splitTelnetStream(flood.data(), flood.size(), state, regular, gmcp, signals);
    expect(state.sbOverflow, "SB flood should set sbOverflow");
    expect(state.state == TelnetParseState::Normal,
           "SB overflow should reset parser to Normal");
    expect(state.gmcpBuf.empty(), "SB overflow should clear gmcpBuf");
    expect(gmcp.empty(), "incomplete GMCP after overflow must not deliver");
}

// #1266: subscriber *count* is capped (queue depth was already #1096).
void testSubscriberCountCap() {
    HydraSession::OutputQueue oq;
    std::vector<std::shared_ptr<HydraSession::SubscriberQueue>> held;
    held.reserve(HydraSession::MAX_SUBSCRIBERS);
    for (size_t i = 0; i < HydraSession::MAX_SUBSCRIBERS; i++) {
        auto [id, sq] = oq.addSubscriber(true, false);
        expect(sq != nullptr, "subscriber within cap should succeed");
        expect(id > 0, "subscriber id should be positive");
        held.push_back(sq);
    }
    auto [id, sq] = oq.addSubscriber(true, true);
    expect(sq == nullptr, "subscriber over cap must return null queue");
    expect(id == -1, "subscriber over cap must return id -1");
    expect(oq.subscribers.size() == HydraSession::MAX_SUBSCRIBERS,
           "map size must stay at MAX_SUBSCRIBERS");
}

// #1268: input line limit is shared with front-door telnet assembly.
void testInputLineLimitShared() {
    expect(HydraSession::MAX_INPUT_LINE_LENGTH == 8192,
           "MAX_INPUT_LINE_LENGTH is 8192");
    expect(FrontDoorState::MAX_LINE_LENGTH == HydraSession::MAX_INPUT_LINE_LENGTH,
           "front-door MAX_LINE_LENGTH matches session input limit");
}

// #1265: work queue documents a hard pending cap.
void testWorkQueuePendingCapConstant() {
    expect(WorkQueue::MAX_PENDING == 1024, "WorkQueue::MAX_PENDING is 1024");
    expect(WorkQueue::MAX_PENDING > 0, "WorkQueue pending cap is positive");
}

// #1267-class: convertInput is the authority for non-UTF8 game targets
// (grpc-web SendInput must call the same path — regression locks the helper).
void testConvertInputNonUtf8Target() {
    TelnetBridge bridge;
    // High-bit UTF-8 must not pass through as raw bytes to an ASCII game.
    const std::string utf8Euro = "\xE2\x82\xAC";  // U+20AC
    std::string out = bridge.convertInput(
        ganl::EncodingType::Utf8,
        ganl::EncodingType::Ascii,
        utf8Euro);
    expect(isAscii(out), "convertInput to Ascii must yield pure ASCII");
    expect(!out.empty(), "convertInput should produce a fallback for Euro");
}

// #1885: truncated multi-byte UTF-8 must not advance the charset encoder
// past the end of the input.  Lead-only / partial sequences fall back one
// byte at a time; convertInput and renderForClient both take this path.
void testTruncatedUtf8CharsetEncode() {
    TelnetBridge bridge;

    const struct {
        const char* label;
        std::string input;
    } cases[] = {
        {"trailing C2 (2-byte lead)", bytes({0xC2})},
        {"trailing E2 82 (3-byte partial)", bytes({0xE2, 0x82})},
        {"trailing F0 9F 92 (4-byte partial)", bytes({0xF0, 0x9F, 0x92})},
        {"ascii + trailing C2", bytes({'o', 'k', 0xC2})},
    };

    for (const auto& c : cases) {
        std::string toLatin1 = bridge.convertInput(
            ganl::EncodingType::Utf8,
            ganl::EncodingType::Latin1,
            c.input);
        expect(isAscii(toLatin1) || toLatin1.size() <= c.input.size() + 4,
               std::string("convertInput Latin1 truncated UTF-8 must not OOB: ")
                   + c.label);
        // One replacement per remaining truncated byte, no runaway growth.
        expect(toLatin1.size() <= c.input.size(),
               std::string("convertInput Latin1 should not expand truncated: ")
                   + c.label);

        std::string rendered = bridge.renderForClient(
            ganl::EncodingType::Latin1,
            ColorDepth::None,
            c.input);
        expect(rendered.size() <= c.input.size() + 16,
               std::string("renderForClient truncated UTF-8 must not OOB: ")
                   + c.label);
    }

    // Complete multi-byte still approximates (does not crash / hang).
    std::string euro = bridge.convertInput(
        ganl::EncodingType::Utf8,
        ganl::EncodingType::Latin1,
        bytes({0xE2, 0x82, 0xAC}));
    expect(euro.size() == 1, "complete Euro should encode to one Latin1/approx byte");
}

// #1897: ports must not wrap via uint16_t cast of out-of-range stoi values.
void testPortRangeRejected() {
    auto writeConf = [](const std::string& body) -> std::string {
        char path[] = "/tmp/hydra-cfg-port-XXXXXX";
        int fd = mkstemp(path);
        expect(fd >= 0, "mkstemp for port test");
        expect(write(fd, body.data(), body.size())
                   == static_cast<ssize_t>(body.size()),
               "write port fixture");
        close(fd);
        return path;
    };

    const char* badListen[] = {
        "listen telnet 127.0.0.1:0\n",
        "listen telnet 127.0.0.1:-1\n",
        "listen telnet 127.0.0.1:65536\n",
        "listen telnet 127.0.0.1:99999\n",
    };
    for (const char* body : badListen) {
        std::string path = writeConf(body);
        HydraConfig cfg;
        std::string err;
        expect(!loadConfig(path, cfg, err),
               std::string("OOR listen port must fail: ") + body);
        expect(err.find("port") != std::string::npos,
               std::string("error should mention port: ") + err);
        unlink(path.c_str());
    }

    // Valid boundaries.
    for (const char* port : {"1", "65535", "4202"}) {
        std::string body = std::string("listen telnet 127.0.0.1:") + port + "\n";
        std::string path = writeConf(body);
        HydraConfig cfg;
        std::string err;
        expect(loadConfig(path, cfg, err),
               std::string("valid port must load: ") + port + " err=" + err);
        expect(cfg.listeners.size() == 1
               && cfg.listeners[0].port == static_cast<uint16_t>(std::stoi(port)),
               std::string("port value stored: ") + port);
        unlink(path.c_str());
    }

    // Game backend port.
    {
        std::string body =
            "listen telnet 127.0.0.1:4202\n"
            "game \"t\" {\n"
            "  host 127.0.0.1\n"
            "  port 65536\n"
            "}\n";
        std::string path = writeConf(body);
        HydraConfig cfg;
        std::string err;
        expect(!loadConfig(path, cfg, err),
               "OOR game port must fail loadConfig");
        unlink(path.c_str());
    }
    {
        std::string body =
            "listen telnet 127.0.0.1:4202\n"
            "game \"t\" {\n"
            "  host 127.0.0.1\n"
            "  port 2860\n"
            "}\n";
        std::string path = writeConf(body);
        HydraConfig cfg;
        std::string err;
        expect(loadConfig(path, cfg, err),
               std::string("valid game port must load: ") + err);
        expect(cfg.games.size() == 1 && cfg.games[0].port == 2860,
               "game port 2860 stored");
        unlink(path.c_str());
    }
}

// #1895: unknown listen types must fail closed, not become plaintext Telnet.
void testUnknownListenerTypeRejected() {
    auto writeConf = [](const std::string& listenLine) -> std::string {
        char path[] = "/tmp/hydra-cfg-test-XXXXXX";
        int fd = mkstemp(path);
        expect(fd >= 0, "mkstemp for config test");
        std::string body = listenLine + "\n";
        expect(write(fd, body.data(), body.size())
                   == static_cast<ssize_t>(body.size()),
               "write config fixture");
        close(fd);
        return path;
    };

    // Typo: was silently accepted as plaintext Telnet.
    {
        std::string path = writeConf("listen websockt 127.0.0.1:4203");
        HydraConfig cfg;
        std::string err;
        expect(!loadConfig(path, cfg, err),
               "unknown listen type websockt must fail loadConfig");
        expect(err.find("unknown type") != std::string::npos
               || err.find("websockt") != std::string::npos,
               "error should mention unknown type");
        unlink(path.c_str());
    }

    // Empty / garbage type token.
    {
        std::string path = writeConf("listen not-a-proto 127.0.0.1:1");
        HydraConfig cfg;
        std::string err;
        expect(!loadConfig(path, cfg, err),
               "garbage listen type must fail loadConfig");
        unlink(path.c_str());
    }

    // Known types still load.
    {
        std::string path = writeConf("listen websocket 127.0.0.1:4203");
        HydraConfig cfg;
        std::string err;
        expect(loadConfig(path, cfg, err),
               std::string("valid websocket listen must load: ") + err);
        expect(cfg.listeners.size() == 1 && cfg.listeners[0].websocket
               && !cfg.listeners[0].tls && !cfg.listeners[0].grpcWeb,
               "websocket listen flags");
        unlink(path.c_str());
    }
    {
        std::string path = writeConf("listen telnet 127.0.0.1:4202");
        HydraConfig cfg;
        std::string err;
        expect(loadConfig(path, cfg, err),
               std::string("valid telnet listen must load: ") + err);
        expect(cfg.listeners.size() == 1 && !cfg.listeners[0].websocket
               && !cfg.listeners[0].tls && !cfg.listeners[0].grpcWeb,
               "telnet listen is plaintext flags-all-false");
        unlink(path.c_str());
    }
    {
        std::string path = writeConf(
            "listen telnet+tls 127.0.0.1:4202 cert=c.pem key=k.pem");
        HydraConfig cfg;
        std::string err;
        expect(loadConfig(path, cfg, err),
               std::string("valid telnet+tls listen must load: ") + err);
        expect(cfg.listeners.size() == 1 && cfg.listeners[0].tls
               && !cfg.listeners[0].websocket,
               "telnet+tls flags");
        unlink(path.c_str());
    }
}

#ifdef GRPC_ENABLED
// #1887: truncated / malformed protobuf must not parse as a default request.
// The grpc-web dispatcher gates every RPC on ParseFromString; these cases
// pin that contract without spinning up SessionManager.
void testMalformedProtobufRejected() {
    const std::string truncated = bytes({0x0A, 0x05, 0x61});  // incomplete string field
    const std::string garbage = bytes({0xFF, 0xFF, 0xFF, 0xFF, 0x0F});

    hydra::AuthRequest auth;
    expect(!auth.ParseFromString(truncated),
           "truncated AuthRequest must fail ParseFromString");
    expect(!auth.ParseFromString(garbage),
           "garbage AuthRequest must fail ParseFromString");

    hydra::SessionRequest sess;
    expect(!sess.ParseFromString(truncated),
           "truncated SessionRequest must fail ParseFromString");
    expect(!sess.ParseFromString(garbage),
           "garbage SessionRequest must fail ParseFromString");

    hydra::ScrollBackRequest scroll;
    expect(!scroll.ParseFromString(truncated),
           "truncated ScrollBackRequest must fail ParseFromString");
    expect(!scroll.ParseFromString(garbage),
           "garbage ScrollBackRequest must fail ParseFromString");

    // Empty body is a valid empty message for some types — that is OK.
    hydra::Empty empty;
    expect(empty.ParseFromString(std::string()),
           "empty body should parse as Empty");
}
#endif

// #1286: a producer blocked on a full queue must be releasable.
//
// cv_space_ is only notified by processPending(), so after the main loop's
// final drain nothing wakes a parked producer.  ~GrpcServer's Shutdown()
// then waits for in-flight RPCs that can never finish.  stop() is the way
// out.
//
// Note on failure mode: if stop() regresses, the producer stays parked and
// this case hangs rather than reporting.  That is deliberate -- releasing
// it any other way would need processPending(), which wants a live
// SessionManager/AccountManager/ProcessManager.  A hang here means stop()
// no longer wakes waiters.
void testWorkQueueStopReleasesBlockedProducer() {
    WorkQueue q;

    auto noop = [](SessionManager&, AccountManager&, const HydraConfig&,
                   ProcessManager&) { return true; };
    for (size_t i = 0; i < WorkQueue::MAX_PENDING; i++) {
        q.enqueue<bool>(noop);
    }
    expect(q.pending() == WorkQueue::MAX_PENDING,
           "queue fills to exactly MAX_PENDING");

    // One more must block: there is no space and nothing is draining.
    // Keep the work future: releasing enqueue() is only half of it.
    std::future<bool> work;
    auto producer = std::async(std::launch::async,
                               [&q, &noop, &work] { work = q.enqueue<bool>(noop); });

    expect(producer.wait_for(std::chrono::milliseconds(250))
               == std::future_status::timeout,
           "producer blocks once the queue is full");

    q.stop();

    expect(producer.wait_for(std::chrono::seconds(5))
               == std::future_status::ready,
           "stop() releases a producer blocked on a full queue");

    producer.get();

    // The caller's future must also resolve.  Returning from enqueue() is
    // not enough: every gRPC handler immediately calls future.get(), so a
    // future that never settles just moves the parked thread from enqueue()
    // to get() -- still an in-flight RPC, and ~GrpcServer's Shutdown() still
    // waits on it.  hydra_main declares WorkQueue before the GrpcServer
    // unique_ptr, so ~GrpcServer runs first and ~WorkQueue never gets to
    // break the promise.  This assertion is what distinguishes completing
    // the item from queueing it past the cap.
    expect(work.valid(), "cancelled enqueue still returns a usable future");
    if (work.valid()) {
        expect(work.wait_for(std::chrono::seconds(2))
                   == std::future_status::ready,
               "the caller's future resolves rather than hanging get()");
        expect(work.get() == false,
               "a cancelled item yields the default result, not an exception");
    }

    // Idempotent: a second stop() must not deadlock or throw.
    q.stop();
    expect(true, "stop() is idempotent");
}

} // namespace

int main() {
    testSplitGmcpAcrossReads();
    testSplitTelnetNegotiationAcrossReads();
    testStripNonGmcpSubnegotiationAcrossReads();
    testSplitTtypeSignals();
    testSplitCharsetSignals();
    testCharsetRequestAndAcceptedInSameRead();
    testSplitEorSignals();
    testSplitEorAcrossReads();
    testAsciiBridgeConversion();
    testWebSocketMaskEnforcement();
    testWebSocketFragmentationAndUtf8();
    testWebSocketExtLenMaskKey();
    testWebSocketExtLen64MaskKey();
    testWebSocketCloseDelivered();
    testWebSocketRsvRejected();
    testWebSocketLargePingRejected();
    testTelnetSbCap();
    testSubscriberCountCap();
    testInputLineLimitShared();
    testWorkQueuePendingCapConstant();
    testConvertInputNonUtf8Target();
    testTruncatedUtf8CharsetEncode();
    testUnknownListenerTypeRejected();
    testPortRangeRejected();
#ifdef GRPC_ENABLED
    testMalformedProtobufRejected();
#endif
    testWorkQueueStopReleasesBlockedProducer();
    std::cout << "proxy_regression: ok\n";
    return 0;
}
