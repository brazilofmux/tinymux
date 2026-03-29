#ifndef HYDRA_TELNET_STREAM_H
#define HYDRA_TELNET_STREAM_H

#include <string>
#include <vector>

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
};

struct TelnetGmcpMessage {
    std::string payload;
};

void splitTelnetStream(const char* data, size_t len,
                       TelnetParseState& parseState,
                       std::string& regular,
                       std::vector<TelnetGmcpMessage>& gmcp,
                       bool& sawWillGmcp, bool& sawDoGmcp,
                       bool stripTelnet = false);

#endif // HYDRA_TELNET_STREAM_H
