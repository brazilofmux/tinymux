#include "telnet_stream.h"
#include "telnet_utils.h"

static constexpr unsigned char T_IAC  = telnet::IAC;
static constexpr unsigned char T_SB   = telnet::SB;
static constexpr unsigned char T_SE   = telnet::SE;
static constexpr unsigned char T_GMCP = telnet::GMCP;
static constexpr unsigned char T_WILL = telnet::WILL;
static constexpr unsigned char T_WONT = telnet::WONT;
static constexpr unsigned char T_DO   = telnet::DO;
static constexpr unsigned char T_DONT = telnet::DONT;

void splitTelnetStream(const char* data, size_t len,
                       TelnetParseState& parseState,
                       std::string& regular,
                       std::vector<TelnetGmcpMessage>& gmcp,
                       TelnetSignals& signals,
                       bool stripTelnet) {
    signals = {};
    regular.reserve(regular.size() + len);

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = static_cast<unsigned char>(data[i]);

        switch (parseState.state) {
        case TelnetParseState::Normal:
            if (ch == T_IAC) { parseState.state = TelnetParseState::SawIAC; }
            else { regular.push_back(static_cast<char>(ch)); }
            break;

        case TelnetParseState::SawIAC:
            if (ch == T_IAC) {
                regular.push_back(static_cast<char>(T_IAC));
                parseState.state = TelnetParseState::Normal;
            } else if (ch == T_SB) {
                parseState.state = TelnetParseState::SawSB;
            } else if (ch == T_WILL || ch == T_WONT ||
                       ch == T_DO   || ch == T_DONT) {
                parseState.cmdByte = ch;
                parseState.state = TelnetParseState::SawCmd;
            } else {
                if (ch == telnet::EOR_CMD) {
                    signals.sawEor = true;
                }
                if (!stripTelnet) {
                    regular.push_back(static_cast<char>(T_IAC));
                    regular.push_back(static_cast<char>(ch));
                }
                parseState.state = TelnetParseState::Normal;
            }
            break;

        case TelnetParseState::SawCmd:
            if (ch == T_GMCP) {
                if (parseState.cmdByte == T_WILL) signals.sawWillGmcp = true;
                if (parseState.cmdByte == T_DO)   signals.sawDoGmcp = true;
            }
            if (ch == telnet::EOR_OPT) {
                if (parseState.cmdByte == T_DO) {
                    signals.sawDoEor = true;
                } else if (parseState.cmdByte == T_DONT) {
                    signals.sawDontEor = true;
                }
            }
            if (ch == telnet::TTYPE && parseState.cmdByte == T_DO) {
                signals.sawDoTtype = true;
            }
            if (ch == telnet::CHARSET && parseState.cmdByte == T_DO) {
                signals.sawDoCharset = true;
            }
            if (!stripTelnet) {
                regular.push_back(static_cast<char>(T_IAC));
                regular.push_back(static_cast<char>(parseState.cmdByte));
                regular.push_back(static_cast<char>(ch));
            }
            parseState.state = TelnetParseState::Normal;
            break;

        case TelnetParseState::SawSB:
            if (ch == T_GMCP) {
                parseState.gmcpBuf.clear();
                parseState.state = TelnetParseState::InGmcpSB;
            } else {
                parseState.otherSBBuf.clear();
                parseState.otherSBBuf.push_back(static_cast<char>(ch));
                parseState.state = TelnetParseState::InOtherSB;
            }
            break;

        case TelnetParseState::InOtherSB:
            if (ch == T_IAC) {
                parseState.state = TelnetParseState::InOtherSBIAC;
            } else {
                parseState.otherSBBuf.push_back(static_cast<char>(ch));
            }
            break;

        case TelnetParseState::InOtherSBIAC:
            if (ch == T_SE) {
                if (!parseState.otherSBBuf.empty() &&
                    static_cast<unsigned char>(parseState.otherSBBuf[0]) == telnet::TTYPE &&
                    parseState.otherSBBuf.size() >= 2 &&
                    static_cast<unsigned char>(parseState.otherSBBuf[1]) == telnet::TELQUAL_SEND) {
                    signals.sawTtypeSend = true;
                } else if (!parseState.otherSBBuf.empty() &&
                           static_cast<unsigned char>(parseState.otherSBBuf[0]) == telnet::CHARSET &&
                           parseState.otherSBBuf.size() >= 2) {
                    unsigned char subcmd =
                        static_cast<unsigned char>(parseState.otherSBBuf[1]);
                    if (subcmd == telnet::TELQUAL_REQUEST) {
                        signals.sawCharsetRequest = true;
                        signals.charsetRequestPayload.assign(parseState.otherSBBuf.begin() + 2,
                                                             parseState.otherSBBuf.end());
                    } else if (subcmd == telnet::TELQUAL_ACCEPTED) {
                        signals.sawCharsetAccepted = true;
                        signals.charsetAcceptedPayload.assign(parseState.otherSBBuf.begin() + 2,
                                                              parseState.otherSBBuf.end());
                    } else if (subcmd == telnet::TELQUAL_REJECTED) {
                        signals.sawCharsetRejected = true;
                    }
                }
                if (!stripTelnet) {
                    regular.push_back(static_cast<char>(T_IAC));
                    regular.push_back(static_cast<char>(T_SB));
                    regular.append(parseState.otherSBBuf);
                    regular.push_back(static_cast<char>(T_IAC));
                    regular.push_back(static_cast<char>(T_SE));
                }
                parseState.otherSBBuf.clear();
                parseState.state = TelnetParseState::Normal;
            } else if (ch == T_IAC) {
                parseState.otherSBBuf.push_back(static_cast<char>(T_IAC));
                parseState.state = TelnetParseState::InOtherSB;
            } else {
                parseState.otherSBBuf.push_back(static_cast<char>(T_IAC));
                parseState.otherSBBuf.push_back(static_cast<char>(ch));
                parseState.state = TelnetParseState::InOtherSB;
            }
            break;

        case TelnetParseState::InGmcpSB:
            if (ch == T_IAC) { parseState.state = TelnetParseState::InGmcpIAC; }
            else { parseState.gmcpBuf.push_back(static_cast<char>(ch)); }
            break;

        case TelnetParseState::InGmcpIAC:
            if (ch == T_SE) {
                gmcp.push_back({parseState.gmcpBuf});
                parseState.gmcpBuf.clear();
                parseState.state = TelnetParseState::Normal;
            } else if (ch == T_IAC) {
                parseState.gmcpBuf.push_back(static_cast<char>(T_IAC));
                parseState.state = TelnetParseState::InGmcpSB;
            } else {
                parseState.gmcpBuf.push_back(static_cast<char>(T_IAC));
                parseState.gmcpBuf.push_back(static_cast<char>(ch));
                parseState.state = TelnetParseState::InGmcpSB;
            }
            break;
        }
    }
}
