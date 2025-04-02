#include "connection.h"
#include "network_engine.h"
#include "secure_transport.h"
#include "protocol_handler.h"
#include "session_manager.h"

#include <iostream>
#include <iomanip>
#include <memory>
#include <string>
#include <algorithm>
#include <atomic>

// Platform-specific includes
#if defined(_WIN32) || defined(WIN32)
    // Windows includes
#include <WinSock2.h>
#include <Windows.h>
#include <WS2tcpip.h>
// Windows error codes
#define EAGAIN WSAEWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#define EINTR WSAEINTR
#define EPIPE WSAECONNRESET
// Function mappings
#define read(fd, buf, len) recv((SOCKET)(fd), (char*)(buf), (int)(len), 0)
#define write(fd, buf, len) send((SOCKET)(fd), (const char*)(buf), (int)(len), 0)
#define MIN min
#else
    // Unix includes
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#define MIN std::min
#endif

// Include appropriate headers based on platform
#ifdef _WIN32
#include <winsock2.h>
#define GANL_READ(fd, buf, len) ::recv(fd, buf, (int)len, 0)
#define GANL_WRITE(fd, buf, len) ::send(fd, buf, (int)len, 0)
#define GANL_SOCKET_ERROR SOCKET_ERROR
#define GANL_WOULDBLOCK WSAEWOULDBLOCK
#define GANL_INTR WSAEINTR // Although EINTR is less common in Winsock I/O
#define GANL_LAST_ERROR() WSAGetLastError()
using SocketReturnType = int; // recv/send return int
#else
#include <unistd.h>
#include <errno.h>
#define GANL_READ(fd, buf, len) ::read(fd, buf, len)
#define GANL_WRITE(fd, buf, len) ::write(fd, buf, len)
#define GANL_SOCKET_ERROR -1
#define GANL_WOULDBLOCK EWOULDBLOCK
#define GANL_AGAIN EAGAIN // Need both for POSIX
#define GANL_INTR EINTR
#define GANL_LAST_ERROR() errno
using SocketReturnType = ssize_t; // read/write return ssize_t
#endif

// Define a macro for debug logging
#ifndef NDEBUG // Only compile debug messages if NDEBUG is not defined
#define GANL_CONN_DEBUG(handle, x) \
    do { std::cerr << "[Conn:" << handle << "][State:" << static_cast<int>(getState()) << "] " << x << std::endl; } while (0)
#else
#define GANL_CONN_DEBUG(handle, x) do {} while (0)
#endif

