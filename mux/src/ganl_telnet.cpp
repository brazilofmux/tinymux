#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "ansi.h"

#include "ganl_telnet.h"
#include "interface.h"

// Global instance
MuxProtocolHandler g_muxProtocolHandler;

// Use GANL's TelnetCommand and TelnetOption directly instead of redefining constants
// We'll use numerical values to avoid conflicts with MUX's TELNET_XXX macros
static const unsigned char IAC_CMD = 255;    // Interpret as command
static const unsigned char DONT_CMD = 254;   // Don't do option
static const unsigned char DO_CMD = 253;     // Do option
static const unsigned char WONT_CMD = 252;   // Won't do option
static const unsigned char WILL_CMD = 251;   // Will do option
static const unsigned char SB_CMD = 250;     // Subnegotiation
static const unsigned char SE_CMD = 240;     // End subnegotiation
static const unsigned char NAWS_OPT = 31;    // Negotiate About Window Size
static const unsigned char TTYPE_OPT = 24;   // Terminal Type
static const unsigned char EOR_OPT = 25;     // End of Record
static const unsigned char CHARSET_OPT = 42; // Character Set

// Constructor
MuxProtocolHandler::MuxProtocolHandler()
    : TelnetProtocolHandler()
{
    // Initialize any MUX-specific settings here
}

// Destructor
MuxProtocolHandler::~MuxProtocolHandler()
{
    // Clean up any MUX-specific resources
}

// Create a protocol context for a new connection
bool MuxProtocolHandler::createProtocolContext(ganl::ConnectionHandle conn)
{
    // First create the basic telnet protocol context
    if (!TelnetProtocolHandler::createProtocolContext(conn)) {
        return false;
    }

    // No need to create the descriptor here as TinyMUX will create it
    // We just need to make sure the protocol context is ready
    return true;
}

// Destroy the protocol context for a connection
void MuxProtocolHandler::destroyProtocolContext(ganl::ConnectionHandle conn)
{
    // Remove from our map
    m_connectionToDescMap.erase(conn);

    // Let the parent class clean up its context
    TelnetProtocolHandler::destroyProtocolContext(conn);
}

// Start telnet negotiation with a client
void MuxProtocolHandler::startNegotiation(ganl::ConnectionHandle conn, ganl::IoBuffer& telnet_responses_out)
{
    // First call the base implementation
    TelnetProtocolHandler::startNegotiation(conn, telnet_responses_out);

    // Get the descriptor associated with this connection
    DESC* d = getDescriptorForConnection(conn);
    if (d == nullptr) {
        // No descriptor yet, can't do MUX-specific negotiation
        return;
    }

    // Add MUX-specific telnet negotiation commands
    // For example, add NAWS (window size) negotiation
    char buffer[4];
    buffer[0] = IAC_CMD;
    buffer[1] = DO_CMD;
    buffer[2] = NAWS_OPT;
    telnet_responses_out.append(buffer, 3);

    // Request terminal type
    buffer[0] = IAC_CMD;
    buffer[1] = DO_CMD;
    buffer[2] = TTYPE_OPT;
    telnet_responses_out.append(buffer, 3);

    // Ask for EOR option (End of Record)
    buffer[0] = IAC_CMD;
    buffer[1] = WILL_CMD;
    buffer[2] = EOR_OPT;
    telnet_responses_out.append(buffer, 3);
}

// Process input data from a client (Telnet protocol parsing)
bool MuxProtocolHandler::processInput(ganl::ConnectionHandle conn,
                                     ganl::IoBuffer& decrypted_in,
                                     ganl::IoBuffer& app_data_out,
                                     ganl::IoBuffer& telnet_responses_out,
                                     bool consumeInput)
{
    // Call the base class to handle basic telnet protocol
    if (!TelnetProtocolHandler::processInput(conn, decrypted_in, app_data_out, telnet_responses_out, consumeInput)) {
        return false;
    }

    // Get the descriptor for this connection
    DESC* d = getDescriptorForConnection(conn);
    if (d == nullptr) {
        // No descriptor yet, can't update MUX state
        return true;
    }

    // Update the descriptor with the new protocol state
    updateDescriptorFromProtocolState(conn, d);

    return true;
}

