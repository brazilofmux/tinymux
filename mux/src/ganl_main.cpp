#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "ganl_types.h"
#include "ganl_mux_session.h"
#include "ganl_telnet.h"
#include "ganl/include/network_engine.h"
#include "ganl/include/network_engine_factory.h"
#include "ganl/include/secure_transport_factory.h"

// Global instances of key GANL components
static std::unique_ptr<ganl::NetworkEngine> g_networkEngine;
static std::unique_ptr<ganl::SecureTransport> g_secureTransport;
static bool g_ganl_initialized = false;

// Event buffer for network events
static ganl::IoEvent g_events[100];

// Initialize the GANL networking subsystem
bool initialize_ganl_networking()
{
    if (g_ganl_initialized) {
        return true;  // Already initialized
    }

    // Initialize the session manager
    if (!g_muxSessionManager.initialize()) {
        return false;
    }

    // Create the network engine based on platform capabilities
    g_networkEngine = ganl::NetworkEngineFactory::createEngine();
    if (!g_networkEngine) {
        return false;
    }

    // Initialize the network engine
    if (!g_networkEngine->initialize()) {
        g_networkEngine.reset();
        return false;
    }

    // Initialize our MuxProtocolHandler instance (already defined in ganl_telnet.cpp)
    // This will handle telnet protocol negotiation

    // Create secure transport if SSL is enabled
#ifdef UNIX_SSL
    g_secureTransport = ganl::SecureTransportFactory::createTransport();
    if (!g_secureTransport) {
        // Continue without SSL - this is not a fatal error
    }
#endif

    g_ganl_initialized = true;
    return true;
}

// Shutdown the GANL networking subsystem
void shutdown_ganl_networking()
{
    if (!g_ganl_initialized) {
        return;  // Not initialized
    }

    // Shutdown the session manager
    g_muxSessionManager.shutdown();

    // Shutdown and reset the secure transport
    g_secureTransport.reset();

    // Our global MuxProtocolHandler g_muxProtocolHandler will be automatically destroyed

    // Shutdown and reset the network engine
    if (g_networkEngine) {
        g_networkEngine->shutdown();
        g_networkEngine.reset();
    }

    g_ganl_initialized = false;
}