namespace ganl {

// --- Constructor / Destructor ---

ConnectionBase::ConnectionBase(ConnectionHandle handle, NetworkEngine& engine,
                       SecureTransport* secureTransport,
                       ProtocolHandler& protocolHandler,
                       SessionManager& sessionManager)
    : handle_(handle),
      networkEngine_(engine),
      secureTransport_(secureTransport), // Can be nullptr
      protocolHandler_(protocolHandler),
      sessionManager_(sessionManager),
      encryptedInput_(8192),   // Raw network data
      decryptedInput_(8192),   // After TLS decryption
      applicationInput_(4096), // After telnet processing (lines/commands)
      applicationOutput_(4096),// Application data to send (raw text)
      formattedOutput_(8192),  // After telnet/formatting (colors, MXP etc)
      encryptedOutput_(8192)   // After TLS encryption (or direct copy if no TLS)
{
    GANL_CONN_DEBUG(handle_, "Constructed. SecureTransport=" << (secureTransport_ ? "Yes" : "No"));
}

// In connection.cpp
ConnectionBase::~ConnectionBase() {
    GANL_CONN_DEBUG(handle_, "Destructor executing. Final State was: " << static_cast<int>(getState()));

    // If the connection wasn't properly closed via the handleClose pathway
    if (getState() != ConnectionState::Closed) {
        GANL_CONN_DEBUG(handle_, "WARNING: Destructor called on non-closed connection (State: " << static_cast<int>(getState()) << "). Forcing cleanup.");

        // 1. Ensure the underlying socket close is requested from the network engine.
        //    This might be redundant if the object is being destroyed because the
        //    network event loop is shutting down, but it's safer to ensure it's called.
        //    The network engine's closeConnection should be safe to call multiple times.
        networkEngine_.closeConnection(handle_);

        // 2. Perform resource cleanup directly using the non-virtual helper.
        //    Use ServerShutdown as the reason for this unexpected/forced closure.
        cleanupResources(disconnectReason_);

        // 3. DO NOT transition state here. The object is being destroyed.
        GANL_CONN_DEBUG(handle_, "Forced cleanup complete in destructor.");
    }
    else {
        GANL_CONN_DEBUG(handle_, "Connection already closed. Normal destruction sequence.");
    }
    // Buffers clean themselves up via RAII.
}
// --- Initialization ---

// In connection.cpp, update the initialize method to handle null secureTransport

bool ConnectionBase::initialize(bool useTls) {
    GANL_CONN_DEBUG(handle_, "Initializing. useTls=" << useTls << ", SecureTransport Provided: " << (secureTransport_ != nullptr));
    useTls_ = useTls && secureTransport_ != nullptr;
    transitionToState(ConnectionState::Initializing); // Explicitly set initial state

    // Associate this Connection object pointer with the network handle
    ErrorCode error = 0;
    if (!networkEngine_.associateContext(handle_, this, error)) {
        std::cerr << "[Conn:" << handle_ << "] FATAL: Failed to associate connection context: "
            << networkEngine_.getErrorString(error) << std::endl;
        transitionToState(ConnectionState::Closed); // Mark as unusable
        return false;
    }

    // Create session with the manager *before* potential TLS handshake requires interaction
    sessionId_ = sessionManager_.onConnectionOpen(handle_, getRemoteAddress());
    if (sessionId_ == InvalidSessionId) {
        std::cerr << "[Conn:" << handle_ << "] FATAL: SessionManager rejected connection from " << getRemoteAddress() << std::endl;
        networkEngine_.closeConnection(handle_); // Ask network to close socket
        transitionToState(ConnectionState::Closing); // Move to closing state
        return false;
    }

    // Start the state machine: TLS Handshake or Telnet Negotiation
    if (useTls_) {
        startTlsHandshake();
    }
    else {
        // Skip TLS, go straight to telnet negotiation
        startTelnetNegotiation();
        if (!postRead()) {
            close(DisconnectReason::NetworkError);
            return false;
        }
    }

    // Check if startTlsHandshake or startTelnetNegotiation failed and closed the connection
    if (state_ == ConnectionState::Closing || state_ == ConnectionState::Closed) {
        return false;
    }

    return true;
}

// --- Event Handling ---

void ConnectionBase::handleNetworkEvent(const IoEvent& event) {
    // Ensure the context matches this connection object
    if (event.context != this) {
         std::cerr << "[Conn:???] Received event for handle " << event.connection
                   << " but context doesn't match this=" << this << ", event.context=" << event.context << std::endl;
         // Avoid processing events not meant for this instance
         return;
    }

    GANL_CONN_DEBUG(handle_, "Received Network Event: Type=" << static_cast<int>(event.type)
              << ", Bytes=" << event.bytesTransferred << ", Error=" << event.error);

    // If we are already closed, ignore stray events (except maybe Error?)
    if (getState() == ConnectionState::Closed && event.type != IoEventType::Error) {
         GANL_CONN_DEBUG(handle_, "Ignoring event, connection already closed.");
         return;
    }

    switch (event.type) {
        case IoEventType::Read:
            // Ensure bytesTransferred doesn't exceed buffer space (shouldn't happen with correct postRead)
             if (event.bytesTransferred > encryptedInput_.writableBytes() && pendingRead_) {
                  std::cerr << "[Conn:" << handle_ << "] ERROR: Read returned more bytes (" << event.bytesTransferred
                            << ") than available write space (" << encryptedInput_.writableBytes() << ")!" << std::endl;
                  // Commit only what fits? Or close? Let's close for safety.
                  handleError(event.error); // Treat as an error
                  break;
             }
            handleRead(event.bytesTransferred);
            break;

        case IoEventType::Write:
            handleWrite(event.bytesTransferred);
            break;

        case IoEventType::Close:
            // This event confirms the socket is closed at the OS level
            handleClose();
            break;

        case IoEventType::Error:
            handleError(event.error);
            break;

        default:
            GANL_CONN_DEBUG(handle_, "Warning: Received unexpected event type: " << static_cast<int>(event.type));
            break;
    }

    // After handling an event, check if more writing is needed (e.g., TLS generated data during read processing)
    // Do this check only if the connection wasn't closed by the event handler.
    if (!isClosingOrClosed()) {
        IoBuffer& encryptedOutput = encryptedOutput_;
        if (encryptedOutput.readableBytes() > 0 && !pendingWriteFlag()) {
             GANL_CONN_DEBUG(handle_, "Data found in encryptedOutput_ after event processing. Posting write.");
             postWrite();
         }
    }
}

// --- Data Flow ---

void ConnectionBase::sendDataToClient(const std::string& data) {
    GANL_CONN_DEBUG(handle_, "Received " << data.length() << " bytes from application to send.");

    if (isClosingOrClosed()) {
        GANL_CONN_DEBUG(handle_, "Attempted to send data on closing/closed connection. Ignoring.");
        return;
    }

    if (data.empty()) {
         GANL_CONN_DEBUG(handle_, "Ignoring empty data send request.");
         return;
    }

    // 1. Add data to application output buffer
    applicationOutput_.append(data.c_str(), data.length());
    GANL_CONN_DEBUG(handle_, "Appended data to applicationOutput_. Size now: " << applicationOutput_.readableBytes());

    // Process if we are in a state where sending is allowed (e.g., Running)
    // During negotiation, sending app data might be deferred or disallowed by protocol handler.
    // For now, let's assume formatOutput handles this.
    if (getState() == ConnectionState::Running || getState() == ConnectionState::TelnetNegotiating) { // Allow during negotiation too? Check ProtocolHandler contract.
        // 2. Format the data (Telnet commands, colors, encoding etc.)
        // Note: formatOutput reads from applicationOutput_ and writes to formattedOutput_
        formattedOutput_.clear(); // Ensure target is empty
        if (!protocolHandler_.formatOutput(handle_, applicationOutput_, formattedOutput_, true)) {
            GANL_CONN_DEBUG(handle_, "Error formatting output data: " << protocolHandler_.getLastProtocolErrorString(handle_) << ". Closing.");
            close(DisconnectReason::ProtocolError);
            return;
        }
        // Explicitly passing consumeInput=true ensures applicationOutput_ is consumed by the method
        GANL_CONN_DEBUG(handle_, "Formatted data. formattedOutput_ size: " << formattedOutput_.readableBytes());

        // 3. Encrypt the data (TLS) if needed
        // Note: processOutgoing reads from formattedOutput_ and writes to encryptedOutput_
        IoBuffer* sourceBuffer = &formattedOutput_; // Start with formatted data

        if (useTls_ && secureTransport_ != nullptr && secureTransport_->isEstablished(handle_)) {
             GANL_CONN_DEBUG(handle_, "Processing " << sourceBuffer->readableBytes() << " bytes through TLS.");
             // Ensure encryptedOutput_ has space? Or assume processOutgoing handles it. Let's assume.
             TlsResult result = secureTransport_->processOutgoing(handle_, *sourceBuffer, encryptedOutput_, true);
             // Explicitly specifying consumeInput=true ensures buffer is consumed by the method

             GANL_CONN_DEBUG(handle_, "TLS processOutgoing result: " << static_cast<int>(result)
                       << ". encryptedOutput_ size now: " << encryptedOutput_.readableBytes());

             if (result == TlsResult::Error || result == TlsResult::Closed) {
                 GANL_CONN_DEBUG(handle_, "TLS error during processOutgoing: " << secureTransport_->getLastTlsErrorString(handle_) << ". Closing.");
                 close(DisconnectReason::TlsError);
                 return;
             }
             // If result is WantRead/WantWrite, it implies handshake messages need I/O,
             // but the application data might still be buffered in encryptedOutput_.
             // The subsequent postWrite() call will handle sending.
        } else {
             GANL_CONN_DEBUG(handle_, "TLS not used or not established. Copying " << sourceBuffer->readableBytes() << " bytes directly to encryptedOutput_.");
             // If TLS is not enabled, copy formatted data directly to encrypted output
             encryptedOutput_.append(sourceBuffer->readPtr(), sourceBuffer->readableBytes());
             sourceBuffer->clear(); // Consume the source to be consistent with TLS path
             GANL_CONN_DEBUG(handle_, "encryptedOutput_ size now: " << encryptedOutput_.readableBytes());
        }

        // 4. Post write operation if not already pending
        if (encryptedOutput_.readableBytes() > 0 && !pendingWrite_) {
            GANL_CONN_DEBUG(handle_, "Posting write for " << encryptedOutput_.readableBytes() << " bytes.");
            postWrite();
        } else if (pendingWrite_) {
             GANL_CONN_DEBUG(handle_, "Write already pending. New data queued in encryptedOutput_ (" << encryptedOutput_.readableBytes() << " total).");
        } else {
             GANL_CONN_DEBUG(handle_, "No data in encryptedOutput_ to write.");
        }
    } else {
         GANL_CONN_DEBUG(handle_, "Cannot send application data in current state: " << static_cast<int>(getState()));
         // Data remains buffered in applicationOutput_
    }
}

bool ConnectionBase::processSecureData() {
    // Called when encryptedInput_ has data (or during handshake start)
    IoBuffer& encryptedInput = encryptedInput_;
    IoBuffer& decryptedInput = decryptedInput_;
    IoBuffer& encryptedOutput = encryptedOutput_;

    if (getState() == ConnectionState::TlsHandshaking) {
        GANL_CONN_DEBUG(handle_, "Continuing TLS Handshake...");
        return continueTlsHandshake();
    }

    // Regular data processing after handshake
    GANL_CONN_DEBUG(handle_, "Processing incoming TLS data (" << encryptedInput.readableBytes() << " bytes available)");
    TlsResult result = secureTransport_->processIncoming(
        handle_, encryptedInput, decryptedInput, encryptedOutput, true);

    GANL_CONN_DEBUG(handle_, "TLS processIncoming result: " << static_cast<int>(result)
              << ". encryptedInput_ remaining=" << encryptedInput.readableBytes()
              << ", decryptedInput_ added=" << decryptedInput.readableBytes() // Assumes buffer was empty before call
              << ", encryptedOutput_ added=" << encryptedOutput.readableBytes());

    switch (result) {
        case TlsResult::Success:
            // Data successfully processed (decrypted or handshake message handled).
            // May have generated handshake data to send back in encryptedOutput_.
             // Fall through to return true
             return true;

        case TlsResult::WantRead:
            // TLS layer needs more data from the network. Stop processing for now.
            GANL_CONN_DEBUG(handle_, "TLS needs more data (WantRead).");
            // Ensure a read is posted (handleRead will do this).
            return false; // Stop processing chain

        case TlsResult::WantWrite:
            // TLS layer needs to send data (handshake messages). Stop processing input for now.
            GANL_CONN_DEBUG(handle_, "TLS needs to write data (WantWrite).");
            // Ensure write is posted (handleNetworkEvent or handleRead will check encryptedOutput_)
            if (encryptedOutput.readableBytes() > 0 && !pendingWriteFlag()) {
                 postWrite();
            }
            return false; // Stop processing chain

        case TlsResult::Closed:
            // TLS session closed gracefully by peer during read.
            GANL_CONN_DEBUG(handle_, "TLS session closed by peer.");
            close(DisconnectReason::UserQuit); // Or TlsError? UserQuit seems better for clean TLS close.
            return false; // Stop processing chain

        case TlsResult::Error:
        default:
            // Unrecoverable TLS error.
            GANL_CONN_DEBUG(handle_, "TLS Error: " << secureTransport_->getLastTlsErrorString(handle_) << ". Closing.");
            close(DisconnectReason::TlsError);
            return false; // Stop processing chain
    }
}

bool ConnectionBase::processProtocolData() {
    // Called when decryptedInput_ has data
    IoBuffer& decryptedInput = decryptedInput_;
    IoBuffer& encryptedOutput = encryptedOutput_;

    IoBuffer telnetResponses(1024); // Buffer for responses generated by processInput

    // Process input: Telnet commands, line assembly, encoding conversion.
    // Reads from decryptedInput_, writes application lines to applicationInput_,
    // writes telnet responses to telnetResponses.
    GANL_CONN_DEBUG(handle_, "Calling protocolHandler_.processInput. decryptedInput_=" << decryptedInput.readableBytes());
    bool success = protocolHandler_.processInput(handle_, decryptedInput, applicationInput_, telnetResponses, true);

    GANL_CONN_DEBUG(handle_, "protocolHandler_.processInput result: " << success
              << ". decryptedInput_ remaining=" << decryptedInput.readableBytes()
              << ", applicationInput_ added=" << applicationInput_.readableBytes() // Assumes buffer was empty
              << ", telnetResponses generated=" << telnetResponses.readableBytes());

    if (!success) {
        GANL_CONN_DEBUG(handle_, "Protocol processing error: " << protocolHandler_.getLastProtocolErrorString(handle_) << ". Closing.");
        close(DisconnectReason::ProtocolError);
        return false; // Stop processing
    }

    // Send any generated Telnet responses
    if (telnetResponses.readableBytes() > 0) {
        GANL_CONN_DEBUG(handle_, "Sending " << telnetResponses.readableBytes() << " bytes of Telnet responses.");
        // Treat telnet responses as application data going out
        IoBuffer* sourceBuf = &telnetResponses;
        if (isTlsEnabled() && secureTransport_ != nullptr && secureTransport_->isEstablished(handle_)) {
             // Encrypt the telnet responses
             GANL_CONN_DEBUG(handle_, "Encrypting telnet responses.");
             TlsResult tlsRes = secureTransport_->processOutgoing(handle_, *sourceBuf, encryptedOutput, true);
             // Explicitly passing consumeInput=true ensures source buffer is consumed

             if (tlsRes == TlsResult::Error || tlsRes == TlsResult::Closed) {
                 GANL_CONN_DEBUG(handle_, "TLS Error sending telnet responses: " << secureTransport_->getLastTlsErrorString(handle_) << ". Closing.");
                 close(DisconnectReason::TlsError);
                 return false;
             }
        } else {
            // Send directly if no TLS
            GANL_CONN_DEBUG(handle_, "Sending telnet responses without TLS.");
            encryptedOutput.append(sourceBuf->readPtr(), sourceBuf->readableBytes());
            sourceBuf->clear(); // Consume all data to be consistent with TLS path
        }
        // Ensure write is posted
        if (encryptedOutput.readableBytes() > 0 && !pendingWriteFlag()) {
            postWrite();
        }
    }

    // Check if Telnet negotiation is complete *only if* we are in that state
    if (getState() == ConnectionState::TelnetNegotiating) {
        NegotiationStatus status = protocolHandler_.getNegotiationStatus(handle_);

        switch (status) {
        case NegotiationStatus::Completed:
            GANL_CONN_DEBUG(handle_, "Telnet negotiation COMPLETED successfully.");
            transitionToState(ConnectionState::Running);
            // Optional: Notify SessionManager to send welcome, etc.
            // sessionManager_.onNegotiationComplete(sessionId_);
            break;

        case NegotiationStatus::Failed:
            GANL_CONN_DEBUG(handle_, "Telnet negotiation FAILED.");
            close(DisconnectReason::ProtocolError);
            return false; // Stop processing

        case NegotiationStatus::InProgress:
        default:
            // Still negotiating, do nothing, wait for more input/events
            GANL_CONN_DEBUG(handle_, "Telnet negotiation still in progress.");
            break;
        }
    } // End if (getState() == ConnectionState::TelnetNegotiating)

    // Process fully formed application lines/commands from applicationInput_
    if (applicationInput_.readableBytes() > 0) {
        GANL_CONN_DEBUG(handle_, "Processing " << applicationInput_.readableBytes() << " bytes of application data from protocol handler.");

        // Example: Assume ProtocolHandler gives one line/command at a time, null-terminated or similar.
        // Here, we'll just consume everything and pass it. Refine based on ProtocolHandler contract.
        std::string commandLine = applicationInput_.consumeReadAllAsString();

        // Trim trailing whitespace/newlines commonly added by telnet/line mode
        // Find the last non-whitespace character
        size_t endpos = commandLine.find_last_not_of(" \t\r\n");
        if (std::string::npos != endpos) {
            commandLine = commandLine.substr(0, endpos + 1);
        } else {
            // String is all whitespace, clear it
            commandLine.clear();
        }

        if (!commandLine.empty() && sessionId_ != InvalidSessionId) {
            GANL_CONN_DEBUG(handle_, "Passing command to SessionManager: '" << commandLine << "'");
            // TODO: Update session stats (last activity, commands processed)
            sessionManager_.onDataReceived(sessionId_, commandLine);
        } else if (commandLine.empty()) {
             GANL_CONN_DEBUG(handle_, "Ignoring empty command line after trimming.");
        } else {
             GANL_CONN_DEBUG(handle_, "No valid session ID. Ignoring command: '" << commandLine << "'");
        }

        if (getState() == ConnectionState::Running) { // Add check?
            GANL_CONN_DEBUG(handle_, "Processing " << applicationInput_.readableBytes() << " bytes of application data from protocol handler.");
            // ... (consume applicationInput_, pass to SessionManager) ...
        }
        else {
            // Buffer app data received during negotiation? Or discard? Depends on protocol.
            GANL_CONN_DEBUG(handle_, "Buffering/Ignoring application data received during negotiation phase.");
            // For simplicity, let's assume ProtocolHandler buffers commands until negotiation completes,
            // or SessionManager handles commands differently based on SessionState.
        }
    }

    return true; // Continue processing possible if more data arrives later
}

// In connection.cpp
void ConnectionBase::handleClose() {
    GANL_CONN_DEBUG(handle_, "handleClose event received. Current State: " << static_cast<int>(getState()));

    // Prevent double cleanup/state transition
    if (getState() == ConnectionState::Closed) {
        GANL_CONN_DEBUG(handle_, "Already in Closed state. Ignoring redundant handleClose event.");
        return;
    }

    // Perform the actual cleanup using the non-virtual helper
    cleanupResources(disconnectReason_);

    // Transition to the final state *after* cleanup
    transitionToState(ConnectionState::Closed);
    GANL_CONN_DEBUG(handle_, "Connection resources cleaned up and state set to Closed.");

    // The Connection object might be destroyed shortly after this if the SessionManager
    // releases its shared_ptr upon receiving the onConnectionClose notification.
}

void ConnectionBase::handleError(ErrorCode error) {
    GANL_CONN_DEBUG(handle_, "handleError event received. Error: " << error << " (" << networkEngine_.getErrorString(error) << ").");

    // Prevent acting on errors if already closed
    if (getState() == ConnectionState::Closed) {
        GANL_CONN_DEBUG(handle_, "Ignoring error, connection already closed.");
        return;
    }

    // Treat any network error as fatal for the connection
    close(DisconnectReason::NetworkError);
}

// --- Connection Lifecycle & State Machine ---

void ConnectionBase::close(DisconnectReason reason) {
    GANL_CONN_DEBUG(handle_, "close() called with reason: " << static_cast<int>(reason));

    // Prevent multiple close attempts or closing an already closed connection
    if (isClosingOrClosed()) {
        GANL_CONN_DEBUG(handle_, "Connection already closing or closed. Ignoring request.");
        return;
    }

    // 1. Transition to Closing state
    transitionToState(ConnectionState::Closing);
    disconnectReason_ = reason;

    // 2. Attempt graceful TLS shutdown (if applicable and established)
    if (useTls_ && secureTransport_ != nullptr && secureTransport_->isEstablished(handle_)) {
        GANL_CONN_DEBUG(handle_, "Attempting graceful TLS shutdown.");
        // Ensure output buffer is clear of app data first? Maybe not necessary.
        TlsResult shutdownResult = secureTransport_->shutdownSession(handle_, encryptedOutput_);
        GANL_CONN_DEBUG(handle_, "TLS shutdownSession result: " << static_cast<int>(shutdownResult)
                  << ". encryptedOutput_ size: " << encryptedOutput_.readableBytes());

        if (shutdownResult == TlsResult::WantWrite || encryptedOutput_.readableBytes() > 0) {
             GANL_CONN_DEBUG(handle_, "TLS shutdown generated data or needs write. Posting write.");
             // Need to send the TLS close_notify alert
             if (!pendingWrite_) {
                  postWrite();
             }
             // TODO: Implement waiting logic: We should wait for this write to complete
             // before calling networkEngine_.closeConnection(), or let handleWrite trigger it.
             // For simplicity now, we proceed, but this isn't fully graceful.
             GANL_CONN_DEBUG(handle_, "WARNING: Proceeding to network close without waiting for TLS shutdown write confirmation.");
        } else if (shutdownResult == TlsResult::Error) {
             GANL_CONN_DEBUG(handle_, "Error during TLS shutdown: " << secureTransport_->getLastTlsErrorString(handle_));
             // Proceed to network close anyway
        }
        // If result is Success or Closed, TLS shutdown is done or already happened.
    }

    // 3. Request network layer to close the connection
    GANL_CONN_DEBUG(handle_, "Requesting network engine close connection.");
    networkEngine_.closeConnection(handle_);

    // 4. Final cleanup (TLS/Protocol context destruction, SessionManager notification,
    //    transition to Closed state) is deferred to handleClose(), which is triggered
    //    by the network engine confirming the closure.
    GANL_CONN_DEBUG(handle_, "Close initiated. Waiting for handleClose event for final cleanup.");
}

void ConnectionBase::startTlsHandshake() {
    if (!useTls_) { // Should not happen if called from initialize correctly
         GANL_CONN_DEBUG(handle_, "ERROR: startTlsHandshake called but TLS is not enabled!");
         close(DisconnectReason::ServerShutdown); // Internal error
         return;
    }

    GANL_CONN_DEBUG(handle_, "Starting TLS handshake sequence.");
    transitionToState(ConnectionState::TlsHandshaking);

    // Create the per-connection TLS context
    if (!secureTransport_->createSessionContext(handle_, true /* isServer */)) {
        GANL_CONN_DEBUG(handle_, "Failed to create TLS session context: " << secureTransport_->getLastTlsErrorString(handle_) << ". Closing.");
        close(DisconnectReason::TlsError);
        return;
    }
    GANL_CONN_DEBUG(handle_, "TLS session context created.");

    // For server-side TLS, the first step often involves the server potentially sending
    // a ServerHello after receiving the ClientHello. Since we haven't received anything yet,
    // we might need to post a read first, or call processIncoming to see if the TLS lib
    // wants to do anything proactively (less common for servers). OpenSSL typically waits for ClientHello.
    // Let's ensure a read is posted.
    if (!postRead()) {
        GANL_CONN_DEBUG(handle_, "Failed to post initial read for TLS handshake. Closing.");
        close(DisconnectReason::NetworkError);
        return;
    }
    GANL_CONN_DEBUG(handle_, "Initial read posted, waiting for ClientHello.");

    // Some TLS implementations might require an initial call to get things going.
    // Let's try calling processIncoming with empty input buffers, maybe it generates initial write data.
    // IoBuffer emptyBuf(0);
    // TlsResult initialResult = secureTransport_->processIncoming(handle_, emptyBuf, emptyBuf, encryptedOutput_);
    // if (encryptedOutput_.readableBytes() > 0 && !pendingWrite_) {
    //    GANL_CONN_DEBUG(handle_, "TLS library generated initial handshake data (" << encryptedOutput_.readableBytes() << " bytes). Posting write.");
    //    postWrite();
    // }
    // Commented out: Most server implementations wait for ClientHello first.
}

bool ConnectionBase::continueTlsHandshake() {
    GANL_CONN_DEBUG(handle_, "Continuing TLS Handshake with input data (" << encryptedInput_.readableBytes() << " bytes).");

    // --- Call processIncoming ---
    TlsResult result = secureTransport_->processIncoming(
        handle_, encryptedInput_, decryptedInput_, encryptedOutput_, true);

    GANL_CONN_DEBUG(handle_, "TLS Handshake processIncoming result: " << static_cast<int>(result)
              << ". encryptedInput_ remaining=" << encryptedInput_.readableBytes()
              << ", decryptedInput_ added=" << decryptedInput_.readableBytes()
              << ", encryptedOutput_ added=" << encryptedOutput_.readableBytes());

    // --- Check for Handshake Completion FIRST ---
    // Regardless of WantRead/WantWrite, did the handshake just complete?
    if (getState() == ConnectionState::TlsHandshaking && secureTransport_->isEstablished(handle_)) {
        GANL_CONN_DEBUG(handle_, "TLS handshake COMPLETE! (isEstablished() is true).");
        startTelnetNegotiation(); // Transitions state internally if successful

        // If startTelnetNegotiation failed, the state might be Closing/Closed
        if (isClosingOrClosed()) {
             return false; // Stop processing if negotiation start failed
        }

        // Handshake is done. Now, did this processIncoming call *also* yield app data?
        // The return value 'true'/'false' from this function determines if handleRead
        // continues to processProtocolData. We should return true only if NO app data
        // was decrypted during this specific final handshake step.
        // If app data WAS decrypted (decryptedInput_ > 0), returning false might seem wrong,
        // but handleRead's logic should handle it based on the tlsProducedData flag anyway.
        // Let's simplify: Return based on whether immediate protocol processing is needed.
        // If decryptedInput_ has data, protocol layer needs to run.
        return decryptedInput_.readableBytes() == 0; // True = stop pipeline now, False = maybe continue in handleRead
    }

    // --- If Handshake NOT Complete, Handle TlsResult ---
    switch (result) {
        case TlsResult::Success:
            // Should not happen if isEstablished() was false above, but handle defensively.
            GANL_CONN_DEBUG(handle_, "TLS Result Success, but isEstablished() still false? Handshake likely ongoing.");
            // Check if we need to write handshake response generated during this 'Success' step
            if (encryptedOutput_.readableBytes() > 0 && !pendingWrite_) {
                postWrite();
            }
            return false; // Wait for next network event

        case TlsResult::WantRead:
            GANL_CONN_DEBUG(handle_, "TLS handshake needs more data (WantRead).");
            // Ensure read is posted (handleRead will usually do this after we return false)
            // if (!pendingRead_) { postRead(); } // Usually redundant
            return false; // Stop processing chain

        case TlsResult::WantWrite:
            GANL_CONN_DEBUG(handle_, "TLS handshake needs to write data (WantWrite).");
            if (encryptedOutput_.readableBytes() > 0 && !pendingWrite_) {
                postWrite();
            }
            return false; // Stop processing chain

        case TlsResult::Closed: // Should not happen during handshake?
            GANL_CONN_DEBUG(handle_, "TLS session closed unexpectedly during handshake. Closing.");
            close(DisconnectReason::TlsError);
            return false;

        case TlsResult::Error:
        default:
            GANL_CONN_DEBUG(handle_, "TLS Handshake Error: " << secureTransport_->getLastTlsErrorString(handle_) << ". Closing.");
            // Don't use getLastError() here, use the specific TLS error function
            //lastError_ = secureTransport_->getLastTlsErrorString(handle_);
            close(DisconnectReason::TlsError);
            return false;
    }
}

void ConnectionBase::startTelnetNegotiation() {
    GANL_CONN_DEBUG(handle_, "Starting Telnet negotiation.");
    transitionToState(ConnectionState::TelnetNegotiating);

    // Create the per-connection protocol handler context
    if (!protocolHandler_.createProtocolContext(handle_)) {
        GANL_CONN_DEBUG(handle_, "Failed to create protocol context: " << protocolHandler_.getLastProtocolErrorString(handle_) << ". Closing.");
        close(DisconnectReason::ProtocolError);
        return;
    }
    GANL_CONN_DEBUG(handle_, "Protocol context created.");

    // Get initial Telnet options to send from the handler
    IoBuffer telnetOptions(1024);
    protocolHandler_.startNegotiation(handle_, telnetOptions);
    GANL_CONN_DEBUG(handle_, "protocolHandler_.startNegotiation generated " << telnetOptions.readableBytes() << " bytes of options.");

    // Send the initial options
    if (telnetOptions.readableBytes() > 0) {
         IoBuffer* sourceBuf = &telnetOptions;
         if (useTls_ && secureTransport_ != nullptr && secureTransport_->isEstablished(handle_)) {
              GANL_CONN_DEBUG(handle_, "Encrypting initial Telnet options.");
              TlsResult tlsRes = secureTransport_->processOutgoing(handle_, *sourceBuf, encryptedOutput_, true);
              // Explicitly passing consumeInput=true ensures source buffer is consumed

              if (tlsRes == TlsResult::Error || tlsRes == TlsResult::Closed) {
                  GANL_CONN_DEBUG(handle_, "TLS Error sending initial Telnet options: " << secureTransport_->getLastTlsErrorString(handle_) << ". Closing.");
                  close(DisconnectReason::TlsError);
                  return; // Stop negotiation start
              }
         } else {
             GANL_CONN_DEBUG(handle_, "Sending initial Telnet options without TLS.");
             encryptedOutput_.append(sourceBuf->readPtr(), sourceBuf->readableBytes());
             sourceBuf->clear(); // Consume all data to be consistent with TLS path
         }

         // Ensure write is posted
         if (encryptedOutput_.readableBytes() > 0 && !pendingWrite_) {
             postWrite();
         }
    } else {
         GANL_CONN_DEBUG(handle_, "No initial Telnet options to send.");
    }

     // Ensure a read is posted to receive client's Telnet responses (or first command)
     // If called from initialize(non-TLS), postRead wasn't called yet.
     // If called from continueTlsHandshake, postRead might be needed if handshake didn't need read.
     if (!pendingRead_) {
          GANL_CONN_DEBUG(handle_, "Posting read operation to wait for Telnet responses/data.");
          if (!postRead()) {
              GANL_CONN_DEBUG(handle_, "Failed to post read during telnet negotiation start. Closing.");
              close(DisconnectReason::NetworkError);
          }
     }
}

void ConnectionBase::transitionToState(ConnectionState newState) {
    if (getState() != newState) {
        GANL_CONN_DEBUG(handle_, "Transitioning State: " << static_cast<int>(getState()) << " -> " << static_cast<int>(newState));
        state_ = newState;
    }
}

// --- Network Operations ---

bool ConnectionBase::postRead() {
    if (pendingReadFlag()) {
        GANL_CONN_DEBUG(handle_, "postRead called, but read already pending. Ignoring.");
        return true; // Not an error, just redundant
    }
    if (isClosingOrClosed()) {
        GANL_CONN_DEBUG(handle_, "postRead called, but connection closing/closed. Ignoring.");
        return false;
    }

    // Ensure buffer has space. Use a reasonable chunk size.
    IoBuffer& encryptedInput = encryptedInput_;
    encryptedInput.ensureWritable(4096);
    size_t readSize = encryptedInput.writableBytes();
    char* bufferPtr = encryptedInput.writePtr();

    if (readSize == 0) {
        // This should ideally not happen if ensureWritable works, but handle defensively.
         std::cerr << "[Conn:" << handle_ << "] ERROR: No writable space in encryptedInput_ even after ensureWritable! Cannot post read." << std::endl;
         close(DisconnectReason::ServerShutdown); // Internal error
         return false;
    }

    GANL_CONN_DEBUG(handle_, "Posting read for up to " << readSize << " bytes into buffer @" << static_cast<void*>(bufferPtr));
    ErrorCode error = 0;
    if (!networkEngine_.postRead(handle_, bufferPtr, readSize, error)) {
        // Immediate error posting read
        GANL_CONN_DEBUG(handle_, "Immediate error posting read: " << error << " (" << networkEngine_.getErrorString(error) << ").");
        // Don't set pendingRead_
        // The error should trigger an Error event, which will call handleError -> close.
        // If the network engine *doesn't* guarantee an Error event on immediate failure, we should close here.
        // Assuming Error event *will* be generated.
        return false; // Indicate immediate failure
    }

    pendingRead_ = true;
    return true;
}

bool ConnectionBase::postWrite() {
    if (pendingWriteFlag()) {
         GANL_CONN_DEBUG(handle_, "postWrite called, but write already pending. Ignoring.");
         return true; // Not an error
    }

    IoBuffer& encryptedOutput = encryptedOutput_;
    if (encryptedOutput.readableBytes() == 0) {
         GANL_CONN_DEBUG(handle_, "postWrite called, but encryptedOutput_ is empty. Ignoring.");
         return false;
    }

    if (isClosingOrClosed()) {
        GANL_CONN_DEBUG(handle_, "postWrite called, but connection closing/closed. Ignoring.");
        return false;
    }

    const char* dataPtr = encryptedOutput.readPtr();
    size_t writeSize = encryptedOutput.readableBytes();

    GANL_CONN_DEBUG(handle_, "Posting write for " << writeSize << " bytes from buffer @" << static_cast<const void*>(dataPtr));
    ErrorCode error = 0;
    if (!networkEngine_.postWrite(handle_, dataPtr, writeSize, error)) {
        // Immediate error posting write
        GANL_CONN_DEBUG(handle_, "Immediate error posting write: " << error << " (" << networkEngine_.getErrorString(error) << ").");
         // Don't set pendingWrite_
         // Assume Error event will be generated by network engine.
        return false; // Indicate immediate failure
    }

    pendingWrite_ = true;
    return true;
}

void ConnectionBase::cleanupResources(DisconnectReason reason) {
    // This function contains the core cleanup logic previously in handleClose.
    // It's non-virtual and safe to call from the destructor or handleClose.
    // It assumes the state is NOT Closed yet, but might be Closing or something else.

    GANL_CONN_DEBUG(handle_, "Performing resource cleanup. Reason: " << static_cast<int>(reason));

    // Clean up TLS context
    if (useTls_ && secureTransport_ != nullptr) {
        // It's important that destroySessionContext is safe to call even if the context
        // doesn't exist or was already destroyed (idempotent).
        GANL_CONN_DEBUG(handle_, "Destroying TLS session context.");
        secureTransport_->destroySessionContext(handle_);
    }

    // Clean up Protocol context
    // Assume destroyProtocolContext is also idempotent or checks internally.
    GANL_CONN_DEBUG(handle_, "Destroying Protocol context.");
    protocolHandler_.destroyProtocolContext(handle_);

    // Notify the session manager *only if* we still have a valid session ID associated
    // This prevents double notification if cleanupResources is called after handleClose
    // already ran and cleared sessionId_.
    if (sessionId_ != InvalidSessionId) {
        GANL_CONN_DEBUG(handle_, "Notifying SessionManager of close for SessionId: " << sessionId_ << " Reason: " << static_cast<int>(reason));
        sessionManager_.onConnectionClose(sessionId_, reason);
        // Mark session ID as invalid *immediately* after notification to prevent re-entry
        sessionId_ = InvalidSessionId;
    }
    else {
        GANL_CONN_DEBUG(handle_, "Session ID already invalid or cleanup called previously. Skipping SessionManager notification.");
    }
    GANL_CONN_DEBUG(handle_, "Resource cleanup finished.");
}

// --- Accessors ---

std::string ConnectionBase::getRemoteAddress() const {
    // Cache this? Network engine call might be expensive.
    // For now, call directly.
    return networkEngine_.getRemoteAddress(handle_);
}

// --- ReadinessConnection Implementation ---

ReadinessConnection::ReadinessConnection(ConnectionHandle handle, NetworkEngine& engine,
    SecureTransport* secureTransport,
    ProtocolHandler& protocolHandler,
    SessionManager& sessionManager)
    // Call the base class constructor to initialize common members
    : ConnectionBase(handle, engine, secureTransport, protocolHandler, sessionManager)
{
    // Constructor body can be empty if no ReadinessConnection-specific initialization needed
    GANL_CONN_DEBUG(handle_, "ReadinessConnection Constructed."); // Use a base class macro or similar
}

ReadinessConnection::~ReadinessConnection() {
    // Destructor body can be empty if no ReadinessConnection-specific cleanup needed
    // Base class destructor handles common cleanup.
    GANL_CONN_DEBUG(handle_, "ReadinessConnection Destructed.");
}

void ReadinessConnection::handleRead(size_t bytesTransferred)
{
    GANL_CONN_DEBUG(handle_, "handleRead event. Bytes from event: " << bytesTransferred << ".");
    pendingReadFlag() = false; // An event arrived, so no longer pending in the engine's view

    bool success = false;
    bool needsClose = false;
    DisconnectReason closeReason = DisconnectReason::NetworkError; // Default reason

    // --- Select / Epoll / Kqueue / Readiness Model Logic ---
    GANL_CONN_DEBUG(handle_, "Readiness event. Attempting reads...");
    bool potentialEOF = false;
    bool readError = false;
    SocketReturnType totalBytesReadInCall = 0;
    std::string lastErrorString;

    // Get reference to input buffer
    IoBuffer& encryptedInput = getEncryptedInputBuffer();
    IoBuffer& decryptedInput = getDecryptedInputBuffer();
    IoBuffer& encryptedOutput = getEncryptedOutputBuffer();

    // Loop: read until EAGAIN/EWOULDBLOCK or real error/EOF
    while (true) {
        encryptedInput.ensureWritable(4096);
        size_t readSize = encryptedInput.writableBytes();
        char* bufferPtr = encryptedInput.writePtr();
#ifdef _WIN32
        SOCKET fd = static_cast<SOCKET>(handle_);
#else
        int fd = static_cast<int>(handle_);
#endif

        if (readSize == 0) {
            lastErrorString = "Read buffer has zero writable space";
            GANL_CONN_DEBUG(handle_, "Error: " << lastErrorString);
            readError = true;
            break;
        }

        SocketReturnType bytesReadThisOp = GANL_READ(fd, bufferPtr, readSize);

        if (bytesReadThisOp > 0) {
            encryptedInput.commitWrite(bytesReadThisOp);
            totalBytesReadInCall += bytesReadThisOp;
        }
        else if (bytesReadThisOp == 0) {
            potentialEOF = true;
            break;
        }
        else { // < 0
#ifdef _WIN32
            ErrorCode error = GANL_LAST_ERROR();
            if (error == GANL_WOULDBLOCK) { break; } // Normal exit for readiness
            else if (error == GANL_INTR) { continue; }
            else { lastErrorString = "Socket Read failed: " + networkEngine_.getErrorString(error); readError = true; break; }
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) { break; } // Normal exit for readiness
            else if (errno == EINTR) { continue; }
            else { lastErrorString = "::read failed: " + std::string(strerror(errno)); readError = true; break; }
#endif
        }

        GANL_CONN_DEBUG(handle_, "Readiness read loop finished. Total read: " << totalBytesReadInCall
            << ", EOF=" << potentialEOF << ", Error=" << readError);

        if (readError) {
            needsClose = true;
            closeReason = DisconnectReason::NetworkError;
            success = false;
        }
        else if (potentialEOF) {
            needsClose = true;
            closeReason = DisconnectReason::UserQuit;
            success = true; // EOF is not a processing error itself
        }
        else {
            success = true; // Read attempts finished normally (hit EAGAIN or read some data)
        }
    } // End Readiness Logic

