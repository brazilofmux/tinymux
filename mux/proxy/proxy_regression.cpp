#include "telnet_bridge.h"
#include "telnet_stream.h"
#include "telnet_utils.h"
#include "websocket.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
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

void testWebSocketMaskEnforcement() {
    WsState ws;
    std::string responses;
    const std::string unmasked = bytes({0x81, 0x02}) + "hi";
    auto messages = wsDecodeFrames(ws, unmasked.data(), unmasked.size(), responses);

    expect(messages.empty(), "unmasked client frame should not be delivered");
    expect(responses == wsCloseFrame(1002),
           "unmasked client frame should trigger protocol-error close");

    WsState maskedWs;
    responses.clear();
    const std::string masked = bytes({0x81, 0x82, 0x01, 0x02, 0x03, 0x04})
        + std::string(1, static_cast<char>('h' ^ 0x01))
        + std::string(1, static_cast<char>('i' ^ 0x02));
    messages = wsDecodeFrames(maskedWs, masked.data(), masked.size(), responses);

    expect(responses.empty(), "masked frame should not trigger close");
    expect(messages.size() == 1, "masked frame should decode to one message");
    expect(messages[0].opcode == WS_OP_TEXT, "masked frame opcode mismatch");
    expect(messages[0].payload == "hi", "masked frame payload mismatch");
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
    std::cout << "proxy_regression: ok\n";
    return 0;
}
