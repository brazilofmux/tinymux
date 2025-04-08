#ifdef USE_GANL

#include "ganl_adapter.h"
#include "connection.h" // Include ConnectionBase definition
#include "network_types.h"

#include <iostream>
#include <chrono>
#include <thread> // For sleep

extern const UTF8* disc_messages[];
extern const UTF8* connect_fail;
void site_mon_send(const SOCKET port, const UTF8* address, DESC* d, const UTF8* msg);
void announce_connect(const dbref player, DESC* d);
const UTF8* encode_iac(const UTF8* szString);

// --- GANL Callback Implementations ---

class GanlTinyMuxSessionManager : public ganl::SessionManager {
private:
    GanlAdapter& adapter_;

public:
    GanlTinyMuxSessionManager(GanlAdapter& adapter) : adapter_(adapter) {}
    ~GanlTinyMuxSessionManager() override = default;

    bool initialize() override { /* TODO: Any TinyMUX session init? */ return true; }
    void shutdown() override { /* TODO: Any TinyMUX session cleanup? */ }

    ganl::SessionId onConnectionOpen(ganl::ConnectionHandle handle, const std::string& remoteAddress) override {
        //GANL_CONN_DEBUG(handle, "GANL: New connection opened from " << remoteAddress);

        // Allocate and initialize a TinyMUX DESC structure
        DESC* d = adapter_.allocate_desc();
        if (!d) {
            std::cerr << "[GANL Adapter] Failed to allocate DESC for handle " << handle << std::endl;
            return ganl::InvalidSessionId; // Reject connection
        }

        // --- Populate DESC essentials (adapted from initializesock) ---
        d->socket = static_cast<int>(handle); // Use handle as pseudo-socket descriptor
        d->flags = 0; // Start with no flags
        d->connected_at.GetUTC();
        d->last_time = d->connected_at;
        d->retries_left = mudconf.retry_limit;
        d->command_count = 0;
        d->timeout = mudconf.idle_timeout;
        d->player = NOTHING; // Not logged in yet
        d->addr[0] = '\0';
        d->doing[0] = '\0';
        d->username[0] = '\0';
        d->output_prefix = nullptr;
        d->output_suffix = nullptr;
        d->output_size = 0;
        d->output_tot = 0;
        d->output_lost = 0;
        d->output_head = nullptr; // GANL handles output buffering
        d->output_tail = nullptr;
        d->input_head = nullptr; // Commands queued here
        d->input_tail = nullptr;
        d->input_size = 0;
        d->input_tot = 0;
        d->input_lost = 0;
        d->raw_input = nullptr; // Legacy raw input buffer unused
        d->raw_input_at = nullptr;
        d->raw_input_state = NVT_IS_NORMAL; // Will be managed by ProtocolHandler?
        d->quota = mudconf.cmd_quota_max;
        d->program_data = nullptr;
        d->ttype = nullptr;
        d->height = 24;
        d->width = 78;
        d->encoding = mudconf.default_charset;
        d->negotiated_encoding = mudconf.default_charset;


        // Copy address info
        strncpy((char*)d->addr, remoteAddress.c_str(), sizeof(d->addr) - 1);
        d->addr[sizeof(d->addr) - 1] = '\0';

        // TODO: Rework address storage/retrieval if needed - mux_sockaddr logic moved
        // The NetworkAddress object could be stored or retrieved from the engine

        //GANL_CONN_DEBUG(handle, "DESC " << d << " initialized for connection.");

        // Create the Connection object (needed before add_mapping)
        ganl::ErrorCode error = 0;
        std::shared_ptr<ganl::ConnectionBase> conn = ganl::ConnectionFactory::createConnection(
            handle,
            *adapter_.get_engine(), // Pass the engine
            g_GanlAdapter.secureTransport_.get(), // Pass TLS handler (can be null)
            *g_GanlAdapter.protocolHandler_, // Pass protocol handler
            *this // Pass session manager (this)
        );

        if (!conn) {
            std::cerr << "[GANL Adapter] Failed to create ConnectionBase for handle " << handle << std::endl;
            adapter_.free_desc2(d);
            return ganl::InvalidSessionId;
        }

        // Associate the ConnectionBase object as the context for network events
        if (!adapter_.get_engine()->associateContext(handle, conn.get(), error)) {
            std::cerr << "[GANL Adapter] Failed to associate ConnectionBase context for handle " << handle << ": " << adapter_.get_engine()->getErrorString(error) << std::endl;
            adapter_.free_desc2(d);
            // ConnectionBase will be destroyed by shared_ptr going out of scope
            return ganl::InvalidSessionId;
        }

        // Add mapping after successful association
        adapter_.add_mapping(handle, d, conn);

        // Check site restrictions
        // Rework: Need access to the NetworkAddress object here
        // ganl::NetworkAddress netAddr = adapter_.get_engine()->getRemoteNetworkAddress(handle);
        // if (netAddr.isValid() && mudstate.access_list.isForbid(&netAddr)) {
        //     GANL_CONN_DEBUG(handle, "Connection refused by site restrictions.");
        //     // Need to properly close via GANL
        //     conn->close(ganl::DisconnectReason::AdminKick); // Or a specific reason
        //     return ganl::InvalidSessionId; // Indicate rejection
        // }


        // Call TinyMUX welcome function (sends MOTD etc.)
        welcome_user(d); // Assumes DESC is sufficiently populated

        // Initialize the connection state machine (TLS handshake or Telnet)
        // Need to know if this listener was SSL
        bool useTls = false;
        auto lctx_it = g_GanlAdapter.listener_contexts_.find(handle); // WRONG handle, need listener handle
        // TODO: Pass listener handle or port info to onConnectionOpen
        // For now, assume non-TLS for simplicity
        if (!conn->initialize(useTls)) {
            //GANL_CONN_DEBUG(handle, "Connection initialization failed.");
            // initialize should trigger close if failed
            // No need to return InvalidSessionId here, let close path handle cleanup
        }

        // Return the handle as the SessionId (simple mapping)
        return static_cast<ganl::SessionId>(handle);
    }

