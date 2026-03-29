#ifndef HYDRA_TELNET_BRIDGE_H
#define HYDRA_TELNET_BRIDGE_H

#include "hydra_types.h"
#include <network_types.h>
#include <string>

class TelnetBridge {
public:
    // Game -> Hydra internal (back-door ingestion)
    // Charset-decode game bytes to UTF-8, then ANSI->PUA.
    std::string ingestGameOutput(const ganl::ProtocolState& gameState,
                                const char* data, size_t len,
                                std::string* utf8Carry = nullptr);

    // Hydra internal -> Client (front-door output)
    // Render PUA->ANSI at client's color depth, charset-encode.
    std::string renderForClient(ganl::EncodingType clientEncoding,
                                ColorDepth colorDepth,
                                const std::string& puaUtf8);

    // Client -> Game (front-door input)
    // Charset conversion only.
    std::string convertInput(ganl::EncodingType clientEncoding,
                             ganl::EncodingType gameEncoding,
                             const std::string& clientLine);
};

#endif // HYDRA_TELNET_BRIDGE_H
