/*! \file bsd.cpp
 * \brief Low-level TCP socket-related code.
 *
 * Contains most of the TCP socket-related code. Some socket-related code also
 * exists in netcommon.cpp, but most of it is here.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "ganl_adapter.h"
using namespace std;

#if defined(HAVE_DLOPEN) && defined(STUB_SLAVE)
extern QUEUE_INFO Queue_In;
extern QUEUE_INFO Queue_Out;
#endif

#ifdef SOLARIS
extern const int _sys_nsig;
#define NSIG _sys_nsig
#endif // SOLARIS

#ifdef UNIX_SSL
SSL_CTX  *ssl_ctx = nullptr;
SSL_CTX  *tls_ctx = nullptr;
port_info main_game_ports[MAX_LISTEN_PORTS * 2];
#else
port_info main_game_ports[MAX_LISTEN_PORTS];
#endif
int      num_main_game_ports = 0;
void site_mon_send(SOCKET, const UTF8 *, DESC *, const UTF8 *);
static int make_nonblocking(SOCKET s);

pid_t game_pid;

#if defined(HAVE_WORKING_FORK)

#ifdef STUB_SLAVE
pid_t stubslave_pid = 0;
int stubslave_socket = INVALID_SOCKET;
#endif // STUB_SLAVE

#ifdef STUB_SLAVE
void CleanUpStubSlaveSocket(void)
{
    if (!IS_INVALID_SOCKET(stubslave_socket))
    {
        shutdown(stubslave_socket, SD_BOTH);
        if (0 == SOCKET_CLOSE(stubslave_socket))
        {
            DebugTotalSockets--;
        }
        stubslave_socket = INVALID_SOCKET;
    }
}

void WaitOnStubSlaveProcess(void)
{
    if (stubslave_pid > 0)
    {
        waitpid(stubslave_pid, nullptr, 0);
    }
    stubslave_pid = 0;
}

/*! \brief Lauch stub slave process.
 *
 * This spawns the stub slave process and creates a socket-oriented,
 * bi-directional communication path between that process and this
 * process. Any existing slave process is killed.
 *
 * \param executor dbref of Executor.
 * \param caller   dbref of Caller.
 * \param enactor  dbref of Enactor.
 * \return         None.
 */

void boot_stubslave(dbref executor, dbref caller, dbref enactor, int)
{
    const char *pFailedFunc = nullptr;
    int sv[2];
    int i;
    int maxfds;

#ifdef HAVE_GETDTABLESIZE
    maxfds = getdtablesize();
#else // HAVE_GETDTABLESIZE
    maxfds = sysconf(_SC_OPEN_MAX);
#endif // HAVE_GETDTABLESIZE

    CleanUpStubSlaveSocket();
    WaitOnStubSlaveProcess();

    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0)
    {
        pFailedFunc = "socketpair() error: ";
        goto failure;
    }

    // Set to nonblocking.
    //
    if (make_nonblocking(sv[0]) < 0)
    {
        pFailedFunc = "make_nonblocking() error: ";
        mux_close(sv[0]);
        mux_close(sv[1]);
        goto failure;
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

    stubslave_socket = sv[0];
    DebugTotalSockets++;
    if (make_nonblocking(stubslave_socket) < 0)
    {
        pFailedFunc = "make_nonblocking() error: ";
        CleanUpStubSlaveSocket();
        goto failure;
    }
    STARTLOG(LOG_ALWAYS, "NET", "STUB");
    log_text(T("Stub slave started on fd "));
    log_number(stubslave_socket);
    ENDLOG;
    return;

failure:

    WaitOnStubSlaveProcess();
    STARTLOG(LOG_ALWAYS, "NET", "STUB");
    log_text(T(pFailedFunc));
    log_number(errno);
    ENDLOG;
}

/*! \brief Get results from the Stubslave.
 *
 * Any communication from the stub slave passed to the module library.
 *
 * There needs to be a FIFO from the stub that is maintained by netmux.  Each
 * packet from the other side should be passed to mux_ReceiveData separately
 * because the next packet may be handled by this same routine at a lower call
 * level.  A packet may contain a call, a return, or a message.  If the packet
 * is a call or a message, it may result in a remote call to the other side
 * which will then block waiting on a return from the other side.  Any return
 * will generally cause us to unblock, however, the returns should be matched
 * with the calls. Without a match, some sort of error has probably occured.
 *
 * Once we have a return, there is a choice as to whether to process the
 * remaining packets in the incoming FIFO (which should be calls or meessages)
 * or whether to return and let the top-level shovechars() loop do it.
 *
 * This function is potentially highly reentrant, so any data passed to the
 * module library must first be removed cleanly from the FIFO.
 *
 * \return         -1 for failure and 0 for success.
 */

static int StubSlaveRead(void)
{
    char buf[LBUF_SIZE];

    int len = mux_read(stubslave_socket, buf, sizeof(buf));
    if (len < 0)
    {
        int iSocketError = SOCKET_LAST_ERROR;
        if (  SOCKET_EAGAIN == iSocketError
           || SOCKET_EWOULDBLOCK == iSocketError)
        {
            return -1;
        }
        CleanUpStubSlaveSocket();
        WaitOnStubSlaveProcess();

        STARTLOG(LOG_ALWAYS, "NET", "STUB");
        log_text(T("read() of stubslave failed. Stubslave stopped."));
        ENDLOG;

        return -1;
    }
    else if (0 == len)
    {
        return -1;
    }

    Pipe_AppendBytes(&Queue_In, len, buf);
    return 0;
}

static int StubSlaveWrite(void)
{
    char buf[LBUF_SIZE];

    size_t nWanted = sizeof(buf);
    if (  Pipe_GetBytes(&Queue_Out, &nWanted, buf)
       && 0 < nWanted)
    {
        int len = mux_write(stubslave_socket, buf, nWanted);
        if (len < 0)
        {
            int iSocketError = SOCKET_LAST_ERROR;
            if (  SOCKET_EAGAIN == iSocketError
               || SOCKET_EWOULDBLOCK == iSocketError)
            {
                return -1;
            }
            CleanUpStubSlaveSocket();
            WaitOnStubSlaveProcess();

            STARTLOG(LOG_ALWAYS, "NET", "STUB");
            log_text(T("write() of stubslave failed. Stubslave stopped."));
            ENDLOG;

            return -1;
        }
    }
    return 0;
}

extern "C" MUX_RESULT DCL_API pipepump(void)
{
    mudstate.debug_cmd = T("< pipepump >");

    if (IS_INVALID_SOCKET(stubslave_socket))
    {
        return MUX_E_FAIL;
    }

    fd_set input_set;
    fd_set output_set;

    FD_ZERO(&input_set);
    FD_ZERO(&output_set);

    FD_SET(stubslave_socket, &input_set);
    if (0 < Pipe_QueueLength(&Queue_Out))
    {
        FD_SET(stubslave_socket, &output_set);
    }

    int nfds = stubslave_socket + 1;
    int found = select(nfds, &input_set, &output_set, nullptr, nullptr);

    if (IS_SOCKET_ERROR(found))
    {
        int iSocketError = SOCKET_LAST_ERROR;
        if (SOCKET_EBADF == iSocketError)
        {
            log_perror(T("NET"), T("FAIL"), T("checking for activity"), T("select"));
            struct stat fstatbuf;
            if (  !IS_INVALID_SOCKET(stubslave_socket)
               && fstat(stubslave_socket, &fstatbuf) < 0)
            {
                CleanUpStubSlaveSocket();
                return MUX_E_FAIL;
            }
        }
        else if (iSocketError != SOCKET_EINTR)
        {
            log_perror(T("NET"), T("FAIL"), T("checking for activity"), T("select"));
        }
        return MUX_S_OK;
    }

    if (FD_ISSET(stubslave_socket, &input_set))
    {
        while (0 == StubSlaveRead())
        {
            ; // Nothing.
        }
    }

    if (!IS_INVALID_SOCKET(stubslave_socket))
    {
        if (FD_ISSET(stubslave_socket, &output_set))
        {
            StubSlaveWrite();
        }
    }
    return MUX_S_OK;
}

#endif // STUB_SLAVE

#endif // HAVE_WORKING_FORK

#ifdef UNIX_SSL
int pem_passwd_callback(char *buf, int size, int rwflag, void *userdata)
{
    const char *passwd = (const char *)userdata;
    int passwdLen = strlen(passwd);
    strncpy(buf, passwd, size);
    return ((passwdLen > size) ? size : passwdLen);
}

bool initialize_ssl()
{
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
    OpenSSL_add_all_digests();
    ssl_ctx = SSL_CTX_new(TLS_server_method());
    tls_ctx = SSL_CTX_new(TLS_server_method());

    if (!SSL_CTX_use_certificate_file(ssl_ctx, (char *)mudconf.ssl_certificate_file, SSL_FILETYPE_PEM))
    {
        STARTLOG(LOG_ALWAYS, "NET", "SSL");
        log_text(T("initialize_ssl: Unable to load SSL certificate file "));
        log_text(mudconf.ssl_certificate_file);
        ENDLOG;
        SSL_CTX_free(ssl_ctx);
        SSL_CTX_free(tls_ctx);
        ssl_ctx = nullptr;
        tls_ctx = nullptr;
        return false;
    }
    if (!SSL_CTX_use_certificate_file(tls_ctx, (char *)mudconf.ssl_certificate_file, SSL_FILETYPE_PEM))
    {
        STARTLOG(LOG_ALWAYS, "NET", "SSL");
        log_text(T("initialize_ssl: Unable to load SSL certificate file "));
        log_text(mudconf.ssl_certificate_file);
        ENDLOG;
        SSL_CTX_free(ssl_ctx);
        SSL_CTX_free(tls_ctx);
        ssl_ctx = nullptr;
        tls_ctx = nullptr;
        return false;
    }

    SSL_CTX_set_default_passwd_cb(ssl_ctx, pem_passwd_callback);
    SSL_CTX_set_default_passwd_cb_userdata(ssl_ctx, (void *)mudconf.ssl_certificate_password);
    SSL_CTX_set_default_passwd_cb(tls_ctx, pem_passwd_callback);
    SSL_CTX_set_default_passwd_cb_userdata(tls_ctx, (void *)mudconf.ssl_certificate_password);

    if (!SSL_CTX_use_PrivateKey_file(ssl_ctx, (char *)mudconf.ssl_certificate_key, SSL_FILETYPE_PEM))
    {
        STARTLOG(LOG_ALWAYS, "NET", "SSL");
        log_text(T("initialize_ssl: Unable to load SSL private key: "));
        log_text(mudconf.ssl_certificate_key);
        ENDLOG;
        SSL_CTX_free(ssl_ctx);
        SSL_CTX_free(tls_ctx);
        ssl_ctx = nullptr;
        tls_ctx = nullptr;
        return false;
    }

    /* Since we're reusing settings, we only need to check the key once.
     * We'll use the SSL ctx for that. */
    if (!SSL_CTX_check_private_key(ssl_ctx))
    {
        STARTLOG(LOG_ALWAYS, "NET", "SSL");
        log_text(T("initialize_ssl: Key, certificate or password does not match."));
        ENDLOG;
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = nullptr;
        return false;
    }


    SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
    SSL_CTX_set_mode(ssl_ctx, SSL_MODE_AUTO_RETRY);
    SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    SSL_CTX_set_mode(tls_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
    SSL_CTX_set_mode(tls_ctx, SSL_MODE_AUTO_RETRY);
    SSL_CTX_set_mode(tls_ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    STARTLOG(LOG_ALWAYS, "NET", "SSL");
    log_text(T("initialize_ssl: SSL engine initialized successfully."));
    ENDLOG;

    return true;
}

void shutdown_ssl()
{
    if (nullptr != ssl_ctx)
    {
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = nullptr;
    }
    if (nullptr != tls_ctx)
    {
        SSL_CTX_free(tls_ctx);
        tls_ctx = nullptr;
    }
}

void CleanUpSSLConnections()
{
    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); )
    {
        DESC* d = *it;
        ++it;
        if (d->ssl_session)
        {
            shutdownsock(d, R_RESTART);
        }
    }
}

#endif

int mux_socket_read(DESC *d, char *buffer, size_t nBytes, int flags)
{
    int result;

#ifdef UNIX_SSL
    if (d->ssl_session)
    {
        result = SSL_read(d->ssl_session, buffer, nBytes);
    }
    else
#endif
    {
        result = SOCKET_READ(d->socket, buffer, nBytes, flags);
    }

    return result;
}



int make_nonblocking(SOCKET s)
{
#if defined(_WIN32)
    unsigned long on = 1;
    if (ioctlsocket(s, FIONBIO, &on) != 0)
    {
        log_perror(T("NET"), T("FAIL"), T("make_nonblocking"), T("ioctlsocket"));
        return -1;
    }
#elif defined(O_NONBLOCK)
    if (fcntl(s, F_SETFL, O_NONBLOCK) < 0)
    {
        log_perror(T("NET"), T("FAIL"), T("make_nonblocking"), T("fcntl"));
        return -1;
    }
#elif defined(FNDELAY)
    if (fcntl(s, F_SETFL, FNDELAY) < 0)
    {
        log_perror(T("NET"), T("FAIL"), T("make_nonblocking"), T("fcntl"));
        return -1;
    }
#elif defined(O_NDELAY)
    if (fcntl(s, F_SETFL, O_NDELAY) < 0)
    {
        log_perror(T("NET"), T("FAIL"), T("make_nonblocking"), T("fcntl"));
        return -1;
    }
#elif defined(FIONBIO)
    unsigned long on = 1;
    if (ioctl(s, FIONBIO, &on) < 0)
    {
        log_perror(T("NET"), T("FAIL"), T("make_nonblocking"), T("ioctl"));
        return -1;
    }
#endif // _WIN32, O_NONBLOCK, FNDELAY, O_NDELAY, FIONBIO
    return 0;
}



// Disconnect reasons that get written to the logfile
//
const UTF8 *disc_reasons[] =
{
    T("Unspecified"),
    T("Quit"),
    T("Inactivity Timeout"),
    T("Booted"),
    T("Remote Close or Net Failure"),
    T("Game Shutdown"),
    T("Login Retry Limit"),
    T("Logins Disabled"),
    T("Logout (Connection Not Dropped)"),
    T("Too Many Connected Players"),
    T("Restarted (SSL Connections Dropped)")
 };

// Disconnect reasons that get fed to A_ADISCONNECT via announce_disconnect
//
const UTF8 *disc_messages[] =
{
    T("Unknown"),
    T("Quit"),
    T("Timeout"),
    T("Boot"),
    T("Netfailure"),
    T("Shutdown"),
    T("BadLogin"),
    T("NoLogins"),
    T("Logout"),
    T("GameFull"),
    T("Restart")
};