    void onDataReceived(ganl::SessionId sessionId, const std::string& commandLine) override {
        ganl::ConnectionHandle handle = static_cast<ganl::ConnectionHandle>(sessionId);
        DESC* d = adapter_.get_desc(handle);
        if (!d) {
            //GANL_CONN_DEBUG(handle, "Data received for unknown/closed session.");
            return;
        }

        //GANL_CONN_DEBUG(handle, "Received application line: '" << commandLine << "'");

        // Use the refactored legacy Telnet/command parser
        process_input_from_ganl(d, commandLine);
    }

    void onConnectionClose(ganl::SessionId sessionId, ganl::DisconnectReason reason) override {
        ganl::ConnectionHandle handle = static_cast<ganl::ConnectionHandle>(sessionId);
        DESC* d = adapter_.get_desc(handle);
        if (!d) {
            //GANL_CONN_DEBUG(handle, "Close notification for unknown/closed session.");
            return;
        }

        //GANL_CONN_DEBUG(handle, "Connection closed. TinyMUX reason: " << static_cast<int>(reason));

        // Perform TinyMUX disconnect actions ONLY if logged in
        if (d->player != NOTHING) {
            // Map GANL reason to TinyMUX reason if needed, or pass directly if compatible
            // For now, use a generic reason or map roughly
            int mux_reason = R_SOCKDIED; // Default/fallback
            switch (reason) {
            case ganl::DisconnectReason::UserQuit: mux_reason = R_QUIT; break;
            case ganl::DisconnectReason::Timeout: mux_reason = R_TIMEOUT; break;
            case ganl::DisconnectReason::NetworkError: mux_reason = R_SOCKDIED; break;
            case ganl::DisconnectReason::ProtocolError: mux_reason = R_SOCKDIED; break; // Maybe specific?
            case ganl::DisconnectReason::TlsError: mux_reason = R_SOCKDIED; break; // Maybe specific?
            case ganl::DisconnectReason::ServerShutdown: mux_reason = R_GOING_DOWN; break;
            case ganl::DisconnectReason::AdminKick: mux_reason = R_BOOT; break;
            case ganl::DisconnectReason::GameFull: mux_reason = R_GAMEFULL; break;
            case ganl::DisconnectReason::LoginFailed: mux_reason = R_BADLOGIN; break;
            case ganl::DisconnectReason::Unknown:
            default: mux_reason = R_UNKNOWN; break;
            }
            announce_disconnect(d->player, d, disc_messages[mux_reason]);
        }
        else {
            // Logged-out disconnect cleanup (e.g., site monitor)
            site_mon_send(d->socket, d->addr, d, T("N/C Connection Closed"));
        }


        // Cleanup TinyMUX resources associated with the DESC
        clearstrings(d);
        freeqs(d);

        // Remove mapping FIRST
        adapter_.remove_mapping(d);

        // Then free the DESC structure
        adapter_.free_desc2(d);

        //GANL_CONN_DEBUG(handle, "TinyMUX DESC resources cleaned up.");
    }

    bool sendToSession(ganl::SessionId sessionId, const std::string& message) override {
        ganl::ConnectionHandle handle = static_cast<ganl::ConnectionHandle>(sessionId);
        DESC* d = adapter_.get_desc(handle);
        if (!d) return false;

        // Directly send using adapter's send_data
        adapter_.send_data(d, message.c_str(), message.length());
        return true;
    }

    bool broadcastMessage(const std::string& message, ganl::SessionId except = ganl::InvalidSessionId) override {
        // This might be better handled by TinyMUX's raw_broadcast logic
        // Iterate through all connections managed by the adapter?
        // raw_broadcast(0, message.c_str()); // Example - needs care with flags/permissions
        std::cerr << "[GANL Adapter] broadcastMessage not fully implemented via GANL." << std::endl;
        return false;
    }

