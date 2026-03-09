/*! \file bsd.cpp
 * \brief Descriptor lifecycle and I/O management.
 *
 * Global state, socket read/write wrappers, connection shutdown, output
 * processing, and emergency shutdown routines.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "interface.h"
#include "ganl_adapter.h"
#include "modules.h"
#include "driverstate.h"
#include "driver_log.h"
using namespace std;

// Driver-local state (definitions).  See driverstate.h for declarations.
//
std::list<DESC*> g_descriptors_list;
std::unordered_map<DESC*, std::list<DESC*>::iterator, DriverPointerHasher> g_descriptors_map;
std::multimap<dbref, DESC*> g_dbref_to_descriptors_map;

UTF8 g_version[128];
UTF8 g_short_ver[64];

mux_IGameEngine  *g_pIGameEngine = nullptr;
mux_IPlayerSession *g_pIPlayerSession = nullptr;

bool g_bStandAlone = false;
bool g_shutdown_flag = false;
bool g_restarting = false;
mux_subnets g_access_list;
StringPtrMap g_logout_cmd_htab;

#ifdef UNIX_SSL
port_info main_game_ports[MAX_LISTEN_PORTS * 2];
#else
port_info main_game_ports[MAX_LISTEN_PORTS];
#endif
int      num_main_game_ports = 0;
void site_mon_send(SOCKET, const UTF8 *, DESC *, const UTF8 *);

pid_t game_pid;

#if defined(HAVE_WORKING_FORK)

#ifdef STUB_SLAVE
pid_t stubslave_pid = 0;

extern "C" MUX_RESULT DCL_API pipepump(void)
{
    return g_GanlAdapter.pump_stubslave();
}

#endif // STUB_SLAVE

#endif // HAVE_WORKING_FORK


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
       && g_access_list.isForbid(&d->address))
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
            g_pILog->log_text(buff);
            g_pILog->log_name(d->player);
            mux_sprintf(buff, MBUF_SIZE, T(" <Reason: %s>"), disc_reasons[reason]);
            g_pILog->log_text(buff);
            free_mbuf(buff);
            ENDLOG;
        }
        else
        {
            fcache_dump(d, FC_QUIT);
            STARTLOG(LOG_NET | LOG_LOGIN, "NET", "DISC")
            buff = alloc_mbuf("shutdownsock.LOG.disconn");
            mux_sprintf(buff, MBUF_SIZE, T("[%u/%s] Logout by "), d->socket, d->addr);
            g_pILog->log_text(buff);
            g_pILog->log_name(d->player);
            mux_sprintf(buff, MBUF_SIZE, T(" <Reason: %s>"), disc_reasons[reason]);
            g_pILog->log_text(buff);
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
        g_pILog->log_text(buff);
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
        g_pILog->log_text(buff);
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

    if (reason == R_LOGOUT)
    {
        d->connected_at.GetUTC();
        d->retries_left = g_dc.retry_limit;
        d->command_count = 0;
        d->timeout = g_dc.idle_timeout;
        d->player = 0;
        d->doing[0] = '\0';
        d->quota = g_dc.cmd_quota_max;
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

        shutdown(d->socket, SD_BOTH);
        if (0 == SOCKET_CLOSE(d->socket))
        {
        }
        d->socket = INVALID_SOCKET;

        auto it = g_descriptors_map.find(d);
        g_descriptors_list.erase(it->second);
        g_descriptors_map.erase(it);

        // If we don't have queued IOs, then we can free these, now.
        //
        freeqs(d);
        destroy_desc(d);
        free_desc(d);
    }
}


void process_output(DESC *d, int bHandleShutdown)
{
    UNUSED_PARAMETER(bHandleShutdown);

    while (!d->output_queue.empty())
    {
        auto &entry = d->output_queue.front();
        if (!entry.empty())
        {
            g_GanlAdapter.send_data(d, entry.c_str(), entry.size());
            d->output_size -= entry.size();
        }
        d->output_queue.pop_front();
    }
}


void close_listening_ports(void)
{
    for (int i = 0; i < num_main_game_ports; i++)
    {
        SOCKET_CLOSE(main_game_ports[i].socket);
        main_game_ports[i].socket = INVALID_SOCKET;
    }
}


static void close_sockets_emergency(const UTF8* message)
{
    for (auto it = g_descriptors_list.begin(); it != g_descriptors_list.end(); ++it)
    {
        DESC* d = *it;
        SOCKET_WRITE(d->socket, reinterpret_cast<const char*>(message), strlen(reinterpret_cast<const char*>(message)), 0);

        if (IS_SOCKET_ERROR(shutdown(d->socket, SD_BOTH)))
        {
            g_pILog->log_perror(T("NET"), T("FAIL"), nullptr, T("shutdown"));
        }
        SOCKET_CLOSE(d->socket);
    }
    close_listening_ports();
}

void emergency_shutdown(void)
{
    close_sockets_emergency(T("Going down - Bye"));
}


