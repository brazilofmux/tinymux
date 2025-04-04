#ifndef GANL_MUX_PROTOCOL_HANDLER_H
#define GANL_MUX_PROTOCOL_HANDLER_H

#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "ansi.h"

#include "ganl_types.h"
#include "ganl/include/protocol_handler.h"
#include "ganl/include/telnet_protocol_handler.h"
#include "interface.h"

// This class bridges between GANL's ProtocolHandler interface and TinyMUX's telnet handling.
// It extends the TelnetProtocolHandler to integrate with TinyMUX's DESC-based handling.

class MuxProtocolHandler : public ganl::TelnetProtocolHandler {
public:
    MuxProtocolHandler();
    ~MuxProtocolHandler() override;

    // Override methods to bridge to TinyMUX's telnet handling
    bool createProtocolContext(ganl::ConnectionHandle conn) override;
    void destroyProtocolContext(ganl::ConnectionHandle conn) override;

    void startNegotiation(ganl::ConnectionHandle conn, ganl::IoBuffer& telnet_responses_out) override;
    bool processInput(ganl::ConnectionHandle conn, ganl::IoBuffer& decrypted_in,
                      ganl::IoBuffer& app_data_out, ganl::IoBuffer& telnet_responses_out,
                      bool consumeInput = true) override;
    bool formatOutput(ganl::ConnectionHandle conn, ganl::IoBuffer& app_data_in,
                      ganl::IoBuffer& formatted_out, bool consumeInput = true) override;

    // These are not overrides but new public methods that use protected methods
    void handleTelnetOptionNegotiation(ganl::ConnectionHandle conn,
                                      ganl::TelnetCommand cmd,
                                      ganl::TelnetOption opt,
                                      ganl::IoBuffer& telnet_responses_out);
    void processSubnegotiationData(ganl::ConnectionHandle conn,
                                  ganl::TelnetOption opt,
                                  ganl::IoBuffer& telnet_responses_out);

    // MUX-specific methods for integration with DESC
    bool associateWithDescriptor(ganl::ConnectionHandle conn, DESC* d);
    DESC* getDescriptorForConnection(ganl::ConnectionHandle conn);

    // Methods to convert between GANL telnet structures and TinyMUX structures
    void updateDescriptorFromProtocolState(ganl::ConnectionHandle conn, DESC* d);
    void updateProtocolStateFromDescriptor(ganl::ConnectionHandle conn, DESC* d);

private:
    // Map of connection handles to TinyMUX descriptors
    std::unordered_map<ganl::ConnectionHandle, DESC*> m_connectionToDescMap;
};

// Global instance
extern MuxProtocolHandler g_muxProtocolHandler;

#endif // GANL_MUX_PROTOCOL_HANDLER_H