    bool disconnectSession(ganl::SessionId sessionId, ganl::DisconnectReason reason) override {
        ganl::ConnectionHandle handle = static_cast<ganl::ConnectionHandle>(sessionId);
        DESC* d = adapter_.get_desc(handle);
        if (!d) return false;

        adapter_.close_connection(d, reason);
        return true;
    }

    bool authenticateSession(ganl::SessionId sessionId, ganl::ConnectionHandle connHandle,
        const std::string& username, const std::string& password) override {
        DESC* d = adapter_.get_desc(connHandle);
        if (!d) return false;

        // Check login enabled, player limits etc.
        if (!(mudconf.control_flags & CF_LOGIN)) {
            fcache_dump(d, FC_CONN_DOWN);
            queue_string(d, mudconf.downmotd_msg);
            queue_write_LEN(d, T("\r\n"), 2);
            adapter_.close_connection(d, ganl::DisconnectReason::LoginFailed); // Or ServerShutdown?
            return false;
        }
        // TODO: Check player limits (similar to check_connect)

        // Rework: Get IP address from the engine
        std::string host = adapter_.get_engine()->getRemoteAddress(connHandle);
        UTF8 szHost[MBUF_SIZE];
        strncpy((char*)szHost, host.c_str(), MBUF_SIZE - 1);
        szHost[MBUF_SIZE - 1] = '\0';

        // Call TinyMUX connection logic
        dbref player = connect_player((UTF8*)username.c_str(), (UTF8*)password.c_str(),
            szHost, d->username, szHost); // Use IP for both hostname/IP for now

        if (player == NOTHING) {
            //GANL_CONN_DEBUG(connHandle, "Authentication failed for user: " << username);
            queue_write(d, connect_fail); // Use TinyMUX message
            if (--(d->retries_left) <= 0) {
                adapter_.close_connection(d, ganl::DisconnectReason::LoginFailed);
            }
            return false;
        }

        //GANL_CONN_DEBUG(connHandle, "Authentication successful for user: " << username << " (#" << player << ")");
        // TinyMUX success logic (moved from check_connect)
        d->flags |= DS_CONNECTED;
        d->connected_at.GetUTC(); // Reset connected time? Or keep original? Keep original for now.
        d->player = player;

        // Associate player dbref with the descriptor for TinyMUX functions
        ganl_associate_player(d, player);

        // TODO: Handle @program association if needed

        // Send MOTD, handle LAST, Comsys, Announce etc.
        announce_connect(player, d); // Handles MOTD, logs, mail, look etc.

        // TODO: Call local_connect and Sink connects
        // int num_cons = fetch_session(player); // Need a way to count sessions for player
        // local_connect(player, 0, num_cons);
        // ServerEventsSinkNode *pNode = g_pServerEventsSinkListHead;
        // while (nullptr != pNode) {
        //     pNode->pSink->connect(player, 0, num_cons);
        //     pNode = pNode->pNext;
        // }

        return true;
    }

    void onAuthenticationSuccess(ganl::SessionId sessionId, int playerId) {
        // This might be redundant if authenticateSession handles everything.
        // Could be used for post-authentication setup if needed.
        //GANL_CONN_DEBUG(static_cast<ganl::ConnectionHandle>(sessionId), "onAuthenticationSuccess called for Player #" << playerId);
    }

    int getPlayerId(ganl::SessionId sessionId) override {
        DESC* d = adapter_.get_desc(static_cast<ganl::ConnectionHandle>(sessionId));
        return (d && d->player != NOTHING) ? d->player : -1;
    }

    ganl::SessionState getSessionState(ganl::SessionId sessionId) override {
        DESC* d = adapter_.get_desc(static_cast<ganl::ConnectionHandle>(sessionId));
        if (!d) return ganl::SessionState::Closed;
        if (d->player == NOTHING) return ganl::SessionState::Authenticating; // Or Connecting?
        return ganl::SessionState::Connected;
    }

    ganl::SessionStats getSessionStats(ganl::SessionId sessionId) override {
        DESC* d = adapter_.get_desc(static_cast<ganl::ConnectionHandle>(sessionId));
        ganl::SessionStats stats = {};
        if (d) {
            // Approximate mapping
            stats.bytesReceived = d->input_tot;
            stats.bytesSent = d->output_tot;
            stats.commandsProcessed = d->command_count;
            stats.connectedAt = d->connected_at.ReturnSeconds();
            stats.lastActivity = d->last_time.ReturnSeconds();
        }
        return stats;
    }

    ganl::ConnectionHandle getConnectionHandle(ganl::SessionId sessionId) override {
        // Simple mapping: Session ID is the Connection Handle
        return static_cast<ganl::ConnectionHandle>(sessionId);
    }