void shutdownsock(DESC *d, int reason)
{
    UTF8 *buff;

    if (  R_LOGOUT == reason
       && mudstate.access_list.isForbid(&d->address))
    {
        reason = R_QUIT;
    }

    if (  reason < R_MIN
       || R_MAX < reason)
    {
        reason = R_UNKNOWN;
    }

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();

    if (d->flags & DS_CONNECTED)
    {
        // Reason: attribute (disconnect reason)
        //
        atr_add_raw(d->player, A_REASON, disc_messages[reason]);

        // Update the A_CONNINFO attribute.
        //
        long anFields[4];
        fetch_ConnectionInfoFields(d->player, anFields);

        // One of the active sessions is going away. It doesn't matter which
        // one.
        //
        anFields[CIF_NUMCONNECTS]++;

        // What are the two longest sessions?
        //
        DESC *dOldest[2];
        find_oldest(d->player, dOldest);

        CLinearTimeDelta ltdFull = ltaNow - dOldest[0]->connected_at;
        const long tFull = ltdFull.ReturnSeconds();
        if (dOldest[0] == d)
        {
            // We are dropping the oldest connection.
            //
            CLinearTimeDelta ltdPart;
            if (dOldest[1])
            {
                // There is another (more recently made) connection.
                //
                ltdPart = dOldest[1]->connected_at - dOldest[0]->connected_at;
            }
            else
            {
                // There is only one connection.
                //
                ltdPart = ltdFull;
            }
            const auto tPart = ltdPart.ReturnSeconds();

            anFields[CIF_TOTALTIME] += tPart;
            if (anFields[CIF_LONGESTCONNECT] < tFull)
            {
                anFields[CIF_LONGESTCONNECT] = tFull;
            }
        }
        anFields[CIF_LASTCONNECT] = tFull;

        put_ConnectionInfoFields(d->player, anFields, ltaNow);

        // If we are doing a LOGOUT, keep the connection open so that the
        // player can connect to a different character. Otherwise, we
        // do the normal disconnect stuff.
        //
        if (reason == R_LOGOUT)
        {
            STARTLOG(LOG_NET | LOG_LOGIN, "NET", "LOGO")
            buff = alloc_mbuf("shutdownsock.LOG.logout");
            mux_sprintf(buff, MBUF_SIZE, T("[%u/%s] Logout by "), d->socket, d->addr);
            log_text(buff);
            log_name(d->player);
            mux_sprintf(buff, MBUF_SIZE, T(" <Reason: %s>"), disc_reasons[reason]);
            log_text(buff);
            free_mbuf(buff);
            ENDLOG;
        }
        else
        {
            fcache_dump(d, FC_QUIT);
            STARTLOG(LOG_NET | LOG_LOGIN, "NET", "DISC")
            buff = alloc_mbuf("shutdownsock.LOG.disconn");
            mux_sprintf(buff, MBUF_SIZE, T("[%u/%s] Logout by "), d->socket, d->addr);
            log_text(buff);
            log_name(d->player);
            mux_sprintf(buff, MBUF_SIZE, T(" <Reason: %s>"), disc_reasons[reason]);
            log_text(buff);
            free_mbuf(buff);
            ENDLOG;
            site_mon_send(d->socket, d->addr, d, T("Disconnection"));
        }

        // If requested, write an accounting record of the form:
        // Plyr# Flags Cmds ConnTime Loc Money [Site] <DiscRsn> Name
        //
        STARTLOG(LOG_ACCOUNTING, "DIS", "ACCT");
        auto ltd = ltaNow - d->connected_at;
        const int Seconds = ltd.ReturnSeconds();
        buff = alloc_lbuf("shutdownsock.LOG.accnt");
        const auto buff2 = decode_flags(GOD, &(db[d->player].fs));
        const auto locPlayer = Location(d->player);
        const auto penPlayer = Pennies(d->player);
        const auto PlayerName = PureName(d->player);
        mux_sprintf(buff, LBUF_SIZE, T("%d %s %d %d %d %d [%s] <%s> %s"), d->player, buff2, d->command_count,
                Seconds, locPlayer, penPlayer, d->addr, disc_reasons[reason],
                PlayerName);
        log_text(buff);
        free_lbuf(buff);
        free_sbuf(buff2);
        ENDLOG;
        announce_disconnect(d->player, d, disc_messages[reason]);
    }
    else
    {
        if (reason == R_LOGOUT)
        {
            reason = R_QUIT;
        }
        STARTLOG(LOG_SECURITY | LOG_NET, "NET", "DISC");
        buff = alloc_mbuf("shutdownsock.LOG.neverconn");
        mux_sprintf(buff, MBUF_SIZE, T("[%u/%s] Connection closed, never connected. <Reason: %s>"),
            d->socket, d->addr, disc_reasons[reason]);
        log_text(buff);
        free_mbuf(buff);
        ENDLOG;
        site_mon_send(d->socket, d->addr, d, T("N/C Connection Closed"));
    }

    process_output(d, false);
    clearstrings(d);

    d->flags &= ~DS_CONNECTED;

    // Is this desc still in interactive mode?
    //
    if (d->program_data != nullptr)
    {
        int num = 0;
        const auto range = mudstate.dbref_to_descriptors_map.equal_range(d->player);
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
            MEMFREE(d->program_data);
            atr_clr(d->player, A_PROGCMD);
        }
        d->program_data = nullptr;
    }

    if (reason == R_LOGOUT)
    {
        d->connected_at.GetUTC();
        d->retries_left = mudconf.retry_limit;
        d->command_count = 0;
        d->timeout = mudconf.idle_timeout;
        d->player = 0;
        d->doing[0] = '\0';
        d->quota = mudconf.cmd_quota_max;
        d->last_time = d->connected_at;
        d->input_tot = d->input_size;
        d->output_tot = 0;
        d->encoding = d->negotiated_encoding;

        welcome_user(d);
    }
    else
    {
        // Cancel any scheduled processing on this socket.
        //
        scheduler.CancelTask(Task_ProcessCommand, d, 0);

#ifdef UNIX_SSL
        if (d->ssl_session)
        {
            SSL_shutdown(d->ssl_session);
            SSL_free(d->ssl_session);
            d->ssl_session = nullptr;
        }
#endif

        shutdown(d->socket, SD_BOTH);
        if (0 == SOCKET_CLOSE(d->socket))
        {
            DebugTotalSockets--;
        }
        d->socket = INVALID_SOCKET;

        auto it = mudstate.descriptors_map.find(d);
        mudstate.descriptors_list.erase(it->second);
        mudstate.descriptors_map.erase(it);

        // If we don't have queued IOs, then we can free these, now.
        //
        freeqs(d);
        free_desc(d);
    }
}


static void process_output_ganl(DESC *d, int bHandleShutdown)
{
    UNUSED_PARAMETER(bHandleShutdown);

    text_block *tb = d->output_head;
    while (nullptr != tb)
    {
        if (0 < tb->hdr.nchars)
        {
            g_GanlAdapter.send_data(d, (const char*)tb->hdr.start, tb->hdr.nchars);
            d->output_size -= tb->hdr.nchars;
            tb->hdr.nchars = 0;
        }
        text_block *save = tb;
        tb = tb->hdr.nxt;
        MEMFREE(save);
        d->output_head = tb;
        if (tb == nullptr)
        {
            d->output_tail = nullptr;
        }
    }
}

void process_output(DESC *d, int bHandleShutdown)
{
    process_output_ganl(d, bHandleShutdown);
}

/*! \brief Table to quickly classify characters recieved from the wire with
 * their Telnet meaning.
 *
 * The use of this table reduces the size of the state table.
 *
 * Class  0 - Any byte.    Class  5 - BRK  (0xF3)  Class 10 - WONT (0xFC)
 * Class  1 - BS   (0x08)  Class  5 - IP   (0xF4)  Class 11 - DO   (0xFD)
 * Class  2 - LF   (0x0A)  Class  5 - AO   (0xF5)  Class 12 - DONT (0xFE)
 * Class  3 - CR   (0x0D)  Class  6 - AYT  (0xF6)  Class 13 - IAC  (0xFF)
 * Class  1 - DEL  (0x7F)  Class  7 - EC   (0xF7)
 * Class  5 - EOR  (0xEF)  Class  5 - EL   (0xF8)
 * Class  4 - SE   (0xF0)  Class  5 - GA   (0xF9)
 * Class  5 - NOP  (0xF1)  Class  8 - SB   (0xFA)
 * Class  5 - DM   (0xF2)  Class  9 - WILL (0xFB)
 */

static const unsigned char nvt_input_xlat_table[256] =
{
//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
//
    0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  2,  0,  0,  3,  0,  0,  // 0
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 1
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 2
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 3
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 4
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 5
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 6
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  // 7

    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 8
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 9
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // A
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // B
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // C
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // D
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  5,  // E
    4,  5,  5,  5,  5,  5,  6,  7,  5,  5,  8,  9, 10, 11, 12, 13   // F
};

/*! \brief Table to map current telnet parsing state state and input to
 * specific actions and state changes.
 *
 * Action  0 - Nothing.
 * Action  1 - Accept CHR(X) (and transition to Normal state).
 * Action  2 - Erase Character.
 * Action  3 - Accept Line.
 * Action  4 - Transition to the Normal state.
 * Action  5 - Transition to Have_IAC state.
 * Action  6 - Transition to the Have_IAC_WILL state.
 * Action  7 - Transition to the Have_IAC_DONT state.
 * Action  8 - Transition to the Have_IAC_DO state.
 * Action  9 - Transition to the Have_IAC_WONT state.
 * Action 10 - Transition to the Have_IAC_SB state.
 * Action 11 - Transition to the Have_IAC_SB_IAC state.
 * Action 12 - Respond to IAC AYT and return to the Normal state.
 * Action 13 - Respond to IAC WILL X
 * Action 14 - Respond to IAC DONT X
 * Action 15 - Respond to IAC DO X
 * Action 16 - Respond to IAC WONT X
 * Action 17 - Accept CHR(X) for Sub-Option (and transition to Have_IAC_SB state).
 * Action 18 - Accept Completed Sub-option and transition to Normal state.
 */

static const int nvt_input_action_table[8][14] =
{
//    Any   BS   LF   CR   SE  NOP  AYT   EC   SB WILL DONT   DO WONT  IAC
    {   1,   2,   3,   0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   5  }, // Normal
    {   4,   4,   4,   4,   4,   4,  12,   2,  10,   6,   7,   8,   9,   1  }, // Have_IAC
    {  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,   4  }, // Have_IAC_WILL
    {  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,   4  }, // Have_IAC_DONT
    {  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,   4  }, // Have_IAC_DO
    {  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,   4  }, // Have_IAC_WONT
    {  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  11  }, // Have_IAC_SB
    {   0,   0,   0,   0,  18,   0,   0,   0,   0,   0,   0,   0,   0,  17  }, // Have_IAC_SB_IAC
};

/*! \brief Transmit a Telnet SB sequence for the given option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \param chRequest Telnet SB command.
 */
static void send_sb(DESC *d, unsigned char chOption, unsigned char chRequest)
{
    UTF8 aSB[6] = { NVT_IAC, NVT_SB, 0, 0, NVT_IAC, NVT_SE };
    aSB[2] = chOption;
    aSB[3] = chRequest;
    queue_write_LEN(d, aSB, sizeof(aSB));
}

/*! \brief Transmit a Telnet SB sequence for the given option with the given payload
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \param chRequest Telnet SB command.
 * \param pPayload  Pointer to the payload.
 * \param nPayload  Length of the payload.
 */
static void send_sb
(
    DESC *d,
    const unsigned char chOption,
    const unsigned char chRequest,
    const unsigned char *pPayload,
    const size_t nPayload
)
{
    const auto nMaximum = 6 + 2*nPayload;

    unsigned char buffer[100];
    auto pSB = buffer;
    if (sizeof(buffer) < nMaximum)
    {
        pSB = static_cast<unsigned char *>(MEMALLOC(nMaximum));
        if (nullptr == pSB)
        {
            return;
        }
    }

    pSB[0] = NVT_IAC;
    pSB[1] = NVT_SB;
    pSB[2] = chOption;
    pSB[3] = chRequest;

    auto p = &pSB[4];

    for (size_t loop = 0; loop < nPayload; loop++)
    {
        if (NVT_IAC == pPayload[loop])
        {
            *(p++) = NVT_IAC;
        }
        *(p++) = pPayload[loop];
    }
    *(p++) = NVT_IAC;
    *(p++) = NVT_SE;

    const size_t length = p - pSB;
    queue_write_LEN(d, pSB, length);

    if (pSB != buffer)
    {
        MEMFREE(pSB);
    }
}

/*! \brief Transmit a Telnet WILL sequence for the given option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
static void send_will(DESC *d, unsigned char chOption)
{
    UTF8 aWill[3] = { NVT_IAC, NVT_WILL, 0 };
    aWill[2] = chOption;
    queue_write_LEN(d, aWill, sizeof(aWill));
}

/*! \brief Transmit a Telnet DONT sequence for the given option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
static void send_dont(DESC *d, unsigned char chOption)
{
    UTF8 aDont[3] = { NVT_IAC, NVT_DONT, 0 };
    aDont[2] = chOption;
    queue_write_LEN(d, aDont, sizeof(aDont));
}

/*! \brief Transmit a Telnet DO sequence for the given option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
static void send_do(DESC *d, unsigned char chOption)
{
    UTF8 aDo[3]   = { NVT_IAC, NVT_DO,   0 };
    aDo[2] = chOption;
    queue_write_LEN(d, aDo, sizeof(aDo));
}

/*! \brief Transmit a Telnet WONT sequence for the given option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
static void send_wont(DESC *d, unsigned char chOption)
{
    unsigned char aWont[3] = { NVT_IAC, NVT_WONT, 0 };
    aWont[2] = chOption;
    queue_write_LEN(d, aWont, sizeof(aWont));
}

/*! \brief Return the other side's negotiation state.
 *
 * The negotiation of each optional feature of telnet can be in one of six
 * states (defined in interface.h): OPTION_NO, OPTION_YES,
 * OPTION_WANTNO_EMPTY, OPTION_WANTNO_OPPOSITE, OPTION_WANTYES_EMPTY, and
 * OPTION_WANTYES_OPPOSITE.
 *
 * An option is only enabled when it is in the OPTION_YES state.
 *
 * \param d        Player connection context.
 * \param chOption Telnet Option
 * \return         One of six states.
 */

int him_state(const DESC *d, const unsigned char chOption)
{
    return d->nvt_him_state[chOption];
}

/*! \brief Return our side's negotiation state.
 *
 * The negotiation of each optional feature of telnet can be in one of six
 * states (defined in interface.h): OPTION_NO, OPTION_YES,
 * OPTION_WANTNO_EMPTY, OPTION_WANTNO_OPPOSITE, OPTION_WANTYES_EMPTY, and
 * OPTION_WANTYES_OPPOSITE.
 *
 * An option is only enabled when it is in the OPTION_YES state.
 *
 * \param d        Player connection context.
 * \param chOption Telnet Option
 * \return         One of six states.
 */

int us_state(const DESC *d, const unsigned char chOption)
{
    return d->nvt_us_state[chOption];
}

void send_charset_request(DESC *d, bool fDefacto = false)
{
    if (  OPTION_YES == d->nvt_us_state[(unsigned char)TELNET_CHARSET]
       || (  fDefacto
          && OPTION_YES == d->nvt_him_state[(unsigned char)TELNET_CHARSET]))
    {
        const unsigned char aCharsets[] = ";UTF-8;ISO-8859-1;ISO-8859-2;US-ASCII;CP437";
        send_sb(d, TELNET_CHARSET, TELNETSB_REQUEST, aCharsets, sizeof(aCharsets)-1);
    }
}

void defacto_charset_check(DESC *d)
{
    if (  nullptr != d->ttype
       && OPTION_NO == d->nvt_us_state[static_cast<unsigned char>(TELNET_CHARSET)]
       && OPTION_YES == d->nvt_him_state[static_cast<unsigned char>(TELNET_CHARSET)]
       && mux_stricmp(d->ttype, T("mushclient")) == 0)
    {
        send_charset_request(d, true);
    }
}

/*! \brief Change the other side's negotiation state.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option
 * \param iHimState One of the six option negotiation states.
 */
