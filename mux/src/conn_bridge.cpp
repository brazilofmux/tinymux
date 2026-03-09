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

void send_text_to_player(dbref target, const mux_string &text)
{
    // For the COM bridge, convert mux_string to raw UTF-8 and send.
    // This loses color encoding; a future mux_IConnectionManager v2
    // can add a mux_string-aware method.
    //
    if (g_pConnMgr)
    {
        UTF8 buf[LBUF_SIZE];
        size_t n = text.export_TextPlain(buf, CursorMin, CursorMax, sizeof(buf));
        buf[n] = '\0';
        g_pConnMgr->SendText(target, buf);
    }
}

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