    // --- Address Checks (delegate to TinyMUX) ---
    bool isAddressAllowed(const std::string& address) override {
        // Rework: Need NetworkAddress
        // ganl::NetworkAddress netAddr = ... ; // How to construct from string? Needs helper
        // return !mudstate.access_list.isForbid(&netAddr);
        return true; // Placeholder
    }
    bool isAddressRegistered(const std::string& address) override {
        // Rework needed
        return false; // Placeholder
    }
    bool isAddressForbidden(const std::string& address) override {
        // Rework needed
        return false; // Placeholder
    }
    bool isAddressSuspect(const std::string& address) override {
        // Rework needed
        return false; // Placeholder
    }

    std::string getLastSessionErrorString(ganl::SessionId sessionId) {
        // TODO: Store last error per session if needed
        return "";
    }
};


class GanlTinyMuxProtocolHandler : public ganl::TelnetProtocolHandler {
public:
    GanlTinyMuxProtocolHandler() = default;
    ~GanlTinyMuxProtocolHandler() override = default;

    // Override processInput to simply pass data through (using base class logic for Telnet parsing)
    // The actual command parsing happens later in GanlSessionManager::onDataReceived
    // by calling process_input_from_ganl.
    bool processInput(ganl::ConnectionHandle conn, ganl::IoBuffer& decrypted_in,
        ganl::IoBuffer& app_data_out, ganl::IoBuffer& telnet_responses_out,
        bool consumeInput = true) override {
        //GANL_TELNET_DEBUG(conn, "GANL ProtocolHandler processInput");
        // Use the base class's Telnet parsing
        return TelnetProtocolHandler::processInput(conn, decrypted_in, app_data_out, telnet_responses_out, consumeInput);
    }


    // Override formatOutput to handle TinyMUX color codes -> ANSI/HTML
    bool formatOutput(ganl::ConnectionHandle conn, ganl::IoBuffer& app_data_in,
        ganl::IoBuffer& formatted_out, bool consumeInput = true) override {

        DESC* d = g_GanlAdapter.get_desc(conn);
        if (!d) return false; // Should not happen

        size_t consumed = 0;
        size_t readable = app_data_in.readableBytes();
        if (readable == 0) return true;

        // Create a temporary string from the IoBuffer for processing
        // Note: This involves a copy. A more efficient way would be to process
        // the buffer directly or use string_view if C++17 is guaranteed.
        std::string plain_text(app_data_in.readPtr(), readable);

        const UTF8* p_converted;
        bool use_html = (d->flags & DS_CONNECTED) && Html(d->player);
        bool use_ansi = (d->flags & DS_CONNECTED) && Ansi(d->player);
        bool no_bleed = (d->flags & DS_CONNECTED) && NoBleed(d->player);
        bool color256 = (d->flags & DS_CONNECTED) && Color256(d->player);


        if (use_html) {
            p_converted = convert_to_html((UTF8*)plain_text.c_str());
        }
        else if (use_ansi) {
            p_converted = convert_color((UTF8*)plain_text.c_str(), no_bleed, color256);
        }
        else {
            p_converted = strip_color((UTF8*)plain_text.c_str());
        }

        // TODO: Handle character set conversion (UTF8 -> Target Encoding)
        // This requires knowing the target encoding from the TelnetContext
        // For now, assume UTF-8 output if Telnet negotiation selected it, else ANSI/stripped.
        // const UTF8* q = p_converted; // Placeholder

        // Escape IAC characters
        const UTF8* q_escaped = encode_iac(p_converted); // encode_iac uses a static buffer

        // Append the final processed string to formatted_out
        formatted_out.append(q_escaped, strlen((const char*)q_escaped));

        if (consumeInput) {
            app_data_in.consumeRead(readable); // Consume the original amount
        }

        return true;
    }

    // Override negotiation start to match TinyMUX's specific requests
    void startNegotiation(ganl::ConnectionHandle conn, ganl::IoBuffer& telnet_responses_out) {
        // Use the base class implementation which sends the standard MUX offers
        TelnetProtocolHandler::startNegotiation(conn, telnet_responses_out);
        // No TinyMUX-specific overrides needed here currently, assuming base matches.
    }

    // Override subnegotiation handler if needed for custom TinyMUX options
    void handleTelnetSubnegotiation(ganl::ConnectionHandle conn,
        ganl::TelnetOption option,
        const ganl::IoBuffer& subnegotiationData,
        ganl::IoBuffer& telnet_responses_out)
    {
        // Let the base class handle standard options like NAWS, TTYPE, CHARSET
        TelnetProtocolHandler::handleTelnetSubnegotiation(conn, option, subnegotiationData, telnet_responses_out);

        // Add handling for any TinyMUX-specific options here if necessary
        // Example:
        // if (option == YourCustomOption) { ... }
    }

};

// --- Global Adapter Instance ---
GanlAdapter g_GanlAdapter;

// --- GanlAdapter Implementation ---

GanlAdapter::GanlAdapter() = default;

GanlAdapter::~GanlAdapter() {
    // Shutdown should be called explicitly, but ensure cleanup if not
    shutdown();
}

