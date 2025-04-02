#ifndef GANL_PROTOCOL_HANDLER_H
#define GANL_PROTOCOL_HANDLER_H

#include "network_types.h"
#include "io_buffer.h"
#include <string>

namespace ganl {

enum class NegotiationStatus {
    InProgress, // Still actively negotiating or waiting for responses/timeouts
    Completed,  // Mandatory initial negotiation finished successfully
    Failed      // Negotiation failed irrecoverably (e.g., conflicting options)
};

/**
 * ProtocolHandler - Interface for telnet and text protocol handling
 */
class ProtocolHandler {
public:
    virtual ~ProtocolHandler() = default;

    /**
     * Create protocol context for a connection
     *
     * @param conn Connection handle
     * @return true on success, false on failure
     */
    virtual bool createProtocolContext(ConnectionHandle conn) = 0;

    /**
     * Destroy protocol context for a connection
     *
     * @param conn Connection handle
     */
    virtual void destroyProtocolContext(ConnectionHandle conn) = 0;

    /**
     * Start telnet negotiation
     *
     * @param conn Connection handle
     * @param telnet_responses_out Buffer to receive telnet commands
     */
    virtual void startNegotiation(ConnectionHandle conn, IoBuffer& telnet_responses_out) = 0;

    /**
     * Process input data (telnet processing, line assembly)
     *
     * @param conn Connection handle
     * @param decrypted_in Buffer containing decrypted input
     * @param app_data_out Buffer to receive application data
     * @param telnet_responses_out Buffer to receive telnet responses
     * @param consumeInput Whether to consume processed data from the input buffer
     * @return true on success, false on failure
     */
    virtual bool processInput(ConnectionHandle conn, IoBuffer& decrypted_in,
                             IoBuffer& app_data_out, IoBuffer& telnet_responses_out,
                             bool consumeInput = true) = 0;

    /**
     * Format output data (apply ANSI color, etc.)
     *
     * @param conn Connection handle
     * @param app_data_in Buffer containing application data
     * @param formatted_out Buffer to receive formatted data
     * @param consumeInput Whether to consume processed data from the input buffer
     * @return true on success, false on failure
     */
    virtual bool formatOutput(ConnectionHandle conn, IoBuffer& app_data_in,
                             IoBuffer& formatted_out, bool consumeInput = true) = 0;

    // Add this method:
    /**
     * @brief Checks the status of the initial mandatory Telnet negotiation phase.
     * @param conn The connection handle.
     * @return The current negotiation status.
     */
    virtual NegotiationStatus getNegotiationStatus(ConnectionHandle conn) = 0;

    /**
     * Set character encoding
     *
     * @param conn Connection handle
     * @param encoding Encoding type
     * @return true on success, false on failure
     */
    virtual bool setEncoding(ConnectionHandle conn, EncodingType encoding) = 0;

    /**
     * Get current character encoding
     *
     * @param conn Connection handle
     * @return Current encoding type
     */
    virtual EncodingType getEncoding(ConnectionHandle conn) = 0;

    /**
     * Get protocol state
     *
     * @param conn Connection handle
     * @return Protocol state
     */
    virtual ProtocolState getProtocolState(ConnectionHandle conn) = 0;

    /**
     * Update terminal width
     *
     * @param conn Connection handle
     * @param width Terminal width
     */
    virtual void updateWidth(ConnectionHandle conn, uint16_t width) = 0;

    /**
     * Update terminal height
     *
     * @param conn Connection handle
     * @param height Terminal height
     */
    virtual void updateHeight(ConnectionHandle conn, uint16_t height) = 0;

    /**
     * Get last protocol error string
     *
     * @param conn Connection handle
     * @return Error string
     */
    virtual std::string getLastProtocolErrorString(ConnectionHandle conn) = 0;
};

} // namespace ganl

#endif // GANL_PROTOCOL_HANDLER_H
