/*! \file conn_bridge.cpp
 * \brief Engine-side bridge to driver's connection manager.
 *
 * This file provides the same free function signatures that engine files
 * call (send_text_to_player, fetch_idle, etc.) but implements them by
 * delegating to a mux_IConnectionManager COM interface provided by the
 * driver.  Engine files need no changes — they continue to call the same
 * functions they always have.
 *
 * In the current in-process build, both conn_bridge.cpp and the real
 * accessor implementations in net.cpp are linked into the same binary,
 * so conn_bridge.cpp is NOT compiled.  It will be compiled into
 * engine.so when the physical split happens.
 *
 * This file exists now to validate that the COM interface is complete
 * and that the bridge pattern works.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

// The engine acquires these during initialization.
//
static mux_IConnectionManager *g_pConnMgr = nullptr;
static mux_IDriverControl     *g_pDriverCtl = nullptr;

void conn_bridge_init(void)
{
    if (nullptr == g_pConnMgr)
    {
        MUX_RESULT mr = mux_CreateInstance(CID_ConnectionManager, nullptr,
            UseSameProcess, IID_IConnectionManager,
            reinterpret_cast<void **>(&g_pConnMgr));
        if (MUX_FAILED(mr))
        {
            g_pConnMgr = nullptr;
        }
    }

    if (nullptr == g_pDriverCtl)
    {
        MUX_RESULT mr = mux_CreateInstance(CID_DriverControl, nullptr,
            UseSameProcess, IID_IDriverControl,
            reinterpret_cast<void **>(&g_pDriverCtl));
        if (MUX_FAILED(mr))
        {
            g_pDriverCtl = nullptr;
        }
    }
}

void conn_bridge_final(void)
{
    if (nullptr != g_pDriverCtl)
    {
        g_pDriverCtl->Release();
        g_pDriverCtl = nullptr;
    }
    if (nullptr != g_pConnMgr)
    {
        g_pConnMgr->Release();
        g_pConnMgr = nullptr;
    }
}

void request_shutdown(void)
{
    if (g_pDriverCtl)
    {
        g_pDriverCtl->ShutdownRequest();
    }
}

int site_update(const UTF8 *subnetStr, dbref player, UTF8 *cmd, int operation)
{
    if (g_pDriverCtl)
    {
        MUX_RESULT mr = g_pDriverCtl->SiteUpdate(subnetStr, player, cmd, operation);
        return MUX_SUCCEEDED(mr) ? 0 : -1;
    }
    return -1;
}

// --- Output ---

void send_text_to_player(dbref target, const UTF8 *text)
{
    if (g_pConnMgr) g_pConnMgr->SendText(target, text);
}

// mux_string overload removed — all callers now use const UTF8 * version.

void send_raw_to_player(dbref target, const UTF8 *data, size_t len)
{
    if (g_pConnMgr) g_pConnMgr->SendRaw(target, data, len);
}

void broadcast_and_flush(int inflags, const UTF8 *text)
{
    if (g_pConnMgr) g_pConnMgr->BroadcastAndFlush(inflags, text);
}

void send_prog_prompt(dbref target)
{
    if (g_pConnMgr) g_pConnMgr->SendProgPrompt(target);
}

void send_keepalive_nops(void)
{
    if (g_pConnMgr) g_pConnMgr->SendKeepaliveNops();
}

// --- Queries by dbref ---

int get_total_connections(void)
{
    int n = 0;
    if (g_pConnMgr) g_pConnMgr->GetTotalConnections(&n);
    return n;
}

int count_player_descs(dbref target)
{
    int n = 0;
    if (g_pConnMgr) g_pConnMgr->CountPlayerDescs(target, &n);
    return n;
}

int sum_player_command_count(dbref target)
{
    int n = -1;
    if (g_pConnMgr) g_pConnMgr->SumPlayerCommandCount(target, &n);
    return n;
}

int fetch_height(dbref target)
{
    int n = 24;
    if (g_pConnMgr) g_pConnMgr->FetchHeight(target, &n);
    return n;
}

int fetch_width(dbref target)
{
    int n = 78;
    if (g_pConnMgr) g_pConnMgr->FetchWidth(target, &n);
    return n;
}

int fetch_idle(dbref target)
{
    int n = -1;
    if (g_pConnMgr) g_pConnMgr->FetchIdle(target, &n);
    return n;
}

int fetch_connect(dbref target)
{
    int n = -1;
    if (g_pConnMgr) g_pConnMgr->FetchConnect(target, &n);
    return n;
}

// --- Queries by opaque DESC handle ---

DESC *find_desc_by_socket(SOCKET s)
{
    DESC *d = nullptr;
    if (g_pConnMgr) g_pConnMgr->FindDescBySocket(s, &d);
    return d;
}

DESC *find_desc_by_player(dbref target)
{
    DESC *d = nullptr;
    if (g_pConnMgr) g_pConnMgr->FindDescByPlayer(target, &d);
    return d;
}

dbref desc_player(const DESC *d)
{
    dbref p = NOTHING;
    if (g_pConnMgr) g_pConnMgr->DescPlayer(d, &p);
    return p;
}

int desc_height(const DESC *d)
{
    int n = 24;
    if (g_pConnMgr) g_pConnMgr->DescHeight(d, &n);
    return n;
}

int desc_width(const DESC *d)
{
    int n = 78;
    if (g_pConnMgr) g_pConnMgr->DescWidth(d, &n);
    return n;
}

int desc_encoding(const DESC *d)
{
    int n = 0;
    if (g_pConnMgr) g_pConnMgr->DescEncoding(d, &n);
    return n;
}

int desc_command_count(const DESC *d)
{
    int n = 0;
    if (g_pConnMgr) g_pConnMgr->DescCommandCount(d, &n);
    return n;
}

const UTF8 *desc_ttype(const DESC *d)
{
    const UTF8 *p = nullptr;
    if (g_pConnMgr) g_pConnMgr->DescTtype(d, &p);
    return p;
}

CLinearTimeAbsolute desc_last_time(const DESC *d)
{
    CLinearTimeAbsolute lta;
    if (g_pConnMgr) g_pConnMgr->DescLastTime(d, &lta);
    return lta;
}

CLinearTimeAbsolute desc_connected_at(const DESC *d)
{
    CLinearTimeAbsolute lta;
    if (g_pConnMgr) g_pConnMgr->DescConnectedAt(d, &lta);
    return lta;
}

int desc_nvt_him_state(const DESC *d, unsigned char chOption)
{
    int n = 0;
    if (g_pConnMgr) g_pConnMgr->DescNvtHimState(d, chOption, &n);
    return n;
}

SocketState desc_socket_state(const DESC *d)
{
    SocketState ss = SocketState::Accepted;
    if (g_pConnMgr) g_pConnMgr->DescSocketState(d, &ss);
    return ss;
}

// --- Iteration ---

void for_each_connected_player(void (*callback)(dbref player, void *context), void *context)
{
    if (g_pConnMgr) g_pConnMgr->ForEachConnectedPlayer(callback, context);
}

void for_each_connected_desc(void (*callback)(dbref player, SOCKET sock, void *context), void *context)
{
    if (g_pConnMgr) g_pConnMgr->ForEachConnectedDesc(callback, context);
}

// --- @program state ---

bool player_has_program(dbref target)
{
    bool b = false;
    if (g_pConnMgr) g_pConnMgr->PlayerHasProgram(target, &b);
    return b;
}

program_data *detach_player_program(dbref target)
{
    program_data *p = nullptr;
    if (g_pConnMgr) g_pConnMgr->DetachPlayerProgram(target, &p);
    return p;
}

void set_player_program(dbref target, program_data *program)
{
    if (g_pConnMgr) g_pConnMgr->SetPlayerProgram(target, program);
}

// --- Encoding / Display ---

void set_player_encoding(dbref target, int encoding)
{
    if (g_pConnMgr) g_pConnMgr->SetPlayerEncoding(target, encoding);
}

void reset_player_encoding(dbref target)
{
    if (g_pConnMgr) g_pConnMgr->ResetPlayerEncoding(target);
}

void set_doing_all(dbref target, const UTF8 *doing, size_t len)
{
    if (g_pConnMgr) g_pConnMgr->SetDoingAll(target, doing, len);
}

bool set_doing_least_idle(dbref target, const UTF8 *doing, size_t len)
{
    bool b = false;
    if (g_pConnMgr) g_pConnMgr->SetDoingLeastIdle(target, doing, len, &b);
    return b;
}

// --- Quota ---

void update_all_desc_quotas(int nExtra, int nMax)
{
    if (g_pConnMgr) g_pConnMgr->UpdateAllDescQuotas(nExtra, nMax);
}

// --- Connection lifecycle ---

int boot_off(dbref target, const UTF8 *message)
{
    int n = 0;
    if (g_pConnMgr) g_pConnMgr->BootOff(target, message, &n);
    return n;
}

int boot_by_port(SOCKET port, bool bGod, const UTF8 *message)
{
    int n = 0;
    if (g_pConnMgr) g_pConnMgr->BootByPort(port, bGod, message, &n);
    return n;
}

// --- Idle check ---

void check_idle(void)
{
    if (g_pConnMgr) g_pConnMgr->CheckIdle();
}

// --- Emergency shutdown ---
// Called from do_shutdown() SHUTDN_PANIC path.  Closes all sockets.
//
void emergency_shutdown(void)
{
    if (g_pConnMgr) g_pConnMgr->EmergencyShutdown();
}

// --- Low-level descriptor I/O ---

void queue_write_LEN(DESC *d, const UTF8 *data, size_t len)
{
    if (g_pConnMgr) g_pConnMgr->DescQueueWrite(d, data, len);
}

void queue_string(DESC *d, const UTF8 *text)
{
    if (g_pConnMgr) g_pConnMgr->DescQueueString(d, text);
}

// --- Driver lifecycle ---
// These are driver-side cleanup calls that the engine invokes during
// SHUTDN_PANIC.  In the future, the engine should signal the driver
// and let the driver handle these directly.
//
void final_modules(void)
{
    // In the .so build, this is a no-op from the engine side.
    // The driver handles module cleanup after engine shutdown.
}

void final_stubslave(void)
{
    // In the .so build, this is a no-op from the engine side.
    // The driver handles stubslave cleanup after engine shutdown.
}

// ---------------------------------------------------------------------------
// Driver data accessors — engine calls these instead of using externs.
// ---------------------------------------------------------------------------

pid_t game_pid;  // Cached copy, filled by conn_bridge_init.

static NAMETAB *s_pDefaultCharsetNametab = nullptr;
static NAMETAB *s_pSigactionsNametab = nullptr;
static NAMETAB *s_pLogoutCmdtable = nullptr;

NAMETAB *conn_bridge_get_charset_nametab(void)
{
    if (nullptr == s_pDefaultCharsetNametab && g_pDriverCtl)
    {
        g_pDriverCtl->GetCharsetNametab(&s_pDefaultCharsetNametab);
    }
    return s_pDefaultCharsetNametab;
}

NAMETAB *conn_bridge_get_sigactions_nametab(void)
{
    if (nullptr == s_pSigactionsNametab && g_pDriverCtl)
    {
        g_pDriverCtl->GetSigactionsNametab(&s_pSigactionsNametab);
    }
    return s_pSigactionsNametab;
}

NAMETAB *conn_bridge_get_logout_cmdtable(void)
{
    if (nullptr == s_pLogoutCmdtable && g_pDriverCtl)
    {
        g_pDriverCtl->GetLogoutCmdtable(&s_pLogoutCmdtable);
    }
    return s_pLogoutCmdtable;
}

// ---------------------------------------------------------------------------
// Login-screen command handler bridges — these have the same signature as
// the real functions in net.cpp so they can be used as function pointers
// in command.cpp's command tables.
// ---------------------------------------------------------------------------

void logged_out0(dbref executor, dbref caller, dbref enactor, int eval,
    int key)
{
    if (g_pDriverCtl)
    {
        g_pDriverCtl->LoggedOut0(executor, caller, enactor, eval, key);
    }
}

void logged_out1(dbref executor, dbref caller, dbref enactor, int eval,
    int key, UTF8 *arg, const UTF8 *cargs[], int ncargs)
{
    if (g_pDriverCtl)
    {
        g_pDriverCtl->LoggedOut1(executor, caller, enactor, eval, key,
            arg, cargs, ncargs);
    }
}

void do_version(dbref executor, dbref caller, dbref enactor, int eval,
    int key)
{
    if (g_pDriverCtl)
    {
        g_pDriverCtl->DoVersion(executor, caller, enactor, eval, key);
    }
}

void do_startslave(dbref executor, dbref caller, dbref enactor, int eval,
    int key)
{
    if (g_pDriverCtl)
    {
        g_pDriverCtl->DoStartSlave(executor, caller, enactor, eval, key);
    }
}

// ---------------------------------------------------------------------------
// Task_ProcessCommand — the scheduler uses this as a function pointer.
// We fetch the real pointer from the driver via COM once, then cache it.
// cque.cpp compares task function pointers by address, so the engine
// must use the same address the driver registered.
// ---------------------------------------------------------------------------

static void (*s_pfTaskProcessCommand)(void *, int) = nullptr;

void Task_ProcessCommand(void *arg_voidptr, int arg_iInteger)
{
    if (nullptr == s_pfTaskProcessCommand && g_pDriverCtl)
    {
        g_pDriverCtl->GetTaskProcessCommand(&s_pfTaskProcessCommand);
    }
    if (s_pfTaskProcessCommand)
    {
        s_pfTaskProcessCommand(arg_voidptr, arg_iInteger);
    }
}

// ---------------------------------------------------------------------------
// Restart / dump helpers.
// ---------------------------------------------------------------------------

void dump_restart_db(void)
{
    if (g_pDriverCtl)
    {
        g_pDriverCtl->DumpRestartDb();
    }
}

// ---------------------------------------------------------------------------
// Connection-related bridges.
// ---------------------------------------------------------------------------

void desc_reload(dbref player)
{
    if (g_pConnMgr)
    {
        g_pConnMgr->DescReload(player);
    }
}

LBUF_OFFSET trimmed_name(const dbref player, UTF8 cbuff[MBUF_SIZE],
    const LBUF_OFFSET nMin, const LBUF_OFFSET nMax,
    const LBUF_OFFSET nPad)
{
    unsigned short result = 0;
    if (g_pConnMgr)
    {
        g_pConnMgr->TrimmedName(player, cbuff, MBUF_SIZE, nMin, nMax,
            nPad, &result);
    }
    return result;
}

void make_portlist(dbref player, dbref target, UTF8 *buff, UTF8 **bufc)
{
    if (g_pConnMgr)
    {
        g_pConnMgr->MakePortlist(player, target, buff, bufc);
    }
}

void for_each_player_desc(dbref target,
    void (*callback)(DESC *d, void *context), void *context)
{
    if (g_pConnMgr)
    {
        g_pConnMgr->ForEachPlayerDesc(target, callback, context);
    }
}

// Softcode function bridges — same signature as the net.cpp originals.
//
void fun_host(FUN *fp, UTF8 *buff, UTF8 **bufc, dbref executor,
    dbref caller, dbref enactor, int eval, UTF8 *fargs[], int nfargs,
    const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);
    if (g_pConnMgr)
    {
        g_pConnMgr->FunHost(executor, caller, enactor, eval,
            fargs, nfargs, buff, bufc);
    }
}

void fun_doing(FUN *fp, UTF8 *buff, UTF8 **bufc, dbref executor,
    dbref caller, dbref enactor, int eval, UTF8 *fargs[], int nfargs,
    const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);
    if (g_pConnMgr)
    {
        g_pConnMgr->FunDoing(executor, caller, enactor, eval,
            fargs, nfargs, buff, bufc);
    }
}

void fun_siteinfo(FUN *fp, UTF8 *buff, UTF8 **bufc, dbref executor,
    dbref caller, dbref enactor, int eval, UTF8 *fargs[], int nfargs,
    const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);
    if (g_pConnMgr)
    {
        g_pConnMgr->FunSiteinfo(executor, caller, enactor, eval,
            fargs, nfargs, buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// Driver-owned nametab arrays.  The originals live in net.cpp and
// signals.cpp (driver).  Engine code (conf.cpp config tables, command.cpp
// do_list) takes addresses of these arrays at compile time, so they must
// exist as actual symbols in engine.so.  We duplicate the read-only content.
// ---------------------------------------------------------------------------

NAMETAB default_charset_nametab[] =
{
    {T("ascii"),           5,       0,     CHARSET_ASCII},
    {T("oem"),             3,       0,     CHARSET_CP437},
    {T("cp437"),           5,       0,     CHARSET_CP437},
    {T("latin-1"),         7,       0,     CHARSET_LATIN1},
    {T("latin-2"),         7,       0,     CHARSET_LATIN2},
    {T("iso8859-1"),       9,       0,     CHARSET_LATIN1},
    {T("iso8859-2"),       9,       0,     CHARSET_LATIN2},
    {nullptr,              0,       0,     0}
};

NAMETAB sigactions_nametab[] =
{
    {T("exit"),        3,  0,  SA_EXIT},
    {T("default"),     1,  0,  SA_DFLT},
    {static_cast<UTF8 *>(nullptr), 0,  0,  0}
};

NAMETAB logout_cmdtable[] =
{
    {T("DOING"),         5,  CA_PUBLIC,  CMD_DOING},
    {T("LOGOUT"),        6,  CA_PUBLIC,  CMD_LOGOUT},
    {T("OUTPUTPREFIX"), 12,  CA_PUBLIC,  CMD_PREFIX|CMD_NOxFIX},
    {T("OUTPUTSUFFIX"), 12,  CA_PUBLIC,  CMD_SUFFIX|CMD_NOxFIX},
    {T("QUIT"),          4,  CA_PUBLIC,  CMD_QUIT},
    {T("SESSION"),       7,  CA_PUBLIC,  CMD_SESSION},
    {T("WHO"),           3,  CA_PUBLIC,  CMD_WHO},
    {T("PUEBLOCLIENT"), 12,  CA_PUBLIC,  CMD_PUEBLOCLIENT},
    {T("HELP"),          4,  CA_PUBLIC,  CMD_HELP},
    {T("INFO"),          4,  CA_PUBLIC,  CMD_INFO},
    {nullptr,            0,          0,         0}
};

// ---------------------------------------------------------------------------
// GANL adapter bridges.
// ---------------------------------------------------------------------------

// GanlAdapter is driver-owned.  The engine references g_GanlAdapter for
// email send and prepare_for_restart.  We provide a stub object that
// delegates through COM.  The class declaration is in ganl_stub.h.
//
#include "ganl_stub.h"

bool GanlAdapter::start_email_send(dbref executor, const UTF8 *recipient,
    const UTF8 *subject, const UTF8 *body)
{
    bool result = false;
    if (g_pDriverCtl)
    {
        g_pDriverCtl->StartEmailSend(executor, recipient, subject,
            body, &result);
    }
    return result;
}

void GanlAdapter::prepare_for_restart(void)
{
    if (g_pDriverCtl)
    {
        g_pDriverCtl->PrepareNetworkForRestart();
    }
}

GanlAdapter g_GanlAdapter;

// ---------------------------------------------------------------------------
// mux_subnets — the class body is in net.cpp (driver).  mudconf contains
// a mux_subnets member, so the constructor/destructor must be available
// when conf.cpp constructs mudconf.  We provide stub implementations
// here; the driver initializes the real access list separately.
// ---------------------------------------------------------------------------

mux_subnets::mux_subnets() : msnRoot(nullptr)
{
}

mux_subnets::~mux_subnets()
{
    // Stub — the real destructor is in net.cpp (driver).
    // Engine-side mudconf members are never populated, so nothing to free.
    //
    msnRoot = nullptr;
}