bool GanlAdapter::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return true;

    Log.WriteString(T("Initializing GANL Adapter...\n"));

    // 1. Create Network Engine
    networkEngine_ = ganl::NetworkEngineFactory::createEngine();
    if (!networkEngine_) {
        std::cerr << "[GANL Adapter] FATAL: Failed to create network engine." << std::endl;
        Log.WriteString(T("FATAL: Failed to create GANL network engine.\n"));
        return false;
    }
    Log.tinyprintf(T("Using GANL Network Engine: %d\n"), networkEngine_->getIoModelType());


    // 2. Initialize Network Engine
    if (!networkEngine_->initialize()) {
        std::cerr << "[GANL Adapter] FATAL: Failed to initialize network engine." << std::endl;
        Log.WriteString(T("FATAL: Failed to initialize GANL network engine.\n"));
        networkEngine_.reset(); // Release the failed engine
        return false;
    }

    // 3. Create Secure Transport (TLS/SSL)
    // TODO: Make transport type configurable?
    secureTransport_ = ganl::SecureTransportFactory::createTransport();
    if (secureTransport_) {
        ganl::TlsConfig tlsConfig;
        tlsConfig.certificateFile = (const char*)mudconf.ssl_certificate_file; // Assuming UTF8 is compatible
        tlsConfig.keyFile = (const char*)mudconf.ssl_certificate_key;
        tlsConfig.password = (const char*)mudconf.ssl_certificate_password;
        // tlsConfig.verifyPeer = false; // Default

        if (!secureTransport_->initialize(tlsConfig)) {
            std::cerr << "[GANL Adapter] Warning: Failed to initialize secure transport: "
                << secureTransport_->getLastTlsErrorString(0) << std::endl;
            Log.tinyprintf(T("Warning: Failed to initialize GANL secure transport: %s\n"),
                secureTransport_->getLastTlsErrorString(0).c_str());
            secureTransport_.reset(); // Don't use TLS if init failed
        }
        else {
            Log.WriteString(T("GANL Secure Transport initialized.\n"));
        }
    }
    else {
        Log.WriteString(T("No GANL Secure Transport available or created.\n"));
    }

    // 4. Create Protocol Handler
    protocolHandler_ = std::make_unique<GanlTinyMuxProtocolHandler>();
    // No specific initialization needed for this simple handler yet

    // 5. Create Session Manager
    sessionManager_ = std::make_unique<GanlTinyMuxSessionManager>(*this);
    if (!sessionManager_->initialize()) {
        std::cerr << "[GANL Adapter] FATAL: Failed to initialize session manager." << std::endl;
        Log.WriteString(T("FATAL: Failed to initialize GANL session manager.\n"));
        // Need proper cleanup
        networkEngine_->shutdown();
        networkEngine_.reset();
        if (secureTransport_) secureTransport_->shutdown();
        secureTransport_.reset();
        protocolHandler_.reset();
        sessionManager_.reset();
        return false;
    }

    // 6. Create Listeners based on mudconf
    ganl::ErrorCode error = 0;
    for (int i = 0; i < mudconf.ports.n; ++i) {
        int port = mudconf.ports.pi[i];
        std::string host = mudconf.ip_address ? (const char*)mudconf.ip_address : ""; // Handle null IP address

        ganl::ListenerHandle handle = networkEngine_->createListener(host, port, error);
        if (handle != ganl::InvalidListenerHandle) {
            // Store listener context
            listener_contexts_[handle] = { port, false };
            if (networkEngine_->startListening(handle, &listener_contexts_[handle] /* Pass context */, error)) {
                port_listeners_[port] = handle;
                Log.tinyprintf(T("GANL listening on %s:%d (Handle: %llu)\n"),
                    host.empty() ? "*" : host.c_str(), port, (unsigned long long)handle);
            }
            else {
                Log.tinyprintf(T("GANL failed to start listening on port %d: %s\n"),
                    port, networkEngine_->getErrorString(error).c_str());
                networkEngine_->closeListener(handle); // Clean up created listener
            }
        }
        else {
            Log.tinyprintf(T("GANL failed to create listener for port %d: %s\n"),
                port, networkEngine_->getErrorString(error).c_str());
        }
    }

    // Create SSL Listeners
    if (secureTransport_) { // Only if TLS is initialized
        for (int i = 0; i < mudconf.sslPorts.n; ++i) {
            int port = mudconf.sslPorts.pi[i];
            std::string host = mudconf.ip_address ? (const char*)mudconf.ip_address : ""; // Handle null IP address

            ganl::ListenerHandle handle = networkEngine_->createListener(host, port, error);
            if (handle != ganl::InvalidListenerHandle) {
                // Store listener context
                listener_contexts_[handle] = { port, true };
                if (networkEngine_->startListening(handle, &listener_contexts_[handle] /* Pass context */, error)) {
                    ssl_port_listeners_[port] = handle;
                    Log.tinyprintf(T("GANL listening with SSL on %s:%d (Handle: %llu)\n"),
                        host.empty() ? "*" : host.c_str(), port, (unsigned long long)handle);
                }
                else {
                    Log.tinyprintf(T("GANL failed to start SSL listening on port %d: %s\n"),
                        port, networkEngine_->getErrorString(error).c_str());
                    networkEngine_->closeListener(handle); // Clean up created listener
                }
            }
            else {
                Log.tinyprintf(T("GANL failed to create SSL listener for port %d: %s\n"),
                    port, networkEngine_->getErrorString(error).c_str());
            }
        }
    }


    if (port_listeners_.empty() && ssl_port_listeners_.empty()) {
        Log.WriteString(T("Warning: No GANL listeners successfully started.\n"));
        // Continue? Or fail? Let's continue for now.
    }


    initialized_ = true;
    Log.WriteString(T("GANL Adapter initialized.\n"));
    return true;
}

