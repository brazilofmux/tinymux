#include "autoconf.h"
#include "ganl_adapter.h"
#include "modules.h"
#include "driverstate.h"
#include "driver_log.h"
#include "driver_bridge.h"
#include "interface.h"
#include "websocket.h"
#include "connection.h" // Include ConnectionBase definition
#include "network_types.h"

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <cerrno>

#ifdef UNIX_SSL
#include <openssl/ssl.h>
#endif

#if defined(_WIN32) || defined(WIN32)
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#endif

#if defined(_WIN32)
static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
    switch (dwCtrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_shutdown_flag = true;
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

extern const UTF8* disc_messages[];
extern const UTF8* disc_reasons[];
extern const UTF8* connect_fail;
void site_mon_send(const SOCKET port, const UTF8* address, DESC* d, const UTF8* msg);
namespace
{
    struct RemoteEndpoint
    {
        std::string host;
        uint16_t port{0};
    };

    RemoteEndpoint ParseRemoteAddress(const std::string& remoteAddress)
    {
        RemoteEndpoint endpoint;
        if (remoteAddress.empty())
        {
            return endpoint;
        }

        std::string hostPart = remoteAddress;
        std::string portPart;

        if (!remoteAddress.empty() && remoteAddress.front() == '[')
        {
            const auto closing = remoteAddress.find(']');
            if (closing != std::string::npos)
            {
                hostPart = remoteAddress.substr(1, closing - 1);
                const auto colon = remoteAddress.find(':', closing);
                if (colon != std::string::npos)
                {
                    portPart = remoteAddress.substr(colon + 1);
                }
            }
        }
        else
        {
            const auto colon = remoteAddress.rfind(':');
            if (colon != std::string::npos)
            {
                hostPart = remoteAddress.substr(0, colon);
                portPart = remoteAddress.substr(colon + 1);
            }
        }

        endpoint.host = hostPart;
        if (!portPart.empty())
        {
            char* endptr = nullptr;
            const unsigned long parsed = std::strtoul(portPart.c_str(), &endptr, 10);
            if (endptr != nullptr && *endptr == '\0' && parsed <= (std::numeric_limits<uint16_t>::max)())
            {
                endpoint.port = static_cast<uint16_t>(parsed);
            }
        }
        return endpoint;
    }

    bool PopulateDescriptorAddress(DESC* d, const RemoteEndpoint& endpoint)
    {
        if (!d || endpoint.host.empty())
        {
            return false;
        }

        UTF8 hostBuf[MBUF_SIZE];
        UTF8 portBuf[16];

        std::memset(hostBuf, 0, sizeof(hostBuf));
        std::memset(portBuf, 0, sizeof(portBuf));

        std::strncpy(reinterpret_cast<char*>(hostBuf), endpoint.host.c_str(), sizeof(hostBuf) - 1);
        if (endpoint.port != 0)
        {
            std::snprintf(reinterpret_cast<char*>(portBuf), sizeof(portBuf), "%u", endpoint.port);
        }

        MUX_ADDRINFO hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

        MUX_ADDRINFO* res = nullptr;
        const UTF8* service = (endpoint.port != 0) ? portBuf : nullptr;
        const int status = mux_getaddrinfo(hostBuf, service, &hints, &res);
        if (status != 0 || res == nullptr || res->ai_addr == nullptr)
        {
            if (res != nullptr)
            {
                mux_freeaddrinfo(res);
            }
            return false;
        }

        const size_t addrLen = static_cast<size_t>(res->ai_addrlen);
        if (addrLen <= d->address.maxaddrlen())
        {
            std::memcpy(d->address.sa(), res->ai_addr, addrLen);
        }
        mux_freeaddrinfo(res);
        return true;
    }

    bool ApplyNetworkAddressToDesc(DESC* d, const ganl::NetworkAddress& netAddr)
    {
        if (!d || !netAddr.isValid())
        {
            return false;
        }

        const struct sockaddr* sa = netAddr.getSockAddr();
        const socklen_t len = netAddr.getSockAddrLen();
        if (len <= 0 || static_cast<size_t>(len) > d->address.maxaddrlen())
        {
            return false;
        }

        std::memcpy(d->address.sa(), sa, static_cast<size_t>(len));
        return true;
    }

    int MapGanlReasonToMux(ganl::DisconnectReason reason)
    {
        switch (reason)
        {
        case ganl::DisconnectReason::UserQuit:     return R_QUIT;
        case ganl::DisconnectReason::Timeout:      return R_TIMEOUT;
        case ganl::DisconnectReason::AdminKick:    return R_BOOT;
        case ganl::DisconnectReason::ServerShutdown:
            return R_GOING_DOWN;
        case ganl::DisconnectReason::LoginFailed:  return R_BADLOGIN;
        case ganl::DisconnectReason::GameFull:     return R_GAMEFULL;
        case ganl::DisconnectReason::TlsError:
        case ganl::DisconnectReason::NetworkError:
        case ganl::DisconnectReason::ProtocolError:
            return R_SOCKDIED;
        default:
            return R_UNKNOWN;
        }
    }

    int ClampMuxReason(int mux_reason)
    {
        if (mux_reason < R_MIN || mux_reason > R_MAX)
        {
            return R_UNKNOWN;
        }
        return mux_reason;
    }

    void InitializeTelnetOptions(DESC* d, bool connectionIsTls)
    {
        if (!d)
        {
            return;
        }

        enable_us(d, TELNET_EOR);
        enable_him(d, TELNET_EOR);
        enable_him(d, TELNET_SGA);
        enable_him(d, TELNET_TTYPE);
        enable_him(d, TELNET_NAWS);
        enable_him(d, TELNET_ENV);
//        enable_him(d, TELNET_OLDENV);
        enable_us(d, TELNET_CHARSET);
        enable_him(d, TELNET_CHARSET);
        enable_us(d, TELNET_MSSP);
        enable_us(d, TELNET_GMCP);
        // STARTTLS is not offered under GANL.  The in-band upgrade
        // path in process_input_helper() drives SSL_new/SSL_set_fd
        // directly on the socket fd, which conflicts with GANL's
        // network engine owning that fd.  Clients wanting TLS should
        // connect to the TLS listener port instead.
    }

    void FinalizeGanlConnection(GanlAdapter& adapter, DESC* d, bool connectionIsTls)
    {
        if (!d)
        {
            return;
        }

        InitializeTelnetOptions(d, connectionIsTls);

        UTF8* siteBuffer = alloc_mbuf("ganl_connection.address");
        if (siteBuffer != nullptr)
        {
            const struct sockaddr* sa = d->address.saro();
            if (sa != nullptr && sa->sa_family != 0)
            {
                d->address.ntop(siteBuffer, MBUF_SIZE);
            }
            else if (d->addr[0] != '\0')
            {
                std::strncpy(reinterpret_cast<char*>(siteBuffer), reinterpret_cast<const char*>(d->addr), MBUF_SIZE - 1);
                siteBuffer[MBUF_SIZE - 1] = '\0';
            }
            else
            {
                siteBuffer[0] = '\0';
            }

            site_mon_send(d->socket, siteBuffer, d, T("Connection"));
            free_mbuf(siteBuffer);
        }

        if (g_dc.use_hostname && d->addr[0] != '\0')
        {
            adapter.queue_dns_lookup(d->addr);
        }

        d->ss = SocketState::Accepted;
        welcome_user(d);

        // GANL READY — suppressed (redundant with NET/CONN log).
    }

    void HandleConnectedDescriptorPreAnnounce(DESC* d, int mux_reason, const CLinearTimeAbsolute& ltaNow)
    {
        if (!d || d->player == NOTHING)
        {
            return;
        }

        mux_reason = ClampMuxReason(mux_reason);

        atr_add_raw(d->player, A_REASON, disc_messages[mux_reason]);

        long anFields[4] = {0, 0, 0, 0};
        fetch_ConnectionInfoFields(d->player, anFields);
        anFields[CIF_NUMCONNECTS]++;

        DESC* dOldest[2] = {nullptr, nullptr};
        find_oldest(d->player, dOldest);
        if (dOldest[0])
        {
            CLinearTimeDelta ltdFull = ltaNow - dOldest[0]->connected_at;
            const long tFull = ltdFull.ReturnSeconds();
            if (dOldest[0] == d)
            {
                CLinearTimeDelta ltdPart;
                if (dOldest[1])
                {
                    ltdPart = dOldest[1]->connected_at - dOldest[0]->connected_at;
                }
                else
                {
                    ltdPart = ltdFull;
                }
                const long tPart = ltdPart.ReturnSeconds();
                anFields[CIF_TOTALTIME] += tPart;
                if (anFields[CIF_LONGESTCONNECT] < tFull)
                {
                    anFields[CIF_LONGESTCONNECT] = tFull;
                }
            }
            anFields[CIF_LASTCONNECT] = tFull;
        }
        CLinearTimeAbsolute ltaLogout = ltaNow;
        put_ConnectionInfoFields(d->player, anFields, ltaLogout);

        if (mux_reason == R_LOGOUT)
        {
            STARTLOG(LOG_NET | LOG_LOGIN, "NET", "LOGO")
            UTF8* buff = alloc_mbuf("ganl_close.LOG.logout");
            mux_sprintf(buff, MBUF_SIZE, T("[%u/%s] Logout by "), d->socket, d->addr);
            g_pILog->log_text(buff);
            g_pILog->log_name(d->player);
            mux_sprintf(buff, MBUF_SIZE, T(" <Reason: %s>"), disc_reasons[mux_reason]);
            g_pILog->log_text(buff);
            free_mbuf(buff);
            ENDLOG;
        }
        else
        {
            fcache_dump(d, FC_QUIT);
            STARTLOG(LOG_NET | LOG_LOGIN, "NET", "DISC")
            UTF8* buff = alloc_mbuf("ganl_close.LOG.disconn");
            mux_sprintf(buff, MBUF_SIZE, T("[%u/%s] Logout by "), d->socket, d->addr);
            g_pILog->log_text(buff);
            g_pILog->log_name(d->player);
            mux_sprintf(buff, MBUF_SIZE, T(" <Reason: %s>"), disc_reasons[mux_reason]);
            g_pILog->log_text(buff);
            free_mbuf(buff);
            ENDLOG;
            site_mon_send(d->socket, d->addr, d, T("Disconnection"));
        }

        STARTLOG(LOG_ACCOUNTING, "DIS", "ACCT");
        CLinearTimeDelta ltd = ltaNow - d->connected_at;
        const int Seconds = ltd.ReturnSeconds();
        UTF8* accnt = alloc_lbuf("ganl_close.LOG.accnt");
        const auto flags = drv_decode_flags(GOD, d->player);
        const auto locPlayer = drv_Location(d->player);
        const auto penPlayer = drv_Pennies(d->player);
        const auto PlayerName = drv_PureName(d->player);
        mux_sprintf(accnt, LBUF_SIZE, T("%d %s %d %d %d %d [%s] <%s> %s"),
            d->player, flags, d->command_count, Seconds, locPlayer, penPlayer,
            d->addr, disc_reasons[mux_reason], PlayerName);
        g_pILog->log_text(accnt);
        free_lbuf(accnt);
        free_sbuf(flags);
        ENDLOG;
    }

    void HandleUnconnectedDescriptorClose(DESC* d)
    {
        if (!d)
        {
            return;
        }

        STARTLOG(LOG_SECURITY | LOG_NET, "NET", "DISC");
        UTF8* buff = alloc_mbuf("ganl_close.LOG.neverconn");
        mux_sprintf(buff, MBUF_SIZE, T("[%u/%s] Connection closed, never connected."),
            d->socket, d->addr);
        g_pILog->log_text(buff);
        free_mbuf(buff);
        ENDLOG;
        site_mon_send(d->socket, d->addr, d, T("N/C Connection Closed"));
    }

    void ResetDescriptorForLogout(DESC* d)
    {
        if (!d)
        {
            return;
        }

        process_output(d, false);
        clearstrings(d);
        freeqs(d);

        if (d->flags & DS_CONNECTED)
        {
            d->flags &= ~DS_CONNECTED;
        }

        if (d->program_data != nullptr)
        {
            int num = 0;
            const auto range = g_dbref_to_descriptors_map.equal_range(d->player);
            for (auto it = range.first; it != range.second; ++it)
            {
                num++;
            }

            if (0 == num)
            {
                for (auto& wait_reg : d->program_data->wait_regs)
                {
                    if (wait_reg)
                    {
                        RegRelease(wait_reg);
                        wait_reg = nullptr;
                    }
                }
                NamedRegsClear(d->program_data->named_wait_regs);
                MEMFREE(d->program_data);
                atr_clr(d->player, A_PROGCMD);
            }
            d->program_data = nullptr;
        }

        drv_CancelTask(Task_ProcessCommand, d, 0);

        if (d->player != NOTHING)
        {
            const dbref player = d->player;
            const auto range = g_dbref_to_descriptors_map.equal_range(player);
            for (auto it = range.first; it != range.second; ++it)
            {
                if (it->second == d)
                {
                    g_dbref_to_descriptors_map.erase(it);
                    break;
                }
            }
        }

        d->connected_at.GetUTC();
        d->retries_left = g_dc.retry_limit;
        d->command_count = 0;
        d->timeout = g_dc.idle_timeout;
        d->player = NOTHING;
        d->username[0] = '\0';
        d->doing[0] = '\0';
        d->quota = g_dc.cmd_quota_max;
        d->last_time = d->connected_at;
        d->input_tot = d->input_size;
        d->output_tot = 0;
        d->encoding = d->negotiated_encoding;

        welcome_user(d);
    }
}

// --- GANL Callback Implementations ---

class GanlTinyMuxSessionManager : public ganl::SessionManager {
private:
    GanlAdapter& adapter_;
    std::map<ganl::SessionId, std::string> session_errors_;

    void setSessionError(ganl::SessionId sid, const std::string& err) {
        if (sid != ganl::InvalidSessionId) {
            session_errors_[sid] = err;
        }
    }

    void clearSessionError(ganl::SessionId sid) {
        session_errors_.erase(sid);
    }

public:
    GanlTinyMuxSessionManager(GanlAdapter& adapter) : adapter_(adapter) {}
    ~GanlTinyMuxSessionManager() override = default;

    bool initialize() override { return true; }
    void shutdown() override { session_errors_.clear(); }


    ganl::SessionId onConnectionOpen(ganl::ConnectionHandle handle, const std::string& remoteAddress) override {
        // During @restart, DESCs already exist — just wire up the mappings.
        if (adapter_.restarting_) {
            for (DESC* d : g_descriptors_list) {
                if (d && d->socket == static_cast<int>(handle)) {
                    std::shared_ptr<ganl::ConnectionBase> conn;
                    auto it = adapter_.handle_to_conn_.find(handle);
                    if (it != adapter_.handle_to_conn_.end()) {
                        conn = it->second;
                    }
                    adapter_.add_mapping(handle, d, conn);
                    return static_cast<ganl::SessionId>(handle);
                }
            }
            return ganl::InvalidSessionId;
        }

        GanlAdapter::ListenerContext listenerCtx{0, false};
        ganl::NetworkAddress remoteNetAddr;
        bool useTls = false;

        auto ctxIt = adapter_.connection_listener_map_.find(handle);
        if (ctxIt != adapter_.connection_listener_map_.end()) {
            listenerCtx = ctxIt->second;
            adapter_.connection_listener_map_.erase(ctxIt);
        }

        auto addrIt = adapter_.pending_remote_addresses_.find(handle);
        if (addrIt != adapter_.pending_remote_addresses_.end()) {
            remoteNetAddr = addrIt->second;
            adapter_.pending_remote_addresses_.erase(addrIt);
        }

        auto tlsIt = adapter_.pending_tls_flags_.find(handle);
        if (tlsIt != adapter_.pending_tls_flags_.end()) {
            useTls = tlsIt->second;
            adapter_.pending_tls_flags_.erase(tlsIt);
        }

        std::shared_ptr<ganl::ConnectionBase> conn;
        auto itConn = adapter_.handle_to_conn_.find(handle);
        if (itConn != adapter_.handle_to_conn_.end()) {
            conn = itConn->second;
        }
        if (!conn) {
            g_pILog->WriteString(tprintf(T("GANL: Missing ConnectionBase for handle %llu\n"),
                static_cast<unsigned long long>(handle)));
            setSessionError(static_cast<ganl::SessionId>(handle), "Missing ConnectionBase");
            return ganl::InvalidSessionId;
        }

        DESC* d = adapter_.allocate_desc();
        if (!d) {
            g_pILog->WriteString(tprintf(T("GANL: Failed to allocate DESC for handle %llu\n"),
                static_cast<unsigned long long>(handle)));
            setSessionError(static_cast<ganl::SessionId>(handle), "Failed to allocate descriptor");
            return ganl::InvalidSessionId;
        }

        d->socket = static_cast<int>(handle);
        d->flags = 0;
        d->connected_at.GetUTC();
        d->last_time = d->connected_at;
        d->retries_left = g_dc.retry_limit;
        d->command_count = 0;
        d->timeout = g_dc.idle_timeout;
        d->player = NOTHING;
        d->addr[0] = '\0';
        d->doing[0] = '\0';
        d->username[0] = '\0';
        d->output_prefix = nullptr;
        d->output_suffix = nullptr;
        d->output_size = 0;
        d->output_tot = 0;
        d->output_lost = 0;
        d->input_size = 0;
        d->input_tot = 0;
        d->input_lost = 0;
        d->raw_input_buf = nullptr;
        d->raw_input_at = nullptr;
        d->nOption = 0;
        d->raw_input_state = NVT_IS_NORMAL;
        d->raw_codepoint_state = CL_PRINT_START_STATE;
        d->raw_codepoint_length = 0;
        d->quota = g_dc.cmd_quota_max;
        d->program_data = nullptr;
        d->ws = nullptr;
        d->ttype = nullptr;
        d->height = 24;
        d->width = 78;
        d->encoding = g_dc.default_charset;
        d->negotiated_encoding = g_dc.default_charset;

        for (auto& state : d->nvt_him_state) {
            state = OPTION_NO;
        }
        for (auto& state : d->nvt_us_state) {
            state = OPTION_NO;
        }

        const RemoteEndpoint endpoint = ParseRemoteAddress(remoteAddress);
        bool haveSockAddr = ApplyNetworkAddressToDesc(d, remoteNetAddr);
        if (!haveSockAddr && !endpoint.host.empty()) {
            haveSockAddr = PopulateDescriptorAddress(d, endpoint);
        }
        if (!haveSockAddr) {
            std::memset(d->address.sa(), 0, d->address.maxaddrlen());
        }

        if (haveSockAddr) {
            d->address.ntop(d->addr, sizeof(d->addr));
        } else if (!endpoint.host.empty()) {
            std::strncpy(reinterpret_cast<char*>(d->addr), endpoint.host.c_str(), sizeof(d->addr) - 1);
            d->addr[sizeof(d->addr) - 1] = '\0';
        } else if (!remoteAddress.empty()) {
            std::strncpy(reinterpret_cast<char*>(d->addr), remoteAddress.c_str(), sizeof(d->addr) - 1);
            d->addr[sizeof(d->addr) - 1] = '\0';
        }

        UTF8 addrText[MBUF_SIZE];
        addrText[0] = '\0';
        if (haveSockAddr) {
            d->address.ntop(addrText, MBUF_SIZE);
        } else if (d->addr[0] != '\0') {
            std::strncpy(reinterpret_cast<char*>(addrText), reinterpret_cast<const char*>(d->addr), MBUF_SIZE - 1);
            addrText[MBUF_SIZE - 1] = '\0';
        }

        if (haveSockAddr && g_access_list.isForbid(&d->address)) {
            STARTLOG(LOG_NET | LOG_SECURITY, "NET", "SITE");
            UTF8* logBuf = alloc_mbuf("ganl_connection.LOG.badsite");
            mux_sprintf(logBuf, MBUF_SIZE, T("[%u/%s] Connection refused.  (Remote port %d)"),
                d->socket, addrText[0] != '\0' ? addrText : T("UNKNOWN"), d->address.port());
            g_pILog->log_text(logBuf);
            free_mbuf(logBuf);
            ENDLOG;

            UTF8* siteBuffer = alloc_mbuf("ganl_connection.address");
            if (siteBuffer != nullptr)
            {
                d->address.ntop(siteBuffer, MBUF_SIZE);
                site_mon_send(d->socket, siteBuffer, nullptr, T("Connection refused"));
                free_mbuf(siteBuffer);
            }
            fcache_rawdump(static_cast<SOCKET>(d->socket), FC_CONN_SITE);

            adapter_.free_desc2(d);
            setSessionError(static_cast<ganl::SessionId>(handle), "Connection refused (forbidden site)");
            return ganl::InvalidSessionId;
        }

        auto listIt = g_descriptors_list.insert(g_descriptors_list.begin(), d);
        g_descriptors_map.insert(std::make_pair(d, listIt));

#ifdef UNIX_SSL
        d->ss = useTls ? SocketState::SSLAcceptAgain : SocketState::Accepted;
#else
        d->ss = SocketState::Accepted;
#endif

        adapter_.add_mapping(handle, d, conn);

        const unsigned short resolvedPort = haveSockAddr ? d->address.port() : endpoint.port;

        STARTLOG(LOG_NET | LOG_LOGIN, "NET", "CONN");
        g_pILog->WriteString(tprintf(T("[%d/%s] Connection opened (remote port %u)"),
            d->socket,
            addrText[0] != '\0' ? addrText : T("UNKNOWN"),
            static_cast<unsigned int>(resolvedPort)));
        ENDLOG;

        // GANL ACPT — suppressed (redundant with NET/CONN log).

        if (useTls) {
            // For TLS connections, defer finalization until the TLS handshake
            // completes and the GANL connection reaches Running state.
            // Sending welcome text before TLS is established causes it to
            // buffer in applicationOutput_ and never get flushed.
            adapter_.pending_finalizations_.push_back({handle, true});
        } else {
            // Defer finalization until first data arrives so we can
            // distinguish telnet from WebSocket before sending telnet
            // negotiation bytes (which would corrupt a WS handshake).
            //
            d->flags |= DS_NEED_PROTO;
        }

        return static_cast<ganl::SessionId>(handle);
    }

    void onDataReceived(ganl::SessionId sessionId, const std::string& data) override {
        ganl::ConnectionHandle handle = static_cast<ganl::ConnectionHandle>(sessionId);
        DESC* d = adapter_.get_desc(handle);
        if (!d) {
            return;
        }

        // GANL RECV — suppressed (per-packet noise).

        // Undo autodark
        //
        if (d->flags & DS_AUTODARK)
        {
            // Clear the DS_AUTODARK on every related session.
            //
            const auto range = g_dbref_to_descriptors_map.equal_range(d->player);
            for (auto it = range.first; it != range.second; ++it)
            {
                DESC* d1 = it->second;
                d1->flags &= ~DS_AUTODARK;
            }
            drv_s_Flags(d->player, FLAG_WORD1, drv_Flags(d->player, FLAG_WORD1) & ~DARK);
        }

        // WebSocket: if handshake is in progress, continue it.
        //
        if (d->flags & DS_WEBSOCKET_HS)
        {
            if (ws_process_handshake(d, data.data(), data.size()))
            {
                // Handshake complete (success or failure).
                //
                d->flags &= ~DS_WEBSOCKET_HS;
            }
            return;
        }

        // WebSocket: if upgraded, decode frames.
        //
        if (d->flags & DS_WEBSOCKET)
        {
            ws_process_input(d, data.data(), data.size());
            return;
        }

        // Protocol detection on first data.  We deferred telnet
        // initialization so IAC bytes don't corrupt a potential
        // WebSocket HTTP upgrade handshake (ws:// or wss://).
        //
        if (d->flags & DS_NEED_PROTO)
        {
            const bool isTls = (d->flags & DS_TLS) != 0;
            d->flags &= ~DS_NEED_PROTO;

            if (ws_is_upgrade_request(data.data(), data.size()))
            {
                d->ws = new ws_state();
                d->flags |= DS_WEBSOCKET_HS;
                if (ws_process_handshake(d, data.data(), data.size()))
                {
                    d->flags &= ~DS_WEBSOCKET_HS;
                }
                return;
            }

            // Not WebSocket — finalize as a telnet connection.
            //
            FinalizeGanlConnection(adapter_, d, isTls);
        }

        // Feed raw bytes through TinyMUX's existing NVT parser.
        // process_input_helper handles all telnet negotiation, charset
        // detection, and command queuing.
        process_input_helper(d, const_cast<char*>(data.data()),
                             static_cast<int>(data.size()));
    }

    void onConnectionClose(ganl::SessionId sessionId, ganl::DisconnectReason reason) override {
        ganl::ConnectionHandle handle = static_cast<ganl::ConnectionHandle>(sessionId);
        DESC* d = adapter_.get_desc(handle);
        if (!d) {
            //GANL_CONN_DEBUG(handle, "Close notification for unknown/closed session.");
            return;
        }

        // GANL CLOSE — suppressed (redundant with NET/DISC or NET/LOGO).

        const CLinearTimeAbsolute ltaNow = [&]() {
            CLinearTimeAbsolute tmp; tmp.GetUTC(); return tmp;
        }();

        const int mux_reason = MapGanlReasonToMux(reason);
        const int clamped_reason = ClampMuxReason(mux_reason);

        if (clamped_reason == R_LOGOUT)
        {
            if (d->player != NOTHING)
            {
                HandleConnectedDescriptorPreAnnounce(d, clamped_reason, ltaNow);
                announce_disconnect(d->player, d, disc_messages[clamped_reason]);
            }
            else
            {
                HandleUnconnectedDescriptorClose(d);
            }

            ResetDescriptorForLogout(d);
            return;
        }

        if (d->player != NOTHING) {
            HandleConnectedDescriptorPreAnnounce(d, clamped_reason, ltaNow);
            announce_disconnect(d->player, d, disc_messages[clamped_reason]);
        }
        else {
            HandleUnconnectedDescriptorClose(d);
        }

        // Cleanup TinyMUX resources associated with the DESC.
        // Skip process_output during destructor-driven cleanup
        // (ServerShutdown) — the socket is already closed, and calling
        // send_data here acquires a shared_ptr to the connection being
        // destroyed, causing a re-entrant destructor → pure virtual call.
        if (reason != ganl::DisconnectReason::ServerShutdown) {
            process_output(d, false);
        }
        clearstrings(d);
        freeqs(d);

        // Free WebSocket state if allocated.
        //
        if (d->ws)
        {
            delete d->ws;
            d->ws = nullptr;
        }

        if (d->flags & DS_CONNECTED)
        {
            d->flags &= ~DS_CONNECTED;
        }

        if (d->program_data != nullptr)
        {
            int num = 0;
            const auto range = g_dbref_to_descriptors_map.equal_range(d->player);
            for (auto it = range.first; it != range.second; ++it)
            {
                num++;
            }

            if (num == 0)
            {
                for (auto& wait_reg : d->program_data->wait_regs)
                {
                    if (wait_reg)
                    {
                        RegRelease(wait_reg);
                        wait_reg = nullptr;
                    }
                }
                NamedRegsClear(d->program_data->named_wait_regs);
                MEMFREE(d->program_data);
                atr_clr(d->player, A_PROGCMD);
            }
            d->program_data = nullptr;
        }

        drv_CancelTask(Task_ProcessCommand, d, 0);

        if (d->player != NOTHING)
        {
            const dbref player = d->player;
            const auto range = g_dbref_to_descriptors_map.equal_range(player);
            for (auto itPlayer = range.first; itPlayer != range.second; ++itPlayer)
            {
                if (itPlayer->second == d)
                {
                    g_dbref_to_descriptors_map.erase(itPlayer);
                    break;
                }
            }
        }

        auto mapIt = g_descriptors_map.find(d);
        if (mapIt != g_descriptors_map.end()) {
            g_descriptors_list.erase(mapIt->second);
            g_descriptors_map.erase(mapIt);
        }

        // Remove mapping FIRST
        adapter_.remove_mapping(d);

        // Free queues, then destroy non-trivial members, then free the DESC.
        freeqs(d);
        adapter_.free_desc2(d);

        clearSessionError(sessionId);
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
        if (message.empty()) {
            return true;
        }

        const ganl::ConnectionHandle exceptHandle = (except != ganl::InvalidSessionId)
            ? static_cast<ganl::ConnectionHandle>(except)
            : ganl::InvalidConnectionHandle;

        bool sentAny = false;
        for (auto it = g_descriptors_list.begin(); it != g_descriptors_list.end(); ++it) {
            DESC* target = *it;
            if (!target) {
                continue;
            }

            const ganl::ConnectionHandle handle = adapter_.get_handle(target);
            if (exceptHandle != ganl::InvalidConnectionHandle && handle == exceptHandle) {
                continue;
            }

            adapter_.send_data(target, message.c_str(), message.size());
            sentAny = true;
        }
        return sentAny;
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
        UNUSED_PARAMETER(sessionId);

        DESC* d = adapter_.get_desc(connHandle);
        if (!d) {
            return false;
        }

        UTF8* user = alloc_lbuf("ganl_auth.user");
        UTF8* pass = alloc_lbuf("ganl_auth.pass");
        if (!user || !pass) {
            if (user) {
                free_lbuf(user);
            }
            if (pass) {
                free_lbuf(pass);
            }
            return false;
        }

        mux_strncpy(user, reinterpret_cast<const UTF8*>(username.c_str()), LBUF_SIZE - 1);
        mux_strncpy(pass, reinterpret_cast<const UTF8*>(password.c_str()), LBUF_SIZE - 1);

        auto cleanupBuffers = [&]() {
            if (user) {
                free_lbuf(user);
                user = nullptr;
            }
            if (pass) {
                free_lbuf(pass);
                pass = nullptr;
            }
        };

        auto logReject = [&](const UTF8* logcode, const UTF8* logtype, const UTF8* logreason, dbref playerRef) {
            STARTLOG(LOG_LOGIN | LOG_SECURITY, logcode, T("RJCT"));
            UTF8* buff = alloc_mbuf("ganl_auth.reject");
            mux_sprintf(buff, MBUF_SIZE, T("[%u/%s] %s rejected to "), d->socket, d->addr, logtype);
            g_pILog->log_text(buff);
            free_mbuf(buff);
            if (playerRef != NOTHING) {
                g_pILog->log_name(playerRef);
            } else {
                g_pILog->log_text(user);
            }
            g_pILog->log_text(T(" ("));
            g_pILog->log_text(logreason);
            g_pILog->log_text(T(")"));
            ENDLOG;
        };

        auto rejectWithMessage = [&](const UTF8* logcode, const UTF8* logtype, const UTF8* logreason,
                                     dbref playerRef, int filecache, const UTF8* motd,
                                     ganl::DisconnectReason reason) {
            logReject(logcode, logtype, logreason, playerRef);
            fcache_dump(d, filecache);
            if (motd && *motd) {
                queue_string(d, motd);
                queue_write_LEN(d, T("\r\n"), 2);
            }
            cleanupBuffers();
            adapter_.close_connection(d, reason);
            return false;
        };

        const int hostInfo = g_access_list.check(&d->address);

        bool isGuestConnect = false;

        if (string_prefix(user, g_dc.guest_prefix)) {
            if (hostInfo & HI_NOGUEST) {
                return rejectWithMessage(T("CONN"), T("Connect"), T("Guest Site Forbidden"), NOTHING,
                    FC_CONN_REG, g_dc.downmotd_msg, ganl::DisconnectReason::ServerShutdown);
            }

            if (g_dc.control_flags & CF_LOGIN) {
                if (g_dc.number_guests <= 0 || !drv_Good_obj(g_dc.guest_char) || !(g_dc.control_flags & CF_GUEST)) {
                    queue_write(d, T("Guest logins are disabled.\r\n"));
                    cleanupBuffers();
                    return false;
                }

                const UTF8* guestUser = drv_CreateGuest(d);
                if (!guestUser) {
                    cleanupBuffers();
                    return false;
                }

                mux_strncpy(user, guestUser, LBUF_SIZE - 1);
                mux_strncpy(pass, reinterpret_cast<const UTF8*>(GUEST_PASSWORD), LBUF_SIZE - 1);
                isGuestConnect = true;
            }
        }

        int nplayers;
        if (g_dc.max_players < 0) {
            nplayers = g_dc.max_players - 1;
        } else {
            nplayers = 0;
            for (auto it = g_descriptors_list.begin(); it != g_descriptors_list.end(); ++it) {
                DESC* d2 = *it;
                if (d2->flags & DS_CONNECTED) {
                    nplayers++;
                }
            }
        }

        UTF8 hostAddress[MBUF_SIZE];
        hostAddress[0] = '\0';
        const struct sockaddr* sa = d->address.saro();
        if (sa != nullptr && sa->sa_family != 0) {
            d->address.ntop(hostAddress, sizeof(hostAddress));
        } else if (d->addr[0] != '\0') {
            mux_strncpy(hostAddress, reinterpret_cast<const UTF8*>(d->addr), sizeof(hostAddress) - 1);
        } else {
            std::string remote = adapter_.get_engine()->getRemoteAddress(connHandle);
            mux_strncpy(hostAddress, reinterpret_cast<const UTF8*>(remote.c_str()), sizeof(hostAddress) - 1);
        }

        dbref player = NOTHING;
        g_pIPlayerSession->ConnectPlayer(user, pass, d->addr, d->username, hostAddress, &player);
        if (player == NOTHING || (!isGuestConnect && drv_CheckGuest(player))) {
            queue_write(d, connect_fail);
            STARTLOG(LOG_LOGIN | LOG_SECURITY, "CON", "BAD");
            UTF8* buff = alloc_lbuf("ganl_auth.badconnect");
            mux_sprintf(buff, LBUF_SIZE, T("[%u/%s] Failed connect to \xE2\x80\x98%s\xE2\x80\x99"), d->socket, d->addr, user);
            g_pILog->log_text(buff);
            free_lbuf(buff);
            ENDLOG;

            if (--(d->retries_left) <= 0) {
                cleanupBuffers();
                adapter_.close_connection(d, ganl::DisconnectReason::LoginFailed);
            } else {
                cleanupBuffers();
            }
            return false;
        }

        const bool loginsEnabled = (g_dc.control_flags & CF_LOGIN) != 0;
        const bool belowCap = (g_dc.max_players < 0) || (nplayers < g_dc.max_players);
        const bool privileged = drv_WizRoy(player) || God(player);

        if (!( (loginsEnabled && belowCap) || privileged )) {
            if (!loginsEnabled) {
                return rejectWithMessage(T("CON"), T("Connect"), T("Logins Disabled"), player,
                    FC_CONN_DOWN, g_dc.downmotd_msg, ganl::DisconnectReason::ServerShutdown);
            }
            return rejectWithMessage(T("CON"), T("Connect"), T("Game Full"), player,
                FC_CONN_FULL, g_dc.fullmotd_msg, ganl::DisconnectReason::GameFull);
        }

        if ((drv_Powers(player) & POW_GUEST) && (hostInfo & HI_NOGUEST)) {
            return rejectWithMessage(T("CON"), T("Connect"), T("Guest Site Forbidden"), player,
                FC_CONN_SITE, g_dc.downmotd_msg, ganl::DisconnectReason::ServerShutdown);
        }

        STARTLOG(LOG_LOGIN, "CON", "LOGIN");
        UTF8* loginBuff = alloc_mbuf("ganl_auth.login");
        mux_sprintf(loginBuff, MBUF_SIZE, T("[%u/%s] Connected to "), d->socket, d->addr);
        g_pILog->log_text(loginBuff);
        g_pILog->log_name_and_loc(player);
        free_mbuf(loginBuff);
        ENDLOG;

        d->flags |= DS_CONNECTED;
        d->connected_at.GetUTC();
        d->player = player;

        const auto range = g_dbref_to_descriptors_map.equal_range(player);
        for (auto it = range.first; it != range.second; ++it) {
            DESC* d2 = it->second;
            if (d2->program_data != nullptr && d->program_data == nullptr) {
                d->program_data = d2->program_data;
            } else if (d2->program_data != nullptr) {
                mux_assert(d->program_data == d2->program_data);
            }
        }

        ganl_associate_player(d, player);

        {
            desc_addhash(d);
            int numConnections = count_player_descs(player);
            bool isPueblo = (d->flags & DS_PUEBLOCLIENT) != 0;
            bool isSusp = g_access_list.isSuspect(&d->address);
            int timeout = g_dc.idle_timeout;
            g_pIPlayerSession->AnnounceConnect(player, numConnections,
                isPueblo, isSusp, d->addr, d->username, hostAddress,
                &timeout, &d->connlog_id);
            d->timeout = timeout;
        }

        if (nullptr != d->program_data) {
            queue_write_LEN(d, T(">\377\371"), 3);
        }

        cleanupBuffers();
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
        // return !g_access_list.isForbid(&netAddr);
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

    std::string getLastSessionErrorString(ganl::SessionId sessionId) override {
        auto it = session_errors_.find(sessionId);
        return (it != session_errors_.end()) ? it->second : std::string();
    }
};


// RawPassthroughHandler: Passes raw decrypted bytes through without any
// telnet parsing. TinyMUX's process_input_helper() handles all telnet
// negotiation, charset detection, and MUD-specific quirks.
//
class RawPassthroughHandler : public ganl::ProtocolHandler {
public:
    RawPassthroughHandler() = default;
    ~RawPassthroughHandler() override = default;

    bool createProtocolContext(ganl::ConnectionHandle) override { return true; }
    void destroyProtocolContext(ganl::ConnectionHandle) override {}

    bool processInput(ganl::ConnectionHandle conn, ganl::IoBuffer& in,
        ganl::IoBuffer& app_out, ganl::IoBuffer& telnet_out,
        bool consumeInput = true) override
    {
        UNUSED_PARAMETER(conn);
        UNUSED_PARAMETER(telnet_out);

        // Move all bytes directly to app_out without telnet parsing.
        size_t avail = in.readableBytes();
        if (avail > 0) {
            app_out.append(in.readPtr(), avail);
            if (consumeInput) {
                in.consumeRead(avail);
            }
        }
        return true;
    }

    bool formatOutput(ganl::ConnectionHandle conn, ganl::IoBuffer& in,
        ganl::IoBuffer& out, bool consumeInput = true) override
    {
        UNUSED_PARAMETER(conn);

        // Pass output through without modification.
        // TinyMUX already handles color conversion in queue_string() ->
        // convert_color() -> queue_write_LEN() before output reaches here.
        size_t avail = in.readableBytes();
        if (avail > 0) {
            out.append(in.readPtr(), avail);
            if (consumeInput) {
                in.consumeRead(avail);
            }
        }
        return true;
    }

    // Negotiation is a no-op. TinyMUX sends its own IAC sequences via
    // the output queue through process_input_helper().
    void startNegotiation(ganl::ConnectionHandle, ganl::IoBuffer&) override {}
    ganl::NegotiationStatus getNegotiationStatus(ganl::ConnectionHandle) override
    {
        return ganl::NegotiationStatus::Completed;
    }
    bool consumeStateChanges(ganl::ConnectionHandle,
        ganl::ProtocolState&, ganl::ProtocolStateChangeFlags&) override
    {
        return false;
    }

    bool setEncoding(ganl::ConnectionHandle, ganl::EncodingType) override { return true; }
    ganl::EncodingType getEncoding(ganl::ConnectionHandle) override
    {
        return ganl::EncodingType::Utf8;
    }
    ganl::ProtocolState getProtocolState(ganl::ConnectionHandle) override
    {
        return ganl::ProtocolState{};
    }
    void updateWidth(ganl::ConnectionHandle, uint16_t) override {}
    void updateHeight(ganl::ConnectionHandle, uint16_t) override {}
    std::string getLastProtocolErrorString(ganl::ConnectionHandle) override { return ""; }
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
    if (initialized_) return true;

    // Register GANL logging callback so the library can write to the game log.
    ganl::setLogger([](const char* msg) {
        g_pILog->WriteString(tprintf(T("GANL %s" ENDLINE), msg));
        g_pILog->Flush();
    });

    g_pILog->WriteString(T("Initializing GANL Adapter...\n"));

    // 1. Create Network Engine
    networkEngine_ = ganl::NetworkEngineFactory::createEngine();
    if (!networkEngine_) {
        g_pILog->WriteString(T("FATAL: Failed to create GANL network engine.\n"));
        return false;
    }
    g_pILog->WriteString(tprintf(T("Using GANL Network Engine: %d\n"), networkEngine_->getIoModelType()));


    // 2. Initialize Network Engine
    if (!networkEngine_->initialize()) {
        g_pILog->WriteString(T("FATAL: Failed to initialize GANL network engine.\n"));
        networkEngine_.reset(); // Release the failed engine
        return false;
    }

    // 3. Create Secure Transport (TLS/SSL)
    // The factory selects the platform backend (Schannel/OpenSSL) automatically.
    secureTransport_ = ganl::SecureTransportFactory::createTransport();
    if (secureTransport_) {
        ganl::TlsConfig tlsConfig;
        tlsConfig.certificateFile = reinterpret_cast<const char *>(g_dc.ssl_certificate_file); // Assuming UTF8 is compatible
        tlsConfig.keyFile = reinterpret_cast<const char *>(g_dc.ssl_certificate_key);
        tlsConfig.password = reinterpret_cast<const char *>(g_dc.ssl_certificate_password);
        // tlsConfig.verifyPeer = false; // Default

        if (!secureTransport_->initialize(tlsConfig)) {
            g_pILog->WriteString(tprintf(T("Warning: Failed to initialize GANL secure transport: %s\n"),
                secureTransport_->getLastTlsErrorString(0).c_str()));
            secureTransport_.reset(); // Don't use TLS if init failed
        }
        else {
            g_pILog->WriteString(T("GANL Secure Transport initialized.\n"));
        }
    }
    else {
        g_pILog->WriteString(T("No GANL Secure Transport available or created.\n"));
    }

    // 4. Create Protocol Handler (raw passthrough — TinyMUX handles telnet)
    protocolHandler_ = std::make_unique<RawPassthroughHandler>();

    // 5. Create Session Manager
    sessionManager_ = std::make_unique<GanlTinyMuxSessionManager>(*this);
    if (!sessionManager_->initialize()) {
        g_pILog->WriteString(T("FATAL: Failed to initialize GANL session manager.\n"));
        // Need proper cleanup
        networkEngine_->shutdown();
        networkEngine_.reset();
        if (secureTransport_) secureTransport_->shutdown();
        secureTransport_.reset();
        protocolHandler_.reset();
        sessionManager_.reset();
        return false;
    }

    // 6. Set up listeners and connections — branching on restart vs. fresh start.
    ganl::ErrorCode error = 0;

#if defined(HAVE_WORKING_FORK)
    if (g_restarting) {
        // --- Restart path: adopt surviving fds from before exec ---
        restarting_ = true;

        // Adopt listener fds from main_game_ports[] (populated by load_restart_db).
        for (int i = 0; i < num_main_game_ports; ++i) {
            int fd = static_cast<int>(main_game_ports[i].socket);
            if (fd < 0) {
                continue;
            }

            bool isSsl = false;
#ifdef UNIX_SSL
            isSsl = main_game_ports[i].fSSL;
#endif
            int port = main_game_ports[i].msa.port();

            ganl::ListenerHandle handle = networkEngine_->adoptListener(fd, error);
            if (handle == ganl::InvalidListenerHandle) {
                g_pILog->WriteString(tprintf(T("GANL: Failed to adopt listener fd %d for port %d: %s\n"),
                    fd, port, networkEngine_->getErrorString(error).c_str()));
                continue;
            }

            listener_contexts_[handle] = { port, isSsl };
            if (!networkEngine_->startListening(handle, &listener_contexts_[handle], error)) {
                g_pILog->WriteString(tprintf(T("GANL: Failed to start adopted listener fd %d for port %d: %s\n"),
                    fd, port, networkEngine_->getErrorString(error).c_str()));
                networkEngine_->closeListener(handle);
                continue;
            }

            if (isSsl) {
                ssl_port_listeners_[port] = handle;
            } else {
                port_listeners_[port] = handle;
            }

            g_pILog->WriteString(tprintf(T("GANL: Adopted listener fd %d for %sport %d\n"),
                fd, isSsl ? "SSL " : "", port));
        }

        // Adopt surviving connection fds from the descriptor list
        // (loaded by load_restart_db into g_descriptors_list).
        for (DESC* d : g_descriptors_list) {
            if (!d || d->socket < 0) {
                continue;
            }

            ganl::ConnectionHandle connHandle = networkEngine_->adoptConnection(
                d->socket, nullptr, error);
            if (connHandle == ganl::InvalidConnectionHandle) {
                g_pILog->WriteString(tprintf(T("GANL: Failed to adopt connection fd %d: %s\n"),
                    d->socket, networkEngine_->getErrorString(error).c_str()));
                continue;
            }

            auto conn = ganl::ConnectionFactory::createConnection(
                connHandle,
                *networkEngine_,
                nullptr,  // No TLS for surviving connections
                *protocolHandler_,
                *sessionManager_);

            if (!conn) {
                g_pILog->WriteString(tprintf(T("GANL: Failed to create ConnectionBase for adopted fd %d\n"),
                    d->socket));
                networkEngine_->closeConnection(connHandle);
                continue;
            }

            {
                handle_to_conn_[connHandle] = conn;
            }

            // initialize(false) triggers onConnectionOpen via the restart path
            if (!conn->initialize(false)) {
                g_pILog->WriteString(tprintf(T("GANL: Connection initialize failed for adopted fd %d\n"),
                    d->socket));
                handle_to_conn_.erase(connHandle);
                continue;
            }

            g_pILog->WriteString(tprintf(T("GANL: Adopted connection fd %d (player %d)\n"),
                d->socket, d->player));
        }

        restarting_ = false;
    } else
#endif // HAVE_WORKING_FORK
    {
        // --- Normal (fresh) start path ---
        for (int i = 0; i < g_dc.nPorts; ++i) {
            int port = g_dc.ports[i];
            std::string host = (g_dc.ip_address[0] != '\0') ? reinterpret_cast<const char *>(g_dc.ip_address) : "";

            ganl::ListenerHandle handle = networkEngine_->createListener(host, port, error);
            if (handle != ganl::InvalidListenerHandle) {
                listener_contexts_[handle] = { port, false };
                if (networkEngine_->startListening(handle, &listener_contexts_[handle], error)) {
                    port_listeners_[port] = handle;
                    g_pILog->WriteString(tprintf(T("GANL listening on %s:%d (Handle: %llu)\n"),
                        host.empty() ? "*" : host.c_str(), port, (unsigned long long)handle));
                }
                else {
                    g_pILog->WriteString(tprintf(T("GANL failed to start listening on port %d: %s\n"),
                        port, networkEngine_->getErrorString(error).c_str()));
                    networkEngine_->closeListener(handle);
                }
            }
            else {
                g_pILog->WriteString(tprintf(T("GANL failed to create listener for port %d: %s\n"),
                    port, networkEngine_->getErrorString(error).c_str()));
            }
        }

        // Create SSL Listeners
        if (secureTransport_) {
            for (int i = 0; i < g_dc.nSslPorts; ++i) {
                int port = g_dc.sslPorts[i];
                std::string host = (g_dc.ip_address[0] != '\0') ? reinterpret_cast<const char *>(g_dc.ip_address) : "";

                ganl::ListenerHandle handle = networkEngine_->createListener(host, port, error);
                if (handle != ganl::InvalidListenerHandle) {
                    listener_contexts_[handle] = { port, true };
                    if (networkEngine_->startListening(handle, &listener_contexts_[handle], error)) {
                        ssl_port_listeners_[port] = handle;
                        g_pILog->WriteString(tprintf(T("GANL listening with SSL on %s:%d (Handle: %llu)\n"),
                            host.empty() ? "*" : host.c_str(), port, (unsigned long long)handle));
                    }
                    else {
                        g_pILog->WriteString(tprintf(T("GANL failed to start SSL listening on port %d: %s\n"),
                            port, networkEngine_->getErrorString(error).c_str()));
                        networkEngine_->closeListener(handle);
                    }
                }
                else {
                    g_pILog->WriteString(tprintf(T("GANL failed to create SSL listener for port %d: %s\n"),
                        port, networkEngine_->getErrorString(error).c_str()));
                }
            }
        }
    }

    if (port_listeners_.empty() && ssl_port_listeners_.empty()) {
        g_pILog->WriteString(T("Warning: No GANL listeners successfully started.\n"));
    }

    initialized_ = true;
    g_pILog->WriteString(T("GANL Adapter initialized.\n"));
    return true;
}

void GanlAdapter::shutdown() {
    if (!initialized_) return;
    initialized_ = false; // Mark as shutting down immediately

    g_pILog->WriteString(T("Shutting down GANL Adapter...\n"));

    shutdown_dns_slave();
    shutdown_email_channel();

    // Close listeners first
    std::vector<ganl::ListenerHandle> plainHandles, sslHandles;
    plainHandles.reserve(port_listeners_.size());
    for (const auto& pair : port_listeners_) plainHandles.push_back(pair.second);
    sslHandles.reserve(ssl_port_listeners_.size());
    for (const auto& pair : ssl_port_listeners_) sslHandles.push_back(pair.second);

    for (ganl::ListenerHandle handle : plainHandles) networkEngine_->closeListener(handle);
    for (ganl::ListenerHandle handle : sslHandles) networkEngine_->closeListener(handle);

    // Send farewell message to all connected clients.
    for (auto it = g_descriptors_list.begin();
         it != g_descriptors_list.end(); ++it)
    {
        DESC* d = *it;
        if (d) {
            queue_string(d, T("Going down - Bye"));
            queue_write_LEN(d, T("\r\n"), 2);
            process_output(d, false);
        }
    }

    // process_output buffers data in GANL's encryptedOutput_ and registers
    // write interest via postWrite(). We must process events so the network
    // engine actually flushes the farewell message to the wire.
    {
        constexpr int MAX_EVENTS = 64;
        ganl::IoEvent events[MAX_EVENTS];
        int num = networkEngine_->processEvents(100, events, MAX_EVENTS);
        for (int i = 0; i < num; ++i) {
            if (events[i].connection != ganl::InvalidConnectionHandle) {
                auto it = handle_to_conn_.find(events[i].connection);
                if (it != handle_to_conn_.end() && it->second) {
                    it->second->handleNetworkEvent(events[i]);
                }
            }
        }
    }

    // Close all connections before destroying the network engine.
    // We must close them explicitly so their destructors don't try to access
    // a destroyed engine.
    {
        std::vector<std::shared_ptr<ganl::ConnectionBase>> connsToClose;
        connsToClose.reserve(handle_to_conn_.size());
        for (auto& pair : handle_to_conn_) {
            connsToClose.push_back(pair.second);
        }
        handle_to_conn_.clear();
        for (auto& conn : connsToClose) {
            if (conn && conn->getState() != ganl::ConnectionState::Closed) {
                conn->close(ganl::DisconnectReason::ServerShutdown);
            }
        }
        connsToClose.clear();
    }

    if (sessionManager_) {
        sessionManager_->shutdown();
        sessionManager_.reset();
    }

    if (protocolHandler_) {
        protocolHandler_.reset();
    }

    if (secureTransport_) {
        secureTransport_->shutdown();
        secureTransport_.reset();
    }

    // Shutdown Network Engine LAST
    if (networkEngine_) {
        networkEngine_->shutdown();
        networkEngine_.reset();
    }

    port_listeners_.clear();
    ssl_port_listeners_.clear();
    handle_to_desc_.clear();
    desc_to_handle_.clear();
    listener_contexts_.clear();

    g_pILog->WriteString(T("GANL Adapter shut down.\n"));
}

void GanlAdapter::prepare_for_restart() {
    g_pILog->WriteString(T("GANL: Preparing for @restart...\n"));

    // 1. Populate main_game_ports[] from listener handles so dump_restart_db()
    //    can serialize them.  Listener handles ARE the file descriptors.
    num_main_game_ports = 0;
    for (const auto& pair : port_listeners_) {
        if (num_main_game_ports >= MAX_LISTEN_PORTS
#ifdef UNIX_SSL
            * 2
#endif
           ) {
            break;
        }
        int idx = num_main_game_ports++;
        main_game_ports[idx].socket = static_cast<SOCKET>(pair.second);
#ifdef UNIX_SSL
        main_game_ports[idx].fSSL = false;
#endif
        socklen_t n = main_game_ports[idx].msa.maxaddrlen();
        getsockname(static_cast<int>(pair.second),
                     main_game_ports[idx].msa.sa(), &n);
    }
    for (const auto& pair : ssl_port_listeners_) {
        if (num_main_game_ports >= MAX_LISTEN_PORTS
#ifdef UNIX_SSL
            * 2
#endif
           ) {
            break;
        }
        int idx = num_main_game_ports++;
        main_game_ports[idx].socket = static_cast<SOCKET>(pair.second);
#ifdef UNIX_SSL
        main_game_ports[idx].fSSL = true;
#endif
        socklen_t n = main_game_ports[idx].msa.maxaddrlen();
        getsockname(static_cast<int>(pair.second),
                     main_game_ports[idx].msa.sa(), &n);
    }

    // 2. Close TLS connections — their in-process session state cannot
    //    survive exec.  Non-TLS connections are kept open.
#ifdef UNIX_SSL
    {
        std::vector<DESC*> tls_descs;
        for (DESC* d : g_descriptors_list) {
            if (d && d->ss != SocketState::Accepted) {
                tls_descs.push_back(d);
            }
        }
        for (DESC* d : tls_descs) {
            close_connection(d, ganl::DisconnectReason::ServerShutdown);
        }
    }
#endif

    // 3. Flush output for remaining non-TLS connections.
    for (DESC* d : g_descriptors_list) {
        if (d) {
            process_output(d, false);
        }
    }
    {
        constexpr int MAX_EVENTS = 64;
        ganl::IoEvent events[MAX_EVENTS];
        int num = networkEngine_->processEvents(100, events, MAX_EVENTS);
        for (int i = 0; i < num; ++i) {
            if (events[i].connection != ganl::InvalidConnectionHandle) {
                auto it = handle_to_conn_.find(events[i].connection);
                if (it != handle_to_conn_.end() && it->second) {
                    it->second->handleNetworkEvent(events[i]);
                }
            }
        }
    }

    // 4. Clear desc mappings so Connection destructors won't trigger
    //    onConnectionClose cleanup for surviving connections.
    handle_to_desc_.clear();
    desc_to_handle_.clear();

    // 5. Detach all remaining connection fds from the engine.
    {
        std::vector<ganl::ConnectionHandle> connHandles;
        connHandles.reserve(handle_to_conn_.size());
        for (const auto& pair : handle_to_conn_) {
            connHandles.push_back(pair.first);
        }
        for (ganl::ConnectionHandle h : connHandles) {
            networkEngine_->detachConnection(h);
        }
    }

    // 6. Detach all listener fds from the engine.
    for (const auto& pair : port_listeners_) {
        networkEngine_->detachListener(pair.second);
    }
    for (const auto& pair : ssl_port_listeners_) {
        networkEngine_->detachListener(pair.second);
    }

    // 7. Clear handle_to_conn_ — Connection objects destruct safely
    //    (closeConnection = no-op since fd already detached,
    //     onConnectionClose = no-op since desc mappings cleared).
    handle_to_conn_.clear();

    // 8. Shut down engine, transport, session manager, protocol handler.
    shutdown_dns_slave();
    shutdown_email_channel();
    if (sessionManager_) {
        sessionManager_->shutdown();
        sessionManager_.reset();
    }
    if (protocolHandler_) {
        protocolHandler_.reset();
    }
    if (secureTransport_) {
        secureTransport_->shutdown();
        secureTransport_.reset();
    }
    if (networkEngine_) {
        networkEngine_->shutdown();
        networkEngine_.reset();
    }

    port_listeners_.clear();
    ssl_port_listeners_.clear();
    listener_contexts_.clear();
    connection_listener_map_.clear();
    pending_remote_addresses_.clear();
    pending_tls_flags_.clear();
    pending_finalizations_.clear();
    initialized_ = false;

    g_pILog->WriteString(T("GANL: Ready for exec.\n"));
}

void GanlAdapter::run_main_loop() {
    g_pILog->WriteString(T("GANL: Entering main loop.\n"));
    g_pILog->Flush();

#if defined(_WIN32)
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#endif

    if (g_dc.use_hostname) {
        start_dns_slave();
    }

    ltaLastSlice_.GetUTC();

    // Calculate available descriptor limit, reserving 7 for system use
    // (logging, slave pipes, etc.) — same as legacy shovechars().
    //
    unsigned int avail_descriptors;
#if defined(_WIN32)
    avail_descriptors = FD_SETSIZE - 7;
#elif defined(HAVE_GETDTABLESIZE)
    avail_descriptors = getdtablesize() - 7;
#else
    avail_descriptors = sysconf(_SC_OPEN_MAX) - 7;
#endif

    while (!g_shutdown_flag) {
        int timeout_ms = 100; // Default timeout for processEvents

        // Calculate minimum timeout based on scheduled tasks
        CLinearTimeAbsolute ltaNextTask;
        if (MUX_SUCCEEDED(g_pIGameEngine->WhenNext(&ltaNextTask))) {
            CLinearTimeAbsolute ltaNow;
            ltaNow.GetUTC();
            if (ltaNextTask > ltaNow) {
                CLinearTimeDelta ltdToNext = ltaNextTask - ltaNow;
                timeout_ms = ltdToNext.ReturnMilliseconds();
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
            g_pILog->WriteString(T("GANL: Network engine processEvents error. Shutting down.\n"));
            g_shutdown_flag = true;
            break;
        }

        // Dispatch network events to Connection objects
        for (int i = 0; i < num_events; ++i) {
            if (dns_slave_ && (events[i].connection == dns_slave_->handle || events[i].context == dns_slave_.get())) {
                handle_dns_slave_event(events[i]);
                continue;
            }

            if (email_channel_ && (events[i].connection == email_channel_->handle || events[i].context == email_channel_.get())) {
                handle_email_channel_event(events[i]);
                continue;
            }

            if (events[i].type == ganl::IoEventType::Accept) {
                ganl::ConnectionHandle connHandle = events[i].connection;
                if (connHandle != ganl::InvalidConnectionHandle) {

                    // Reject if we've exhausted available descriptors.
                    //
                    if (g_descriptors_list.size() >= avail_descriptors)
                    {
                        STARTLOG(LOG_NET, "NET", "FULL");
                        g_pILog->WriteString(tprintf(T("%.90s"), T("Descriptor limit reached, rejecting connection.")));
                        ENDLOG;
                        networkEngine_->closeConnection(connHandle);
                        continue;
                    }

                    ListenerContext listenerCtx{0, false};
                    bool useTls = false;
                    if (events[i].context) {
                        auto* ctx = static_cast<ListenerContext*>(events[i].context);
                        listenerCtx = *ctx;
                        useTls = ctx->is_ssl;
                    }

                    std::shared_ptr<ganl::ConnectionBase> conn = ganl::ConnectionFactory::createConnection(
                        connHandle,
                        *networkEngine_,
                        secureTransport_.get(),
                        *protocolHandler_,
                        *sessionManager_);

                    if (!conn) {
                        g_pILog->WriteString(tprintf(T("GANL: Failed to allocate ConnectionBase for handle %llu\n"),
                            static_cast<unsigned long long>(connHandle)));
                        networkEngine_->closeConnection(connHandle);
                        continue;
                    }

                    connection_listener_map_[connHandle] = listenerCtx;
                    pending_remote_addresses_[connHandle] = events[i].remoteAddress;
                    pending_tls_flags_[connHandle] = useTls;
                    handle_to_conn_[connHandle] = conn;

                    if (!conn->initialize(useTls)) {
                        handle_to_conn_.erase(connHandle);
                        connection_listener_map_.erase(connHandle);
                        pending_remote_addresses_.erase(connHandle);
                        pending_tls_flags_.erase(connHandle);
                    }
                }
                continue;
            }
            else if (events[i].connection != ganl::InvalidConnectionHandle) {
                std::shared_ptr<ganl::ConnectionBase> conn = nullptr;
                auto it = handle_to_conn_.find(events[i].connection);
                if (it != handle_to_conn_.end()) {
                    conn = it->second; // Get the shared_ptr
                }
                if (conn) {
                    // Event context should be the ConnectionBase* itself
                    if (events[i].context == conn.get()) {
                        conn->handleNetworkEvent(events[i]);
                    }
                    else {
                        g_pILog->WriteString(tprintf(T("GANL: Mismatched context for event on handle %llu\n"),
                            static_cast<unsigned long long>(events[i].connection)));
                    }
                }
                else {
                    //GANL_CONN_DEBUG(events[i].connection, "Warning: Event received for untracked/closed connection.");
                }
            }
            else if (events[i].listener != ganl::InvalidListenerHandle
                  && events[i].type == ganl::IoEventType::Error)
            {
                // A listener experienced an error (e.g., failed accept).
                // The network engines re-arm the listener internally, so
                // we just log the event for diagnostics.
                //
                int port = 0;
                bool isSsl = false;
                auto ctxIt = listener_contexts_.find(events[i].listener);
                if (ctxIt != listener_contexts_.end()) {
                    port = ctxIt->second.port;
                    isSsl = ctxIt->second.is_ssl;
                }

                STARTLOG(LOG_NET, "NET", "LERR");
                g_pILog->WriteString(tprintf(T("Listener error on %sport %d (handle %llu): error %d"),
                    isSsl ? T("SSL ") : T(""),
                    port,
                    static_cast<unsigned long long>(events[i].listener),
                    events[i].error));
                ENDLOG;
            }
        }

        // If SIGCHLD recorded a dump child exit, report it to the
        // engine now (safe context, not a signal handler).
        //
        pid_t dump_pid = g_dump_child_pid;
        if (0 != dump_pid)
        {
            g_dump_child_pid = 0;
            g_pIGameEngine->DumpChildExited(dump_pid);
        }

        // Process TinyMUX Tasks (Timers, Idle, Quotas, etc.)
        process_tinyMUX_tasks();

    } // end while (!g_shutdown_flag)

#if defined(_WIN32)
    SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
#endif

    g_pILog->WriteString(T("GANL: Exiting main loop.\n"));
}

// Helper to run periodic TinyMUX tasks (quotas, scheduler, output flush).
void GanlAdapter::process_tinyMUX_tasks() {
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();

    // Update command quotas (same as shovechars timeslice logic).
    g_pIGameEngine->UpdateQuotas(ltaLastSlice_, ltaNow);
    ltaLastSlice_ = ltaNow;

    // Finalize TLS connections that have completed their handshake.
    // We deferred welcome_user() until the connection reaches Running state
    // so that output doesn't get stuck in GANL's applicationOutput_ buffer.
    if (!pending_finalizations_.empty()) {
        std::vector<PendingFinalization> ready;
        auto it = pending_finalizations_.begin();
        while (it != pending_finalizations_.end()) {
            auto connIt = handle_to_conn_.find(it->handle);
            if (connIt != handle_to_conn_.end() && connIt->second &&
                connIt->second->getState() == ganl::ConnectionState::Running) {
                ready.push_back(*it);
                it = pending_finalizations_.erase(it);
            } else if (connIt == handle_to_conn_.end()) {
                // Connection was closed before TLS completed
                it = pending_finalizations_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& pf : ready) {
            DESC* d = get_desc(pf.handle);
            if (d) {
                // Defer protocol detection to onDataReceived so that
                // telnet negotiation bytes don't corrupt a wss:// handshake.
                //
                d->flags |= DS_NEED_PROTO;
                if (pf.isTls)
                {
                    d->flags |= DS_TLS;
                }
            }
        }
    }

    // Run scheduled tasks (timers, idle checks, @daily, DB checkpoints).
    g_pIGameEngine->RunTasks(ltaNow);

#if defined(_WIN32)
    // Collect completed reverse DNS lookups from worker threads.
    drain_dns_results();
#endif

    // Flush pending output so queued text reaches clients promptly.
    for (auto it = g_descriptors_list.begin(); it != g_descriptors_list.end(); ++it)
    {
        DESC* d = *it;
        if (d && !d->output_queue.empty())
        {
            process_output(d, false);
        }
    }

    g_pILog->Flush();
}


bool GanlAdapter::start_dns_slave() {
#if defined(_WIN32)
    if (!g_dc.use_hostname) {
        return false;
    }

    std::lock_guard<std::mutex> lock(dnsMutex_);
    if (!dnsThreads_.empty()) {
        return true; // Already started.
    }

    dnsShuttingDown_ = false;
    for (int i = 0; i < DNS_THREAD_COUNT; ++i) {
        dnsThreads_.emplace_back(&GanlAdapter::dns_worker_func, this);
    }

    g_pILog->WriteString(tprintf(T("GANL: DNS worker threads started (%d threads).\n"), DNS_THREAD_COUNT));
    return true;
#else
    if (!g_dc.use_hostname || !networkEngine_) {
        return false;
    }

    if (dns_slave_) {
        return true;
    }

    auto channel = std::make_unique<DnsSlaveChannel>();
    ganl::SlaveSpawnOptions options;
    options.executable = "bin/slave";
    options.arguments = {"slave"};
    options.attachToStandardIO = true;
    options.communicationFd = 0;
    options.connectionContext = channel.get();

    ganl::ErrorCode error = 0;
    ganl::ConnectionHandle handle = networkEngine_->spawnSlave(options, error);
    if (handle == ganl::InvalidConnectionHandle) {
        g_pILog->WriteString(tprintf(T("GANL: Failed to spawn DNS slave: %s\n"),
            networkEngine_->getErrorString(error).c_str()));
        return false;
    }

    channel->handle = handle;
    channel->fd = static_cast<int>(handle);
    dns_slave_ = std::move(channel);

    g_pILog->WriteString(T("GANL: DNS slave channel started.\n"));
    return true;
#endif
}

void GanlAdapter::shutdown_dns_slave() {
#if defined(_WIN32)
    {
        std::lock_guard<std::mutex> lock(dnsMutex_);
        if (dnsThreads_.empty()) {
            return;
        }
        dnsShuttingDown_ = true;
        dnsRequests_.clear();
    }
    dnsCv_.notify_all();

    for (auto& t : dnsThreads_) {
        if (t.joinable()) {
            t.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(dnsMutex_);
        dnsThreads_.clear();
        dnsResults_.clear();
    }

    g_pILog->WriteString(T("GANL: DNS worker threads stopped.\n"));
#else
    if (!dns_slave_) {
        return;
    }

    auto handle = dns_slave_->handle;
    dns_slave_.reset();

    if (networkEngine_ && handle != ganl::InvalidConnectionHandle) {
        networkEngine_->closeConnection(handle);
    }
#endif
}

void GanlAdapter::queue_dns_lookup(const UTF8* numericAddress) {
#if defined(_WIN32)
    if (!g_dc.use_hostname || !numericAddress || *numericAddress == '\0') {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(dnsMutex_);
        if (dnsShuttingDown_ || dnsThreads_.empty()) {
            return;
        }
        dnsRequests_.emplace_back(reinterpret_cast<const char*>(numericAddress));
    }
    dnsCv_.notify_one();
#else
    if (!g_dc.use_hostname || !numericAddress || *numericAddress == '\0') {
        return;
    }

    if (!dns_slave_) {
        return;
    }

    dns_slave_->pendingWrites.emplace_back(reinterpret_cast<const char*>(numericAddress));
    dns_slave_->pendingWrites.back().push_back('\n');
    bool needShutdown = !flush_dns_slave_writes_locked();

    if (needShutdown) {
        shutdown_dns_slave();
    }
#endif
}

void GanlAdapter::handle_dns_slave_event(const ganl::IoEvent& event) {
#if defined(_WIN32)
    UNUSED_PARAMETER(event);
#else
    if (!dns_slave_ || (event.connection != dns_slave_->handle && event.context != dns_slave_.get())) {
        return;
    }

    bool needShutdown = false;
    switch (event.type) {
    case ganl::IoEventType::Read:
        needShutdown = !process_dns_slave_read_locked();
        break;
    case ganl::IoEventType::Write:
        needShutdown = !process_dns_slave_write_locked();
        break;
    case ganl::IoEventType::Close:
    case ganl::IoEventType::Error:
        needShutdown = true;
        break;
    default:
        break;
    }

    if (needShutdown) {
        shutdown_dns_slave();
    }
#endif
}

bool GanlAdapter::process_dns_slave_read_locked() {
#if defined(_WIN32)
    return true;
#else
    if (!dns_slave_) {
        return false;
    }

    char buffer[MBUF_SIZE];
    ssize_t nbytes = read(dns_slave_->fd, buffer, sizeof(buffer));
    if (nbytes > 0) {
        dns_slave_->readBuffer.append(buffer, static_cast<size_t>(nbytes));

        size_t newlinePos = std::string::npos;
        while ((newlinePos = dns_slave_->readBuffer.find('\n')) != std::string::npos) {
            std::string line = dns_slave_->readBuffer.substr(0, newlinePos);
            dns_slave_->readBuffer.erase(0, newlinePos + 1);

            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line.empty()) {
                continue;
            }

            size_t spacePos = line.find(' ');
            if (spacePos == std::string::npos) {
                continue;
            }

            std::string numeric = line.substr(0, spacePos);
            std::string hostname = line.substr(spacePos + 1);
            if (!hostname.empty()) {
                apply_reverse_dns_result(numeric, hostname);
            }
        }
        return true;
    }

    if (nbytes == 0) {
        g_pILog->WriteString(T("GANL: DNS slave closed its connection.\n"));
        return false;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return true;
    }

    g_pILog->WriteString(tprintf(T("GANL: DNS slave read error: %s\n"), strerror(errno)));
    return false;
#endif
}

bool GanlAdapter::flush_dns_slave_writes_locked() {
#if defined(_WIN32)
    return true;
#else
    if (!dns_slave_) {
        return false;
    }

    while (true) {
        if (dns_slave_->currentWrite.empty()) {
            if (dns_slave_->pendingWrites.empty()) {
                dns_slave_->writeInterest = false;
                return true;
            }
            dns_slave_->currentWrite = std::move(dns_slave_->pendingWrites.front());
            dns_slave_->pendingWrites.pop_front();
        }

        const char* data = dns_slave_->currentWrite.data();
        size_t remaining = dns_slave_->currentWrite.size();
        ssize_t written = mux_write(dns_slave_->fd, data, remaining);
        if (written > 0) {
            dns_slave_->currentWrite.erase(0, static_cast<size_t>(written));
            continue;
        }

        if (written == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!dns_slave_->writeInterest) {
                ganl::ErrorCode error = 0;
                if (networkEngine_->postWrite(dns_slave_->handle, nullptr, 0, dns_slave_.get(), error)) {
                    dns_slave_->writeInterest = true;
                } else {
                    g_pILog->WriteString(tprintf(T("GANL: Failed to request write interest for DNS slave: %s\n"),
                        networkEngine_->getErrorString(error).c_str()));
                    return false;
                }
            }
            return true;
        }

        g_pILog->WriteString(tprintf(T("GANL: DNS slave write error: %s\n"), strerror(errno)));
        return false;
    }
#endif
}

bool GanlAdapter::process_dns_slave_write_locked() {
#if defined(_WIN32)
    return true;
#else
    return flush_dns_slave_writes_locked();
#endif
}

void GanlAdapter::apply_reverse_dns_result(const std::string& numericAddress, const std::string& hostname) {
    UTF8 numericBuffer[MBUF_SIZE];
    UTF8 hostBuffer[MBUF_SIZE];

    std::strncpy(reinterpret_cast<char*>(numericBuffer), numericAddress.c_str(), sizeof(numericBuffer) - 1);
    numericBuffer[sizeof(numericBuffer) - 1] = '\0';
    std::strncpy(reinterpret_cast<char*>(hostBuffer), hostname.c_str(), sizeof(hostBuffer) - 1);
    hostBuffer[sizeof(hostBuffer) - 1] = '\0';

    for (auto it = g_descriptors_list.begin(); it != g_descriptors_list.end(); ++it) {
        DESC* d = *it;
        if (std::strcmp(reinterpret_cast<const char*>(d->addr), reinterpret_cast<const char*>(numericBuffer)) != 0) {
            continue;
        }

        std::strncpy(reinterpret_cast<char*>(d->addr), reinterpret_cast<const char*>(hostBuffer), sizeof(d->addr) - 1);
        d->addr[sizeof(d->addr) - 1] = '\0';

        if (d->player != NOTHING) {
            if (d->username[0]) {
                atr_add_raw(d->player, A_LASTSITE, tprintf(T("%s@%s"), d->username, d->addr));
            } else {
                atr_add_raw(d->player, A_LASTSITE, d->addr);
            }
            atr_add_raw(d->player, A_LASTIP, numericBuffer);
        }
    }
}

#if defined(_WIN32)
void GanlAdapter::dns_worker_func() {
    for (;;) {
        std::string ip;
        {
            std::unique_lock<std::mutex> lock(dnsMutex_);
            dnsCv_.wait(lock, [this]() {
                return dnsShuttingDown_ || !dnsRequests_.empty();
            });

            if (dnsShuttingDown_) {
                return;
            }

            ip = std::move(dnsRequests_.front());
            dnsRequests_.pop_front();
        }

        // Parse the numeric address string into a sockaddr via getaddrinfo
        // with AI_NUMERICHOST (no DNS query, just parsing).
        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_NUMERICHOST;

        struct addrinfo* servinfo = nullptr;
        int rv = getaddrinfo(ip.c_str(), nullptr, &hints, &servinfo);
        if (rv != 0 || !servinfo) {
            if (servinfo) {
                freeaddrinfo(servinfo);
            }
            continue;
        }

        // Reverse lookup via getnameinfo.
        char hostbuf[NI_MAXHOST];
        rv = getnameinfo(servinfo->ai_addr, static_cast<socklen_t>(servinfo->ai_addrlen),
                         hostbuf, sizeof(hostbuf), nullptr, 0, 0);
        freeaddrinfo(servinfo);

        if (rv != 0) {
            continue;
        }

        // Only publish if the hostname differs from the numeric address.
        std::string resolved(hostbuf);
        if (resolved == ip) {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(dnsMutex_);
            dnsResults_.emplace_back(std::move(ip), std::move(resolved));
        }
    }
}

void GanlAdapter::drain_dns_results() {
    std::deque<std::pair<std::string, std::string>> local;
    {
        std::lock_guard<std::mutex> lock(dnsMutex_);
        local.swap(dnsResults_);
    }

    for (const auto& result : local) {
        apply_reverse_dns_result(result.first, result.second);
    }
}
#endif


// ---------------------------------------------------------------------------
// Email channel — async SMTP client via GANL event-driven I/O
// ---------------------------------------------------------------------------

bool GanlAdapter::start_email_send(dbref executor, const UTF8* recipient,
                                   const UTF8* subject, const UTF8* encodedBody) {
    if (!networkEngine_) {
        return false;
    }

    if (email_channel_) {
        notify(executor, T("@email: Another email is already in progress."));
        return false;
    }

    // DNS resolution (blocking — acceptable for same-box SMTP).
    MUX_ADDRINFO hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags    = AI_ADDRCONFIG;

    UTF8* pMailServer = ConvertCRLFtoSpace(g_dc.mail_server);

    MUX_ADDRINFO* servinfo = nullptr;
    if (0 != mux_getaddrinfo(pMailServer, reinterpret_cast<const UTF8*>("25"),
                             &hints, &servinfo)) {
        notify(executor, tprintf(T("@email: Unable to resolve hostname %s!"),
            pMailServer));
        return false;
    }

    // Try each address until we find a socket we can connect on.
    // We create the socket, adopt it into GANL (which sets non-blocking),
    // then call connect().  A non-blocking connect returns EINPROGRESS
    // (Unix) or WSAEWOULDBLOCK (Windows) when it cannot complete
    // immediately, which is normal for async operation.
    //
    SOCKET sockFd = INVALID_SOCKET;
    struct sockaddr_storage connectAddr;
    socklen_t connectAddrLen = 0;

    for (MUX_ADDRINFO* p = servinfo; nullptr != p; p = p->ai_next) {
        SOCKET s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (IS_INVALID_SOCKET(s)) {
            continue;
        }
        sockFd = s;
        memcpy(&connectAddr, p->ai_addr, p->ai_addrlen);
        connectAddrLen = p->ai_addrlen;
        break;
    }
    mux_freeaddrinfo(servinfo);

    if (IS_INVALID_SOCKET(sockFd)) {
        notify(executor, T("@email: Unable to connect to mailserver, aborting!"));
        return false;
    }

    auto channel = std::make_unique<EmailChannel>();
    channel->fd       = static_cast<int>(sockFd);
    channel->executor = executor;
    channel->state    = EmailChannel::State::Connecting;

    // Copy all static-buffer results into stable std::string storage.
    channel->recipientAddr = reinterpret_cast<const char*>(recipient);
    channel->subject       = reinterpret_cast<const char*>(subject);
    channel->encodedBody   = reinterpret_cast<const char*>(encodedBody);
    channel->senderAddr    = reinterpret_cast<const char*>(
        ConvertCRLFtoSpace(g_dc.mail_sendaddr));
    channel->senderName    = reinterpret_cast<const char*>(
        ConvertCRLFtoSpace(g_dc.mail_sendname));
    channel->ehloHost      = reinterpret_cast<const char*>(
        ConvertCRLFtoSpace(g_dc.mail_ehlo));

    // adoptConnection sets non-blocking and registers with the I/O engine.
    ganl::ErrorCode error = 0;
    ganl::ConnectionHandle handle = networkEngine_->adoptConnection(
        static_cast<int>(sockFd), channel.get(), error);
    if (handle == ganl::InvalidConnectionHandle) {
        SOCKET_CLOSE(sockFd);
        notify(executor, T("@email: Unable to register socket with network engine."));
        return false;
    }

    channel->handle = handle;

    // Now that the fd is non-blocking (via adoptConnection), initiate
    // the connect.  Expect EINPROGRESS / EWOULDBLOCK for async.
    int rc = connect(static_cast<int>(sockFd),
                     reinterpret_cast<struct sockaddr*>(&connectAddr),
                     connectAddrLen);
    if (rc != 0) {
        int err = SOCKET_LAST_ERROR;
        if (err != SOCKET_EWOULDBLOCK
#if !defined(_WIN32)
            && err != EINPROGRESS
#endif
           ) {
            networkEngine_->closeConnection(handle);
            notify(executor, T("@email: Unable to connect to mailserver, aborting!"));
            return false;
        }
    }

    // Register write interest so we get notified when connect() completes
    // (socket becomes writable = connected).
    if (!networkEngine_->postWrite(handle, nullptr, 0, channel.get(), error)) {
        networkEngine_->closeConnection(handle);
        notify(executor, T("@email: Unable to register socket with network engine."));
        return false;
    }

    email_channel_ = std::move(channel);

    return true;
}

void GanlAdapter::shutdown_email_channel() {
    if (!email_channel_) {
        return;
    }

    auto handle = email_channel_->handle;
    email_channel_.reset();

    if (networkEngine_ && handle != ganl::InvalidConnectionHandle) {
        networkEngine_->closeConnection(handle);
    }
}

void GanlAdapter::handle_email_channel_event(const ganl::IoEvent& event) {
    if (!email_channel_ || (event.connection != email_channel_->handle &&
                            event.context != email_channel_.get())) {
        return;
    }

    bool needCleanup = false;
    const UTF8* errorMsg = nullptr;

    switch (event.type) {
        case ganl::IoEventType::Write:
            if (email_channel_->state == EmailChannel::State::Connecting) {
                // Check if non-blocking connect() succeeded.
                int sockerr = 0;
                socklen_t errlen = sizeof(sockerr);
                if (getsockopt(email_channel_->fd, SOL_SOCKET, SO_ERROR,
                               reinterpret_cast<char*>(&sockerr), &errlen) < 0 || sockerr != 0) {
                    errorMsg = T("@email: Unable to connect to mailserver, aborting!");
                    needCleanup = true;
                } else {
                    email_channel_->state = EmailChannel::State::WaitGreeting;
                }
            } else {
                if (!flush_email_writes_locked()) {
                    errorMsg = T("@email: Connection to mailserver lost.");
                    needCleanup = true;
                }
            }
            break;

        case ganl::IoEventType::Read:
            if (!process_email_read_locked()) {
                // process_email_read_locked handles its own notifications
                // for protocol errors.  A false return with no state
                // transition means a read error.
                if (email_channel_ &&
                    email_channel_->state != EmailChannel::State::Done &&
                    email_channel_->state != EmailChannel::State::Error) {
                    errorMsg = T("@email: Connection to mailserver lost.");
                }
                needCleanup = true;
            }
            break;

        case ganl::IoEventType::Close:
        case ganl::IoEventType::Error:
            if (email_channel_->state != EmailChannel::State::Done &&
                email_channel_->state != EmailChannel::State::SentQuit) {
                errorMsg = T("@email: Connection to mailserver lost.");
            }
            needCleanup = true;
            break;

        default:
            break;
        }

    if (needCleanup) {
        if (errorMsg) {
            email_notify_and_cleanup(errorMsg);
        } else {
            shutdown_email_channel();
        }
    }
}

bool GanlAdapter::process_email_read_locked() {
    if (!email_channel_) {
        return false;
    }

    // Loop reads until EAGAIN (edge-triggered epoll).
    for (;;) {
        char buffer[MBUF_SIZE];
        int nbytes = mux_read(email_channel_->fd, buffer, sizeof(buffer));
        if (nbytes > 0) {
            email_channel_->readBuffer.append(buffer,
                static_cast<size_t>(nbytes));
        } else if (nbytes == 0) {
            // Connection closed by remote.
            if (email_channel_->state == EmailChannel::State::SentQuit ||
                email_channel_->state == EmailChannel::State::Done) {
                email_channel_->state = EmailChannel::State::Done;
                return false; // Signal cleanup (not an error)
            }
            return false;
        } else {
            if (SOCKET_LAST_ERROR == SOCKET_EWOULDBLOCK) {
                break;
            }
            return false;
        }
    }

    // Extract complete lines from the read buffer.  SMTP multi-line
    // responses have '-' at position 3 for continuation lines; only the
    // final line (with ' ' at position 3) is passed to the state machine.
    size_t newlinePos;
    while ((newlinePos = email_channel_->readBuffer.find('\n'))
           != std::string::npos) {
        std::string line = email_channel_->readBuffer.substr(0, newlinePos);
        email_channel_->readBuffer.erase(0, newlinePos + 1);

        // Strip trailing CR.
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }

        if (line.empty()) {
            continue;
        }

        // SMTP multi-line: if char at position 3 is '-', it's a
        // continuation line — skip it and wait for the final line.
        if (line.size() > 3 && line[3] == '-') {
            continue;
        }

        email_advance_state_locked(line);

        if (email_channel_->state == EmailChannel::State::Done ||
            email_channel_->state == EmailChannel::State::Error) {
            return false; // Signal cleanup
        }
    }

    return true;
}

void GanlAdapter::email_queue_write_locked(const std::string& data) {
    if (!email_channel_) {
        return;
    }

    email_channel_->pendingWrites.push_back(data);
    // Try to flush immediately; if EAGAIN, postWrite will be called.
    flush_email_writes_locked();
}

bool GanlAdapter::flush_email_writes_locked() {
    if (!email_channel_) {
        return false;
    }

    while (true) {
        if (email_channel_->currentWrite.empty()) {
            if (email_channel_->pendingWrites.empty()) {
                email_channel_->writeInterest = false;
                return true;
            }
            email_channel_->currentWrite = std::move(
                email_channel_->pendingWrites.front());
            email_channel_->pendingWrites.pop_front();
        }

        const char* data = email_channel_->currentWrite.data();
        size_t remaining = email_channel_->currentWrite.size();
        int written = mux_write(email_channel_->fd, data, remaining);
        if (written > 0) {
            email_channel_->currentWrite.erase(
                0, static_cast<size_t>(written));
            continue;
        }

        if (written == -1 && SOCKET_LAST_ERROR == SOCKET_EWOULDBLOCK) {
            if (!email_channel_->writeInterest) {
                ganl::ErrorCode error = 0;
                if (networkEngine_->postWrite(email_channel_->handle,
                        nullptr, 0, email_channel_.get(), error)) {
                    email_channel_->writeInterest = true;
                } else {
                    return false;
                }
            }
            return true;
        }

        // Write error.
        return false;
    }
}

void GanlAdapter::email_advance_state_locked(const std::string& responseLine) {
    if (!email_channel_ || responseLine.empty()) {
        return;
    }

    char code = responseLine[0];

    switch (email_channel_->state) {
    case EmailChannel::State::WaitGreeting:
        if (code == '2') {
            // Good greeting, send EHLO.
            email_queue_write_locked(
                "EHLO " + email_channel_->ehloHost + "\r\n");
            email_channel_->state = EmailChannel::State::SentEhlo;
        } else {
            // Bad greeting — notify and quit.
            notify(email_channel_->executor,
                tprintf(T("@email: Invalid mailserver greeting (%s)"),
                    responseLine.c_str()));
            email_queue_write_locked("QUIT\r\n");
            email_channel_->state = EmailChannel::State::SentQuit;
        }
        break;

    case EmailChannel::State::SentEhlo:
        if (code != '2') {
            // Warn but continue (matching original behavior).
            notify(email_channel_->executor,
                tprintf(T("@email: Error response on EHLO (%s)"),
                    responseLine.c_str()));
        }
        // Send MAIL FROM.
        email_queue_write_locked(
            "MAIL FROM:<" + email_channel_->senderAddr + ">\r\n");
        email_channel_->state = EmailChannel::State::SentMailFrom;
        break;

    case EmailChannel::State::SentMailFrom:
        if (code != '2') {
            // Warn but continue (matching original behavior).
            notify(email_channel_->executor,
                tprintf(T("@email: Error response on MAIL FROM (%s)"),
                    responseLine.c_str()));
        }
        // Send RCPT TO.
        email_queue_write_locked(
            "RCPT TO:<" + email_channel_->recipientAddr + ">\r\n");
        email_channel_->state = EmailChannel::State::SentRcptTo;
        break;

    case EmailChannel::State::SentRcptTo:
        if (code != '2') {
            // RCPT TO error — abort.
            notify(email_channel_->executor,
                tprintf(T("@email: Error response on RCPT TO (%s)"),
                    responseLine.c_str()));
            email_queue_write_locked("QUIT\r\n");
            email_channel_->state = EmailChannel::State::SentQuit;
        } else {
            email_queue_write_locked("DATA\r\n");
            email_channel_->state = EmailChannel::State::SentData;
        }
        break;

    case EmailChannel::State::SentData:
        if (code != '3') {
            // DATA error — abort.
            notify(email_channel_->executor,
                tprintf(T("@email: Error response on DATA (%s)"),
                    responseLine.c_str()));
            email_queue_write_locked("QUIT\r\n");
            email_channel_->state = EmailChannel::State::SentQuit;
        } else {
            // Send headers + encoded body.
            std::string payload;
            payload += "From: " + email_channel_->senderName +
                       " <" + email_channel_->senderAddr + ">\r\n";
            payload += "To: " + email_channel_->recipientAddr + "\r\n";
            payload += "X-Mailer: TinyMUX ";
            payload += reinterpret_cast<const char*>(g_short_ver);
            payload += "\r\n";
            payload += "MIME-Version: 1.0\r\n";
            payload += "Content-Type: text/plain; charset=utf-8\r\n";
            payload += "Subject: " + email_channel_->subject + "\r\n\r\n";
            payload += email_channel_->encodedBody;
            email_queue_write_locked(payload);
            email_channel_->state = EmailChannel::State::SentBody;
        }
        break;

    case EmailChannel::State::SentBody:
        if (code == '2') {
            // Success — extract message from response (skip "250 ").
            const char* detail = "";
            if (responseLine.size() > 4) {
                detail = responseLine.c_str() + 4;
            }
            notify(email_channel_->executor,
                tprintf(T("@email: Mail sent to %s (%s)"),
                    email_channel_->recipientAddr.c_str(), detail));
        } else {
            notify(email_channel_->executor,
                tprintf(T("@email: Message rejected (%s)"),
                    responseLine.c_str()));
        }
        email_queue_write_locked("QUIT\r\n");
        email_channel_->state = EmailChannel::State::SentQuit;
        break;

    case EmailChannel::State::SentQuit:
        // Any response after QUIT — we're done.
        email_channel_->state = EmailChannel::State::Done;
        break;

    default:
        break;
    }
}

void GanlAdapter::email_notify_and_cleanup(const UTF8* message) {
    dbref executor = NOTHING;
    if (email_channel_) {
        executor = email_channel_->executor;
    }

    if (executor != NOTHING && message) {
        notify(executor, message);
    }

    shutdown_email_channel();
}


// --- Stub Slave Channel ---

#if defined(HAVE_WORKING_FORK) && defined(STUB_SLAVE)

extern QUEUE_INFO Queue_In;
extern QUEUE_INFO Queue_Out;
extern pid_t stubslave_pid;

bool GanlAdapter::boot_stubslave()
{
    const char *pFailedFunc = nullptr;
    int sv[2];
    int i;
    int maxfds;

#ifdef HAVE_GETDTABLESIZE
    maxfds = getdtablesize();
#else
    maxfds = sysconf(_SC_OPEN_MAX);
#endif

    shutdown_stubslave();

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
    {
        pFailedFunc = "socketpair() error: ";
        goto failure;
    }

    // Set parent end to nonblocking.
    //
    {
        int flags = fcntl(sv[0], F_GETFL, 0);
        if (flags < 0 || fcntl(sv[0], F_SETFL, flags | O_NONBLOCK) < 0)
        {
            pFailedFunc = "fcntl(O_NONBLOCK) error: ";
            mux_close(sv[0]);
            mux_close(sv[1]);
            goto failure;
        }
    }

    stubslave_pid = fork();
    switch (stubslave_pid)
    {
    case -1:

        pFailedFunc = "fork() error: ";
        mux_close(sv[0]);
        mux_close(sv[1]);
        goto failure;

    case 0:

        // If we don't clear this alarm, the child will eventually receive a
        // SIG_PROF.
        //
        alarm_clock.clear();

        // Child.  The following calls to dup2() assume only the minimal
        // dup2() functionality.  That is, the destination descriptor is
        // always available for it, and sv[1] is never that descriptor.
        // It is likely that the standard defined behavior of dup2()
        // would handle the job by itself more directly, but a little
        // extra code is low-cost insurance.
        //
        mux_close(sv[0]);
        if (sv[1] != 0)
        {
            mux_close(0);
            if (dup2(sv[1], 0) == -1)
            {
                _exit(1);
            }
        }
        if (sv[1] != 1)
        {
            mux_close(1);
            if (dup2(sv[1], 1) == -1)
            {
                _exit(1);
            }
        }
        for (i = 3; i < maxfds; i++)
        {
            mux_close(i);
        }
        execlp("bin/stubslave", "stubslave", static_cast<char *>(nullptr));
        _exit(1);
    }
    mux_close(sv[1]);

    stubslave_channel_ = std::make_unique<StubSlaveChannel>();
    stubslave_channel_->fd = sv[0];

    STARTLOG(LOG_ALWAYS, "NET", "STUB");
    g_pILog->log_text(T("Stub slave started on fd "));
    g_pILog->log_number(stubslave_channel_->fd);
    ENDLOG;
    return true;

failure:

    if (stubslave_pid > 0)
    {
        waitpid(stubslave_pid, nullptr, 0);
    }
    stubslave_pid = 0;

    STARTLOG(LOG_ALWAYS, "NET", "STUB");
    g_pILog->log_text(T(pFailedFunc));
    g_pILog->log_number(errno);
    ENDLOG;
    return false;
}

void GanlAdapter::shutdown_stubslave()
{
    if (stubslave_channel_)
    {
        if (stubslave_channel_->fd >= 0)
        {
            ::shutdown(stubslave_channel_->fd, SD_BOTH);
            SOCKET_CLOSE(stubslave_channel_->fd);
        }
        stubslave_channel_.reset();
    }

    if (stubslave_pid > 0)
    {
        waitpid(stubslave_pid, nullptr, 0);
    }
    stubslave_pid = 0;
}

MUX_RESULT GanlAdapter::pump_stubslave()
{
    g_debug_cmd = T("< pipepump >");

    if (!stubslave_channel_ || stubslave_channel_->fd < 0)
    {
        return MUX_E_FAIL;
    }

    int fd = stubslave_channel_->fd;

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    if (0 < Pipe_QueueLength(&Queue_Out))
    {
        pfd.events |= POLLOUT;
    }
    pfd.revents = 0;

    int found = poll(&pfd, 1, -1);

    if (found < 0)
    {
        int iSocketError = errno;
        if (EBADF == iSocketError)
        {
            g_pILog->log_perror(T("NET"), T("FAIL"), T("checking for activity"), T("poll"));
            struct stat fstatbuf;
            if (fstat(fd, &fstatbuf) < 0)
            {
                shutdown_stubslave();
                return MUX_E_FAIL;
            }
        }
        else if (EINTR != iSocketError)
        {
            g_pILog->log_perror(T("NET"), T("FAIL"), T("checking for activity"), T("poll"));
        }
        return MUX_S_OK;
    }

    if (pfd.revents & (POLLIN | POLLHUP | POLLERR))
    {
        char buf[LBUF_SIZE];
        for (;;)
        {
            int len = mux_read(fd, buf, sizeof(buf));
            if (len < 0)
            {
                int iSocketError = errno;
                if (EAGAIN == iSocketError || EWOULDBLOCK == iSocketError)
                {
                    break;
                }
                shutdown_stubslave();

                STARTLOG(LOG_ALWAYS, "NET", "STUB");
                g_pILog->log_text(T("read() of stubslave failed. Stubslave stopped."));
                ENDLOG;

                return MUX_E_FAIL;
            }
            else if (0 == len)
            {
                // EOF — stubslave closed its end.
                //
                shutdown_stubslave();

                STARTLOG(LOG_ALWAYS, "NET", "STUB");
                g_pILog->log_text(T("Stubslave pipe closed (EOF). Stubslave stopped."));
                ENDLOG;

                return MUX_E_FAIL;
            }
            Pipe_AppendBytes(&Queue_In, len, buf);
        }
    }

    if (  stubslave_channel_
       && stubslave_channel_->fd >= 0
       && (pfd.revents & POLLOUT))
    {
        char buf[LBUF_SIZE];
        size_t nWanted = sizeof(buf);
        if (  Pipe_GetBytes(&Queue_Out, &nWanted, buf)
           && 0 < nWanted)
        {
            int len = mux_write(stubslave_channel_->fd, buf, nWanted);
            if (len < 0)
            {
                int iSocketError = errno;
                if (EAGAIN != iSocketError && EWOULDBLOCK != iSocketError)
                {
                    shutdown_stubslave();

                    STARTLOG(LOG_ALWAYS, "NET", "STUB");
                    g_pILog->log_text(T("write() of stubslave failed. Stubslave stopped."));
                    ENDLOG;

                    return MUX_E_FAIL;
                }
            }
        }
    }
    return MUX_S_OK;
}

#endif // HAVE_WORKING_FORK && STUB_SLAVE


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
        const CLinearTimeAbsolute ltaNow = [&]() {
            CLinearTimeAbsolute tmp; tmp.GetUTC(); return tmp;
        }();
        const int mux_reason = MapGanlReasonToMux(reason);
        const int clamped_reason = ClampMuxReason(mux_reason);

        if (clamped_reason == R_LOGOUT)
        {
            if (d->player != NOTHING)
            {
                HandleConnectedDescriptorPreAnnounce(d, clamped_reason, ltaNow);
                announce_disconnect(d->player, d, disc_messages[clamped_reason]);
            }
            else
            {
                HandleUnconnectedDescriptorClose(d);
            }

            ResetDescriptorForLogout(d);
            return;
        }

        if (d->player != NOTHING)
        {
            HandleConnectedDescriptorPreAnnounce(d, clamped_reason, ltaNow);
            announce_disconnect(d->player, d, disc_messages[clamped_reason]);
        }
        else
        {
            HandleUnconnectedDescriptorClose(d);
        }

        process_output(d, false);
        clearstrings(d);
        freeqs(d);
        if (d->flags & DS_CONNECTED)
        {
            d->flags &= ~DS_CONNECTED;
        }
        drv_CancelTask(Task_ProcessCommand, d, 0);
        destroy_desc(d);
        free_desc(d); // Free DESC if GANL doesn't know about it
    }
}