    // --- Common Processing After Read Attempt ---
    if (needsClose) {
        close(closeReason); // Close the connection
        return;             // Stop processing
    }

    // Only proceed if the read operation itself was okay (success=true) AND we have data
    if (success && encryptedInput.readableBytes() > 0) {
        // Data processing pipeline (TLS -> Protocol -> Application)
        bool continueProcessing = true;
        bool tlsProducedData = false;

        // 1. TLS Layer
        if (isTlsEnabled()) {
            size_t decryptedBefore = decryptedInput.readableBytes();
            bool tlsCanContinue = processSecureData(); // Process data now in encryptedInput_
            tlsProducedData = (decryptedInput.readableBytes() > decryptedBefore);
            continueProcessing = tlsProducedData; // Only continue to protocol if TLS produced data
            if (!tlsCanContinue && !tlsProducedData) {
                continueProcessing = false; // Halted by TLS needs
            }
        }
        else {
            // Non-TLS: Copy data directly
            size_t copied = encryptedInput.readableBytes();
            if (copied > 0) {
                decryptedInput.append(encryptedInput.readPtr(), copied);
                encryptedInput.consumeRead(copied);
                tlsProducedData = true;
                continueProcessing = true;
            }
        }

        // 2. Protocol Layer
        if (continueProcessing) {
            bool protocolOk = processProtocolData();
            // If protocolOk is false, processProtocolData should have called close()
        }
    }
    else if (success) {
        GANL_CONN_DEBUG(handle_, "Read successful but no new data in buffer (or IOCP reported data but commit failed?).");
    }
    // Note: If success is false, we already returned after calling close().

