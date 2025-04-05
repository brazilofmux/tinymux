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
std::unique_ptr<ganl::NetworkEngine> g_networkEngine;
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

    // Log events processed if there are any
    if (numEvents > 0) {
        STARTLOG(LOG_DEBUG, "NET", "GANL");
        UTF8 buf[MBUF_SIZE];
        mux_sprintf(buf, sizeof(buf), T("Processed %d network events"), numEvents);
        log_text(buf);
        ENDLOG;
    }

    // Handle events
    for (int i = 0; i < numEvents; i++) {
        const ganl::IoEvent& event = g_events[i];

        switch (event.type) {
            case ganl::IoEventType::Accept: {
                // New connection accepted
                std::string remoteAddress = g_networkEngine->getRemoteAddress(event.connection);

                STARTLOG(LOG_ALWAYS, "NET", "GANL");
                UTF8 buf[MBUF_SIZE];
                mux_sprintf(buf, sizeof(buf), T("New connection from %s"), remoteAddress.c_str());
                log_text(buf);
                ENDLOG;

                // Get the raw network address for site checking
                ganl::NetworkAddress netAddr = g_networkEngine->getRemoteNetworkAddress(event.connection);

                // During integration, we're temporarily skipping site checks
                if (false) { // Disabled during development
                    // Placeholder for site checking - will implement later
                    mux_sockaddr maddr;
                    int siteResult = 0; // Always allow connections during integration

                    if (false) { // Never execute during integration
                        STARTLOG(LOG_ALWAYS, "NET", "GANL");
                        // Variable 'buf' is declared earlier
                        // mux_sprintf(buf, sizeof(buf), T("Connection from %s rejected by site checks (result=%d)"),
                        //          remoteAddress.c_str(), siteResult);
                        // log_text(buf);
                        ENDLOG;

                        // Close the connection (passing only the connection handle)
                        g_networkEngine->closeConnection(event.connection);
                        break;
                    }
                }
                // Fallback to string-based check is also disabled during integration
                else if (false) { // Disabled during development
                    STARTLOG(LOG_ALWAYS, "NET", "GANL");
                    // Variable 'buf' is declared earlier
                    // mux_sprintf(buf, sizeof(buf), T("Connection from %s rejected by site checks (string-based)"),
                    //          remoteAddress.c_str());
                    // log_text(buf);
                    ENDLOG;

                    // Close the connection (passing only the connection handle)
                    g_networkEngine->closeConnection(event.connection);
                    break;
                }

                // Associate the connection with our telnet protocol handler
                g_muxProtocolHandler.createProtocolContext(event.connection);

                // Register with session manager which will create a descriptor
                ganl::SessionId sessionId = g_muxSessionManager.onConnectionOpen(event.connection, remoteAddress);
                if (sessionId == ganl::InvalidSessionId) {
                    STARTLOG(LOG_ALWAYS, "NET", "GANL");
                    log_text(T("Failed to create session for new connection"));
                    ENDLOG;

                    // Close the connection (passing only the connection handle)
                    g_networkEngine->closeConnection(event.connection);
                    break;
                }

                // Get the descriptor for this connection
                DESC* d = g_muxSessionManager.getDescriptor(sessionId);
                if (d == nullptr) {
                    STARTLOG(LOG_ALWAYS, "NET", "GANL");
                    log_text(T("Failed to get descriptor for new connection"));
                    ENDLOG;

                    // Close the connection (passing only the connection handle)
                    g_networkEngine->closeConnection(event.connection);
                    break;
                }

                // Associate the descriptor with our telnet protocol handler
                g_muxProtocolHandler.associateWithDescriptor(event.connection, d);

                // Start telnet negotiation
                ganl::IoBuffer telnetCommands;
                g_muxProtocolHandler.startNegotiation(event.connection, telnetCommands);

                // Send initial telnet negotiation
                if (telnetCommands.readableBytes() > 0) {
                    STARTLOG(LOG_DEBUG, "NET", "GANL");
                    UTF8 telnetBuf[MBUF_SIZE];
                    mux_sprintf(telnetBuf, sizeof(telnetBuf), T("Sending %d bytes of telnet negotiation"),
                              static_cast<int>(telnetCommands.readableBytes()));
                    log_text(telnetBuf);
                    ENDLOG;

                    ganl::ErrorCode error = 0;
                    const char* data = telnetCommands.readPtr();
                    size_t len = telnetCommands.readableBytes();
                    g_networkEngine->postWrite(event.connection, data, len, error);
                }

                // Post initial read
                ganl::ErrorCode error = 0;
                char* buffer = new char[4096]; // Use a reasonably sized buffer
                g_networkEngine->postRead(event.connection, buffer, 4096, error);

                // Send welcome message
                welcome_user(d);

                break;
            }

            case ganl::IoEventType::Read: {
                // Data available to read
                // Get the descriptor for this connection from our MuxProtocolHandler
                DESC* d = g_muxProtocolHandler.getDescriptorForConnection(event.connection);

                if (d != nullptr) {
                    // Find session ID for this descriptor
                    ganl::SessionId sessionId = g_muxSessionManager.getSessionIdFromDesc(d);

                    // Process the input data through our telnet handler
                    ganl::IoBuffer inputData;
                    ganl::IoBuffer appData;
                    ganl::IoBuffer telnetResponses;

                    // During integration, we'll use a simpler approach to handle read data
                    char buffer[4096] = {0}; // Placeholder buffer
                    size_t len = event.bytesTransferred; // We'll still record the transfer size

                    STARTLOG(LOG_DEBUG, "NET", "GANL");
                    UTF8 logbuf[MBUF_SIZE];
                    mux_sprintf(logbuf, sizeof(logbuf), T("Read %d bytes from connection %d"),
                             static_cast<int>(len), static_cast<int>(event.connection));
                    log_text(logbuf);
                    ENDLOG;

                    if (len > 0) {
                        // Copy the data into our input buffer
                        inputData.append(buffer, len);

                        // Process through our MuxProtocolHandler
                        g_muxProtocolHandler.processInput(event.connection, inputData, appData, telnetResponses);

                        // Send any telnet responses
                        if (telnetResponses.readableBytes() > 0) {
                            STARTLOG(LOG_DEBUG, "NET", "GANL");
                            UTF8 respBuf[MBUF_SIZE];
                            mux_sprintf(respBuf, sizeof(respBuf), T("Sending %d bytes of telnet responses"),
                                      static_cast<int>(telnetResponses.readableBytes()));
                            log_text(respBuf);
                            ENDLOG;

                            ganl::ErrorCode error = 0;
                            const char* respData = telnetResponses.readPtr();
                            size_t respLen = telnetResponses.readableBytes();
                            g_networkEngine->postWrite(event.connection, respData, respLen, error);
                        }

                        // Handle application data if any
                        if (appData.readableBytes() > 0) {
                            // Convert to a string and process as a command
                            std::string commandLine(appData.readPtr(), appData.readableBytes());

                            STARTLOG(LOG_DEBUG, "NET", "GANL");
                            UTF8 cmdBuf[MBUF_SIZE];
                            mux_sprintf(cmdBuf, sizeof(cmdBuf), T("Received command: %s"),
                                      commandLine.c_str());
                            log_text(cmdBuf);
                            ENDLOG;

                            // Update the last activity time
                            d->last_time.GetUTC();

                            // Process the input using our session manager
                            g_muxSessionManager.onDataReceived(sessionId, commandLine);
                        }
                    }

                    // Post another read
                    ganl::ErrorCode error = 0;
                    char* readBuffer = new char[4096]; // Use a new buffer for each read
                    g_networkEngine->postRead(event.connection, readBuffer, 4096, error);
                }
                break;
            }

            case ganl::IoEventType::Write: {
                // Ready to write data - nothing special needed here
                STARTLOG(LOG_DEBUG, "NET", "GANL");
                UTF8 logbuf[MBUF_SIZE];
                mux_sprintf(logbuf, sizeof(logbuf), T("Write of %d bytes completed"),
                         static_cast<int>(event.bytesTransferred));
                log_text(logbuf);
                ENDLOG;
                break;
            }

            case ganl::IoEventType::Close: {
                // Connection closed
                // Get the descriptor for this connection from our MuxProtocolHandler
                DESC* d = g_muxProtocolHandler.getDescriptorForConnection(event.connection);

                STARTLOG(LOG_ALWAYS, "NET", "GANL");
                UTF8 logbuf[MBUF_SIZE];
                mux_sprintf(logbuf, sizeof(logbuf), T("Connection %d closed"),
                         static_cast<int>(event.connection));
                log_text(logbuf);
                ENDLOG;

                // Find session ID for this descriptor
                ganl::SessionId sessionId = ganl::InvalidSessionId;
                if (d != nullptr) {
                    sessionId = g_muxSessionManager.getSessionIdFromDesc(d);
                }

                // Clean up GANL resources
                g_muxProtocolHandler.destroyProtocolContext(event.connection);

                // Notify session manager
                g_muxSessionManager.onConnectionClose(sessionId, ganl::DisconnectReason::NetworkError);

                // Delete any queued buffers associated with this connection
                // GANL does not provide buffers in IoEvent structure yet
                // We'll add buffer clean-up later when GANL is enhanced
                break;
            }

            case ganl::IoEventType::Error: {
                // Error occurred
                DESC* d = g_muxProtocolHandler.getDescriptorForConnection(event.connection);

                STARTLOG(LOG_ALWAYS, "NET", "GANL");
                UTF8 logbuf[MBUF_SIZE];
                mux_sprintf(logbuf, sizeof(logbuf), T("Error on connection %d: code=%d"),
                         static_cast<int>(event.connection), static_cast<int>(event.error));
                log_text(logbuf);
                ENDLOG;

                // Find session ID for this descriptor
                ganl::SessionId sessionId = ganl::InvalidSessionId;
                if (d != nullptr) {
                    sessionId = g_muxSessionManager.getSessionIdFromDesc(d);
                }

                // Clean up GANL resources
                g_muxProtocolHandler.destroyProtocolContext(event.connection);

                // For now during integration, just use NetworkError for all errors
                ganl::DisconnectReason reason = ganl::DisconnectReason::NetworkError;
                // We'll implement detailed error handling in the next integration phase

                // Notify session manager
                g_muxSessionManager.onConnectionClose(sessionId, reason);

                // Delete any queued buffers associated with this connection
                // GANL does not provide buffers in IoEvent structure yet
                // We'll add buffer clean-up later when GANL is enhanced
                break;
            }

            default:
                STARTLOG(LOG_ALWAYS, "NET", "GANL");
                UTF8 logbuf[MBUF_SIZE];
                mux_sprintf(logbuf, sizeof(logbuf), T("Unknown event type: %d"),
                         static_cast<int>(event.type));
                log_text(logbuf);
                ENDLOG;
                break;
        }
    }

    // Check for idle sessions after processing events
    static CLinearTimeAbsolute last_idle_check;
    CLinearTimeAbsolute now;
    now.GetUTC();

    if (false) { // Disable idle checking during integration
        last_idle_check = now;

        STARTLOG(LOG_DEBUG, "NET", "GANL");
        log_text(T("Checking for idle sessions"));
        ENDLOG;

        // For each session, check if it's idle
        for (const auto& sessionId : g_muxSessionManager) {
            DESC* d = g_muxSessionManager.getDescriptor(sessionId);
            if (!d) {
                continue;
            }

            // Check for idle timeout
            if (false) { // Disable idle checking during integration
                STARTLOG(LOG_ALWAYS, "NET", "IDLE");
                UTF8 logbuf[MBUF_SIZE];
                mux_sprintf(logbuf, sizeof(logbuf), T("Session %lu idle timeout"),
                         static_cast<unsigned long>(sessionId));
                log_text(logbuf);
                ENDLOG;

                // Disconnect the session
                g_muxSessionManager.disconnectSession(sessionId, ganl::DisconnectReason::Timeout);
            }
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