static void set_him_state(DESC *d, unsigned char chOption, int iHimState)
{
    d->nvt_him_state[chOption] = iHimState;

    if (OPTION_YES == iHimState)
    {
        if (TELNET_TTYPE == chOption)
        {
            send_sb(d, chOption, TELNETSB_SEND);
        }
        else if (TELNET_ENV == chOption)
        {
            // Request environment variables.
            //
            const unsigned char aEnvReq[2] = { TELNETSB_VAR, TELNETSB_USERVAR };
            send_sb(d, chOption, TELNETSB_SEND, aEnvReq, 2);
        }
#ifdef UNIX_SSL
        else if ((TELNET_STARTTLS == chOption) && (tls_ctx != nullptr))
        {
            send_sb(d, TELNET_STARTTLS, TELNETSB_FOLLOWS);
        }
#endif
        else if (TELNET_BINARY == chOption)
        {
            enable_us(d, TELNET_BINARY);
        }
        else if (TELNET_CHARSET == chOption)
        {
            defacto_charset_check(d);
        }
    }
    else if (OPTION_NO == iHimState)
    {
        if (TELNET_BINARY == chOption)
        {
            disable_us(d, TELNET_BINARY);
        }
    }
}

/*! \brief Change our side's negotiation state.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \param iUsState  One of the six option negotiation states.
 */
static void set_us_state(DESC *d, unsigned char chOption, int iUsState)
{
    d->nvt_us_state[chOption] = iUsState;

    if (OPTION_YES == iUsState)
    {
        if (TELNET_EOR == chOption)
        {
            enable_us(d, TELNET_SGA);
        }
        else if (TELNET_CHARSET == chOption)
        {
            send_charset_request(d);
        }
    }
    else if (OPTION_NO == iUsState)
    {
        if (TELNET_EOR == chOption)
        {
            disable_us(d, TELNET_SGA);
        }
        else if (TELNET_CHARSET == chOption)
        {
            defacto_charset_check(d);
        }
    }
}

/*! \brief Determine whether we want a particular option on his side of the
 * link to be enabled.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \return          Yes if we want it enabled.
 */

static bool desired_him_option(DESC *d, unsigned char chOption)
{
    UNUSED_PARAMETER(d);

    if (  TELNET_NAWS    == chOption
       || TELNET_EOR     == chOption
       || TELNET_SGA     == chOption
       || TELNET_ENV     == chOption
       || TELNET_BINARY  == chOption
#ifdef UNIX_SSL
       || ((TELNET_STARTTLS== chOption) && (tls_ctx != nullptr))
#endif
       || TELNET_CHARSET == chOption)
    {
        return true;
    }
    return false;
}

/*! \brief Determine whether we want a particular option on our side of the
 * link to be enabled.
 *
 * It doesn't make sense for NAWS to be enabled on the server side, and we
 * only negotiate SGA on our side if we have already successfully negotiated
 * the EOR option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \return          Yes if we want it enabled.
 */

static bool desired_us_option(DESC *d, unsigned char chOption)
{
    return TELNET_EOR == chOption || TELNET_BINARY == chOption || TELNET_CHARSET == chOption || (TELNET_SGA == chOption
        && OPTION_YES == us_state(d, TELNET_EOR));
}

/*! \brief Start the process of negotiating the enablement of an option on
 * his side.
 *
 * Whether we actually send anything across the wire to enable this depends
 * on the negotiation state. The option could potentially already be enabled.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
void enable_him(DESC *d, unsigned char chOption)
{
    switch (him_state(d, chOption))
    {
    case OPTION_NO:
        set_him_state(d, chOption, OPTION_WANTYES_EMPTY);
        send_do(d, chOption);
        break;

    case OPTION_WANTNO_EMPTY:
        set_him_state(d, chOption, OPTION_WANTNO_OPPOSITE);
        break;

    case OPTION_WANTYES_OPPOSITE:
        set_him_state(d, chOption, OPTION_WANTYES_EMPTY);
        break;
    }
}

/*! \brief Start the process of negotiating the disablement of an option on
 * his side.
 *
 * Whether we actually send anything across the wire to disable this depends
 * on the negotiation state. The option could potentially already be disabled.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
void disable_him(DESC *d, unsigned char chOption)
{
    switch (him_state(d, chOption))
    {
    case OPTION_YES:
        set_him_state(d, chOption, OPTION_WANTNO_EMPTY);
        send_dont(d, chOption);
        break;

    case OPTION_WANTNO_OPPOSITE:
        set_him_state(d, chOption, OPTION_WANTNO_EMPTY);
        break;

    case OPTION_WANTYES_EMPTY:
        set_him_state(d, chOption, OPTION_WANTYES_OPPOSITE);
        break;
    }
}

/*! \brief Start the process of negotiating the enablement of an option on
 * our side.
 *
 * Whether we actually send anything across the wire to enable this depends
 * on the negotiation state. The option could potentially already be enabled.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
void enable_us(DESC *d, unsigned char chOption)
{
    switch (him_state(d, chOption))
    {
    case OPTION_NO:
        set_us_state(d, chOption, OPTION_WANTYES_EMPTY);
        send_will(d, chOption);
        break;

    case OPTION_WANTNO_EMPTY:
        set_us_state(d, chOption, OPTION_WANTNO_OPPOSITE);
        break;

    case OPTION_WANTYES_OPPOSITE:
        set_us_state(d, chOption, OPTION_WANTYES_EMPTY);
        break;
    }
}

/*! \brief Start the process of negotiating the disablement of an option on
 * our side.
 *
 * Whether we actually send anything across the wire to disable this depends
 * on the negotiation state. The option could potentially already be disabled.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 */
void disable_us(DESC *d, unsigned char chOption)
{
    switch (him_state(d, chOption))
    {
    case OPTION_YES:
        set_us_state(d, chOption, OPTION_WANTNO_EMPTY);
        send_wont(d, chOption);
        break;

    case OPTION_WANTNO_OPPOSITE:
        set_us_state(d, chOption, OPTION_WANTNO_EMPTY);
        break;

    case OPTION_WANTYES_EMPTY:
        set_us_state(d, chOption, OPTION_WANTYES_OPPOSITE);
        break;
    }
}

/*! \brief Begin initial telnet negotiations on a socket.
 *
 * The two sides of the connection may not agree on the following set of
 * options, and keep in mind that the successful negotiation of a particular
 * option may cause the negotiation of another option.
 *
 * Without this function, we are only react to client requests.
 *
 * \param d        Player connection on which the input arrived.
 */

/*! \brief Parse raw data from network connection into command lines and
 * Telnet indications.
 *
 * Once input has been received from a particular socket, it is given to this
 * function for initial parsing. While most clients do line editing on their
 * side, a raw telnet client is still capable of sending backspace (BS) and
 * Delete (DEL) to the server, so we perform basic editing on our side.
 *
 * TinyMUX only allows printable characters through, imposes a maximum line
 * length, and breaks lines at CRLF.
 *
 * \param d        Player connection on which the input arrived.
 * \param pBytes   Point to received bytes.
 * \param nBytes   Number of received bytes in above buffer.
 */