    // Final check: If processing generated output, trigger a write
    if (!isClosingOrClosed()) {
        if (encryptedOutput.readableBytes() > 0 && !pendingWriteFlag()) {
            GANL_CONN_DEBUG(handle_, "Data found in encryptedOutput_ after read processing. Posting write.");
            postWrite();
        }
    }
}

void ReadinessConnection::handleWrite(size_t bytesTransferred)
{
    GANL_CONN_DEBUG(handle_, "handleWrite event. Bytes from event: " << bytesTransferred << ".");
    pendingWriteFlag() = false; // Event arrived, no longer pending in engine's view

    // Get reference to output buffer
    IoBuffer& encryptedOutput = getEncryptedOutputBuffer();

    // --- Select / Epoll / Kqueue / Readiness Model Logic ---
    GANL_CONN_DEBUG(handle_, "Readiness event (Write Ready). Trying to write remaining "
        << encryptedOutput.readableBytes() << " bytes.");

    if (encryptedOutput.readableBytes() == 0) {
        GANL_CONN_DEBUG(handle_, "Readiness Write event, but encryptedOutput_ is empty. False trigger or race condition?");
        // No need to do anything since there's no data to write
        // The network engine will automatically unregister write interest after this call
        return;
    }

    SocketReturnType totalBytesWrittenInCall = 0;
    bool socketBufferFull = false;
    std::string lastErrorString;

    // Loop: write until buffer empty or EAGAIN/EWOULDBLOCK
    while (encryptedOutput.readableBytes() > 0) {
        const char* dataPtr = encryptedOutput.readPtr();
        size_t dataSize = encryptedOutput.readableBytes();
#ifdef _WIN32
        SOCKET fd = static_cast<SOCKET>(handle_);
#else
        int fd = static_cast<int>(handle_);
#endif

        SocketReturnType bytesWrittenThisOp = GANL_WRITE(fd, dataPtr, dataSize);

        if (bytesWrittenThisOp > 0) {
            encryptedOutput.consumeRead(bytesWrittenThisOp);
            totalBytesWrittenInCall += bytesWrittenThisOp;
        }
        else if (bytesWrittenThisOp == 0) {
            lastErrorString = "::write returned 0";
            GANL_CONN_DEBUG(handle_, "Error: " << lastErrorString);
            close(DisconnectReason::NetworkError);
            return;
        }
        else { // < 0
#ifdef _WIN32
            ErrorCode error = GANL_LAST_ERROR();
            if (error == GANL_WOULDBLOCK) { socketBufferFull = true; break; }
            else if (error == GANL_INTR) { continue; }
            else { lastErrorString = "Socket Write failed: " + networkEngine_.getErrorString(error); close(DisconnectReason::NetworkError); return; }
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) { socketBufferFull = true; break; }
            else if (errno == EINTR) { continue; }
            else { lastErrorString = "::write failed: " + std::string(strerror(errno)); close(DisconnectReason::NetworkError); return; }
#endif
        }
    } // end while readiness write loop

    GANL_CONN_DEBUG(handle_, "Readiness write loop finished. Total written: " << totalBytesWrittenInCall
        << ", SocketFull=" << socketBufferFull << ", Remaining=" << encryptedOutput.readableBytes());

    // If we couldn't write everything because the socket buffer was full,
    // we need to re-register write interest to receive another write event when ready
    if (encryptedOutput.readableBytes() > 0 && socketBufferFull) {
        GANL_CONN_DEBUG(handle_, "Data remains and socket buffer was full. Re-registering write interest.");
        if (!pendingWriteFlag()) {
            ErrorCode error = 0;
            // According to the NetworkEngine interface, postWrite registers write interest
            // The first parameter (handle_) specifies which connection needs write interest
            // The data and length parameters aren't used by readiness-based engines for interest registration
            if (!networkEngine_.postWrite(handle_, nullptr, 0, error)) {
                GANL_CONN_DEBUG(handle_, "Failed to re-register write interest: " << networkEngine_.getErrorString(error));
            } else {
                pendingWriteFlag() = true;
            }
        }
    }
    // No need to explicitly unregister write interest when buffer is empty
    // The network engine will automatically unregister write interest after this call
    // based on the return from handleWrite
}