// Process GANL network events with a timeout
void ganl_process_events(int timeout_ms)
{
    if (!g_ganl_initialized || !g_networkEngine) {
        return;  // Not initialized
    }

    // Process events for up to timeout_ms milliseconds
    // Get up to 100 events at a time
    int numEvents = g_networkEngine->processEvents(timeout_ms, g_events, 100);

    // Handle events
    for (int i = 0; i < numEvents; i++) {
        const ganl::IoEvent& event = g_events[i];

        switch (event.type) {
            case ganl::IoEventType::Accept: {
                // New connection accepted
                std::string remoteAddress = g_networkEngine->getRemoteAddress(event.connection);

                // Create a TinyMUX descriptor for this connection
                mux_sockaddr addr;
                DESC* d = nullptr;

                // TODO: Actually initialize the descriptor correctly
                // For now this is a placeholder

                // Just use a temporary structure for now
                d = alloc_desc("ganl_accept");
                // Store the connection handle as the socket
                d->socket = static_cast<SOCKET>(event.connection);
                d->connected_at.GetUTC();
                d->last_time = d->connected_at;

                // Associate the descriptor with the connection
                // Store a simple mapping from connection to descriptor
                // We'll implement a real protocol handler later

                // Associate the descriptor with our MuxProtocolHandler
                g_muxProtocolHandler.createProtocolContext(event.connection);
                g_muxProtocolHandler.associateWithDescriptor(event.connection, d);

                // Start telnet negotiation
                ganl::IoBuffer telnetCommands;
                g_muxProtocolHandler.startNegotiation(event.connection, telnetCommands);

                // Send initial telnet negotiation
                if (telnetCommands.readableBytes() > 0) {
                    ganl::ErrorCode error = 0;
                    size_t len = telnetCommands.readableBytes();
                    const char* data = telnetCommands.readPtr();
                    g_networkEngine->postWrite(event.connection, data, len, error);
                }

                // Register with session manager
                g_muxSessionManager.onConnectionOpen(event.connection, remoteAddress);
                break;
            }

            case ganl::IoEventType::Read: {
                // Data available to read
                // Get the descriptor for this connection from our MuxProtocolHandler
                DESC* d = g_muxProtocolHandler.getDescriptorForConnection(event.connection);

                if (d != nullptr) {
                    // Process the input data through our telnet handler
                    ganl::IoBuffer inputData;
                    ganl::IoBuffer appData;
                    ganl::IoBuffer telnetResponses;

                    // Create a buffer to hold the read data
                    char buffer[4096]; // Use a reasonably sized buffer
                    size_t len = event.bytesTransferred;

                    if (len > 0) {
                        // Copy the data into our input buffer
                        inputData.append(buffer, len);

                        // Process through our MuxProtocolHandler
                        g_muxProtocolHandler.processInput(event.connection, inputData, appData, telnetResponses);

                        // Send any telnet responses
                        if (telnetResponses.readableBytes() > 0) {
                            ganl::ErrorCode error = 0;
                            const char* respData = telnetResponses.readPtr();
                            size_t respLen = telnetResponses.readableBytes();
                            g_networkEngine->postWrite(event.connection, respData, respLen, error);
                        }

                        // Handle application data if any
                        if (appData.readableBytes() > 0) {
                            // Convert to a string and process as a command
                            std::string commandLine(appData.readPtr(), appData.readableBytes());

                            // Update the last activity time
                            d->last_time.GetUTC();

                            // Process the input using TinyMUX's command handling
                            // TODO: Update this to use proper MUX handling
                            // For now, we'll directly queue the command to the descriptor
                            do_command(d, const_cast<UTF8 *>(reinterpret_cast<const UTF8 *>(commandLine.c_str())));
                        }
                    }

                    // Post another read
                    ganl::ErrorCode error = 0;
                    g_networkEngine->postRead(event.connection, buffer, sizeof(buffer), error);
                }
                break;
            }

            case ganl::IoEventType::Write: {
                // Ready to write data - nothing special needed here
                break;
            }

            case ganl::IoEventType::Close: {
                // Connection closed
                // Get the descriptor for this connection from our MuxProtocolHandler
                DESC* d = g_muxProtocolHandler.getDescriptorForConnection(event.connection);

                if (d != nullptr) {
                    // Clean up using TinyMUX's connection shutdown
                    shutdownsock(d, R_QUIT);
                }

                // Clean up GANL resources
                g_muxProtocolHandler.destroyProtocolContext(event.connection);

                ganl::SessionId sessionId = ganl::InvalidSessionId;
                if (d != nullptr) {
                    sessionId = g_muxSessionManager.getSessionIdFromDesc(d);
                }
                g_muxSessionManager.onConnectionClose(sessionId,
                                                      ganl::DisconnectReason::NetworkError);
                break;
            }

            case ganl::IoEventType::Error: {
                // Error occurred
                // Get the descriptor for this connection from our MuxProtocolHandler
                DESC* d = g_muxProtocolHandler.getDescriptorForConnection(event.connection);

                if (d != nullptr) {
                    // Clean up using TinyMUX's connection shutdown
                    shutdownsock(d, R_SOCKDIED);
                }

                // Clean up GANL resources
                g_muxProtocolHandler.destroyProtocolContext(event.connection);

                ganl::SessionId sessionId = ganl::InvalidSessionId;
                if (d != nullptr) {
                    sessionId = g_muxSessionManager.getSessionIdFromDesc(d);
                }
                g_muxSessionManager.onConnectionClose(sessionId,
                                                      ganl::DisconnectReason::TlsError);
                break;
            }

            default:
                break;
        }
    }
}

// Add a listening port to GANL
bool ganl_add_listener(const char* host, int port, bool use_ssl)
{
    if (!g_ganl_initialized || !g_networkEngine) {
        return false;  // Not initialized
    }

    // Create a listener
    ganl::ErrorCode error = 0;
    std::string hostStr(host);
    ganl::ListenerHandle listener = g_networkEngine->createListener(hostStr, static_cast<uint16_t>(port), error);
    if (listener == ganl::InvalidListenerHandle) {
        return false;
    }

    // Start listening for connections
    if (!g_networkEngine->startListening(listener, nullptr, error)) {
        g_networkEngine->closeListener(listener);
        return false;
    }

    // TODO: Set up SSL with GANL in future
#ifdef UNIX_SSL
    if (use_ssl && g_secureTransport) {
        // This API doesn't exist in GANL yet, need to implement
        // For now, log that SSL is not yet supported
        STARTLOG(LOG_ALWAYS, "NET", "SSL");
        log_text(T("GANL SSL support not yet implemented"));
        ENDLOG;
    }
#endif

    return true;
}

