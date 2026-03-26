/*! \file mux_main.cpp
 * \brief Script-mode driver for TinyMUX.
 *
 * Minimal binary that loads engine.so via COM and runs softcode from
 * stdin or a file.  No networking, no telnet, no SSL, no descriptors.
 * Same engine, different front-end.
 *
 * Usage:
 *   muxscript -g /path/to/game -c netmux.conf < script.mux
 *   muxscript -e 'think add(2,3)'
 *   echo 'think sha1(hello)' | muxscript
 *
 * Game directory resolution (in priority order):
 *   1. -g <dir> command-line flag
 *   2. MUX_HOME environment variable
 *   3. Current working directory
 *
 * engine.so is loaded from <gamedir>/bin/engine.so.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "libmux.h"
#include "modules.h"
#include "timeutil.h"
#include "alloc.h"
#include "svdrand.h"

#include "_build.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <cerrno>

// Script mode doesn't include externs.h or db.h, so we define the
// constants we need directly.
//
#ifndef GOD
#define GOD (static_cast<dbref>(1))
#endif
#ifndef NOTHING
#define NOTHING (-1)
#endif


// ---------------------------------------------------------------------------
// Globals.
// ---------------------------------------------------------------------------

static mux_IGameEngine *g_pEngine = nullptr;
static char g_gamedir[4096];
static bool g_script_shutdown = false;
static dbref g_player = GOD;      // Player identity for commands

// ---------------------------------------------------------------------------
// CScriptConnectionManager — stub mux_IConnectionManager for script mode.
// Output goes to stdout.  No descriptors exist.
// ---------------------------------------------------------------------------

class CScriptConnectionManager : public mux_IConnectionManager
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    // Output
    virtual MUX_RESULT SendText(dbref target, const UTF8 *text);
    virtual MUX_RESULT SendRaw(dbref target, const UTF8 *data, size_t len);
    virtual MUX_RESULT BroadcastAndFlush(int inflags, const UTF8 *text);
    virtual MUX_RESULT SendProgPrompt(dbref target);
    virtual MUX_RESULT SendKeepaliveNops(void);
    virtual MUX_RESULT SendGmcp(dbref target, const UTF8 *pkg, const UTF8 *json);

    // Queries by dbref
    virtual MUX_RESULT GetTotalConnections(int *pCount);
    virtual MUX_RESULT CountPlayerDescs(dbref target, int *pCount);
    virtual MUX_RESULT SumPlayerCommandCount(dbref target, int *pCount);
    virtual MUX_RESULT FetchHeight(dbref target, int *pHeight);
    virtual MUX_RESULT FetchWidth(dbref target, int *pWidth);
    virtual MUX_RESULT FetchIdle(dbref target, int *pIdle);
    virtual MUX_RESULT FetchConnect(dbref target, int *pConnect);

    // Queries by opaque DESC handle
    virtual MUX_RESULT FindDescBySocket(SOCKET s, DESC **ppDesc);
    virtual MUX_RESULT FindDescByPlayer(dbref target, DESC **ppDesc);
    virtual MUX_RESULT DescPlayer(const DESC *d, dbref *pPlayer);
    virtual MUX_RESULT DescHeight(const DESC *d, int *pHeight);
    virtual MUX_RESULT DescWidth(const DESC *d, int *pWidth);
    virtual MUX_RESULT DescEncoding(const DESC *d, int *pEncoding);
    virtual MUX_RESULT DescCommandCount(const DESC *d, int *pCount);
    virtual MUX_RESULT DescTtype(const DESC *d, const UTF8 **ppTtype);
    virtual MUX_RESULT DescLastTime(const DESC *d, CLinearTimeAbsolute *pTime);
    virtual MUX_RESULT DescConnectedAt(const DESC *d, CLinearTimeAbsolute *pTime);
    virtual MUX_RESULT DescNvtHimState(const DESC *d, unsigned char chOption, int *pState);
    virtual MUX_RESULT DescSocketState(const DESC *d, SocketState *pState);

    // Iteration
    virtual MUX_RESULT ForEachConnectedPlayer(
        void (*callback)(dbref player, void *context), void *context);
    virtual MUX_RESULT ForEachConnectedDesc(
        void (*callback)(dbref player, SOCKET sock, void *context), void *context);

    // @program state
    virtual MUX_RESULT PlayerHasProgram(dbref target, bool *pbHas);
    virtual MUX_RESULT DetachPlayerProgram(dbref target, program_data **ppProgram);
    virtual MUX_RESULT SetPlayerProgram(dbref target, program_data *program);

    // Encoding / Display
    virtual MUX_RESULT SetPlayerEncoding(dbref target, int encoding);
    virtual MUX_RESULT ResetPlayerEncoding(dbref target);
    virtual MUX_RESULT SetDoingAll(dbref target, const UTF8 *doing, size_t len);
    virtual MUX_RESULT SetDoingLeastIdle(dbref target, const UTF8 *doing, size_t len, bool *pbFound);

    // Quota
    virtual MUX_RESULT UpdateAllDescQuotas(int nExtra, int nMax);

    // Connection lifecycle
    virtual MUX_RESULT BootOff(dbref target, const UTF8 *message, int *pCount);
    virtual MUX_RESULT BootByPort(SOCKET port, bool bGod, const UTF8 *message, int *pCount);

    // Idle check
    virtual MUX_RESULT CheckIdle(void);
    virtual MUX_RESULT EmergencyShutdown(void);

    virtual MUX_RESULT DescQueueWrite(DESC *d, const UTF8 *data, size_t len);
    virtual MUX_RESULT DescQueueString(DESC *d, const UTF8 *text);

    virtual MUX_RESULT DescReload(dbref player);
    virtual MUX_RESULT TrimmedName(dbref player, UTF8 *cbuff,
        size_t cbuffSize, unsigned short nMin, unsigned short nMax,
        unsigned short nPad, unsigned short *pResult);
    virtual MUX_RESULT MakePortlist(dbref player, dbref target,
        UTF8 *buff, UTF8 **bufc);
    virtual MUX_RESULT ForEachPlayerDesc(dbref target,
        void (*callback)(DESC *d, void *context), void *context);
    virtual MUX_RESULT FunHost(dbref executor, dbref caller, dbref enactor,
        int eval, UTF8 *fargs[], int nfargs, UTF8 *buff,
        UTF8 **bufc);
    virtual MUX_RESULT FunDoing(dbref executor, dbref caller, dbref enactor,
        int eval, UTF8 *fargs[], int nfargs, UTF8 *buff,
        UTF8 **bufc);
    virtual MUX_RESULT FunSiteinfo(dbref executor, dbref caller,
        dbref enactor, int eval, UTF8 *fargs[], int nfargs,
        UTF8 *buff, UTF8 **bufc);

    CScriptConnectionManager(void);
    virtual ~CScriptConnectionManager();

private:
    uint32_t m_cRef;
};

CScriptConnectionManager::CScriptConnectionManager(void) : m_cRef(1)
{
}

CScriptConnectionManager::~CScriptConnectionManager()
{
}

MUX_RESULT CScriptConnectionManager::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IConnectionManager *>(this);
    }
    else if (IID_IConnectionManager == iid)
    {
        *ppv = static_cast<mux_IConnectionManager *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    AddRef();
    return MUX_S_OK;
}

uint32_t CScriptConnectionManager::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CScriptConnectionManager::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

// --- Output ---

MUX_RESULT CScriptConnectionManager::SendText(dbref target, const UTF8 *text)
{
    UNUSED_PARAMETER(target);
    if (text)
    {
        fputs(reinterpret_cast<const char *>(text), stdout);
    }
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::SendRaw(dbref target, const UTF8 *data, size_t len)
{
    UNUSED_PARAMETER(target);
    if (data && len > 0)
    {
        fwrite(data, 1, len, stdout);
    }
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::BroadcastAndFlush(int inflags, const UTF8 *text)
{
    UNUSED_PARAMETER(inflags);
    if (text)
    {
        fputs(reinterpret_cast<const char *>(text), stdout);
        fflush(stdout);
    }
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::SendProgPrompt(dbref target)
{
    UNUSED_PARAMETER(target);
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::SendKeepaliveNops(void)
{
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::SendGmcp(dbref target, const UTF8 *pkg, const UTF8 *json)
{
    UNUSED_PARAMETER(target);
    UNUSED_PARAMETER(pkg);
    UNUSED_PARAMETER(json);
    return MUX_S_OK;
}

// --- Queries by dbref ---

MUX_RESULT CScriptConnectionManager::GetTotalConnections(int *pCount)
{
    if (nullptr == pCount) return MUX_E_INVALIDARG;
    *pCount = 0;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::CountPlayerDescs(dbref target, int *pCount)
{
    UNUSED_PARAMETER(target);
    if (nullptr == pCount) return MUX_E_INVALIDARG;
    *pCount = 0;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::SumPlayerCommandCount(dbref target, int *pCount)
{
    UNUSED_PARAMETER(target);
    if (nullptr == pCount) return MUX_E_INVALIDARG;
    *pCount = 0;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::FetchHeight(dbref target, int *pHeight)
{
    UNUSED_PARAMETER(target);
    if (nullptr == pHeight) return MUX_E_INVALIDARG;
    *pHeight = 24;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::FetchWidth(dbref target, int *pWidth)
{
    UNUSED_PARAMETER(target);
    if (nullptr == pWidth) return MUX_E_INVALIDARG;
    *pWidth = 78;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::FetchIdle(dbref target, int *pIdle)
{
    UNUSED_PARAMETER(target);
    if (nullptr == pIdle) return MUX_E_INVALIDARG;
    *pIdle = 0;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::FetchConnect(dbref target, int *pConnect)
{
    UNUSED_PARAMETER(target);
    if (nullptr == pConnect) return MUX_E_INVALIDARG;
    *pConnect = 0;
    return MUX_S_OK;
}

// --- Queries by opaque DESC handle ---

MUX_RESULT CScriptConnectionManager::FindDescBySocket(SOCKET s, DESC **ppDesc)
{
    UNUSED_PARAMETER(s);
    if (nullptr == ppDesc) return MUX_E_INVALIDARG;
    *ppDesc = nullptr;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::FindDescByPlayer(dbref target, DESC **ppDesc)
{
    UNUSED_PARAMETER(target);
    if (nullptr == ppDesc) return MUX_E_INVALIDARG;
    *ppDesc = nullptr;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::DescPlayer(const DESC *d, dbref *pPlayer)
{
    UNUSED_PARAMETER(d);
    if (nullptr == pPlayer) return MUX_E_INVALIDARG;
    *pPlayer = NOTHING;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::DescHeight(const DESC *d, int *pHeight)
{
    UNUSED_PARAMETER(d);
    if (nullptr == pHeight) return MUX_E_INVALIDARG;
    *pHeight = 24;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::DescWidth(const DESC *d, int *pWidth)
{
    UNUSED_PARAMETER(d);
    if (nullptr == pWidth) return MUX_E_INVALIDARG;
    *pWidth = 78;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::DescEncoding(const DESC *d, int *pEncoding)
{
    UNUSED_PARAMETER(d);
    if (nullptr == pEncoding) return MUX_E_INVALIDARG;
    *pEncoding = 0;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::DescCommandCount(const DESC *d, int *pCount)
{
    UNUSED_PARAMETER(d);
    if (nullptr == pCount) return MUX_E_INVALIDARG;
    *pCount = 0;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::DescTtype(const DESC *d, const UTF8 **ppTtype)
{
    UNUSED_PARAMETER(d);
    if (nullptr == ppTtype) return MUX_E_INVALIDARG;
    *ppTtype = nullptr;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::DescLastTime(const DESC *d, CLinearTimeAbsolute *pTime)
{
    UNUSED_PARAMETER(d);
    UNUSED_PARAMETER(pTime);
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::DescConnectedAt(const DESC *d, CLinearTimeAbsolute *pTime)
{
    UNUSED_PARAMETER(d);
    UNUSED_PARAMETER(pTime);
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::DescNvtHimState(const DESC *d, unsigned char chOption, int *pState)
{
    UNUSED_PARAMETER(d);
    UNUSED_PARAMETER(chOption);
    if (nullptr == pState) return MUX_E_INVALIDARG;
    *pState = 0;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::DescSocketState(const DESC *d, SocketState *pState)
{
    UNUSED_PARAMETER(d);
    if (nullptr == pState) return MUX_E_INVALIDARG;
    // SocketState::Accepted == 1, but the enum is only forward-declared
    // in modules.h.  Use a cast to avoid needing the full definition.
    *pState = static_cast<SocketState>(1);
    return MUX_S_OK;
}

// --- Iteration ---

MUX_RESULT CScriptConnectionManager::ForEachConnectedPlayer(
    void (*callback)(dbref player, void *context), void *context)
{
    UNUSED_PARAMETER(callback);
    UNUSED_PARAMETER(context);
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::ForEachConnectedDesc(
    void (*callback)(dbref player, SOCKET sock, void *context), void *context)
{
    UNUSED_PARAMETER(callback);
    UNUSED_PARAMETER(context);
    return MUX_S_OK;
}

// --- @program state ---

MUX_RESULT CScriptConnectionManager::PlayerHasProgram(dbref target, bool *pbHas)
{
    UNUSED_PARAMETER(target);
    if (nullptr == pbHas) return MUX_E_INVALIDARG;
    *pbHas = false;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::DetachPlayerProgram(dbref target, program_data **ppProgram)
{
    UNUSED_PARAMETER(target);
    if (nullptr == ppProgram) return MUX_E_INVALIDARG;
    *ppProgram = nullptr;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::SetPlayerProgram(dbref target, program_data *program)
{
    UNUSED_PARAMETER(target);
    UNUSED_PARAMETER(program);
    return MUX_S_OK;
}

// --- Encoding / Display ---

MUX_RESULT CScriptConnectionManager::SetPlayerEncoding(dbref target, int encoding)
{
    UNUSED_PARAMETER(target);
    UNUSED_PARAMETER(encoding);
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::ResetPlayerEncoding(dbref target)
{
    UNUSED_PARAMETER(target);
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::SetDoingAll(dbref target, const UTF8 *doing, size_t len)
{
    UNUSED_PARAMETER(target);
    UNUSED_PARAMETER(doing);
    UNUSED_PARAMETER(len);
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::SetDoingLeastIdle(dbref target, const UTF8 *doing, size_t len, bool *pbFound)
{
    UNUSED_PARAMETER(target);
    UNUSED_PARAMETER(doing);
    UNUSED_PARAMETER(len);
    if (nullptr == pbFound) return MUX_E_INVALIDARG;
    *pbFound = false;
    return MUX_S_OK;
}

// --- Quota ---

MUX_RESULT CScriptConnectionManager::UpdateAllDescQuotas(int nExtra, int nMax)
{
    UNUSED_PARAMETER(nExtra);
    UNUSED_PARAMETER(nMax);
    return MUX_S_OK;
}

// --- Connection lifecycle ---

MUX_RESULT CScriptConnectionManager::BootOff(dbref target, const UTF8 *message, int *pCount)
{
    UNUSED_PARAMETER(target);
    UNUSED_PARAMETER(message);
    if (pCount) *pCount = 0;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::BootByPort(SOCKET port, bool bGod, const UTF8 *message, int *pCount)
{
    UNUSED_PARAMETER(port);
    UNUSED_PARAMETER(bGod);
    UNUSED_PARAMETER(message);
    if (pCount) *pCount = 0;
    return MUX_S_OK;
}

// --- Idle check ---

MUX_RESULT CScriptConnectionManager::CheckIdle(void)
{
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::EmergencyShutdown(void)
{
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::DescQueueWrite(DESC *d, const UTF8 *data, size_t len)
{
    UNUSED_PARAMETER(d);
    if (data && len > 0)
    {
        fwrite(data, 1, len, stdout);
    }
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::DescQueueString(DESC *d, const UTF8 *text)
{
    UNUSED_PARAMETER(d);
    if (text)
    {
        fputs(reinterpret_cast<const char *>(text), stdout);
    }
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::DescReload(dbref player)
{
    UNUSED_PARAMETER(player);
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::TrimmedName(dbref player, UTF8 *cbuff,
    size_t cbuffSize, unsigned short nMin, unsigned short nMax,
    unsigned short nPad, unsigned short *pResult)
{
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(cbuff);
    UNUSED_PARAMETER(cbuffSize);
    UNUSED_PARAMETER(nMin);
    UNUSED_PARAMETER(nMax);
    UNUSED_PARAMETER(nPad);
    if (nullptr == pResult) return MUX_E_INVALIDARG;
    *pResult = 0;
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::MakePortlist(dbref player, dbref target,
    UTF8 *buff, UTF8 **bufc)
{
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(target);
    UNUSED_PARAMETER(buff);
    UNUSED_PARAMETER(bufc);
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::ForEachPlayerDesc(dbref target,
    void (*callback)(DESC *d, void *context), void *context)
{
    UNUSED_PARAMETER(target);
    UNUSED_PARAMETER(callback);
    UNUSED_PARAMETER(context);
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::FunHost(dbref executor, dbref caller,
    dbref enactor, int eval, UTF8 *fargs[], int nfargs, UTF8 *buff,
    UTF8 **bufc)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(buff);
    UNUSED_PARAMETER(bufc);
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::FunDoing(dbref executor, dbref caller,
    dbref enactor, int eval, UTF8 *fargs[], int nfargs, UTF8 *buff,
    UTF8 **bufc)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(buff);
    UNUSED_PARAMETER(bufc);
    return MUX_S_OK;
}

MUX_RESULT CScriptConnectionManager::FunSiteinfo(dbref executor, dbref caller,
    dbref enactor, int eval, UTF8 *fargs[], int nfargs, UTF8 *buff,
    UTF8 **bufc)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(buff);
    UNUSED_PARAMETER(bufc);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CScriptConnectionManagerFactory
// ---------------------------------------------------------------------------

DEFINE_FACTORY(CScriptConnectionManagerFactory)

CScriptConnectionManagerFactory::CScriptConnectionManagerFactory(void) : m_cRef(1)
{
}

CScriptConnectionManagerFactory::~CScriptConnectionManagerFactory()
{
}

MUX_RESULT CScriptConnectionManagerFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else if (mux_IID_IClassFactory == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    AddRef();
    return MUX_S_OK;
}

uint32_t CScriptConnectionManagerFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CScriptConnectionManagerFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CScriptConnectionManagerFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    CScriptConnectionManager *pConnMgr = nullptr;
    try
    {
        pConnMgr = new CScriptConnectionManager;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pConnMgr)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pConnMgr->QueryInterface(iid, ppv);
    pConnMgr->Release();
    return mr;
}

MUX_RESULT CScriptConnectionManagerFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CScriptDriverControl — stub mux_IDriverControl for script mode.
// ---------------------------------------------------------------------------

class CScriptDriverControl : public mux_IDriverControl
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT ShutdownRequest(void);
    virtual MUX_RESULT GetRestarting(bool *pbRestarting);
    virtual MUX_RESULT SiteUpdate(const UTF8 *subnetStr,
        dbref player, UTF8 *cmd, int operation);

    virtual MUX_RESULT GetPid(int *pPid);
    virtual MUX_RESULT GetCharsetNametab(NAMETAB **ppTable);
    virtual MUX_RESULT GetSigactionsNametab(NAMETAB **ppTable);
    virtual MUX_RESULT GetLogoutCmdtable(NAMETAB **ppTable);
    virtual MUX_RESULT LoggedOut0(dbref executor, dbref caller,
        dbref enactor, int eval, int key);
    virtual MUX_RESULT LoggedOut1(dbref executor, dbref caller,
        dbref enactor, int eval, int key, UTF8 *arg,
        const UTF8 *cargs[], int ncargs);
    virtual MUX_RESULT DoVersion(dbref executor, dbref caller,
        dbref enactor, int eval, int key);
    virtual MUX_RESULT DoStartSlave(dbref executor, dbref caller,
        dbref enactor, int eval, int key);
    virtual MUX_RESULT GetTaskProcessCommand(
        void (**ppfTask)(void *, int));
    virtual MUX_RESULT DumpRestartDb(void);
    virtual MUX_RESULT PrepareNetworkForRestart(void);
    virtual MUX_RESULT StartEmailSend(dbref executor, const UTF8 *recipient,
        const UTF8 *subject, const UTF8 *body, bool *pResult);
    virtual MUX_RESULT ListSiteInfo(dbref player);

    CScriptDriverControl(void);
    virtual ~CScriptDriverControl();

private:
    uint32_t m_cRef;
};

CScriptDriverControl::CScriptDriverControl(void) : m_cRef(1)
{
}

CScriptDriverControl::~CScriptDriverControl()
{
}

MUX_RESULT CScriptDriverControl::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IDriverControl *>(this);
    }
    else if (IID_IDriverControl == iid)
    {
        *ppv = static_cast<mux_IDriverControl *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    AddRef();
    return MUX_S_OK;
}

uint32_t CScriptDriverControl::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CScriptDriverControl::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CScriptDriverControl::ShutdownRequest(void)
{
    g_script_shutdown = true;
    return MUX_S_OK;
}

MUX_RESULT CScriptDriverControl::GetRestarting(bool *pbRestarting)
{
    if (nullptr == pbRestarting) return MUX_E_INVALIDARG;
    *pbRestarting = false;
    return MUX_S_OK;
}

MUX_RESULT CScriptDriverControl::SiteUpdate(const UTF8 *subnetStr,
    dbref player, UTF8 *cmd, int operation)
{
    UNUSED_PARAMETER(subnetStr);
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(cmd);
    UNUSED_PARAMETER(operation);
    return MUX_S_OK;
}

MUX_RESULT CScriptDriverControl::GetPid(int *pPid)
{
    if (nullptr == pPid) return MUX_E_INVALIDARG;
    *pPid = static_cast<int>(getpid());
    return MUX_S_OK;
}

MUX_RESULT CScriptDriverControl::GetCharsetNametab(NAMETAB **ppTable)
{
    if (nullptr == ppTable) return MUX_E_INVALIDARG;
    *ppTable = nullptr;
    return MUX_S_OK;
}

MUX_RESULT CScriptDriverControl::GetSigactionsNametab(NAMETAB **ppTable)
{
    if (nullptr == ppTable) return MUX_E_INVALIDARG;
    *ppTable = nullptr;
    return MUX_S_OK;
}

MUX_RESULT CScriptDriverControl::GetLogoutCmdtable(NAMETAB **ppTable)
{
    if (nullptr == ppTable) return MUX_E_INVALIDARG;
    *ppTable = nullptr;
    return MUX_S_OK;
}

MUX_RESULT CScriptDriverControl::LoggedOut0(dbref executor, dbref caller,
    dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    return MUX_S_OK;
}

MUX_RESULT CScriptDriverControl::LoggedOut1(dbref executor, dbref caller,
    dbref enactor, int eval, int key, UTF8 *arg,
    const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);
    return MUX_S_OK;
}

MUX_RESULT CScriptDriverControl::DoVersion(dbref executor, dbref caller,
    dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);

#if defined(ALPHA)
    fprintf(stdout, "MUX %s [ALPHA]\r\n", MUX_VERSION);
#elif defined(BETA)
    fprintf(stdout, "MUX %s [BETA]\r\n", MUX_VERSION);
#else
    fprintf(stdout, "MUX %s [%s]\r\n", MUX_VERSION, MUX_RELEASE_DATE);
#endif
    UNUSED_PARAMETER(executor);
    return MUX_S_OK;
}

MUX_RESULT CScriptDriverControl::DoStartSlave(dbref executor, dbref caller,
    dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    return MUX_S_OK;
}

MUX_RESULT CScriptDriverControl::GetTaskProcessCommand(
    void (**ppfTask)(void *, int))
{
    if (nullptr == ppfTask) return MUX_E_INVALIDARG;
    *ppfTask = nullptr;
    return MUX_S_OK;
}

MUX_RESULT CScriptDriverControl::DumpRestartDb(void)
{
    return MUX_S_OK;
}

MUX_RESULT CScriptDriverControl::PrepareNetworkForRestart(void)
{
    return MUX_S_OK;
}

MUX_RESULT CScriptDriverControl::StartEmailSend(dbref executor,
    const UTF8 *recipient, const UTF8 *subject, const UTF8 *body,
    bool *pResult)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(recipient);
    UNUSED_PARAMETER(subject);
    UNUSED_PARAMETER(body);
    if (nullptr == pResult) return MUX_E_INVALIDARG;
    *pResult = false;
    return MUX_S_OK;
}

MUX_RESULT CScriptDriverControl::ListSiteInfo(dbref player)
{
    UNUSED_PARAMETER(player);
    return MUX_E_NOTIMPLEMENTED;
}

// ---------------------------------------------------------------------------
// CScriptDriverControlFactory
// ---------------------------------------------------------------------------

DEFINE_FACTORY(CScriptDriverControlFactory)

CScriptDriverControlFactory::CScriptDriverControlFactory(void) : m_cRef(1)
{
}

CScriptDriverControlFactory::~CScriptDriverControlFactory()
{
}

MUX_RESULT CScriptDriverControlFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else if (mux_IID_IClassFactory == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    AddRef();
    return MUX_S_OK;
}

uint32_t CScriptDriverControlFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CScriptDriverControlFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CScriptDriverControlFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    CScriptDriverControl *pDC = nullptr;
    try
    {
        pDC = new CScriptDriverControl;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pDC)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pDC->QueryInterface(iid, ppv);
    pDC->Release();
    return mr;
}

MUX_RESULT CScriptDriverControlFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// COM registration — register script-mode factory classes before loading
// engine.so, so the engine can acquire them during its initialization.
// ---------------------------------------------------------------------------

static MUX_CLASS_INFO script_classes[] =
{
    { CID_ConnectionManager },
    { CID_DriverControl     }
};
#define NUM_SCRIPT_CLASSES (sizeof(script_classes)/sizeof(script_classes[0]))

extern "C" MUX_RESULT DCL_API script_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    if (CID_ConnectionManager == cid)
    {
        CScriptConnectionManagerFactory *pFactory = nullptr;
        try
        {
            pFactory = new CScriptConnectionManagerFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pFactory->QueryInterface(iid, ppv);
        pFactory->Release();
    }
    else if (CID_DriverControl == cid)
    {
        CScriptDriverControlFactory *pFactory = nullptr;
        try
        {
            pFactory = new CScriptDriverControlFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pFactory->QueryInterface(iid, ppv);
        pFactory->Release();
    }
    return mr;
}

// ---------------------------------------------------------------------------
// Game directory resolution and engine loading.
// ---------------------------------------------------------------------------

static bool resolve_gamedir(const char *flag_dir)
{
    const char *dir = flag_dir;

    if (!dir)
    {
        dir = getenv("MUX_HOME");
    }
    if (!dir)
    {
        dir = ".";
    }

    if (!realpath(dir, g_gamedir))
    {
        fprintf(stderr, "muxscript: cannot resolve game directory '%s': %s\n",
                dir, strerror(errno));
        return false;
    }

    char engine_path[4096 + 64];
    snprintf(engine_path, sizeof(engine_path), "%s/bin/engine.so", g_gamedir);
    if (access(engine_path, R_OK) != 0)
    {
        fprintf(stderr, "muxscript: engine.so not found at '%s'\n", engine_path);
        return false;
    }

    return true;
}

static MUX_RESULT init_com(void)
{
    MUX_RESULT mr = mux_InitModuleLibrary(IsMainProcess);
    if (MUX_FAILED(mr))
    {
        fprintf(stderr, "muxscript: mux_InitModuleLibrary failed (%d)\n", mr);
        return mr;
    }

    // Register script-mode driver classes BEFORE loading engine.so.
    mr = mux_RegisterClassObjects(NUM_SCRIPT_CLASSES, script_classes,
        script_GetClassObject);
    if (MUX_FAILED(mr))
    {
        fprintf(stderr, "muxscript: mux_RegisterClassObjects failed (%d)\n", mr);
        return mr;
    }

    // Build engine.so path relative to game directory.
    char engine_path[4096 + 64];
    snprintf(engine_path, sizeof(engine_path), "%s/bin/engine.so", g_gamedir);

    mr = mux_AddModule(T("engine"),
                        reinterpret_cast<const UTF8 *>(engine_path));
    if (MUX_FAILED(mr))
    {
        fprintf(stderr, "muxscript: cannot load '%s' (%d)\n", engine_path, mr);
        return mr;
    }

    // Create IGameEngine via COM.
    mr = mux_CreateInstance(CID_GameEngine, nullptr, UseSameProcess,
                            IID_IGameEngine,
                            reinterpret_cast<void **>(&g_pEngine));
    if (MUX_FAILED(mr) || !g_pEngine)
    {
        fprintf(stderr, "muxscript: cannot create IGameEngine (%d)\n", mr);
        return mr;
    }
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// Command execution helpers.
// ---------------------------------------------------------------------------

static void execute_command(const UTF8 *line)
{
    UTF8 *pLogBuf = nullptr;

    g_pEngine->PrepareForCommand(g_player);
    g_pEngine->ProcessCommand(g_player, g_player, g_player, 0, true,
        const_cast<UTF8 *>(line), nullptr, 0, &pLogBuf);
    g_pEngine->FinishCommand();
    fflush(stdout);
}

static void run_tasks_now(void)
{
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();
    g_pEngine->RunTasks(ltaNow);
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// Event loop: interleave stdin input with queue processing.
//
// Uses poll() on stdin so the engine can process queued commands
// between input lines.  After EOF on stdin, continues draining the
// queue until no tasks remain.
// ---------------------------------------------------------------------------

static void script_loop(FILE *input)
{
    int fd = fileno(input);

    // Set stdin to non-blocking so we can interleave input with
    // queue processing.
    //
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
    {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    bool eof_seen = false;
    UTF8 linebuf[8192];
    size_t linepos = 0;

    while (!g_script_shutdown)
    {
        // Determine poll timeout from the scheduler.
        // If no tasks are pending, use a short timeout when stdin
        // is still open, or exit if stdin is EOF.
        //
        int timeout_ms = -1;
        CLinearTimeAbsolute ltaNext;
        if (MUX_SUCCEEDED(g_pEngine->WhenNext(&ltaNext)))
        {
            CLinearTimeAbsolute ltaNow;
            ltaNow.GetUTC();
            if (ltaNext <= ltaNow)
            {
                timeout_ms = 0;
            }
            else
            {
                long ms = (ltaNext - ltaNow).ReturnMilliseconds();
                if (ms > 1000) ms = 1000;
                timeout_ms = (int)ms;
            }
        }
        else if (eof_seen)
        {
            // No pending tasks and stdin is closed — we're done.
            //
            break;
        }
        else
        {
            // No pending tasks but stdin is still open — wait for input.
            //
            timeout_ms = -1;
        }

        // After EOF, keep running the scheduler until @shutdown fires
        // or no more tasks remain.  This allows @wait/@notify chains
        // (like smoke tests) to complete fully.
        //

        // Poll for stdin readability.
        //
        if (!eof_seen)
        {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLIN;
            pfd.revents = 0;

            int found = poll(&pfd, 1, timeout_ms);
            if (found < 0 && EINTR != errno)
            {
                break;
            }

            // Read available bytes and process complete lines.
            //
            if (found > 0 && (pfd.revents & (POLLIN | POLLHUP)))
            {
                for (;;)
                {
                    int n = read(fd, linebuf + linepos,
                                 sizeof(linebuf) - linepos - 1);
                    if (n > 0)
                    {
                        linepos += (size_t)n;

                        // Process complete lines.
                        //
                        size_t start = 0;
                        for (size_t i = 0; i < linepos; i++)
                        {
                            if (linebuf[i] == '\n')
                            {
                                linebuf[i] = '\0';
                                size_t len = i - start;
                                if (len > 0 && linebuf[start + len - 1] == '\r')
                                {
                                    linebuf[start + len - 1] = '\0';
                                    len--;
                                }

                                UTF8 *line = linebuf + start;
                                // Skip comments and blank lines.
                                //
                                if (len > 0
                                    && !(line[0] == '#'
                                         && (len < 2 || line[1] < '0' || line[1] > '9')))
                                {
                                    execute_command(line);
                                }
                                start = i + 1;
                            }
                        }

                        // Shift remaining partial line to front.
                        //
                        if (start > 0)
                        {
                            linepos -= start;
                            if (linepos > 0)
                            {
                                memmove(linebuf, linebuf + start, linepos);
                            }
                        }

                        // Protect against line overflow.
                        //
                        if (linepos >= sizeof(linebuf) - 1)
                        {
                            linebuf[linepos] = '\0';
                            execute_command(linebuf);
                            linepos = 0;
                        }
                    }
                    else if (n == 0)
                    {
                        // EOF on stdin.
                        //
                        eof_seen = true;

                        // Process any remaining partial line.
                        //
                        if (linepos > 0)
                        {
                            linebuf[linepos] = '\0';
                            size_t len = linepos;
                            if (len > 0 && linebuf[len - 1] == '\r')
                            {
                                linebuf[--len] = '\0';
                            }
                            if (len > 0
                                && !(linebuf[0] == '#'
                                     && (len < 2 || linebuf[1] < '0' || linebuf[1] > '9')))
                            {
                                execute_command(linebuf);
                            }
                            linepos = 0;
                        }
                        break;
                    }
                    else
                    {
                        // EAGAIN — no more data right now.
                        //
                        break;
                    }
                }
            }
        }
        else
        {
            // stdin is EOF — just sleep until next task is due.
            //
            if (timeout_ms > 0)
            {
                poll(nullptr, 0, timeout_ms);
            }
        }

        // Run ready tasks.  Loop until no more are immediately ready,
        // since processing a task may queue additional tasks.
        //
        for (int i = 0; i < 100; i++)
        {
            run_tasks_now();

            // Check if more tasks are immediately ready.
            //
            CLinearTimeAbsolute ltaCheck;
            if (MUX_FAILED(g_pEngine->WhenNext(&ltaCheck)))
            {
                break;  // No more tasks.
            }
            CLinearTimeAbsolute ltaNow2;
            ltaNow2.GetUTC();
            if (ltaCheck > ltaNow2)
            {
                break;  // Next task is in the future.
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

static void usage(void)
{
    fprintf(stderr,
        "Usage: muxscript [options]\n"
        "  -g <dir>      Game directory (default: $MUX_HOME or cwd)\n"
        "  -c <config>   Configuration file (default: netmux.conf)\n"
        "  -e <expr>     Evaluate single expression\n"
        "  -p <player>   Run as player (dbref like #4 or number, default: #1)\n"
        "  --readonly    Don't save database on exit\n"
        "  --help        Show this help\n"
        "\n"
        "Reads softcode commands from stdin (or -e) and writes output to stdout.\n"
        "Loads engine.so from <gamedir>/bin/engine.so.\n"
        "\n"
        "Game directory resolution:\n"
        "  1. -g <dir> flag\n"
        "  2. MUX_HOME environment variable\n"
        "  3. Current working directory\n"
    );
}

int main(int argc, char *argv[])
{
    const char *conffile = "netmux.conf";
    const char *gamedir_flag = nullptr;
    const char *eval_expr = nullptr;
    const char *player_arg = nullptr;
    bool readonly = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-g") == 0 && i + 1 < argc)
        {
            gamedir_flag = argv[++i];
        }
        else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
        {
            conffile = argv[++i];
        }
        else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc)
        {
            eval_expr = argv[++i];
        }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
        {
            player_arg = argv[++i];
        }
        else if (strcmp(argv[i], "--readonly") == 0)
        {
            readonly = true;
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            usage();
            return 0;
        }
        else
        {
            fprintf(stderr, "muxscript: unknown option '%s'\n", argv[i]);
            usage();
            return 1;
        }
    }

    // Resolve game directory.
    if (!resolve_gamedir(gamedir_flag))
    {
        return 1;
    }

    // chdir to game directory so config file paths resolve correctly.
    if (chdir(g_gamedir) != 0)
    {
        fprintf(stderr, "muxscript: cannot chdir to '%s': %s\n",
                g_gamedir, strerror(errno));
        return 1;
    }

    // Runtime initialization.
    SeedRandomNumberGenerator();
    pool_init(POOL_LBUF, LBUF_SIZE);
    pool_init(POOL_MBUF, MBUF_SIZE);
    pool_init(POOL_SBUF, SBUF_SIZE);

    // Initialize COM and load engine.so.
    MUX_RESULT mr = init_com();
    if (MUX_FAILED(mr))
    {
        return 2;
    }

    // Load game database.
    mr = g_pEngine->LoadGame(reinterpret_cast<const UTF8 *>(conffile),
                              nullptr, false);
    if (MUX_FAILED(mr))
    {
        fprintf(stderr, "muxscript: LoadGame failed (%d)\n", mr);
        g_pEngine->Release();
        return 2;
    }

    // Set timing state.
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();
    g_pEngine->SetStartTime(ltaNow);
    g_pEngine->SetRestartTime(ltaNow);
    g_pEngine->SetRestartCount(0);
    g_pEngine->SetCpuCountFrom(ltaNow);

    // Post-load initialization.
    mr = g_pEngine->Startup();
    if (MUX_FAILED(mr))
    {
        fprintf(stderr, "muxscript: Startup failed (%d)\n", mr);
        g_pEngine->Release();
        return 2;
    }

    // Resolve -p player argument.
    //
    if (player_arg)
    {
        const char *p = player_arg;
        if (*p == '#') p++;
        char *end = nullptr;
        long val = strtol(p, &end, 10);
        if (end == p || *end != '\0' || val < 0)
        {
            fprintf(stderr, "muxscript: invalid player '%s' (use dbref like #4 or 4)\n",
                    player_arg);
            g_pEngine->Release();
            return 1;
        }
        g_player = static_cast<dbref>(val);
    }

    fprintf(stderr, "muxscript: loaded game from %s (player #%d)\n",
            g_gamedir, g_player);

    // Mark the player as connected so raw_notify() delivers output.
    //
    g_pEngine->MarkConnected(g_player);

    // Run script.
    if (eval_expr)
    {
        execute_command(reinterpret_cast<const UTF8 *>(eval_expr));
        run_tasks_now();
    }
    else
    {
        script_loop(stdin);
    }

    // Save and shutdown.
    if (!readonly)
    {
        g_pEngine->DumpDatabase();
    }
    g_pEngine->Shutdown();
    g_pEngine->Release();

    // Revoke script-mode classes and finalize.
    mux_RevokeClassObjects(NUM_SCRIPT_CLASSES, script_classes);
    mux_FinalizeModuleLibrary();

    return 0;
}
