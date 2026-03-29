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
    bool sawWill = false;
    bool sawDo = false;

    const std::string chunk1 = "look\r\n" + bytes({0xff, 0xfa, 0xc9}) + "Core.Hello ";
    const std::string chunk2 = "{}" + bytes({0xff, 0xf0});

    splitTelnetStream(chunk1.data(), chunk1.size(), state, regular, gmcp,
                      sawWill, sawDo);
    expect(regular == "look\r\n", "chunk1 regular text mismatch");
    expect(gmcp.empty(), "chunk1 should not complete GMCP");
    expect(state.state == TelnetParseState::InGmcpSB,
           "chunk1 should leave parser inside GMCP subnegotiation");

    splitTelnetStream(chunk2.data(), chunk2.size(), state, regular, gmcp,
                      sawWill, sawDo);
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
    bool sawWill = false;
    bool sawDo = false;

    const std::string chunk1 = bytes({0xff, 0xfb});
    const std::string chunk2 = bytes({0xc9});

    splitTelnetStream(chunk1.data(), chunk1.size(), state, regular, gmcp,
                      sawWill, sawDo);
    expect(!sawWill, "partial WILL GMCP should not fire early");
    expect(state.state == TelnetParseState::SawCmd,
           "parser should remember partial WILL command");

    splitTelnetStream(chunk2.data(), chunk2.size(), state, regular, gmcp,
                      sawWill, sawDo);
    expect(sawWill, "WILL GMCP should be detected across reads");
    expect(state.state == TelnetParseState::Normal,
           "parser should return to Normal after option byte");
}

void testStripNonGmcpSubnegotiationAcrossReads() {
    TelnetParseState state;
    std::string regular;
    std::vector<TelnetGmcpMessage> gmcp;
    bool sawWill = false;
    bool sawDo = false;

    const std::string chunk1 = "A" + bytes({0xff, 0xfa, 0x1f});
    const std::string chunk2 = bytes({0x00, 0x50, 0x00, 0x28, 0xff, 0xf0}) + "B";

    splitTelnetStream(chunk1.data(), chunk1.size(), state, regular, gmcp,
                      sawWill, sawDo, true);
    splitTelnetStream(chunk2.data(), chunk2.size(), state, regular, gmcp,
                      sawWill, sawDo, true);

    expect(regular == "AB", "stripTelnet should remove split NAWS subnegotiation");
    expect(gmcp.empty(), "NAWS test should not create GMCP messages");
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
    testAsciiBridgeConversion();
    testWebSocketMaskEnforcement();
    std::cout << "proxy_regression: ok\n";
    return 0;
}