// Send data to a client (using the MUX output system)
bool ganl_send_data(ganl::ConnectionHandle conn, const char* data, size_t length)
{
    if (!g_ganl_initialized || !g_networkEngine) {
        return false;  // Not initialized
    }

    // Get the descriptor for this connection
    DESC* d = g_muxProtocolHandler.getDescriptorForConnection(conn);

    // Format the output using our MuxProtocolHandler
    ganl::IoBuffer inputBuffer;
    ganl::IoBuffer formattedBuffer;

    // Copy the data into the input buffer
    inputBuffer.append(data, length);

    // Format the output (ANSI color handling, etc.)
    g_muxProtocolHandler.formatOutput(conn, inputBuffer, formattedBuffer);

    // Send the formatted data
    if (formattedBuffer.readableBytes() > 0) {
        ganl::ErrorCode error = 0;
        const char* fmtData = formattedBuffer.readPtr();
        size_t fmtLen = formattedBuffer.readableBytes();
        return g_networkEngine->postWrite(conn, fmtData, fmtLen, error);
    }

    return true;
}

// Called when a new connection is accepted
// This integrates with the existing TinyMUX descriptor system
void ganl_handle_new_connection(ganl::ConnectionHandle connection, const char* host, int port)
{
    // Create a traditional TinyMUX descriptor, but use GANL for I/O
    mux_sockaddr addr;
    struct sockaddr_in* sin = reinterpret_cast<struct sockaddr_in*>(addr.sa());
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = inet_addr(host);
    sin->sin_port = htons(static_cast<uint16_t>(port));

    // Create a descriptor for this connection
    DESC* d = alloc_desc("ganl_handle_new_connection");
    d->socket = static_cast<SOCKET>(connection);
    d->connected_at.GetUTC();
    d->last_time = d->connected_at;

    // We need to copy the address info
    memcpy(&d->address, addr.sa(), sizeof(struct sockaddr));

    // Setup telnet protocol handling
    d->flags &= ~DS_CONNECTED;  // Not connected yet until we finish negotiation
    // For now we'll handle negotiation tracking in the MuxProtocolHandler

    // Associate the descriptor with the connection in our telnet handler
    g_muxProtocolHandler.createProtocolContext(connection);
    g_muxProtocolHandler.associateWithDescriptor(connection, d);

    // Start telnet negotiation
    ganl::IoBuffer telnetCommands;
    g_muxProtocolHandler.startNegotiation(connection, telnetCommands);

    // Send initial telnet negotiation
    if (telnetCommands.readableBytes() > 0) {
        ganl::ErrorCode error = 0;
        const char* cmdData = telnetCommands.readPtr();
        size_t cmdLen = telnetCommands.readableBytes();
        g_networkEngine->postWrite(connection, cmdData, cmdLen, error);
    }

    // Register with session manager
    std::string remoteAddr(host);
    remoteAddr += ":" + std::to_string(port);
    g_muxSessionManager.onConnectionOpen(connection, remoteAddr);
}

// Handle data received from a connection
void ganl_handle_data(ganl::ConnectionHandle connection, const char* data, size_t length)
{
    // Get the descriptor for this connection
    DESC* d = g_muxProtocolHandler.getDescriptorForConnection(connection);
    if (d == nullptr) {
        return;
    }

    // Create input buffer
    ganl::IoBuffer inputBuffer;
    ganl::IoBuffer appBuffer;
    ganl::IoBuffer telnetResponses;

    // Copy the data into the input buffer
    inputBuffer.append(data, length);

    // Process through our MuxProtocolHandler
    g_muxProtocolHandler.processInput(connection, inputBuffer, appBuffer, telnetResponses);

    // Send any telnet responses
    if (telnetResponses.readableBytes() > 0) {
        ganl::ErrorCode error = 0;
        const char* respData = telnetResponses.readPtr();
        size_t respLen = telnetResponses.readableBytes();
        g_networkEngine->postWrite(connection, respData, respLen, error);
    }

    // Handle application data if any
    if (appBuffer.readableBytes() > 0) {
        // Convert to a string and process as a command
        std::string commandLine(appBuffer.readPtr(), appBuffer.readableBytes());

        // Update the last activity time
        d->last_time.GetUTC();

        // Mark the connection as connected if it's not already
        if (!(d->flags & DS_CONNECTED)) {
            // Finished negotiation, mark as connected
            d->flags |= DS_CONNECTED;
        }

        // Process the input using TinyMUX's command handling
        do_command(d, const_cast<UTF8 *>(reinterpret_cast<const UTF8 *>(commandLine.c_str())));
    }
}