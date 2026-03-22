#include "telnet_bridge.h"

std::string TelnetBridge::ingestGameOutput(
    const ganl::ProtocolState& gameState,
    const std::string& gameBytes) {
    // TODO: charset-decode to UTF-8, then co_parse_ansi() -> PUA
    // For now, pass through unchanged.
    (void)gameState;
    return gameBytes;
}

std::string TelnetBridge::renderForClient(
    const ganl::ProtocolState& clientState,
    const std::string& puaUtf8) {
    // TODO: co_render_ansi*() based on client color depth,
    //       then charset-encode to client's encoding.
    // For now, pass through unchanged.
    (void)clientState;
    return puaUtf8;
}

std::string TelnetBridge::convertInput(
    const ganl::ProtocolState& clientState,
    const ganl::ProtocolState& gameState,
    const std::string& clientLine) {
    // TODO: charset conversion client -> game
    (void)clientState;
    (void)gameState;
    return clientLine;
}