void GanlAdapter::shutdown() {
    std::unique_lock<std::mutex> lock(mutex_); // Use unique_lock for manual control
    if (!initialized_) return;
    initialized_ = false; // Mark as shutting down immediately

    Log.WriteString(T("Shutting down GANL Adapter...\n"));

    // Release lock before calling potentially blocking shutdown methods
    lock.unlock();

    // Close listeners first
    // Create copies of handles to avoid iterator invalidation during closeListener call
    std::vector<ganl::ListenerHandle> plainHandles, sslHandles;
    plainHandles.reserve(port_listeners_.size());
    for (const auto& pair : port_listeners_) plainHandles.push_back(pair.second);
    sslHandles.reserve(ssl_port_listeners_.size());
    for (const auto& pair : ssl_port_listeners_) sslHandles.push_back(pair.second);

    for (ganl::ListenerHandle handle : plainHandles) networkEngine_->closeListener(handle);
    for (ganl::ListenerHandle handle : sslHandles) networkEngine_->closeListener(handle);


    // Shutdown Session Manager (should notify TinyMUX about remaining connections)
    if (sessionManager_) {
        sessionManager_->shutdown();
        sessionManager_.reset();
    }

    // Shutdown Protocol Handler (if needed)
    if (protocolHandler_) {
        // protocolHandler_->shutdown(); // Assuming no specific shutdown needed
        protocolHandler_.reset();
    }

    // Shutdown Secure Transport
    if (secureTransport_) {
        secureTransport_->shutdown();
        secureTransport_.reset();
    }

    // Shutdown Network Engine LAST
    if (networkEngine_) {
        networkEngine_->shutdown();
        networkEngine_.reset();
    }

    // Re-acquire lock to clear maps safely
    lock.lock();
    port_listeners_.clear();
    ssl_port_listeners_.clear();
    handle_to_desc_.clear();
    desc_to_handle_.clear();
    handle_to_conn_.clear();
    listener_contexts_.clear();

    Log.WriteString(T("GANL Adapter shut down.\n"));
}

void GanlAdapter::run_main_loop() {
    Log.WriteString(T("GANL: Entering main loop.\n"));

    CLinearTimeAbsolute ltaLastSlice;
    ltaLastSlice.GetUTC();

    while (!mudstate.shutdown_flag) {
        int timeout_ms = 100; // Default timeout for processEvents

        // Calculate minimum timeout based on scheduled tasks
        CLinearTimeAbsolute ltaNextTask;
        if (scheduler.WhenNext(&ltaNextTask)) {
            CLinearTimeAbsolute ltaNow;
            ltaNow.GetUTC();
            if (ltaNextTask > ltaNow) {
                CLinearTimeDelta ltdToNext = ltaNextTask - ltaNow;
                int timeout_ms = ltdToNext.ReturnMilliseconds();
            }
            else {
                timeout_ms = 0; // Task is due now or overdue
            }
        }

        // Process Network Events
        constexpr int MAX_EVENTS_PER_CALL = 64;
        ganl::IoEvent events[MAX_EVENTS_PER_CALL];
        int num_events = networkEngine_->processEvents(timeout_ms, events, MAX_EVENTS_PER_CALL);

        if (num_events < 0) {
            Log.WriteString(T("GANL: Network engine processEvents error. Shutting down.\n"));
            mudstate.shutdown_flag = true;
            break;
        }

        // Dispatch network events to Connection objects
        for (int i = 0; i < num_events; ++i) {
            if (events[i].type == ganl::IoEventType::Accept) {
                // Accept event is handled by SessionManager::onConnectionOpen
                // which is triggered indirectly when the connection is initialized
                //GANL_CONN_DEBUG(events[i].connection, "GANL Accept event processed implicitly.");
            }
            else if (events[i].connection != ganl::InvalidConnectionHandle) {
                std::shared_ptr<ganl::ConnectionBase> conn = nullptr;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = handle_to_conn_.find(events[i].connection);
                    if (it != handle_to_conn_.end()) {
                        conn = it->second; // Get the shared_ptr
                    }
                }
                if (conn) {
                    // Event context should be the ConnectionBase* itself
                    if (events[i].context == conn.get()) {
                        conn->handleNetworkEvent(events[i]);
                    }
                    else {
                        std::cerr << "[GANL Adapter] Mismatched context for event on handle "
                            << events[i].connection << "! Expected " << conn.get()
                            << ", Got " << events[i].context << std::endl;
                    }
                }
                else {
                    //GANL_CONN_DEBUG(events[i].connection, "Warning: Event received for untracked/closed connection.");
                }
            }
            // TODO: Handle listener errors? (events[i].listener != InvalidListenerHandle)
        }

        // Process TinyMUX Tasks (Timers, Idle, Quotas, etc.)
        process_tinyMUX_tasks();

    } // end while (!mudstate.shutdown_flag)

    Log.WriteString(T("GANL: Exiting main loop.\n"));
}