// --- CompletionConnection Implementation ---

CompletionConnection::CompletionConnection(ConnectionHandle handle, NetworkEngine& engine,
    SecureTransport* secureTransport,
    ProtocolHandler& protocolHandler,
    SessionManager& sessionManager)
    // Call the base class constructor to initialize common members
    : ConnectionBase(handle, engine, secureTransport, protocolHandler, sessionManager)
{
    // Constructor body can be empty if no CompletionConnection-specific initialization needed
    GANL_CONN_DEBUG(handle_, "CompletionConnection Constructed.");
}

CompletionConnection::~CompletionConnection() {
    // Destructor body can be empty if no CompletionConnection-specific cleanup needed
    // Base class destructor handles common cleanup.
    GANL_CONN_DEBUG(handle_, "CompletionConnection Destructed.");
}

void CompletionConnection::handleRead(size_t bytesTransferred)
{
    GANL_CONN_DEBUG(handle_, "handleRead event. Bytes from event: " << bytesTransferred << ".");
    pendingReadFlag() = false; // An event arrived, so no longer pending in the engine's view

    bool success = false;
    bool needsClose = false;
    DisconnectReason closeReason = DisconnectReason::NetworkError; // Default reason

    // Get references to buffers
    IoBuffer& encryptedInput = getEncryptedInputBuffer();
    IoBuffer& decryptedInput = getDecryptedInputBuffer();
    IoBuffer& encryptedOutput = getEncryptedOutputBuffer();

    // --- IOCP / Completion Model Logic ---
    if (bytesTransferred == 0) {
        // Read operation completed with 0 bytes -> Graceful close by peer
        GANL_CONN_DEBUG(handle_, "IOCP Read completed with 0 bytes (EOF).");
        needsClose = true;
        closeReason = DisconnectReason::UserQuit; // Standard EOF reason
        success = true; // The operation itself succeeded (in reporting EOF)
    }
    else {
        // Read operation completed with data
        GANL_CONN_DEBUG(handle_, "IOCP Read completed with " << bytesTransferred << " bytes.");
        // Commit the data received from this specific completion event
        if (bytesTransferred <= encryptedInput.writableBytes()) {
            encryptedInput.commitWrite(bytesTransferred);
            success = true; // Successfully committed data
        }
        else {
            // Error: IOCP reported more bytes than buffer space - should not happen if postRead is correct
            std::cerr << "[Conn:" << handle_ << "] ERROR: IOCP Read returned more bytes (" << bytesTransferred
                << ") than available write space (" << encryptedInput.writableBytes() << ")!" << std::endl;
            needsClose = true;
            closeReason = DisconnectReason::NetworkError;
            success = false; // Indicate processing failure
        }
    }
    // Note: No read loop needed for IOCP.

    // --- Common Processing After Read Attempt ---
    if (needsClose) {
        close(closeReason); // Close the connection
        return;             // Stop processing
    }

    // Only proceed if the read operation itself was okay (success=true) AND we have data
    if (success && encryptedInput.readableBytes() > 0) {
        // Data processing pipeline (TLS -> Protocol -> Application)
        bool continueProcessing = true;
        bool tlsProducedData = false;

        // 1. TLS Layer
        if (isTlsEnabled()) {
            size_t decryptedBefore = decryptedInput.readableBytes();
            bool tlsCanContinue = processSecureData(); // Process data now in encryptedInput_
            tlsProducedData = (decryptedInput.readableBytes() > decryptedBefore);
            continueProcessing = tlsProducedData; // Only continue to protocol if TLS produced data
            if (!tlsCanContinue && !tlsProducedData) {
                continueProcessing = false; // Halted by TLS needs
            }
        }
        else {
            // Non-TLS: Copy data directly
            size_t copied = encryptedInput.readableBytes();
            if (copied > 0) {
                decryptedInput.append(encryptedInput.readPtr(), copied);
                encryptedInput.consumeRead(copied);
                tlsProducedData = true;
                continueProcessing = true;
            }
        }

        // 2. Protocol Layer
        if (continueProcessing) {
            bool protocolOk = processProtocolData();
            // If protocolOk is false, processProtocolData should have called close()
        }
    }
    else if (success) {
        GANL_CONN_DEBUG(handle_, "Read successful but no new data in buffer (or IOCP reported data but commit failed?).");
    }
    // Note: If success is false, we already returned after calling close().

    // After processing a completion, we MUST post the next read
    // unless we are closing.
    if (!isClosingOrClosed()) {
        GANL_CONN_DEBUG(handle_, "IOCP: Posting next read.");
        if (!postRead()) {
            GANL_CONN_DEBUG(handle_, "IOCP: Failed to post subsequent read. Closing.");
            close(DisconnectReason::NetworkError);
            return; // Stop processing
        }
    }
    // --- Readiness model does NOT re-post read here ---

    // Final check: If processing generated output, trigger a write
    if (!isClosingOrClosed()) {
        if (encryptedOutput.readableBytes() > 0 && !pendingWriteFlag()) {
            GANL_CONN_DEBUG(handle_, "Data found in encryptedOutput_ after read processing. Posting write.");
            postWrite();
        }
    }
}