// Format output data for a client (add ANSI color, etc.)
bool MuxProtocolHandler::formatOutput(ganl::ConnectionHandle conn,
                                     ganl::IoBuffer& app_data_in,
                                     ganl::IoBuffer& formatted_out,
                                     bool consumeInput)
{
    // Get the descriptor for this connection
    DESC* d = getDescriptorForConnection(conn);
    if (d == nullptr) {
        // No descriptor, just pass through the data
        if (consumeInput) {
            size_t len = app_data_in.readableBytes();
            formatted_out.ensureWritable(len);
            const char* data = app_data_in.readPtr();
            formatted_out.append(data, len);
            app_data_in.consumeRead(len);
        } else {
            size_t len = app_data_in.readableBytes();
            formatted_out.ensureWritable(len);
            const char* data = app_data_in.readPtr();
            formatted_out.append(data, len);
        }
        return true;
    }

    // Apply ANSI color formatting based on client capabilities
    ganl::ProtocolState state = getProtocolState(conn);
    bool useAnsi = state.supportsANSI;

    // Read the input data
    size_t len = app_data_in.readableBytes();
    const char* data = app_data_in.readPtr();

    std::string output(data, len);

    if (consumeInput) {
        app_data_in.consumeRead(len);
    }

    // If the client supports ANSI or Xterm colors, we leave them intact
    // If not, we need to strip ANSI sequences
    if (!useAnsi) {
        // Strip ANSI sequences - simplified version
        size_t pos = 0;
        while ((pos = output.find(ESC_CHAR, pos)) != std::string::npos) {
            size_t end = output.find('m', pos);
            if (end != std::string::npos) {
                output.erase(pos, end - pos + 1);
            } else {
                break;
            }
        }
    }

    // Write the formatted output
    formatted_out.append(output.c_str(), output.length());

    return true;
}

// Custom telnet option negotiation to handle MUX-specific options
// This is NOT an override since the base class method is protected
void MuxProtocolHandler::handleTelnetOptionNegotiation(ganl::ConnectionHandle conn,
                                                     ganl::TelnetCommand cmd,
                                                     ganl::TelnetOption opt,
                                                     ganl::IoBuffer& telnet_responses_out)
{
    // We can't call the base class implementation directly since it's protected
    // But we can handle our MUX-specific options here

    // Get the descriptor for this connection
    DESC* d = getDescriptorForConnection(conn);
    if (d == nullptr) {
        return;
    }

    // Handle MUX-specific telnet options
    switch (static_cast<unsigned char>(opt)) {
        case TELNET_NAWS:
            if (cmd == ganl::TelnetCommand::WILL) {
                // Client supports NAWS, but there's no flag for this in TinyMUX
                // We'll update the descriptor when we receive NAWS data
                // Send DO NAWS to confirm we support it
                char buffer[3];
                buffer[0] = IAC_CMD;
                buffer[1] = DO_CMD;
                buffer[2] = NAWS_OPT;
                telnet_responses_out.append(buffer, 3);
            }
            break;

        case TELNET_TTYPE:
            if (cmd == ganl::TelnetCommand::WILL) {
                // Client supports terminal type

                // Request the terminal type
                char buffer[6];
                buffer[0] = IAC_CMD;
                buffer[1] = SB_CMD;
                buffer[2] = TTYPE_OPT;
                buffer[3] = 1;  // SEND
                buffer[4] = IAC_CMD;
                buffer[5] = SE_CMD;
                telnet_responses_out.append(buffer, 6);
            }
            break;

        default:
            // For other options, send a generic response
            if (cmd == ganl::TelnetCommand::WILL || cmd == ganl::TelnetCommand::DO) {
                // Generally refuse options we don't specifically handle
                char buffer[3];
                buffer[0] = IAC_CMD;
                buffer[1] = (cmd == ganl::TelnetCommand::WILL) ? DONT_CMD : WONT_CMD;
                buffer[2] = static_cast<unsigned char>(opt);
                telnet_responses_out.append(buffer, 3);
            }
            break;
    }
}