std::string GanlAdapter::get_remote_address(DESC* d) {
    if (!d) return "";
    // Use the stored address in DESC for now
    return reinterpret_cast<const char *>(d->addr);
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
    handle_to_desc_[handle] = d;
    desc_to_handle_[d] = handle;
    handle_to_conn_[handle] = conn; // Store the shared_ptr
    //GANL_CONN_DEBUG(handle, "Mapped Handle <-> DESC " << d << " and Connection " << conn.get());
}

void GanlAdapter::remove_mapping(DESC* d) {
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
    auto it = handle_to_desc_.find(handle);
    return (it != handle_to_desc_.end()) ? it->second : nullptr;
}

std::shared_ptr<ganl::ConnectionBase> GanlAdapter::get_connection(DESC* d) {
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
    auto it = desc_to_handle_.find(d);
    return (it != desc_to_handle_.end()) ? it->second : ganl::InvalidConnectionHandle;
}


// --- DESC Allocation ---
DESC* GanlAdapter::allocate_desc() {
    // Use TinyMUX's pool allocator
    DESC* d = alloc_desc("ganl_connection");
    if (d)
    {
        init_desc(d);
    }
    return d;
}

void GanlAdapter::free_desc2(DESC* d) {
    // Use TinyMUX's pool allocator
    destroy_desc(d);
    free_desc(d);
}