void CompletionConnection::handleWrite(size_t bytesTransferred)
{
    GANL_CONN_DEBUG(handle_, "handleWrite event. Bytes from event: " << bytesTransferred << ".");
    pendingWriteFlag() = false; // Event arrived, no longer pending in engine's view

    // Get reference to output buffer
    IoBuffer& encryptedOutput = getEncryptedOutputBuffer();

    // --- IOCP / Completion Model Logic ---
    GANL_CONN_DEBUG(handle_, "IOCP Write completed. Bytes transferred: " << bytesTransferred);

    // Consume the bytes that were successfully sent in the *completed* operation
    if (bytesTransferred > encryptedOutput.readableBytes()) {
        GANL_CONN_DEBUG(handle_, "WARNING: IOCP Write completion transferred (" << bytesTransferred
            << ") more bytes than in buffer (" << encryptedOutput.readableBytes() << "). Consuming all readable.");
        encryptedOutput.consumeRead(encryptedOutput.readableBytes());
    }
    else {
        encryptedOutput.consumeRead(bytesTransferred);
    }
    GANL_CONN_DEBUG(handle_, "Consumed " << bytesTransferred << " bytes. encryptedOutput_ remaining: " << encryptedOutput.readableBytes());

    // If more data remains in our buffer, post the *next* asynchronous write
    if (encryptedOutput.readableBytes() > 0) {
        GANL_CONN_DEBUG(handle_, "IOCP: More data to send. Posting next write.");
        if (!postWrite()) {
            GANL_CONN_DEBUG(handle_, "IOCP: Failed to post subsequent write. Closing.");
            close(DisconnectReason::NetworkError);
            // Return? Error event should handle close.
        }
    }
    else {
        GANL_CONN_DEBUG(handle_, "IOCP: Write complete. No further data to send for now.");
    }
}

// --- Factory ---

// static
std::shared_ptr<ConnectionBase> ConnectionFactory::createConnection(
    ConnectionHandle handle,
    NetworkEngine& engine,
    SecureTransport* secureTransport, // Can be nullptr
    ProtocolHandler& protocolHandler,
    SessionManager& sessionManager) {

    IoModel model = engine.getIoModelType();
    if (model == IoModel::Completion) {
        return std::make_shared<CompletionConnection>(handle, engine, secureTransport, protocolHandler, sessionManager);
    }
    else if (model == IoModel::Readiness) {
        return std::make_shared<ReadinessConnection>(handle, engine, secureTransport, protocolHandler, sessionManager);
    }
    else {
        // Handle error - Unknown model
        std::cerr << "FATAL: Unknown Network Engine IO Model in Factory!" << std::endl;
        return nullptr;
    }
}

} // namespace ganl
