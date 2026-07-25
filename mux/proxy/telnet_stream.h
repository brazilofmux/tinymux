#ifndef HYDRA_TELNET_STREAM_H
#define HYDRA_TELNET_STREAM_H

#include <string>
#include <vector>

// #1101: cap IAC SB reassembly so a hostile peer cannot grow memory forever.
constexpr size_t TELNET_SB_MAX = 65536;

struct TelnetParseState {
    enum State {
        Normal,
        SawIAC,
        SawSB,
        InGmcpSB,
        InGmcpIAC,
        SawCmd,
        InOtherSB,
        InOtherSBIAC
    };

    State state{Normal};
    unsigned char cmdByte{0};
    std::string gmcpBuf;
    std::string otherSBBuf;
    bool sbOverflow{false};  // set when gmcpBuf/otherSBBuf hit TELNET_SB_MAX
};

struct TelnetGmcpMessage {
    std::string payload;
};

struct TelnetSignals {
    bool sawWillGmcp{false};
    bool sawDoGmcp{false};
    bool sawDoTtype{false};
    bool sawDontTtype{false};
    bool sawTtypeSend{false};
    bool sawDoCharset{false};
    bool sawDontCharset{false};
    bool sawDoEor{false};
    bool sawDontEor{false};
    bool sawDontNaws{false};
    bool sawCharsetRequest{false};
    bool sawCharsetAccepted{false};
    bool sawCharsetRejected{false};
    bool sawEor{false};
    std::string charsetRequestPayload;
    std::string charsetAcceptedPayload;
};

void splitTelnetStream(const char* data, size_t len,
                       TelnetParseState& parseState,
                       std::string& regular,
                       std::vector<TelnetGmcpMessage>& gmcp,
                       TelnetSignals& signals,
                       bool stripTelnet = false);

#endif // HYDRA_TELNET_STREAM_H