// Helper to run periodic TinyMUX tasks
void GanlAdapter::process_tinyMUX_tasks() {
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();

    // Check Scheduler (frequently)
    scheduler.RunTasks(ltaNow); // Run tasks due now or earlier

    // Update Quotas (every timeslice) - TODO: Need ltaLastSlice persisted
    // update_quotas(ltaLastSlice, ltaNow); // Needs ltaLastSlice from adapter state

    // Check Idle Players (periodically) - TODO: Need ltaLastIdleCheck
    // if (ltaNow >= ltaLastIdleCheck + ltdIdleInterval) {
    //      check_idle();
    //      ltaLastIdleCheck = ltaNow;
    // }

    // Check @daily Events (periodically) - TODO: Need ltaLastDBCheck
    // if (ltaNow >= ltaLastDBCheck + ltdDBInterval) {
    //      check_events();
    //      ltaLastDBCheck = ltaNow;
    // }

     // TODO: Add periodic calls for mail expiration, DB dump timers etc.
}


void GanlAdapter::send_data(DESC* d, const char* data, size_t len) {
    if (!d) return;
    std::shared_ptr<ganl::ConnectionBase> conn = get_connection(d);
    if (conn) {
        // Convert char* to std::string for ConnectionBase interface
        conn->sendDataToClient(std::string(data, len));
    }
    else {
        //GANL_CONN_DEBUG(d->socket, "Attempted send on unmapped/closed connection.");
    }
}

void GanlAdapter::close_connection(DESC* d, ganl::DisconnectReason reason) {
    if (!d) return;
    std::shared_ptr<ganl::ConnectionBase> conn = get_connection(d);
    if (conn) {
        conn->close(reason);
        // Note: Connection::close() might trigger SessionManager::onConnectionClose
        // which can lead to remove_mapping and free_desc being called.
    }
    else {
        //GANL_CONN_DEBUG(d->socket, "Attempted close on unmapped/closed connection.");
        // If not found in GANL, maybe it was never fully mapped? Clean up DESC anyway.
        if (d->player != NOTHING) {
            announce_disconnect(d->player, d, T("Unknown Closure")); // Provide a default reason
        }
        clearstrings(d);
        freeqs(d);
        free_desc(d); // Free DESC if GANL doesn't know about it
    }
}

std::string GanlAdapter::get_remote_address(DESC* d) {
    if (!d) return "";
    // Use the stored address in DESC for now
    return (const char*)d->addr;
    // Future:
    // ganl::ConnectionHandle handle = get_handle(d);
    // if (handle != ganl::InvalidConnectionHandle) {
    //     return networkEngine_->getRemoteAddress(handle);
    // }
    // return "";
}

int GanlAdapter::get_socket_descriptor(DESC* d) {
    if (!d) return -1;
    // Return the ConnectionHandle cast to int as a pseudo-descriptor
    // This is risky if handles exceed int range, but needed for some legacy funcs.
    return static_cast<int>(get_handle(d));
}


// --- Mapping Implementation ---
void GanlAdapter::add_mapping(ganl::ConnectionHandle handle, DESC* d, std::shared_ptr<ganl::ConnectionBase> conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    handle_to_desc_[handle] = d;
    desc_to_handle_[d] = handle;
    handle_to_conn_[handle] = conn; // Store the shared_ptr
    //GANL_CONN_DEBUG(handle, "Mapped Handle <-> DESC " << d << " and Connection " << conn.get());
}

void GanlAdapter::remove_mapping(DESC* d) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it_dth = desc_to_handle_.find(d);
    if (it_dth != desc_to_handle_.end()) {
        ganl::ConnectionHandle handle = it_dth->second;
        handle_to_desc_.erase(handle);
        handle_to_conn_.erase(handle); // Remove connection pointer
        desc_to_handle_.erase(it_dth); // Use iterator for efficiency
        //GANL_CONN_DEBUG(handle, "Unmapped Handle <-> DESC " << d);
    }
    else {
        // Attempting to remove DESC not in map (might happen if already closed)
        //GANL_CONN_DEBUG(0, "Warning: Attempted remove_mapping for unknown DESC " << d);
    }
}