// Process subnegotiation data for MUX-specific options
void MuxProtocolHandler::processSubnegotiationData(ganl::ConnectionHandle conn,
                                                 ganl::TelnetOption opt,
                                                 ganl::IoBuffer& telnet_responses_out)
{
    // Handle MUX-specific subnegotiations

    // Get the descriptor for this connection
    DESC* d = getDescriptorForConnection(conn);
    if (d == nullptr) {
        return;
    }

    // We can't directly call the base class's private methods
    // In a future implementation, we'll properly handle subnegotiations

    // Get the protocol state
    ganl::ProtocolState state = getProtocolState(conn);

    // Handle MUX-specific subnegotiations
    switch (static_cast<unsigned char>(opt)) {
        case TELNET_NAWS: {
            // Window size negotiation
            // Update the descriptor
            d->width = state.width;
            d->height = state.height;
            break;
        }

        case TELNET_TTYPE: {
            // Terminal type negotiation
            // We need to check if it supports ANSI
            // TinyMUX doesn't have a direct ANSI flag, we'll check for "ANSI" in
            // the terminal type in the full implementation
            break;
        }

        default:
            // Do nothing for other option types
            break;
    }
}

// Associate a GANL connection with a TinyMUX descriptor
bool MuxProtocolHandler::associateWithDescriptor(ganl::ConnectionHandle conn, DESC* d)
{
    if (d == nullptr) {
        return false;
    }

    // Store the association
    m_connectionToDescMap[conn] = d;

    // Initialize the protocol state from the descriptor
    updateProtocolStateFromDescriptor(conn, d);

    return true;
}

// Get the descriptor associated with a connection
DESC* MuxProtocolHandler::getDescriptorForConnection(ganl::ConnectionHandle conn)
{
    auto it = m_connectionToDescMap.find(conn);
    if (it != m_connectionToDescMap.end()) {
        return it->second;
    }
    return nullptr;
}

// Update a MUX descriptor from the GANL protocol state
void MuxProtocolHandler::updateDescriptorFromProtocolState(ganl::ConnectionHandle conn, DESC* d)
{
    if (d == nullptr) {
        return;
    }

    // Get the protocol state
    ganl::ProtocolState state = getProtocolState(conn);

    // TODO: Update descriptor flags for ANSI support when available

    // Update width and height
    d->width = state.width;
    d->height = state.height;

    // Update encoding
    switch (getEncoding(conn)) {
        case ganl::EncodingType::ASCII:
            d->encoding = CHARSET_ASCII; // ASCII in TinyMUX
            break;
        case ganl::EncodingType::Latin1:
            d->encoding = CHARSET_LATIN1; // Latin-1 in TinyMUX
            break;
        case ganl::EncodingType::CP437:
            d->encoding = CHARSET_CP437; // CP437 in TinyMUX
            break;
        case ganl::EncodingType::UTF8:
        default:
            d->encoding = CHARSET_UTF8; // UTF-8 in TinyMUX
            break;
    }
}

// Update the GANL protocol state from a MUX descriptor
void MuxProtocolHandler::updateProtocolStateFromDescriptor(ganl::ConnectionHandle conn, DESC* d)
{
    if (d == nullptr) {
        return;
    }

    // Set the encoding
    ganl::EncodingType encoding;
    switch (d->encoding) {
        case CHARSET_ASCII:
            encoding = ganl::EncodingType::ASCII;
            break;
        case CHARSET_LATIN1:
            encoding = ganl::EncodingType::Latin1;
            break;
        case CHARSET_CP437:
            encoding = ganl::EncodingType::CP437;
            break;
        case CHARSET_UTF8:
        default:
            encoding = ganl::EncodingType::UTF8;
            break;
    }
    setEncoding(conn, encoding);

    // Update width and height
    updateWidth(conn, d->width);
    updateHeight(conn, d->height);

    // TinyMUX doesn't have a direct ANSI flag, so there's no flag to check
    // in the descriptor. We'll use the terminal type to determine this when
    // we receive it.
}