void process_input_helper(DESC *d, char *pBytes, int nBytes)
{
    char szUTF8[] = "UTF-8";
    char szISO8859_1[] = "ISO-8859-1";
    char szISO8859_2[] = "ISO-8859-2";
    char szCp437[] = "CP437";
    char szUSASCII[] = "US-ASCII";
    constexpr size_t nUTF8 = sizeof(szUTF8) - 1;
    constexpr size_t nISO8859_1 = sizeof(szISO8859_1) - 1;
    constexpr size_t nISO8859_2 = sizeof(szISO8859_2) - 1;
    constexpr size_t nCp437 = sizeof(szCp437) - 1;
    constexpr size_t nUSASCII = sizeof(szUSASCII) - 1;

    if (!d->raw_input)
    {
        d->raw_input = reinterpret_cast<command_block *>(alloc_lbuf("process_input.raw"));
        d->raw_input_at = d->raw_input->cmd;
    }

    size_t nInputBytes = 0;
    size_t nLostBytes  = 0;

    auto p    = d->raw_input_at;
    auto pend = d->raw_input->cmd + (LBUF_SIZE - sizeof(command_block_header) - 1);

    auto q    = d->aOption + d->nOption;
    const auto qend = d->aOption + SBUF_SIZE - 1;

    auto n = nBytes;
    while (n--)
    {
        const auto ch = static_cast<unsigned char>(*pBytes);
        const auto iAction = nvt_input_action_table[d->raw_input_state][nvt_input_xlat_table[ch]];
        switch (iAction)
        {
        case 1:
            // Action 1 - Accept CHR(X).
            //
            if (CHARSET_UTF8 == d->encoding)
            {
                // Execute UTF-8 state machine.
                //
                auto iColumn = cl_print_itt[static_cast<unsigned char>(ch)];
                auto iOffset = cl_print_sot[d->raw_codepoint_state];
                for (;;)
                {
                    int y = static_cast<char>(cl_print_sbt[iOffset]);
                    if (0 < y)
                    {
                        // RUN phrase.
                        //
                        if (iColumn < y)
                        {
                            d->raw_codepoint_state = cl_print_sbt[iOffset+1];
                            break;
                        }
                        else
                        {
                            iColumn = static_cast<unsigned char>(iColumn - y);
                            iOffset += 2;
                        }
                    }
                    else
                    {
                        // COPY phrase.
                        //
                        y = -y;
                        if (iColumn < y)
                        {
                            d->raw_codepoint_state = cl_print_sbt[iOffset+iColumn+1];
                            break;
                        }
                        else
                        {
                            iColumn = static_cast<unsigned char>(iColumn - y);
                            iOffset = static_cast<unsigned short>(iOffset + y + 1);
                        }
                    }
                }

                if (  1 == d->raw_codepoint_state - CL_PRINT_ACCEPTING_STATES_START
                   && p < pend)
                {
                    // Save the byte and reset the state machine.  This is
                    // the most frequently-occuring case.
                    //
                    *p++ = ch;
                    nInputBytes += d->raw_codepoint_length + 1;
                    d->raw_codepoint_length = 0;
                    d->raw_codepoint_state = CL_PRINT_START_STATE;
                }
                else if (  d->raw_codepoint_state < CL_PRINT_ACCEPTING_STATES_START
                        && p < pend)
                {
                    // Save the byte and we're done for now.
                    //
                    *p++ = ch;
                    d->raw_codepoint_length++;
                }
                else
                {
                    // The code point is not printable or there isn't enough room.
                    // Back out any bytes in this code point.
                    //
                    if (pend <= p)
                    {
                        nLostBytes += d->raw_codepoint_length + 1;
                    }

                    p -= d->raw_codepoint_length;
                    if (p < d->raw_input->cmd)
                    {
                        p = d->raw_input->cmd;
                    }
                    d->raw_codepoint_length = 0;
                    d->raw_codepoint_state = CL_PRINT_START_STATE;
                }
            }
            else if (CHARSET_LATIN1 == d->encoding)
            {
                // CHARSET_LATIN1
                //
                if (mux_isprint_latin1(ch))
                {
                    // Convert this latin1 character to the internal UTF-8 form.
                    //
                    auto pUTF = latin1_utf8(ch);
                    UTF8 nUTF = utf8_FirstByte[pUTF[0]];

                    if (p + nUTF < pend)
                    {
                        nInputBytes += nUTF;
                        while (nUTF--)
                        {
                            *p++ = *pUTF++;
                        }
                    }
                    else
                    {
                        nLostBytes += nUTF;
                    }
                }
            }
            else if (CHARSET_LATIN2 == d->encoding)
            {
                // CHARSET_LATIN2
                //
                if (mux_isprint_latin2(ch))
                {
                    // Convert this latin2 character to the internal UTF-8 form.
                    //
                    auto pUTF = latin2_utf8(ch);
                    UTF8 nUTF = utf8_FirstByte[pUTF[0]];

                    if (p + nUTF < pend)
                    {
                        nInputBytes += nUTF;
                        while (nUTF--)
                        {
                            *p++ = *pUTF++;
                        }
                    }
                    else
                    {
                        nLostBytes += nUTF;
                    }
                }
            }
            else if (CHARSET_CP437 == d->encoding)
            {
                // CHARSET_CP437
                //
                if (mux_isprint_cp437(ch))
                {
                    // Convert this cp437 character to the internal UTF-8 form.
                    //
                    auto pUTF = cp437_utf8(ch);
                    UTF8 nUTF = utf8_FirstByte[pUTF[0]];

                    if (p + nUTF < pend)
                    {
                        nInputBytes += nUTF;
                        while (nUTF--)
                        {
                            *p++ = *pUTF++;
                        }
                    }
                    else
                    {
                        nLostBytes += nUTF;
                    }
                }
            }
            else if (CHARSET_ASCII == d->encoding)
            {
                // CHARSET_ASCII
                //
                if (mux_isprint_ascii(ch))
                {
                    if (p < pend)
                    {
                        *p++ = ch;
                        nInputBytes++;
                    }
                    else
                    {
                        nLostBytes++;
                    }
                }
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 0:
            // Action 0 - Nothing.
            //
            break;

        case 2:
            // Action 2 - Erase Character.
            //
            if (  CHARSET_UTF8 == d->encoding
               && 0 < d->raw_codepoint_length)
            {
                p -= d->raw_codepoint_length;
                if (p < d->raw_input->cmd)
                {
                    p = d->raw_input->cmd;
                }
                d->raw_codepoint_length = 0;
                d->raw_codepoint_state = CL_PRINT_START_STATE;
            }

            if (NVT_DEL == ch)
            {
                queue_string(d, T("\b \b"));
            }
            else
            {
                queue_string(d, T(" \b"));
            }

            // Rewind until we pass the first byte of a UTF-8 sequence.
            //
            while (d->raw_input->cmd < p)
            {
                nInputBytes--;
                p--;
                if (utf8_FirstByte[static_cast<UTF8>(*p)] < UTF8_CONTINUE)
                {
                    break;
                }
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 3:
            // Action  3 - Accept Line.
            //
            if (  CHARSET_UTF8 == d->encoding
               && 0 < d->raw_codepoint_length)
            {
                p -= d->raw_codepoint_length;
                if (p < d->raw_input->cmd)
                {
                    p = d->raw_input->cmd;
                }
                d->raw_codepoint_length = 0;
                d->raw_codepoint_state = CL_PRINT_START_STATE;
            }

            *p = '\0';
            if (d->raw_input->cmd < p)
            {
                save_command(d, d->raw_input);
                d->raw_input = reinterpret_cast<command_block *>(alloc_lbuf("process_input.raw"));

                p = d->raw_input_at = d->raw_input->cmd;
                pend = d->raw_input->cmd + (LBUF_SIZE - sizeof(command_block_header) - 1);
            }
            break;

        case 4:
            // Action 4 - Transition to the Normal state.
            //
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 5:
            // Action  5 - Transition to Have_IAC state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC;
            break;

        case 6:
            // Action 6 - Transition to the Have_IAC_WILL state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_WILL;
            break;

        case 7:
            // Action  7 - Transition to the Have_IAC_DONT state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_DONT;
            break;

        case 8:
            // Action  8 - Transition to the Have_IAC_DO state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_DO;
            break;

        case 9:
            // Action  9 - Transition to the Have_IAC_WONT state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_WONT;
            break;

        case 10:
            // Action 10 - Transition to the Have_IAC_SB state.
            //
            q = d->aOption;
            d->raw_input_state = NVT_IS_HAVE_IAC_SB;
            break;

        case 11:
            // Action 11 - Transition to the Have_IAC_SB_IAC state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_SB_IAC;
            break;

        case 12:
            // Action 12 - Respond to IAC AYT and return to the Normal state.
            //
            queue_string(d, T("\r\n[Yes]\r\n"));
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 13:
            // Action 13 - Respond to IAC WILL X
            //
            switch (him_state(d, ch))
            {
            case OPTION_NO:
                if (desired_him_option(d, ch))
                {
                    set_him_state(d, ch, OPTION_YES);
                    send_do(d, ch);
                }
                else
                {
                    send_dont(d, ch);
                }
                break;

            case OPTION_WANTNO_EMPTY:
                set_him_state(d, ch, OPTION_NO);
                break;

            case OPTION_WANTYES_OPPOSITE:
                set_him_state(d, ch, OPTION_WANTNO_EMPTY);
                send_dont(d, ch);
                break;

            default:
                set_him_state(d, ch, OPTION_YES);
                break;
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 14:
            // Action 14 - Respond to IAC DONT X
            //
            switch (us_state(d, ch))
            {
            case OPTION_YES:
                set_us_state(d, ch, OPTION_NO);
                send_wont(d, ch);
                break;

            case OPTION_WANTNO_OPPOSITE:
                set_us_state(d, ch, OPTION_WANTYES_EMPTY);
                send_will(d, ch);
                break;

            default:
                set_us_state(d, ch, OPTION_NO);
                break;
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 15:
            // Action 15 - Respond to IAC DO X
            //
            switch (us_state(d, ch))
            {
            case OPTION_NO:
                if (desired_us_option(d, ch))
                {
                    set_us_state(d, ch, OPTION_YES);
                    send_will(d, ch);
                }
                else
                {
                    send_wont(d, ch);
                }
                break;

            case OPTION_WANTNO_EMPTY:
                set_us_state(d, ch, OPTION_NO);
                break;

            case OPTION_WANTYES_OPPOSITE:
                set_us_state(d, ch, OPTION_WANTNO_EMPTY);
                send_wont(d, ch);
                break;

            default:
                set_us_state(d, ch, OPTION_YES);
                break;
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 16:
            // Action 16 - Respond to IAC WONT X
            //
            switch (him_state(d, ch))
            {
            case OPTION_NO:
                break;

            case OPTION_YES:
                set_him_state(d, ch, OPTION_NO);
                send_dont(d, ch);
                break;

            case OPTION_WANTNO_OPPOSITE:
                set_him_state(d, ch, OPTION_WANTYES_EMPTY);
                send_do(d, ch);
                break;

            default:
                set_him_state(d, ch, OPTION_NO);
                break;
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 17:
            // Action 17 - Accept CHR(X) for Sub-Option (and transition to Have_IAC_SB state).
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_SB;
            if (  d->aOption <= q
               && q < qend)
            {
                *q++ = ch;
            }
            break;

        case 18:
            // Action 18 - Accept Completed Sub-option and transition to Normal state.
            //
            if (  d->aOption < q
               && q < qend)
            {
                const size_t m = q - d->aOption;
                switch (d->aOption[0])
                {
                case TELNET_NAWS:
                    if (5 == m)
                    {
                        d->width  = (d->aOption[1] << 8 ) | d->aOption[2];
                        d->height = (d->aOption[3] << 8 ) | d->aOption[4];
                    }
                    break;

#ifdef UNIX_SSL
                case TELNET_STARTTLS:
                    if (  2 == m
                       && TELNETSB_FOLLOWS == d->aOption[1]
                       && tls_ctx != nullptr)
                    {
                       d->ssl_session = SSL_new(tls_ctx);
                       SSL_set_fd(d->ssl_session, d->socket);
                       d->ss = SocketState::SSLAcceptAgain;
                    }
                    break;
#endif

                case TELNET_TTYPE:
                    if (  2 <= m
                       && TELNETSB_IS == d->aOption[1])
                    {
                        // Skip past the TTYPE and TELQUAL_IS bytes validating
                        // that terminal type information is an NVT ASCII
                        // string.
                        //
                        const auto nTermType = m-2;
                        const auto pTermType = &d->aOption[2];

                        auto fASCII = true;
                        for (size_t i = 0; i < nTermType; i++)
                        {
                            if (!mux_isprint_ascii(pTermType[i]))
                            {
                                fASCII = false;
                                break;
                            }
                        }

                        if (fASCII)
                        {
                            if (nullptr != d->ttype)
                            {
                                MEMFREE(d->ttype);
                                d->ttype = nullptr;
                            }
                            d->ttype = static_cast<UTF8 *>(MEMALLOC(nTermType+1));
                            memcpy(d->ttype, pTermType, nTermType);
                            d->ttype[nTermType] = '\0';

                            defacto_charset_check(d);
                        }
                    }
                    break;

                case TELNET_ENV:
                case TELNET_OLDENV:
                    if (  2 <= m
                       && (  TELNETSB_IS == d->aOption[1]
                          || TELNETSB_INFO == d->aOption[1]))
                    {
                        auto envPtr = &d->aOption[2];
                        while (envPtr < &d->aOption[m])
                        {
                            auto ch2 = *envPtr++;
                            if (  TELNETSB_USERVAR == ch2
                               || TELNETSB_VAR     == ch2)
                            {
                                const auto pVarnameStart = envPtr;
                                unsigned char *pVarnameEnd = nullptr;
                                unsigned char *pVarvalStart = nullptr;
                                unsigned char *pVarvalEnd = nullptr;

                                while (envPtr < &d->aOption[m])
                                {
                                    ch2 = *envPtr++;
                                    if (TELNETSB_VALUE == ch2)
                                    {
                                        pVarnameEnd = envPtr - 1;
                                        pVarvalStart = envPtr;

                                        while (envPtr < &d->aOption[m])
                                        {
                                            ch2 = *envPtr++;
                                            if (  TELNETSB_USERVAR == ch2
                                               || TELNETSB_VAR == ch2)
                                            {
                                                pVarvalEnd = envPtr - 1;
                                                break;
                                            }
                                        }

                                        if (envPtr == &d->aOption[m])
                                        {
                                            pVarvalEnd = envPtr;
                                        }
                                        break;
                                    }
                                }

                                if (  envPtr == &d->aOption[m]
                                   && nullptr == pVarnameEnd)
                                {
                                    pVarnameEnd = envPtr;
                                }

                                size_t nVarname = 0;
                                size_t nVarval = 0;

                                if (  nullptr != pVarnameStart
                                   && nullptr != pVarnameEnd)
                                {
                                    nVarname = pVarnameEnd - pVarnameStart;
                                }

                                if (  nullptr != pVarvalStart
                                   && nullptr != pVarvalEnd)
                                {
                                    nVarval = pVarvalEnd - pVarvalStart;
                                }

                                UTF8 varname[1024];
                                UTF8 varval[1024];
                                if (  nullptr != pVarvalStart
                                   && 0 < nVarname
                                   && nVarname < sizeof(varname) - 1
                                   && 0 < nVarval
                                   && nVarval < sizeof(varval) - 1)
                                {
                                    memcpy(varname, pVarnameStart, nVarname);
                                    varname[nVarname] = '\0';
                                    memcpy(varval, pVarvalStart, nVarval);
                                    varval[nVarval] = '\0';

                                    // This is a horrible, horrible nasty hack
                                    // to try and detect UTF8.  We do not even
                                    // try to figure out the other encodings
                                    // this way, and just default to Latin1 if
                                    // we can't get a UTF8 locale.
                                    //
                                    if (  mux_stricmp(varname, T("LC_CTYPE")) == 0
                                       || mux_stricmp(varname, T("LC_ALL")) == 0
                                       || mux_stricmp(varname, T("LANG")) == 0)
                                    {
                                        auto pEncoding = reinterpret_cast<UTF8 *>(strchr(reinterpret_cast<char *>(varval), '.'));
                                        if (nullptr != pEncoding)
                                        {
                                            pEncoding++;
                                        }
                                        else
                                        {
                                            pEncoding = &varval[0];
                                        }

                                        if (  mux_stricmp(pEncoding, T("utf-8")) == 0
                                           && CHARSET_UTF8 != d->encoding)
                                        {
                                            // Since we are changing to the
                                            // UTF-8 character set, the
                                            // printable state machine needs
                                            // to be initialized.
                                            //
                                            d->encoding = CHARSET_UTF8;
                                            d->negotiated_encoding = CHARSET_UTF8;
                                            d->raw_codepoint_state = CL_PRINT_START_STATE;

                                            enable_us(d, TELNET_BINARY);
                                            enable_him(d, TELNET_BINARY);
                                        }
                                    }
                                    else if (mux_stricmp(varname, T("USER")) == 0)
                                    {
                                        memcpy(d->username, varval, nVarval + 1);
                                    }

                                    // We can also get 'DISPLAY' here if we were
                                    // feeling masochistic, and actually use
                                    // Xterm functionality.
                                }
                            }
                        }
                    }
                    break;

                case TELNET_CHARSET:
                    if (2 <= m)
                    {
                        if (TELNETSB_ACCEPT == d->aOption[1])
                        {
                            const auto pCharset = &d->aOption[2];

                            if (  nUTF8 == m - 2
                               && memcmp(reinterpret_cast<char *>(pCharset), szUTF8, nUTF8) == 0)
                            {
                                if (CHARSET_UTF8 != d->encoding)
                                {
                                    // Since we are changing to the UTF-8
                                    // character set, the printable state machine
                                    // needs to be initialized.
                                    //
                                    d->encoding = CHARSET_UTF8;
                                    d->negotiated_encoding = CHARSET_UTF8;
                                    d->raw_codepoint_state = CL_PRINT_START_STATE;

                                    enable_us(d, TELNET_BINARY);
                                    enable_him(d, TELNET_BINARY);
                                }
                            }
                            else if (  nISO8859_1 == m - 2
                                    && memcmp(reinterpret_cast<char *>(pCharset), szISO8859_1, nISO8859_1) == 0)
                            {
                                d->encoding = CHARSET_LATIN1;
                                d->negotiated_encoding = CHARSET_LATIN1;

                                enable_us(d, TELNET_BINARY);
                                enable_him(d, TELNET_BINARY);
                            }
                            else if (  nISO8859_2 == m - 2
                                    && memcmp(reinterpret_cast<char *>(pCharset), szISO8859_2, nISO8859_2) == 0)
                            {
                                d->encoding = CHARSET_LATIN2;
                                d->negotiated_encoding = CHARSET_LATIN2;

                                enable_us(d, TELNET_BINARY);
                                enable_him(d, TELNET_BINARY);
                            }
                            else if (  nCp437 == m - 2
                                    && memcmp(reinterpret_cast<char *>(pCharset), szCp437, nCp437) == 0)
                            {
                                d->encoding = CHARSET_CP437;
                                d->negotiated_encoding = CHARSET_CP437;

                                enable_us(d, TELNET_BINARY);
                                enable_him(d, TELNET_BINARY);
                            }
                            else if (  nUSASCII == m - 2
                                    && memcmp(reinterpret_cast<char *>(pCharset), szUSASCII, nUSASCII) == 0)
                            {
                                d->encoding = CHARSET_ASCII;
                                d->negotiated_encoding = CHARSET_ASCII;

                                disable_us(d, TELNET_BINARY);
                                disable_him(d, TELNET_BINARY);
                            }
                        }
                        else if (TELNETSB_REJECT == d->aOption[1])
                        {
                            // The client has replied that it doesn't even support
                            // Latin1/ISO-8859-1 accented characters.  Thus, we
                            // should probably record this to strip out any
                            // accents.
                            //
                            d->encoding = CHARSET_ASCII;
                            d->negotiated_encoding = CHARSET_ASCII;

                            disable_us(d, TELNET_BINARY);
                            disable_him(d, TELNET_BINARY);
                        }
                        else if (TELNETSB_REQUEST == d->aOption[1])
                        {
                            auto fRequestAcknowledged = false;
                            auto reqPtr = &d->aOption[2];
                            if (reqPtr < &d->aOption[m])
                            {
                                // NVT_IAC is not permitted as a separator.
                                // '[' might be the beginning of "[TTABLE]"
                                // <version>, but we don't support parsing
                                // and ignoring that.
                                //
                                auto chSep = *reqPtr++;
                                if (  NVT_IAC != chSep
                                   && '[' != chSep)
                                {
                                    auto pTermStart = reqPtr;

                                    while (reqPtr < &d->aOption[m])
                                    {
                                        auto ch3 = *reqPtr++;
                                        if (  chSep == ch3
                                           || reqPtr == &d->aOption[m])
                                        {
                                            const size_t nTerm = reqPtr - pTermStart - 1;

                                            // Process [pTermStart, pTermStart+nTermEnd)
                                            // We let the client determine priority by its order of the list.
                                            //
                                            if (  nUTF8 == nTerm
                                               && memcmp(reinterpret_cast<char *>(pTermStart), szUTF8, nUTF8) == 0)
                                            {
                                                send_sb(d, TELNET_CHARSET, TELNETSB_ACCEPT, pTermStart, nTerm);
                                                fRequestAcknowledged = true;
                                                if (CHARSET_UTF8 != d->encoding)
                                                {
                                                    // Since we are changing to the UTF-8
                                                    // character set, the printable state machine
                                                    // needs to be initialized.
                                                    //
                                                    d->encoding = CHARSET_UTF8;
                                                    d->negotiated_encoding = CHARSET_UTF8;
                                                    d->raw_codepoint_state = CL_PRINT_START_STATE;

                                                    enable_us(d, TELNET_BINARY);
                                                    enable_him(d, TELNET_BINARY);
                                                }
                                                break;
                                            }
                                            else if (  nISO8859_1 == nTerm
                                                    && memcmp(reinterpret_cast<char *>(pTermStart), szISO8859_1, nISO8859_1) == 0)
                                            {
                                                send_sb(d, TELNET_CHARSET, TELNETSB_ACCEPT, pTermStart, nTerm);
                                                fRequestAcknowledged = true;
                                                d->encoding = CHARSET_LATIN1;
                                                d->negotiated_encoding = CHARSET_LATIN1;

                                                enable_us(d, TELNET_BINARY);
                                                enable_him(d, TELNET_BINARY);
                                                break;
                                            }
                                            else if (  nISO8859_2 == nTerm
                                                    && memcmp(reinterpret_cast<char *>(pTermStart), szISO8859_2, nISO8859_2) == 0)
                                            {
                                                send_sb(d, TELNET_CHARSET, TELNETSB_ACCEPT, pTermStart, nTerm);
                                                fRequestAcknowledged = true;
                                                d->encoding = CHARSET_LATIN2;
                                                d->negotiated_encoding = CHARSET_LATIN2;

                                                enable_us(d, TELNET_BINARY);
                                                enable_him(d, TELNET_BINARY);
                                                break;
                                            }
                                            else if (  nCp437 == nTerm
                                                    && memcmp(reinterpret_cast<char *>(pTermStart), szCp437, nCp437) == 0)
                                            {
                                                send_sb(d, TELNET_CHARSET, TELNETSB_ACCEPT, pTermStart, nTerm);
                                                fRequestAcknowledged = true;
                                                d->encoding = CHARSET_CP437;
                                                d->negotiated_encoding = CHARSET_CP437;

                                                enable_us(d, TELNET_BINARY);
                                                enable_him(d, TELNET_BINARY);
                                                break;
                                            }
                                            else if (  nUSASCII== nTerm
                                                    && memcmp(reinterpret_cast<char *>(pTermStart), szUSASCII, nUSASCII) == 0)
                                            {
                                                fRequestAcknowledged = true;
                                                send_sb(d, TELNET_CHARSET, TELNETSB_ACCEPT, pTermStart, nTerm);
                                                d->encoding = CHARSET_ASCII;
                                                d->negotiated_encoding = CHARSET_ASCII;

                                                disable_us(d, TELNET_BINARY);
                                                disable_him(d, TELNET_BINARY);
                                                break;
                                            }
                                            pTermStart = reqPtr;
                                        }
                                    }
                                }
                            }

                            if (!fRequestAcknowledged)
                            {
                                send_sb(d, TELNET_CHARSET, TELNETSB_REJECT, nullptr, 0);
                            }
                        }
                    }
                }
            }
            q = d->aOption;
            d->raw_input_state = NVT_IS_NORMAL;
            break;
        }
        pBytes++;
    }

    if (  d->raw_input->cmd < p
       && p <= pend)
    {
        d->raw_input_at = p;
    }
    else
    {
        free_lbuf(d->raw_input);
        d->raw_input = nullptr;
        d->raw_input_at = nullptr;
    }

    if (  d->aOption <= q
       && q < qend)
    {
        d->nOption = q - d->aOption;
    }
    else
    {
        d->nOption = 0;
    }
    d->input_tot  += nBytes;
    d->input_size += nInputBytes;
    d->input_lost += nLostBytes;
}


void close_listening_ports(void)
{
    for (int i = 0; i < num_main_game_ports; i++)
    {
        if (0 == SOCKET_CLOSE(main_game_ports[i].socket))
        {
            DebugTotalSockets--;
        }
        main_game_ports[i].socket = INVALID_SOCKET;
    }
}


void close_sockets_emergency(const UTF8* message)
{
    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); ++it)
    {
        DESC* d = *it;
#ifdef UNIX_SSL
        if (d->ssl_session)
        {
            SSL_write(d->ssl_session, reinterpret_cast<const char*>(message), strlen(reinterpret_cast<const char*>(message)));
            SSL_shutdown(d->ssl_session);
            SSL_free(d->ssl_session);
            d->ssl_session = nullptr;
        }
        else
#endif
        {
            SOCKET_WRITE(d->socket, reinterpret_cast<const char*>(message), strlen(reinterpret_cast<const char*>(message)), 0);
        }

        if (IS_SOCKET_ERROR(shutdown(d->socket, SD_BOTH)))
        {
            log_perror(T("NET"), T("FAIL"), nullptr, T("shutdown"));
        }
        if (0 == SOCKET_CLOSE(d->socket))
        {
            DebugTotalSockets--;
        }
    }
    close_listening_ports();
}

void emergency_shutdown(void)
{
    close_sockets_emergency(T("Going down - Bye"));
}


// ---------------------------------------------------------------------------
// Signal handling routines.
//
#ifdef _SGI_SOURCE
#define CAST_SIGNAL_FUNC (SIG_PF)
#else // _SGI_SOURCE
#define CAST_SIGNAL_FUNC
#endif // _SGI_SOURCE

// The purpose of the following code is support the case where sys_siglist is
// is not part of the environment.  This is the case for some Unix platforms
// and also for Windows.
//
typedef struct
{
    int         iSignal;
    const UTF8 *szSignal;
} SIGNALTYPE, *PSIGNALTYPE;

const SIGNALTYPE aSigTypes[] =
{
#ifdef SIGHUP
    // Hangup detected on controlling terminal or death of controlling process.
    //
    { SIGHUP,   T("SIGHUP")},
#endif // SIGHUP
#ifdef SIGINT
    // Interrupt from keyboard.
    //
    { SIGINT,   T("SIGINT")},
#endif // SIGINT
#ifdef SIGQUIT
    // Quit from keyboard.
    //
    { SIGQUIT,  T("SIGQUIT")},
#endif // SIGQUIT
#ifdef SIGILL
    // Illegal Instruction.
    //
    { SIGILL,   T("SIGILL")},
#endif // SIGILL
#ifdef SIGTRAP
    // Trace/breakpoint trap.
    //
    { SIGTRAP,  T("SIGTRAP")},
#endif // SIGTRAP
#if defined(SIGABRT)
    // Abort signal from abort(3).
    //
    { SIGABRT,  T("SIGABRT")},
#elif defined(SIGIOT)
#define SIGABRT SIGIOT
    // Abort signal from abort(3).
    //
    { SIGIOT,   T("SIGIOT")},
#endif // SIGABRT
#ifdef SIGEMT
    { SIGEMT,   T("SIGEMT")},
#endif // SIGEMT
#ifdef SIGFPE
    // Floating-point exception.
    //
    { SIGFPE,   T("SIGFPE")},
#endif // SIGFPE
#ifdef SIGKILL
    // Kill signal. Not catchable.
    //
    { SIGKILL,  T("SIGKILL")},
#endif // SIGKILL
#ifdef SIGSEGV
    // Invalid memory reference.
    //
    { SIGSEGV,  T("SIGSEGV")},
#endif // SIGSEGV
#ifdef SIGPIPE
    // Broken pipe: write to pipe with no readers.
    //
    { SIGPIPE,  T("SIGPIPE")},
#endif // SIGPIPE
#ifdef SIGALRM
    // Timer signal from alarm(2).
    //
    { SIGALRM,  T("SIGALRM")},
#endif // SIGALRM
#ifdef SIGTERM
    // Termination signal.
    //
    { SIGTERM,  T("SIGTERM")},
#endif // SIGTERM
#ifdef SIGBREAK
    // Ctrl-Break.
    //
    { SIGBREAK, T("SIGBREAK")},
#endif // SIGBREAK
#ifdef SIGUSR1
    // User-defined signal 1.
    //
    { SIGUSR1,  T("SIGUSR1")},
#endif // SIGUSR1
#ifdef SIGUSR2
    // User-defined signal 2.
    //
    { SIGUSR2,  T("SIGUSR2")},
#endif // SIGUSR2
#if defined(SIGCHLD)
    // Child stopped or terminated.
    //
    { SIGCHLD,  T("SIGCHLD")},
#elif defined(SIGCLD)
#define SIGCHLD SIGCLD
    // Child stopped or terminated.
    //
    { SIGCLD,   T("SIGCLD")},
#endif // SIGCHLD
#ifdef SIGCONT
    // Continue if stopped.
    //
    { SIGCONT,  T("SIGCONT")},
#endif // SIGCONT
#ifdef SIGSTOP
    // Stop process. Not catchable.
    //
    { SIGSTOP,  T("SIGSTOP")},
#endif // SIGSTOP
#ifdef SIGTSTP
    // Stop typed at tty
    //
    { SIGTSTP,  T("SIGTSTP")},
#endif // SIGTSTP
#ifdef SIGTTIN
    // tty input for background process.
    //
    { SIGTTIN,  T("SIGTTIN")},
#endif // SIGTTIN
#ifdef SIGTTOU
    // tty output for background process.
    //
    { SIGTTOU,  T("SIGTTOU")},
#endif // SIGTTOU
#ifdef SIGBUS
    // Bus error (bad memory access).
    //
    { SIGBUS,   T("SIGBUS")},
#endif // SIGBUS
#ifdef SIGPROF
    // Profiling timer expired.
    //
    { SIGPROF,  T("SIGPROF")},
#endif // SIGPROF
#ifdef SIGSYS
    // Bad argument to routine (SVID).
    //
    { SIGSYS,   T("SIGSYS")},
#endif // SIGSYS
#ifdef SIGURG
    // Urgent condition on socket (4.2 BSD).
    //
    { SIGURG,   T("SIGURG")},
#endif // SIGURG
#ifdef SIGVTALRM
    // Virtual alarm clock (4.2 BSD).
    //
    { SIGVTALRM, T("SIGVTALRM")},
#endif // SIGVTALRM
#ifdef SIGXCPU
    // CPU time limit exceeded (4.2 BSD).
    //
    { SIGXCPU,  T("SIGXCPU")},
#endif // SIGXCPU
#ifdef SIGXFSZ
    // File size limit exceeded (4.2 BSD).
    //
    { SIGXFSZ,  T("SIGXFSZ")},
#endif // SIGXFSZ
#ifdef SIGSTKFLT
    // Stack fault on coprocessor.
    //
    { SIGSTKFLT, T("SIGSTKFLT")},
#endif // SIGSTKFLT
#if defined(SIGIO)
    // I/O now possible (4.2 BSD). File lock lost.
    //
    { SIGIO,    T("SIGIO")},
#elif defined(SIGPOLL)
#define SIGIO SIGPOLL
    // Pollable event (Sys V).
    //
    { SIGPOLL,  T("SIGPOLL")},
#endif // SIGIO
#ifdef SIGLOST
    { SIGLOST,  T("SIGLOST")},
#endif // SIGLOST
#if defined(SIGPWR)
    // Power failure (System V).
    //
    { SIGPWR,   T("SIGPWR")},
#elif defined(SIGINFO)
#define SIGPWR SIGINFO
    // Power failure (System V).
    //
    { SIGINFO,  T("SIGINFO")},
#endif // SIGPWR
#ifdef SIGWINCH
    // Window resize signal (4.3 BSD, Sun).
    //
    { SIGWINCH, T("SIGWINCH")},
#endif // SIGWINCH
    { 0,        T("SIGZERO") },
    { -1, nullptr }
};

typedef struct
{
    const UTF8 *pShortName;
    const UTF8 *pLongName;
} MUX_SIGNAMES;

static MUX_SIGNAMES signames[NSIG];

#if defined(HAVE_SYS_SIGNAME)
#define SysSigNames sys_signame
#elif defined(SYS_SIGLIST_DECLARED)
#define SysSigNames sys_siglist
#endif // HAVE_SYS_SIGNAME

void build_signal_names_table(void)
{
    int i;
    for (i = 0; i < NSIG; i++)
    {
        signames[i].pShortName = nullptr;
        signames[i].pLongName  = nullptr;
    }

    const SIGNALTYPE *pst = aSigTypes;
    while (pst->szSignal)
    {
        const auto sig = pst->iSignal;
        if (  0 <= sig
           && sig < NSIG)
        {
            MUX_SIGNAMES *tsn = &signames[sig];
            if (tsn->pShortName == nullptr)
            {
                tsn->pShortName = pst->szSignal;
#if defined(UNIX_SIGNALS)
                if (sig == SIGUSR1)
                {
                    tsn->pLongName = T("Restart server");
                }
                else if (sig == SIGUSR2)
                {
                    tsn->pLongName = T("Drop flatfile");
                }
#endif // UNIX_SIGNALS
#ifdef SysSigNames
                if (  tsn->pLongName == nullptr
                   && SysSigNames[sig]
                   && strcmp((char *)tsn->pShortName, (char *)SysSigNames[sig]) != 0)
                {
                    tsn->pLongName = (UTF8 *)SysSigNames[sig];
                }
#endif // SysSigNames
            }
        }
        pst++;
    }
    for (i = 0; i < NSIG; i++)
    {
        MUX_SIGNAMES *tsn = &signames[i];
        if (tsn->pShortName == nullptr)
        {
#ifdef SysSigNames
            if (SysSigNames[i])
            {
                tsn->pLongName = (UTF8 *)SysSigNames[i];
            }
#endif // SysSigNames

            // This is the only non-const memory case.
            //
            tsn->pShortName = StringClone(tprintf(T("SIG%03d"), i));
        }
    }
}

static void unset_signals(void)
{
    const SIGNALTYPE *pst = aSigTypes;
    while (pst->szSignal)
    {
        int sig = pst->iSignal;
        signal(sig, SIG_DFL);
    }
}

static void check_panicking(int sig)
{
    // If we are panicking, turn off signal catching and resignal.
    //
    if (mudstate.panicking)
    {
        unset_signals();

#ifdef WINDOWS_PROCESSES
        UNUSED_PARAMETER(sig);
        abort();
#endif // WINDOWS_PROCESSES

#ifdef UNIX_PROCESSES
        kill(game_pid, sig);
#endif // UNIX_PROCESSES
    }
    mudstate.panicking = true;
}

static UTF8 *signal_desc(const int iSignal)
{
    static UTF8 buff[LBUF_SIZE];
    auto bufc = buff;
    safe_str(signames[iSignal].pShortName, buff, &bufc);
    if (signames[iSignal].pLongName)
    {
        safe_str(T(" ("), buff, &bufc);
        safe_str(signames[iSignal].pLongName, buff, &bufc);
        safe_chr(')', buff, &bufc);
    }
    *bufc = '\0';
    return buff;
}

static void log_signal(const int iSignal)
{
    STARTLOG(LOG_PROBLEMS, T("SIG"), T("CATCH"));
    log_text(T("Caught signal "));
    log_text(signal_desc(iSignal));
    ENDLOG;
}

#if defined(UNIX_SIGNALS)

static void log_signal_ignore(int iSignal)
{
    STARTLOG(LOG_PROBLEMS, "SIG", "CATCH");
    log_text(T("Caught signal and ignored signal "));
    log_text(signal_desc(iSignal));
    log_text(T(" because server just came up."));
    ENDLOG;
}

void LogStatBuf(int stat_buf, const char *Name)
{
    STARTLOG(LOG_ALWAYS, "NET", Name);
    if (WIFEXITED(stat_buf))
    {
        Log.tinyprintf(T("process exited unexpectedly with exit status %d."), WEXITSTATUS(stat_buf));
    }
    else if (WIFSIGNALED(stat_buf))
    {
        Log.tinyprintf(T("process was terminated with signal %s."), signal_desc(WTERMSIG(stat_buf)));
    }
    else
    {
        log_text(T("process ended unexpectedly."));
    }
    ENDLOG;
}

#endif  // UNIX_SIGNALS

static void DCL_CDECL sighandler(int sig)
{
#if defined(UNIX_SIGNALS)
    int stat_buf;
    pid_t child;
#endif // UNIX_SIGNALS

    switch (sig)
    {
#if defined(UNIX_SIGNALS)
    case SIGUSR1:
        if (mudstate.bCanRestart)
        {
            log_signal(sig);
            do_restart(GOD, GOD, GOD, 0, 0);
        }
        else
        {
            log_signal_ignore(sig);
        }
        break;

    case SIGUSR2:

        // Drop a flatfile.
        //
        log_signal(sig);
        raw_broadcast(0, T("Caught signal %s requesting a flatfile @dump. Please wait."), signal_desc(sig));
        dump_database_internal(DUMP_I_SIGNAL);
        break;

    case SIGCHLD:

        // Change in child status.
        //
#ifndef SIGNAL_SIGCHLD_BRAINDAMAGE
        signal(SIGCHLD, CAST_SIGNAL_FUNC sighandler);
#endif // !SIGNAL_SIGCHLD_BRAINDAMAGE

        while ((child = waitpid(0, &stat_buf, WNOHANG)) > 0)
        {
#if defined(HAVE_WORKING_FORK)
            if (  WIFEXITED(stat_buf)
               || WIFSIGNALED(stat_buf))
            {
#ifdef STUB_SLAVE
                if (child == stubslave_pid)
                {
                    // The Stub slave process ended unexpectedly.
                    //
                    stubslave_pid = 0;

                    LogStatBuf(stat_buf, "STUB");

                    continue;
                }
                else
#endif // STUB_SLAVE
                if (  mudconf.fork_dump
                   && mudstate.dumping)
                {
                    mudstate.dumped = child;
                    if (mudstate.dumper == mudstate.dumped)
                    {
                        // The dumping process finished.
                        //
                        mudstate.dumper  = 0;
                        mudstate.dumped  = 0;
                    }
                    else
                    {
                        // The dumping process finished before we could
                        // obtain its process id from fork().
                        //
                    }
                    mudstate.dumping = false;
                    local_dump_complete_signal();
                    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
                    while (nullptr != p)
                    {
                        p->pSink->dump_complete_signal();
                        p = p->pNext;
                    }
                    continue;
                }
            }
#endif // HAVE_WORKING_FORK

            log_signal(sig);
            LogStatBuf(stat_buf, "UKNWN");

#if defined(HAVE_WORKING_FORK)
            STARTLOG(LOG_PROBLEMS, "SIG", "DEBUG");
#ifdef STUB_SLAVE
            Log.tinyprintf(T("mudstate.dumper=%d, child=%d, stubslave_pid=%d" ENDLINE),
                mudstate.dumper, child, stubslave_pid);
#else
            Log.tinyprintf(T("mudstate.dumper=%d, child=%d" ENDLINE),
                mudstate.dumper, child);
#endif // STUB_SLAVE
            ENDLOG;
#endif // HAVE_WORKING_FORK
        }
        break;

    case SIGHUP:

        // Perform a database dump.
        //
        log_signal(sig);
        extern void dispatch_DatabaseDump(void *pUnused, int iUnused);
        scheduler.CancelTask(dispatch_DatabaseDump, 0, 0);
        mudstate.dump_counter.GetUTC();
        scheduler.DeferTask(mudstate.dump_counter, PRIORITY_SYSTEM, dispatch_DatabaseDump, 0, 0);
        break;

#ifdef HAVE_SETITIMER
    case SIGPROF:

        // Softcode is running longer than is reasonable.  Apply the brakes.
        //
        log_signal(sig);
        alarm_clock.signal();
        break;
#endif

#endif // UNIX_SIGNALS

    case SIGINT:

        // Log + ignore
        //
        log_signal(sig);
        break;

#if defined(UNIX_SIGNALS)
    case SIGQUIT:
#endif // UNIX_SIGNALS
    case SIGTERM:
#ifdef SIGXCPU
    case SIGXCPU:
#endif // SIGXCPU
        // Time for a normal and short-winded shutdown.
        //
        check_panicking(sig);
        log_signal(sig);
        raw_broadcast(0, T("GAME: Caught signal %s, exiting."), signal_desc(sig));
        if ('\0' != mudconf.crash_msg[0])
        {
            raw_broadcast(0, T("GAME: %s"), mudconf.crash_msg);
        }
        mudstate.shutdown_flag = true;
        break;

    case SIGILL:
    case SIGFPE:
    case SIGSEGV:
#if defined(UNIX_SIGNALS)
    case SIGTRAP:
#ifdef SIGXFSZ
    case SIGXFSZ:
#endif // SIGXFSZ
#ifdef SIGEMT
    case SIGEMT:
#endif // SIGEMT
#ifdef SIGBUS
    case SIGBUS:
#endif // SIGBUS
#ifdef SIGSYS
    case SIGSYS:
#endif // SIGSYS
#endif // UNIX_SIGNALS

        // Panic save + restart.
        //
        Log.Flush();
        check_panicking(sig);
        log_signal(sig);
        report();

        local_presync_database_sigsegv();
        {
            ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
            while (nullptr != p)
            {
                p->pSink->presync_database_sigsegv();
                p = p->pNext;
            }
        }
#if defined(STUB_SLAVE)
        final_stubslave();
#endif // STUB_SLAVE
        final_modules();

#ifndef MEMORY_BASED
        al_store();
#endif
        pcache_sync();
        SYNC;

        if (  mudconf.sig_action != SA_EXIT
           && mudstate.bCanRestart)
        {
            raw_broadcast(0,
                    T("GAME: Fatal signal %s caught, restarting."), signal_desc(sig));

            if ('\0' != mudconf.crash_msg[0])
            {
                raw_broadcast(0, T("GAME: %s"), mudconf.crash_msg);
            }

            // There is no older DB. It's a fiction. Our only choice is
            // between unamed attributes and named ones. We go with what we
            // got.
            //
            dump_database_internal(DUMP_I_RESTART);
            SYNC;
            CLOSE;
#if defined(WINDOWS_PROCESSES)
            unset_signals();
            signal(sig, SIG_DFL);
            exit(12345678);
#endif // WINDOWS_PROCESSES

#if defined(UNIX_PROCESSES)
#if defined(HAVE_WORKING_FORK)
            // Try our best to dump a core first
            //
            if (!fork())
            {
                // We are the broken parent. Die.
                //
                unset_signals();
                exit(1);
            }

            // We are the reproduced child with a slightly better chance.
            //
            dump_restart_db();
#endif // HAVE_WORKING_FORK

#ifdef GAME_DOOFERMUX
            execl("bin/netmux", mudconf.mud_name, "-c", mudconf.config_file, "-p", mudconf.pid_file, "-e", mudconf.log_dir, (char *)nullptr);
#else // GAME_DOOFERMUX
            execl("bin/netmux", "netmux", "-c", mudconf.config_file, "-p", mudconf.pid_file, "-e", mudconf.log_dir, (char *)nullptr);
#endif // GAME_DOOFERMUX
            mux_assert(false);
            break;
#endif // UNIX_PROCESSES
        }
        else
        {
            unset_signals();
            signal(sig, SIG_DFL);
            exit(1);
        }
        break;

    case SIGABRT:

        // Coredump.
        //
        log_signal(sig);
        report();

        exit(1);
    }
    signal(sig, CAST_SIGNAL_FUNC sighandler);
    mudstate.panicking = false;
}

NAMETAB sigactions_nametab[] =
{
    {T("exit"),        3,  0,  SA_EXIT},
    {T("default"),     1,  0,  SA_DFLT},
    {static_cast<UTF8 *>(nullptr), 0,  0,  0}
};

void set_signals(void)
{
#if defined(UNIX_SIGNALS)
    sigset_t sigs;

    // We have to reset our signal mask, because of the possibility
    // that we triggered a restart on a SIGUSR1. If we did so, then
    // the signal became blocked, and stays blocked, since control
    // never returns to the caller; i.e., further attempts to send
    // a SIGUSR1 would fail.
    //
#undef sigfillset
#undef sigprocmask
    sigfillset(&sigs);
    sigprocmask(SIG_UNBLOCK, &sigs, nullptr);
#endif // UNIX_SIGNALS

    signal(SIGINT,  CAST_SIGNAL_FUNC sighandler);
    signal(SIGTERM, CAST_SIGNAL_FUNC sighandler);
    signal(SIGILL,  CAST_SIGNAL_FUNC sighandler);
    signal(SIGSEGV, CAST_SIGNAL_FUNC sighandler);
    signal(SIGABRT, CAST_SIGNAL_FUNC sighandler);
    signal(SIGFPE,  SIG_IGN);

#if defined(UNIX_SIGNALS)
    signal(SIGCHLD, CAST_SIGNAL_FUNC sighandler);
    signal(SIGHUP,  CAST_SIGNAL_FUNC sighandler);
    signal(SIGQUIT, CAST_SIGNAL_FUNC sighandler);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGUSR1, CAST_SIGNAL_FUNC sighandler);
    signal(SIGUSR2, CAST_SIGNAL_FUNC sighandler);
    signal(SIGTRAP, CAST_SIGNAL_FUNC sighandler);
    signal(SIGILL,  CAST_SIGNAL_FUNC sighandler);
#ifdef HAVE_SETITIMER
    signal(SIGPROF,  CAST_SIGNAL_FUNC sighandler);
#endif

#ifdef SIGXCPU
    signal(SIGXCPU, CAST_SIGNAL_FUNC sighandler);
#endif // SIGXCPU
#ifdef SIGFSZ
    signal(SIGXFSZ, CAST_SIGNAL_FUNC sighandler);
#endif // SIGFSZ
#ifdef SIGEMT
    signal(SIGEMT, CAST_SIGNAL_FUNC sighandler);
#endif // SIGEMT
#ifdef SIGBUS
    signal(SIGBUS, CAST_SIGNAL_FUNC sighandler);
#endif // SIGBUS
#ifdef SIGSYS
    signal(SIGSYS, CAST_SIGNAL_FUNC sighandler);
#endif // SIGSYS
#endif // UNIX_SIGNALS
}

void list_system_resources(dbref player)
{
    UTF8 buffer[80];

    int nTotal = 0;
    notify(player, T("System Resources"));

    mux_sprintf(buffer, sizeof(buffer), T("Total Open Files: %ld"), DebugTotalFiles);
    notify(player, buffer);
    nTotal += DebugTotalFiles;

    mux_sprintf(buffer, sizeof(buffer), T("Total Sockets: %ld"), DebugTotalSockets);
    notify(player, buffer);
    nTotal += DebugTotalSockets;

    mux_sprintf(buffer, sizeof(buffer), T("Total Handles (sum of above): %d"), nTotal);
    notify(player, buffer);
}


void site_mon_send(const SOCKET port, const UTF8 *address, DESC *d, const UTF8 *msg)
{
    int host_info = 0;
    if (nullptr != d)
    {
        host_info = mudstate.access_list.check(&d->address);
    }

    // Don't do sitemon for blocked sites.
    //
    if (host_info & HI_NOSITEMON)
    {
        return;
    }

    // Build the msg.
    //
    UTF8 *send_msg;
    const bool suspect = (0 != (host_info & HI_SUSPECT));
    if (IS_INVALID_SOCKET(port))
    {
        send_msg = tprintf(T("SITEMON: [UNKNOWN] %s from %s.%s"), msg, address,
            suspect ? T(" (SUSPECT)"): T(""));
    }
    else
    {
        send_msg = tprintf(T("SITEMON: [%d] %s from %s.%s"), port, msg,
            address, suspect ? T(" (SUSPECT)"): T(""));
    }

    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); )
    {
        DESC* nd = *it;
        ++it;
        if (nd->flags & DS_CONNECTED)
        {
            if (SiteMon(nd->player))
            {
                queue_string(nd, send_msg);
                queue_write_LEN(nd, T("\r\n"), 2);
                process_output(nd, false);
            }
        }
    }
}

#if defined(HAVE_IN_ADDR)
typedef struct
{
    int    nShift;
    UINT32 maxValue;
    size_t maxOctLen;
    size_t maxDecLen;
    size_t maxHexLen;
} DECODEIPV4;

static bool DecodeN(const int nType, size_t len, const UTF8 *p, in_addr_t *pu32)
{
    static DECODEIPV4 decode_IPv4_table[4] =
    {
        { 8,         255UL,  3,  3, 2 },
        { 16,      65535UL,  6,  5, 4 },
        { 24,   16777215UL,  8,  8, 6 },
        { 32, 4294967295UL, 11, 10, 8 }
    };

    *pu32  = (*pu32 << decode_IPv4_table[nType].nShift) & 0xFFFFFFFFUL;
    if (len == 0)
    {
        return false;
    }
    in_addr_t ul = 0;
    in_addr_t ul2;
    if (  len >= 3
       && p[0] == '0'
       && (  'x' == p[1]
          || 'X' == p[1]))
    {
        // Hexadecimal Path
        //
        // Skip the leading zeros.
        //
        p += 2;
        len -= 2;
        while (*p == '0' && len)
        {
            p++;
            len--;
        }
        if (len > decode_IPv4_table[nType].maxHexLen)
        {
            return false;
        }
        while (len)
        {
            const auto ch = *p;
            ul2 = ul;
            ul  = (ul << 4) & 0xFFFFFFFFUL;
            if (ul < ul2)
            {
                // Overflow
                //
                return false;
            }
            if ('0' <= ch && ch <= '9')
            {
                ul |= ch - '0';
            }
            else if ('A' <= ch && ch <= 'F')
            {
                ul |= ch - 'A';
            }
            else if ('a' <= ch && ch <= 'f')
            {
                ul |= ch - 'a';
            }
            else
            {
                return false;
            }
            p++;
            len--;
        }
    }
    else if (len >= 1 && p[0] == '0')
    {
        // Octal Path
        //
        // Skip the leading zeros.
        //
        p++;
        len--;
        while (*p == '0' && len)
        {
            p++;
            len--;
        }
        if (len > decode_IPv4_table[nType].maxOctLen)
        {
            return false;
        }
        while (len)
        {
            const auto ch = *p;
            ul2 = ul;
            ul  = (ul << 3) & 0xFFFFFFFFUL;
            if (ul < ul2)
            {
                // Overflow
                //
                return false;
            }
            if ('0' <= ch && ch <= '7')
            {
                ul |= ch - '0';
            }
            else
            {
                return false;
            }
            p++;
            len--;
        }
    }
    else
    {
        // Decimal Path
        //
        if (len > decode_IPv4_table[nType].maxDecLen)
        {
            return false;
        }
        while (len)
        {
            const auto ch = *p;
            ul2 = ul;
            ul  = (ul * 10) & 0xFFFFFFFFUL;
            if (ul < ul2)
            {
                // Overflow
                //
                return false;
            }
            ul2 = ul;
            if ('0' <= ch && ch <= '9')
            {
                ul += ch - '0';
            }
            else
            {
                return false;
            }
            if (ul < ul2)
            {
                // Overflow
                //
                return false;
            }
            p++;
            len--;
        }
    }
    if (ul > decode_IPv4_table[nType].maxValue)
    {
        return false;
    }
    *pu32 |= ul;
    return true;
}

// ---------------------------------------------------------------------------
// MakeCanonicalIPv4: inet_addr() does not do reasonable checking for sane
// syntax on all platforms. On certain operating systems, if passed less than
// four octets, it will cause a segmentation violation. Furthermore, there is
// confusion between return values for valid input "255.255.255.255" and
// return values for invalid input (INADDR_NONE as -1). To overcome these
// problems, it appears necessary to re-implement inet_addr() with a different
// interface.
//
// n8.n8.n8.n8  Class A format. 0 <= n8 <= 255.
//
// Supported Berkeley IP formats:
//
//    n8.n8.n16  Class B 128.net.host format. 0 <= n16 <= 65535.
//    n8.n24     Class A net.host format. 0 <= n24 <= 16777215.
//    n32        Single 32-bit number. 0 <= n32 <= 4294967295.
//
// Each element may be expressed in decimal, octal or hexadecimal. '0' is the
// octal prefix. '0x' or '0X' is the hexadecimal prefix. Otherwise the number
// is taken as decimal.
//
//    08  Octal
//    0x8 Hexadecimal
//    0X8 Hexadecimal
//    8   Decimal
//
bool make_canonical_IPv4(const UTF8 *str, in_addr_t *pnIP)
{
    *pnIP = 0;
    if (!str)
    {
        return false;
    }

    // Skip leading spaces.
    //
    auto q = str;
    while (*q == ' ')
    {
        q++;
    }

    const auto* p = reinterpret_cast<UTF8 const *>(strchr(reinterpret_cast<char const *>(q), '.'));
    auto n = 0;
    while (p)
    {
        // Decode
        //
        n++;
        if (n > 3)
        {
            return false;
        }
        if (!DecodeN(0, p-q, q, pnIP))
        {
            return false;
        }
        q = p + 1;
        p = reinterpret_cast<UTF8 const *>(strchr(reinterpret_cast<char const *>(q), '.'));
    }

    // Decode last element.
    //
    const auto len = strlen(reinterpret_cast<char const *>(q));
    return DecodeN(3 - n, len, q, pnIP);
}

// Given a host-ordered mask, this function will determine whether it is a
// valid one. Valid masks consist of a N-bit sequence of '1' bits followed by
// a (32-N)-bit sequence of '0' bits, where N is 0 to 32.
//
bool mux_in_addr::isValidMask(int *pnLeadingBits) const
{
    in_addr_t test = 0xFFFFFFFFUL;
    const in_addr_t mask = m_ia.s_addr;
    for (auto i = 0; i <= 32; i++)
    {
        if (mask == test)
        {
            *pnLeadingBits = i;
            return true;
        }
        test = (test << 1) & 0xFFFFFFFFUL;
    }
    return false;
}

void mux_in_addr::makeMask(const int num_leading_bits)
{
    // << [0,31] works. << 32 is problematic on some systems.
    //
    in_addr_t mask = 0;
    if (num_leading_bits > 0)
    {
        mask = (0xFFFFFFFFUL << (32 - num_leading_bits)) & 0xFFFFFFFFUL;
    }
    m_ia.s_addr = htonl(mask);
}
#endif

#if defined(HAVE_IN6_ADDR)
bool mux_in6_addr::isValidMask(int *pnLeadingBits) const
{
    const unsigned char allones = 0xFF;
    unsigned char mask = 0;
    size_t i;
    for (i = 0; i < sizeof(m_ia6.s6_addr)/sizeof(m_ia6.s6_addr[0]); i++)
    {
        mask = m_ia6.s6_addr[i];
        if (allones != mask)
        {
            break;
        }
    }

    int num_leading_bits = 8*i;

    if (i < sizeof(m_ia6.s6_addr)/sizeof(m_ia6.s6_addr[0]))
    {
        if (0 != mask)
        {
            auto found = false;
            auto test = allones;
            for (auto j = 0; j <= 8 && !found; j++)
            {
                if (mask == test)
                {
                    num_leading_bits += j;
                    found = true;
                    break;
                }
                test = (test << 1) & allones;
            }

            if (!found)
            {
                return false;
            }
            i++;
        }

        for ( ; i < sizeof(m_ia6.s6_addr)/sizeof(m_ia6.s6_addr[0]); i++)
        {
            mask = m_ia6.s6_addr[i];
            if (0 != mask)
            {
                return false;
            }
        }
    }
    *pnLeadingBits = num_leading_bits;
    return true;
}

void mux_in6_addr::makeMask(const int num_leading_bits)
{
    constexpr unsigned char allones = 0xFF;
    memset(&m_ia6, 0, sizeof(m_ia6));
    const size_t num_bytes = num_leading_bits / 8;
    for (size_t i = 0; i < num_bytes; i++)
    {
        m_ia6.s6_addr[i] = allones;
    }

    if (num_bytes < sizeof(m_ia6.s6_addr)/sizeof(m_ia6.s6_addr[0]))
    {
        const size_t num_leftover_bits = num_leading_bits % 8;
        if (num_leftover_bits > 0)
        {
            m_ia6.s6_addr[num_bytes] = (allones << (8 - num_leftover_bits)) & allones;
        }
    }
}
#endif

mux_subnet::~mux_subnet()
{
    delete m_iaBase;
    delete m_iaMask;
    delete m_iaEnd;
}

bool mux_subnet::listinfo(UTF8 *sAddress, int *pnLeadingBits) const
{
    // Base Address
    //
    mux_sockaddr msa;
    msa.set_address(m_iaBase);
    msa.ntop(sAddress, LBUF_SIZE);

    // Leading significant bits
    //
    *pnLeadingBits = m_iLeadingBits;

    return true;
}

mux_subnet::SubnetComparison mux_subnet::compare_to(mux_subnet *t) const
{
    if (*(t->m_iaEnd) < *m_iaBase)
    {
        // this > t
        //
        return SubnetComparison::kGreaterThan;
    }
    else if (*m_iaEnd < *(t->m_iaBase))
    {
        // this < t
        //
        return SubnetComparison::kLessThan;
    }
    else if (  *m_iaBase < *(t->m_iaBase)
            && *(t->m_iaEnd) < *m_iaEnd)
    {
        // this contains t
        //
        return SubnetComparison::kContains;
    }
    else if (  *m_iaBase == *(t->m_iaBase)
            && m_iLeadingBits == t->m_iLeadingBits)
    {
        // this == t
        //
        return SubnetComparison::kEqual;
    }
    else
    {
        // this is contained by t
        //
        return SubnetComparison::kContainedBy;
    }
}

mux_subnet::SubnetComparison mux_subnet::compare_to(MUX_SOCKADDR *msa) const
{
    mux_addr *ma = nullptr;
    switch (msa->Family())
    {
#if defined(HAVE_IN_ADDR)
    case AF_INET:
        {
            struct in_addr ia{};
            msa->get_address(&ia);
            ma = static_cast<mux_addr *>(new mux_in_addr(&ia));
        }
        break;
#endif

#if defined(HAVE_IN6_ADDR)
    case AF_INET6:
        {
            struct in6_addr ia6{};
            msa->get_address(&ia6);
            ma = static_cast<mux_addr *>(new mux_in6_addr(&ia6));
        }
        break;
#endif
    default:
        return SubnetComparison::kGreaterThan;
    }

    mux_subnet::SubnetComparison fComp;
    if (*ma < *m_iaBase)
    {
        // this > t
        //
        fComp = SubnetComparison::kGreaterThan;
    }
    else if (*m_iaEnd < *ma)
    {
        // this < t
        //
        fComp = SubnetComparison::kLessThan;
    }
    else
    {
        // this contains t
        //
        fComp = SubnetComparison::kContains;
    }
    delete ma;
    return fComp;
}

mux_subnet *parse_subnet(UTF8 *str, const dbref player, UTF8 *cmd)
{
    mux_addr *mux_address_mask = nullptr;
    mux_addr *mux_address_base = nullptr;
    mux_addr *mux_address_end  = nullptr;
    auto num_leading_bits = 0;

    MUX_ADDRINFO hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICHOST|AI_NUMERICSERV;

    int n;
    in_addr_t net_address_bits;
    MUX_ADDRINFO *servinfo;

    UTF8 *addr_txt;
    auto mask_txt = reinterpret_cast<UTF8 *>(strchr(reinterpret_cast<char *>(str), '/'));
    if (nullptr == mask_txt)
    {
        // Standard IP range and netmask notation.
        //
        string_token st(str, T(" \t=,"));
        addr_txt = st.parse();
        if (nullptr != addr_txt)
        {
            mask_txt = st.parse();
        }

        if (  nullptr == addr_txt
           || '\0' == *addr_txt
           || nullptr == mask_txt
           || '\0' == *mask_txt)
        {
            cf_log_syntax(player, cmd, T("Missing host address or mask."));
            return nullptr;
        }

        n = 0;
        if (0 == mux_getaddrinfo(mask_txt, nullptr, &hints, &servinfo))
        {
            for (auto ai = servinfo; nullptr != ai; ai = ai->ai_next)
            {
                delete mux_address_mask;
                switch (ai->ai_family)
                {
#if defined(HAVE_SOCKADDR_IN) && defined(HAVE_IN_ADDR)
                case AF_INET:
                    {
                        const auto sai = reinterpret_cast<struct sockaddr_in *>(ai->ai_addr);
                        mux_address_mask = static_cast<mux_addr *>(new mux_in_addr(&sai->sin_addr));
                    }
                    break;
#endif
#if defined(HAVE_SOCKADDR_IN6) && defined(HAVE_IN6_ADDR)
                case AF_INET6:
                    {
                        const auto sai6 = reinterpret_cast<struct sockaddr_in6 *>(ai->ai_addr);
                        mux_address_mask = static_cast<mux_addr *>(new mux_in6_addr(&sai6->sin6_addr));
                    }
                    break;
#endif
                default:
                    return nullptr;
                }
                n++;
            }
            mux_freeaddrinfo(servinfo);
        }
#if defined(HAVE_SOCKADDR_IN) && defined(HAVE_IN_ADDR)
        else if (make_canonical_IPv4(mask_txt, &net_address_bits))
        {
            delete mux_address_mask;
            mux_address_mask = static_cast<mux_addr *>(new mux_in_addr(net_address_bits));
            n++;
        }
#endif

        if (  1 != n
           || !mux_address_mask->isValidMask(&num_leading_bits))
        {
            cf_log_syntax(player, cmd, T("Malformed mask address: %s"), mask_txt);
            delete mux_address_mask;
            return nullptr;
        }
    }
    else
    {
        // RFC 1517, 1518, 1519, 1520: CIDR IP prefix notation
        //
        addr_txt = str;
        *mask_txt++ = '\0';
        if (!is_integer(mask_txt, nullptr))
        {
            cf_log_syntax(player, cmd, T("Mask field (%s) in CIDR IP prefix is not numeric."), mask_txt);
            return nullptr;
        }

        num_leading_bits = mux_atol(mask_txt);
    }

    n = 0;
    if (0 == mux_getaddrinfo(addr_txt, nullptr, &hints, &servinfo))
    {
        for (MUX_ADDRINFO *ai = servinfo; nullptr != ai; ai = ai->ai_next)
        {
            delete mux_address_base;
            switch (ai->ai_family)
            {
#if defined(HAVE_SOCKADDR_IN) && defined(HAVE_IN_ADDR)
            case AF_INET:
                {
                    const auto sai = reinterpret_cast<struct sockaddr_in *>(ai->ai_addr);
                    mux_address_base = static_cast<mux_addr *>(new mux_in_addr(&sai->sin_addr));
                }
                break;
#endif
#if defined(HAVE_SOCKADDR_IN6) &&  defined(HAVE_IN6_ADDR)
            case AF_INET6:
                {
                    const auto sai6 = reinterpret_cast<struct sockaddr_in6 *>(ai->ai_addr);
                    mux_address_base = static_cast<mux_addr *>(new mux_in6_addr(&sai6->sin6_addr));
                }
                break;
#endif
            default:
                delete mux_address_mask;
                return nullptr;
            }
            n++;
        }
        mux_freeaddrinfo(servinfo);
    }
#if defined(HAVE_IN_ADDR)
    else if (make_canonical_IPv4(addr_txt, &net_address_bits))
    {
        delete mux_address_base;
        mux_address_base = static_cast<mux_addr *>(new mux_in_addr(net_address_bits));
        n++;
    }
#endif

    if (1 != n)
    {
        cf_log_syntax(player, cmd, T("Malformed host address: %s"), addr_txt);
        delete mux_address_mask;
        delete mux_address_base;
        return nullptr;
    }

    if (nullptr == mux_address_mask)
    {
        bool fOutOfRange = false;
        switch (mux_address_base->getFamily())
        {
#if defined(HAVE_IN_ADDR)
        case AF_INET:
            mux_address_mask = static_cast<mux_addr *>(new mux_in_addr());
            if (  num_leading_bits < 0
               || 32 < num_leading_bits)
            {
                fOutOfRange = true;
            }
            break;
#endif
#if defined(HAVE_IN6_ADDR)
        case AF_INET6:
            mux_address_mask = static_cast<mux_addr *>(new mux_in6_addr());
            if (  num_leading_bits < 0
               || 128 < num_leading_bits)
            {
                fOutOfRange = true;
            }
            break;
#endif
        default:
            return nullptr;
        }

        if (fOutOfRange)
        {
            cf_log_syntax(player, cmd, T("Mask bits (%d) in CIDR IP prefix out of range."), num_leading_bits);
            return nullptr;
        }
        mux_address_mask->makeMask(num_leading_bits);
    }
    else if (mux_address_base->getFamily() != mux_address_mask->getFamily())
    {
        cf_log_syntax(player, cmd, T("Mask type is not compatible with address type: %s %s"), addr_txt, mask_txt);
        delete mux_address_mask;
        delete mux_address_base;
        return nullptr;
    }

    if (mux_address_base->clearOutsideMask(*mux_address_mask))
    {
        // The given subnet address contains 'one' bits which are outside the given subnet mask. If we don't clear these bits, they
        // will interfere with the subnet tests in site_check. The subnet spec would be defunct and useless.
        //
        cf_log_syntax(player, cmd, T("Non-zero host address bits outside the subnet mask (fixed): %s %s"), addr_txt, mask_txt);
    }

    delete mux_address_end;
    mux_address_end = mux_address_base->calculateEnd(*mux_address_mask);

    const auto msn = new mux_subnet();
    msn->m_iaBase = mux_address_base;
    msn->m_iaMask = mux_address_mask;
    msn->m_iaEnd = mux_address_end;
    msn->m_iLeadingBits = num_leading_bits;
    return msn;
}

#if !defined(HAVE_GETADDRINFO) && defined(HAVE_IN_ADDR)
static struct addrinfo *gai_addrinfo_new(const int socktype, const UTF8 *canonical, const struct in_addr addr, const unsigned short port)
{
    const auto ai = static_cast<struct addrinfo *>(MEMALLOC(sizeof(struct addrinfo)));
    if (nullptr == ai)
    {
        return nullptr;
    }
    ai->ai_addr = static_cast<sockaddr *>(MEMALLOC(sizeof(struct sockaddr_in)));
    if (nullptr == ai->ai_addr)
    {
        free(ai);
        return nullptr;
    }
    ai->ai_next = nullptr;
    if (nullptr == canonical)
    {
        ai->ai_canonname = nullptr;
    }
    else
    {
        ai->ai_canonname = reinterpret_cast<char *>(StringClone(canonical));
        if (nullptr == ai->ai_canonname)
        {
            mux_freeaddrinfo(ai);
            return nullptr;
        }
    }
    memset(ai->ai_addr, 0, sizeof(struct sockaddr_in));
    ai->ai_flags = 0;
    ai->ai_family = AF_INET;
    ai->ai_socktype = socktype;
    ai->ai_protocol = (socktype == SOCK_DGRAM) ? IPPROTO_UDP : IPPROTO_TCP;
    ai->ai_addrlen = sizeof(struct sockaddr_in);
    reinterpret_cast<struct sockaddr_in *>(ai->ai_addr)->sin_family = AF_INET;
    reinterpret_cast<struct sockaddr_in *>(ai->ai_addr)->sin_addr = addr;
    reinterpret_cast<struct sockaddr_in *>(ai->ai_addr)->sin_port = htons(port);
    return ai;
}

static bool convert_service(const UTF8 *string, long *result)
{
    if ('\0' == *string)
    {
        return false;
    }
    *result = mux_atol(string);
    return *result >= 0;
}

static int gai_service(const UTF8 *servname, int flags, int *type, unsigned short *port)
{
    long value;
    if (convert_service(servname, &value))
    {
        if (value > (1L << 16) - 1)
        {
            return EAI_SERVICE;
        }
        *port = static_cast<unsigned short>(value);
    }
    else
    {
        if (flags & AI_NUMERICSERV)
        {
            return EAI_NONAME;
        }
        const UTF8 *protocol;
        if (0 != *type)
            protocol = (SOCK_DGRAM == *type) ? T("udp") : T("tcp");
        else
            protocol = nullptr;

        struct servent *servent = getservbyname(reinterpret_cast<const char*>(servname), reinterpret_cast<const char*>(protocol));
        if (nullptr == servent)
        {
            return EAI_NONAME;
        }
        if (strcmp(servent->s_proto, "udp") == 0)
        {
            *type = SOCK_DGRAM;
        }
        else if (strcmp(servent->s_proto, "tcp") == 0)
        {
            *type = SOCK_STREAM;
        }
        else
        {
            return EAI_SERVICE;
        }
        *port = htons(servent->s_port);
    }
    return 0;
}

static int gai_lookup(const UTF8 *nodename, const int flags, const int socktype, const unsigned short port, struct addrinfo **res)
{
    struct addrinfo *ai;
    struct in_addr addr{};
    const UTF8 *canonical;

    in_addr_t address_bits;
    if (make_canonical_IPv4(nodename, &address_bits))
    {
        addr.s_addr = address_bits;
        canonical = (flags & AI_CANONNAME) ? nodename : nullptr;
        ai = gai_addrinfo_new(socktype, canonical, addr, port);
        if (nullptr == ai)
        {
            return EAI_MEMORY;
        }
        *res = ai;
        return 0;
    }
    else
    {
        if (flags & AI_NUMERICHOST)
        {
            return EAI_NONAME;
        }
        const auto host = gethostbyname(reinterpret_cast<const char *>(nodename));
        if (nullptr == host)
        {
            switch (h_errno)
            {
            case HOST_NOT_FOUND:
                return EAI_NONAME;
            case TRY_AGAIN:
            case NO_DATA:
                return EAI_AGAIN;
            default:
                return EAI_FAIL;
            }
        }
        if (nullptr == host->h_addr_list[0])
        {
            return EAI_FAIL;
        }
        if (flags & AI_CANONNAME)
        {
            if (nullptr != host->h_name)
            {
                canonical = reinterpret_cast<UTF8 *>(host->h_name);
            }
            else
            {
                canonical = nodename;
            }
        }
        else
        {
            canonical = nullptr;
        }
        struct addrinfo *first = nullptr;
        struct addrinfo *prev = nullptr;
        for (auto i = 0; host->h_addr_list[i] != nullptr; i++)
        {
            if (host->h_length != sizeof(addr))
            {
                mux_freeaddrinfo(first);
                return EAI_FAIL;
            }
            memcpy(&addr, host->h_addr_list[i], sizeof(addr));
            ai = gai_addrinfo_new(socktype, canonical, addr, port);
            if (nullptr == ai)
            {
                mux_freeaddrinfo(first);
                return EAI_MEMORY;
            }
            if (first == nullptr)
            {
                first = ai;
                prev = ai;
            }
            else
            {
                prev->ai_next = ai;
                prev = ai;
            }
        }
        *res = first;
        return 0;
    }
}

#endif

int mux_getaddrinfo(const UTF8 *node, const UTF8 *service, const MUX_ADDRINFO *hints, MUX_ADDRINFO **res)
{
#if defined(HAVE_GETADDRINFO)
    return getaddrinfo((const char *)node, (const char *)service, hints, res);
#elif !defined(HAVE_GETADDRINFO) && defined(HAVE_IN_ADDR)
    unsigned short port;

    int flags;
    int socktype;
    if (nullptr != hints)
    {
        flags = hints->ai_flags;
        socktype = hints->ai_socktype;
        if ((flags & (AI_PASSIVE|AI_CANONNAME|AI_NUMERICHOST|AI_NUMERICSERV|AI_ADDRCONFIG|AI_V4MAPPED)) != flags)
        {
            return EAI_BADFLAGS;
        }

        if (  hints->ai_family != AF_UNSPEC
           && hints->ai_family != AF_INET)
        {
            return EAI_FAMILY;
        }

        if (  0 != socktype
           && SOCK_STREAM != socktype
           && SOCK_DGRAM != socktype)
        {
            return EAI_SOCKTYPE;
        }

        if (0 != hints->ai_protocol)
        {
            if (  IPPROTO_TCP != hints->ai_protocol
               && IPPROTO_UDP != hints->ai_protocol)
            {
                return EAI_SOCKTYPE;
            }
        }
    }
    else
    {
        flags = 0;
        socktype = 0;
    }

    if (nullptr == service)
    {
        port = 0;
    }
    else
    {
        const auto status = gai_service(service, flags, &socktype, &port);
        if (0 != status)
        {
            return status;
        }
    }
    if (node != nullptr)
    {
        return gai_lookup(node, flags, socktype, port, res);
    }
    else
    {
        in_addr addr;
        if (nullptr == service)
        {
            return EAI_NONAME;
        }
        if ((flags & AI_PASSIVE) == AI_PASSIVE)
        {
            addr.s_addr = INADDR_ANY;
        }
        else
        {
            addr.s_addr = htonl(0x7f000001UL);
        }
        struct addrinfo *ai = gai_addrinfo_new(socktype, nullptr, addr, port);
        if (nullptr == ai)
        {
            return EAI_MEMORY;
        }
        *res = ai;
        return 0;
    }
#endif
}

void mux_freeaddrinfo(MUX_ADDRINFO *res)
{
#if defined(HAVE_GETADDRINFO)
    freeaddrinfo(res);
#else
    while (nullptr != res)
    {
        const auto next = res->ai_next;
        if (nullptr != res->ai_addr)
        {
            free(res->ai_addr);
        }
        if (nullptr != res->ai_canonname)
        {
            free(res->ai_canonname);
        }
        free(res);
        res = next;
    }
#endif
}

#if !defined(HAVE_GETNAMEINFO)
static bool try_name(const char *name, UTF8 *host, size_t hostlen, int *status)
{
    if (nullptr == strchr(static_cast<const char *>(name), '.'))
    {
        return false;
    }
    UTF8 *bufc = host;
    safe_str(reinterpret_cast<const UTF8 *>(name), host, &bufc);
    *bufc = '\0';
    return true;
}

static int lookup_hostname(const struct in_addr *addr, UTF8 *host, size_t hostlen, int flags)
{
    UTF8 *bufc;
#ifdef HAVE_GETHOSTBYADDR
    if (0 == (flags & NI_NUMERICHOST))
    {
        auto he = gethostbyaddr(reinterpret_cast<const char *>(addr), sizeof(struct in_addr), AF_INET);
        if (nullptr == he)
        {
            if (flags & NI_NAMEREQD)
            {
                return EAI_NONAME;
            }
        }
        else
        {
            int status;
            if (try_name(he->h_name, host, hostlen, &status))
            {
                return status;
            }

            for (char **alias = he->h_aliases; nullptr != *alias; alias++)
            {
                if (try_name(*alias, host, hostlen, &status))
                {
                    return status;
                }
            }
        }
    }
#endif

    bufc = host;
    safe_str(reinterpret_cast<UTF8 *>(inet_ntoa(*addr)), host, &bufc);
    *bufc = '\0';
    return 0;
}

static int lookup_servicename(const unsigned short port, UTF8 *serv, size_t servlen, const int flags)
{
    UTF8 *bufc;
    if (0 == (flags & NI_NUMERICSERV))
    {
        const auto protocol = (flags & NI_DGRAM) ? "udp" : "tcp";
        const auto srv = getservbyport(htons(port), protocol);
        if (nullptr != srv)
        {
            bufc = serv;
            safe_str(reinterpret_cast<UTF8 *>(srv->s_name), serv, &bufc);
            *bufc = '\0';
            return 0;
        }
    }

    bufc = serv;
    safe_ltoa(port, serv, &bufc);
    *bufc = '\0';
    return 0;
}
#endif

int mux_getnameinfo(const MUX_SOCKADDR *msa, UTF8 *host, const size_t hostlen, UTF8 *serv, const size_t servlen, const int flags)
{
#if defined(HAVE_GETNAMEINFO)
    return getnameinfo(msa->saro(), msa->salen(), reinterpret_cast<char *>(host), hostlen, reinterpret_cast<char *>(serv), servlen, flags);
#else
    if (  (  nullptr == host
          || hostlen <= 0)
       && (  nullptr == serv
          || servlen <= 0))
    {
        return EAI_NONAME;
    }

    if (AF_INET != msa->Family())
    {
        return EAI_FAMILY;
    }

    if (  nullptr != host
       && 0 < hostlen)
    {
        const auto status = lookup_hostname(&msa->sairo()->sin_addr, host, hostlen, flags);
        if (0 != status)
        {
            return status;
        }
    }

    if (  nullptr != serv
       && 0 < servlen)
    {
        const auto port = msa->port();
        return lookup_servicename(port, serv, servlen, flags);
    }
    return 0;
#endif
}

unsigned short mux_sockaddr::port() const
{
    switch (u.sa.sa_family)
    {
#if defined(HAVE_SOCKADDR_IN)
    case AF_INET:
        return ntohs(u.sai.sin_port);
#endif

#if defined(HAVE_SOCKADDR_IN6)
    case AF_INET6:
        return ntohs(u.sai6.sin6_port);
#endif

    default:
        return 0;
    }
}

struct sockaddr *mux_sockaddr::sa()
{
    return &u.sa;
}

size_t mux_sockaddr::maxaddrlen() const
{
    return sizeof(u);
}

void mux_sockaddr::ntop(UTF8 *sAddress, size_t len) const
{
    if (0 != mux_getnameinfo(this, sAddress, len, nullptr, 0, NI_NUMERICHOST|NI_NUMERICSERV))
    {
        sAddress[0] = '\0';
    }
}

void mux_sockaddr::set_address(mux_addr *ma)
{
    switch (ma->getFamily())
    {
#if defined(HAVE_IN_ADDR)
    case AF_INET:
        {
            const auto mia = dynamic_cast<mux_in_addr *>(ma);
            u.sai.sin_family = AF_INET;
            u.sai.sin_addr = mia->m_ia;
        }
        break;
#endif
#if defined(HAVE_IN6_ADDR)
    case AF_INET6:
        {
            const auto mia6 = dynamic_cast<mux_in6_addr *>(ma);
            u.sai6.sin6_family = AF_INET6;
            u.sai6.sin6_addr = mia6->m_ia6;
        }
        break;
#endif
    }
}

void mux_sockaddr::Clear()
{
    memset(&u, 0, sizeof(u));
}

#if defined(HAVE_SOCKADDR_IN)
struct sockaddr_in *mux_sockaddr::sai()
{
    return &u.sai;
}

struct sockaddr_in const *mux_sockaddr::sairo() const
{
    return &u.sai;
}
#endif

unsigned short mux_sockaddr::Family() const
{
    return u.sa.sa_family;
}

struct sockaddr const *mux_sockaddr::saro() const
{
    return &u.sa;
}

size_t mux_sockaddr::salen() const
{
    switch (u.sa.sa_family)
    {
#if defined(HAVE_SOCKADDR_IN)
    case AF_INET:
        return sizeof(u.sai);
#endif
#if defined(HAVE_SOCKADDR_IN6)
    case AF_INET6:
        return sizeof(u.sai6);
#endif

    default:
        return 0;
    }
}

mux_sockaddr::mux_sockaddr()
{
    Clear();
}

mux_sockaddr::mux_sockaddr(const sockaddr *sa)
{
    switch (sa->sa_family)
    {
#if defined(HAVE_SOCKADDR_IN)
    case AF_INET:
        memcpy(&u.sai, sa, sizeof(u.sai));
        break;
#endif
#if defined(HAVE_SOCKADDR_IN6)
    case AF_INET6:
        memcpy(&u.sai6, sa, sizeof(u.sai6));
        break;
#endif
    }
}

bool mux_sockaddr::operator==(const mux_sockaddr &it) const
{
    if (it.u.sa.sa_family != u.sa.sa_family)
    {
        return false;
    }

    switch (u.sa.sa_family)
    {
#if defined(HAVE_SOCKADDR_IN)
    case AF_INET:
        if (  memcmp(&it.u.sai.sin_addr, &u.sai.sin_addr, sizeof(u.sai.sin_addr)) == 0
           && it.u.sai.sin_family == u.sai.sin_family
           && it.u.sai.sin_port == u.sai.sin_port)
        {
            return true;
        }
        break;
#endif
#if defined(HAVE_SOCKADDR_IN6)
    case AF_INET6:
        // Intentionally ignoring sin6_flowinfo, sin6_scopeid, and others for now.
        //
        if (  memcmp(&it.u.sai6.sin6_addr, &u.sai6.sin6_addr, sizeof(u.sai6.sin6_family)) == 0
           && it.u.sai6.sin6_family == u.sai6.sin6_family
           && it.u.sai6.sin6_port == u.sai6.sin6_port)
        {
            return true;
        }
        break;
#endif
    }
    return false;
}

mux_addr::~mux_addr() = default;

#if defined(HAVE_IN_ADDR)
mux_in_addr::~mux_in_addr() = default;

mux_in_addr::mux_in_addr(in_addr *ia) : m_ia(*ia)
{
}

mux_in_addr::mux_in_addr(const unsigned int bits)
{
    m_ia.s_addr = htonl(bits);
}

void mux_sockaddr::get_address(in_addr *ia) const
{
    *ia = u.sai.sin_addr;
}

bool mux_in_addr::operator<(const mux_addr &it) const
{
    if (AF_INET == it.getFamily())
    {
        const auto* t = dynamic_cast<const mux_in_addr *>(&it);
        return (ntohl(m_ia.s_addr) < ntohl(t->m_ia.s_addr));
    }
    return true;
}

bool mux_in_addr::operator==(const mux_addr &it) const
{
    if (AF_INET == it.getFamily())
    {
        const auto* t = dynamic_cast<const mux_in_addr *>(&it);
        return (ntohl(m_ia.s_addr) == ntohl(t->m_ia.s_addr));
    }
    return false;
}

bool mux_in_addr::clearOutsideMask(const mux_addr &it)
{
    if (AF_INET == it.getFamily())
    {
        const auto* t = dynamic_cast<const mux_in_addr *>(&it);
        if (m_ia.s_addr & ~t->m_ia.s_addr)
        {
            m_ia.s_addr &= t->m_ia.s_addr;
            return true;
        }
        return false;
    }
    return true;
}

mux_addr *mux_in_addr::calculateEnd(const mux_addr &it) const
{
    if (AF_INET == it.getFamily())
    {
        const auto* t = dynamic_cast<const mux_in_addr *>(&it);
        auto* e = new mux_in_addr();
        e->m_ia.s_addr = m_ia.s_addr | ~t->m_ia.s_addr;
        return static_cast<mux_addr *>(e);
    }
    return nullptr;
}
#endif

#if defined(HAVE_IN6_ADDR)
mux_in6_addr::~mux_in6_addr() = default;

mux_in6_addr::mux_in6_addr(in6_addr *ia6) : m_ia6(*ia6)
{
}

void mux_sockaddr::get_address(in6_addr *ia6) const
{
    *ia6 = u.sai6.sin6_addr;
}

bool mux_in6_addr::operator<(const mux_addr &it) const
{
    if (AF_INET6 == it.getFamily())
    {
        const auto* t = dynamic_cast<const mux_in6_addr *>(&it);
        for (size_t i = 0; i < sizeof(m_ia6.s6_addr)/sizeof(m_ia6.s6_addr[0]); i++)
        {
            if (m_ia6.s6_addr[i] < t->m_ia6.s6_addr[i])
            {
                return true;
            }
        }
    }
    return false;
}

bool mux_in6_addr::operator==(const mux_addr &it) const
{
    if (AF_INET6 == it.getFamily())
    {
        const auto* t = dynamic_cast<const mux_in6_addr *>(&it);
        return (m_ia6.s6_addr == t->m_ia6.s6_addr);
    }
    return false;
}

bool mux_in6_addr::clearOutsideMask(const mux_addr &it)
{
    if (AF_INET6 == it.getFamily())
    {
        bool fOutside = false;
        const auto* t = dynamic_cast<const mux_in6_addr *>(&it);
        for (size_t  i = 0; i < sizeof(m_ia6.s6_addr)/sizeof(m_ia6.s6_addr[0]); i++)
        {
            if (m_ia6.s6_addr[i] & ~t->m_ia6.s6_addr[i])
            {
                fOutside = true;
                m_ia6.s6_addr[i] &= t->m_ia6.s6_addr[i];
            }
        }
        return fOutside;
    }
    return true;
}

mux_addr *mux_in6_addr::calculateEnd(const mux_addr &it) const
{
    if (AF_INET6 == it.getFamily())
    {
        const auto* t = dynamic_cast<const mux_in6_addr *>(&it);
        auto* e = new mux_in6_addr();
        for (size_t  i = 0; i < sizeof(m_ia6.s6_addr)/sizeof(m_ia6.s6_addr[0]); i++)
        {
            e->m_ia6.s6_addr[i] = m_ia6.s6_addr[i] | ~t->m_ia6.s6_addr[i];
        }
        return static_cast<mux_addr *>(e);
    }
    return nullptr;
}
#endif