DESC* GanlAdapter::get_desc(ganl::ConnectionHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handle_to_desc_.find(handle);
    return (it != handle_to_desc_.end()) ? it->second : nullptr;
}

std::shared_ptr<ganl::ConnectionBase> GanlAdapter::get_connection(DESC* d) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it_dth = desc_to_handle_.find(d);
    if (it_dth != desc_to_handle_.end()) {
        ganl::ConnectionHandle handle = it_dth->second;
        auto it_htc = handle_to_conn_.find(handle);
        if (it_htc != handle_to_conn_.end()) {
            return it_htc->second; // Return the shared_ptr
        }
    }
    return nullptr; // Return null shared_ptr if not found
}


ganl::ConnectionHandle GanlAdapter::get_handle(DESC* d) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = desc_to_handle_.find(d);
    return (it != desc_to_handle_.end()) ? it->second : ganl::InvalidConnectionHandle;
}


// --- DESC Allocation ---
DESC* GanlAdapter::allocate_desc() {
    // Use TinyMUX's pool allocator
    return alloc_desc("ganl_connection");
}

void GanlAdapter::free_desc2(DESC* d) {
    // Use TinyMUX's pool allocator
    free_desc(d);
}


// --- Public Interface Functions ---

void ganl_initialize() {
    if (!g_GanlAdapter.initialize()) {
        // Handle fatal initialization error - perhaps exit?
        std::cerr << "FATAL: GANL Adapter initialization failed." << std::endl;
        exit(1);
    }
}

void ganl_shutdown() {
    g_GanlAdapter.shutdown();
}

void ganl_main_loop() {
    g_GanlAdapter.run_main_loop();
}

void ganl_send_data_str(DESC* d, const UTF8* data) {
    if (data) {
        g_GanlAdapter.send_data(d, (const char*)data, strlen((const char*)data));
    }
}

#if 0
void ganl_send_data_mux_string(DESC* d, const mux_string& data) {
    // Export to a temporary buffer (might need optimization)
    char temp_buf[LBUF_SIZE];
    size_t exported_len = data.export_TextOnly(temp_buf, sizeof(temp_buf));
    g_GanlAdapter.send_data(d, temp_buf, exported_len);
}
#endif

void ganl_close_connection(DESC* d, int reason) {
    // Map TinyMUX reason to GANL reason
    ganl::DisconnectReason ganl_reason = ganl::DisconnectReason::Unknown;
    switch (reason) {
    case R_QUIT:       ganl_reason = ganl::DisconnectReason::UserQuit; break;
    case R_TIMEOUT:    ganl_reason = ganl::DisconnectReason::Timeout; break;
    case R_BOOT:       ganl_reason = ganl::DisconnectReason::AdminKick; break;
    case R_SOCKDIED:   ganl_reason = ganl::DisconnectReason::NetworkError; break;
    case R_GOING_DOWN: ganl_reason = ganl::DisconnectReason::ServerShutdown; break;
    case R_BADLOGIN:   ganl_reason = ganl::DisconnectReason::LoginFailed; break;
    case R_GAMEDOWN:   ganl_reason = ganl::DisconnectReason::ServerShutdown; break; // Or LoginFailed?
    case R_LOGOUT:     ganl_reason = ganl::DisconnectReason::UserQuit; break; // Or specific?
    case R_GAMEFULL:   ganl_reason = ganl::DisconnectReason::GameFull; break;
    case R_RESTART:    ganl_reason = ganl::DisconnectReason::ServerShutdown; break; // Restart unsupported
    default:           ganl_reason = ganl::DisconnectReason::Unknown; break;
    }
    g_GanlAdapter.close_connection(d, ganl_reason);
}

void ganl_associate_player(DESC* d, dbref player) {
    if (d) {
        d->player = player;
        // Add to TinyMUX's player->desc map
        desc_addhash(d);
    }
}

// Refactored legacy parser entry point
void process_input_from_ganl(DESC* d, const std::string& line) {
    if (!d) return;

    // TinyMUX expects commands in command_blocks. We need to adapt.
    // Option 1: Call process_command directly (might bypass queue logic)
    // Option 2: Create a command_block and queue it (preserves queuing)

    // Option 2: Queue the command
    command_block* new_command = reinterpret_cast<command_block*>(alloc_lbuf("ganl_input"));
    if (!new_command) {
        // Handle memory allocation failure
        std::cerr << "[GANL Adapter] Failed to allocate command_block for input." << std::endl;
        return;
    }

    // Copy the command line, ensuring null termination
    strncpy((char*)new_command->cmd, line.c_str(), LBUF_SIZE - 1);
    new_command->cmd[LBUF_SIZE - 1] = '\0';

    save_command(d, new_command); // Queues the command and schedules processing
}

#endif // USE_GANL