// --- Public Interface Functions ---

void ganl_initialize() {
    if (!g_GanlAdapter.initialize()) {
        // Handle fatal initialization error - perhaps exit?
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
        g_GanlAdapter.send_data(d, reinterpret_cast<const char *>(data), strlen(reinterpret_cast<const char *>(data)));
    }
}

void ganl_close_connection(DESC* d, int reason) {
    if (!d) {
        return;
    }

    if (reason == R_LOGOUT) {
        CLinearTimeAbsolute ltaNow;
        ltaNow.GetUTC();

        if (d->player != NOTHING)
        {
            HandleConnectedDescriptorPreAnnounce(d, R_LOGOUT, ltaNow);
            announce_disconnect(d->player, d, disc_messages[R_LOGOUT]);
        }
        else
        {
            HandleUnconnectedDescriptorClose(d);
        }

        ResetDescriptorForLogout(d);
        return;
    }

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
    case R_GAMEFULL:   ganl_reason = ganl::DisconnectReason::GameFull; break;
    case R_RESTART:    ganl_reason = ganl::DisconnectReason::ServerShutdown; break;
    default:           ganl_reason = ganl::DisconnectReason::Unknown; break;
    }
    g_GanlAdapter.close_connection(d, ganl_reason);
}

void ganl_associate_player(DESC* d, dbref player) {
    if (d) {
        d->player = player;
    }
}

void do_startslave(dbref executor, dbref caller, dbref enactor,
                   int eval, int key)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);

    g_GanlAdapter.shutdown_dns_slave();
    if (g_GanlAdapter.start_dns_slave()) {
        notify(executor, T("DNS slave restarted."));
    } else {
        notify(executor, T("DNS slave failed to start."));
    }
}
