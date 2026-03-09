/*! \file modules.cpp
 * \brief Driver-side module registration and class factory dispatch.
 *
 * This file registers the apriori COM class objects with the module library
 * and dispatches CID lookups to factory classes.  The factory and component
 * implementations live in engine_com.cpp (engine.so) and log.cpp.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "interface.h"
#include "driverstate.h"
#include "sqlite_backend.h"
#include "driver_log.h"

// Driver-side CIDs that live in netmux.
//
static MUX_CLASS_INFO driver_classes[] =
{
    { CID_ConnectionManager },
    { CID_DriverControl     }
};
#define NUM_DRIVER_CLASSES (sizeof(driver_classes)/sizeof(driver_classes[0]))

extern "C" MUX_RESULT DCL_API netmux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    if (CID_ConnectionManager == cid)
    {
        CConnectionManagerFactory *pFactory = nullptr;
        try
        {
            pFactory = new CConnectionManagerFactory;
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
        CDriverControlFactory *pFactory = nullptr;
        try
        {
            pFactory = new CDriverControlFactory;
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

// engine.so front-door — called directly since engine.so is linked.
//
extern "C" MUX_RESULT DCL_API mux_Register(void);
extern "C" MUX_RESULT DCL_API mux_Unregister(void);

MUX_RESULT init_modules(void)
{
    MUX_RESULT mr = mux_InitModuleLibrary(IsMainProcess);

    // Register driver-side CIDs (CConnectionManager).
    //
    if (MUX_SUCCEEDED(mr))
    {
        mr = mux_RegisterClassObjects(NUM_DRIVER_CLASSES, driver_classes, netmux_GetClassObject);
    }

    // Register engine-side CIDs via engine.so's COM front-door.
    //
    if (MUX_SUCCEEDED(mr))
    {
        mr = mux_Register();
    }
    return mr;
}

#ifdef STUB_SLAVE
QUEUE_INFO Queue_In;
QUEUE_INFO Queue_Out;

MUX_RESULT init_stubslave(void)
{
    Pipe_InitializeQueueInfo(&Queue_In);
    Pipe_InitializeQueueInfo(&Queue_Out);
    MUX_RESULT mr = mux_InitModuleLibraryPump(pipepump, &Queue_In, &Queue_Out);

    if (nullptr != mudstate.pISlaveControl)
    {
        mudstate.pISlaveControl->Release();
        mudstate.pISlaveControl = nullptr;
    }

    mr = mux_CreateInstance(CID_StubSlave, nullptr, UseSlaveProcess, IID_ISlaveControl, (void **)&mudstate.pISlaveControl);
    if (MUX_SUCCEEDED(mr))
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        g_pILog->log_text(T("Opened interface for StubSlave management."));
        ENDLOG;
    }
    else
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        g_pILog->log_text(tprintf(T("Failed to open interface for StubSlave management (%d)."), mr));
        ENDLOG;
    }
    return mr;
}

void final_stubslave(void)
{
    MUX_RESULT mr = MUX_S_OK;

    if (nullptr != mudstate.pISlaveControl)
    {
        mr = mudstate.pISlaveControl->ShutdownSlave();
        if (MUX_FAILED(mr))
        {
            STARTLOG(LOG_ALWAYS, "INI", "LOAD");
            g_pILog->log_text(tprintf(T("Failed to request stubslave to shutdown (%d)."), mr));
            ENDLOG;
        }

        mudstate.pISlaveControl->Release();
        mudstate.pISlaveControl = nullptr;
    }
}
#endif // STUB_SLAVE

void final_modules(void)
{
    MUX_RESULT mr = MUX_S_OK;

    // Revoke engine-side CIDs.
    //
    mux_Unregister();

    // Revoke driver-side CIDs.
    //
    mr = mux_RevokeClassObjects(NUM_DRIVER_CLASSES, driver_classes);
    if (MUX_FAILED(mr))
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        g_pILog->log_text(tprintf(T("Failed to revoke netmux modules (%d)."), mr));
        ENDLOG;
    }
    else
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        g_pILog->log_text(T("Revoked netmux modules."));
        ENDLOG;
    }
    mux_FinalizeModuleLibrary();
}
// ---------------------------------------------------------------------------
// CConnectionManager — driver-side implementation of mux_IConnectionManager.
// Wraps the net.cpp accessor layer so engine code can interact with
// connections without linking against driver symbols.
// ---------------------------------------------------------------------------

class CConnectionManager : public mux_IConnectionManager
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

    CConnectionManager(void);
    virtual ~CConnectionManager();

private:
    uint32_t m_cRef;
};

CConnectionManager::CConnectionManager(void) : m_cRef(1)
{
}

CConnectionManager::~CConnectionManager()
{
}

MUX_RESULT CConnectionManager::QueryInterface(MUX_IID iid, void **ppv)
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

uint32_t CConnectionManager::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CConnectionManager::Release(void)
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

MUX_RESULT CConnectionManager::SendText(dbref target, const UTF8 *text)
{
    send_text_to_player(target, text);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::SendRaw(dbref target, const UTF8 *data, size_t len)
{
    send_raw_to_player(target, data, len);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::BroadcastAndFlush(int inflags, const UTF8 *text)
{
    broadcast_and_flush(inflags, text);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::SendProgPrompt(dbref target)
{
    send_prog_prompt(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::SendKeepaliveNops(void)
{
    send_keepalive_nops();
    return MUX_S_OK;
}

// --- Queries by dbref ---

MUX_RESULT CConnectionManager::GetTotalConnections(int *pCount)
{
    if (nullptr == pCount) return MUX_E_INVALIDARG;
    *pCount = get_total_connections();
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::CountPlayerDescs(dbref target, int *pCount)
{
    if (nullptr == pCount) return MUX_E_INVALIDARG;
    *pCount = count_player_descs(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::SumPlayerCommandCount(dbref target, int *pCount)
{
    if (nullptr == pCount) return MUX_E_INVALIDARG;
    *pCount = sum_player_command_count(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::FetchHeight(dbref target, int *pHeight)
{
    if (nullptr == pHeight) return MUX_E_INVALIDARG;
    *pHeight = fetch_height(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::FetchWidth(dbref target, int *pWidth)
{
    if (nullptr == pWidth) return MUX_E_INVALIDARG;
    *pWidth = fetch_width(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::FetchIdle(dbref target, int *pIdle)
{
    if (nullptr == pIdle) return MUX_E_INVALIDARG;
    *pIdle = fetch_idle(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::FetchConnect(dbref target, int *pConnect)
{
    if (nullptr == pConnect) return MUX_E_INVALIDARG;
    *pConnect = fetch_connect(target);
    return MUX_S_OK;
}

// --- Queries by opaque DESC handle ---

MUX_RESULT CConnectionManager::FindDescBySocket(SOCKET s, DESC **ppDesc)
{
    if (nullptr == ppDesc) return MUX_E_INVALIDARG;
    *ppDesc = find_desc_by_socket(s);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::FindDescByPlayer(dbref target, DESC **ppDesc)
{
    if (nullptr == ppDesc) return MUX_E_INVALIDARG;
    *ppDesc = find_desc_by_player(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescPlayer(const DESC *d, dbref *pPlayer)
{
    if (nullptr == d || nullptr == pPlayer) return MUX_E_INVALIDARG;
    *pPlayer = desc_player(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescHeight(const DESC *d, int *pHeight)
{
    if (nullptr == d || nullptr == pHeight) return MUX_E_INVALIDARG;
    *pHeight = desc_height(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescWidth(const DESC *d, int *pWidth)
{
    if (nullptr == d || nullptr == pWidth) return MUX_E_INVALIDARG;
    *pWidth = desc_width(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescEncoding(const DESC *d, int *pEncoding)
{
    if (nullptr == d || nullptr == pEncoding) return MUX_E_INVALIDARG;
    *pEncoding = desc_encoding(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescCommandCount(const DESC *d, int *pCount)
{
    if (nullptr == d || nullptr == pCount) return MUX_E_INVALIDARG;
    *pCount = desc_command_count(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescTtype(const DESC *d, const UTF8 **ppTtype)
{
    if (nullptr == d || nullptr == ppTtype) return MUX_E_INVALIDARG;
    *ppTtype = desc_ttype(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescLastTime(const DESC *d, CLinearTimeAbsolute *pTime)
{
    if (nullptr == d || nullptr == pTime) return MUX_E_INVALIDARG;
    *pTime = desc_last_time(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescConnectedAt(const DESC *d, CLinearTimeAbsolute *pTime)
{
    if (nullptr == d || nullptr == pTime) return MUX_E_INVALIDARG;
    *pTime = desc_connected_at(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescNvtHimState(const DESC *d, unsigned char chOption, int *pState)
{
    if (nullptr == d || nullptr == pState) return MUX_E_INVALIDARG;
    *pState = desc_nvt_him_state(d, chOption);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescSocketState(const DESC *d, SocketState *pState)
{
    if (nullptr == d || nullptr == pState) return MUX_E_INVALIDARG;
    *pState = desc_socket_state(d);
    return MUX_S_OK;
}

// --- Iteration ---

MUX_RESULT CConnectionManager::ForEachConnectedPlayer(
    void (*callback)(dbref player, void *context), void *context)
{
    if (nullptr == callback) return MUX_E_INVALIDARG;
    for_each_connected_player(callback, context);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::ForEachConnectedDesc(
    void (*callback)(dbref player, SOCKET sock, void *context), void *context)
{
    if (nullptr == callback) return MUX_E_INVALIDARG;
    for_each_connected_desc(callback, context);
    return MUX_S_OK;
}

// --- @program state ---

MUX_RESULT CConnectionManager::PlayerHasProgram(dbref target, bool *pbHas)
{
    if (nullptr == pbHas) return MUX_E_INVALIDARG;
    *pbHas = player_has_program(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DetachPlayerProgram(dbref target, program_data **ppProgram)
{
    if (nullptr == ppProgram) return MUX_E_INVALIDARG;
    *ppProgram = detach_player_program(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::SetPlayerProgram(dbref target, program_data *program)
{
    set_player_program(target, program);
    return MUX_S_OK;
}

// --- Encoding / Display ---

MUX_RESULT CConnectionManager::SetPlayerEncoding(dbref target, int encoding)
{
    set_player_encoding(target, encoding);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::ResetPlayerEncoding(dbref target)
{
    reset_player_encoding(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::SetDoingAll(dbref target, const UTF8 *doing, size_t len)
{
    set_doing_all(target, doing, len);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::SetDoingLeastIdle(dbref target, const UTF8 *doing, size_t len, bool *pbFound)
{
    if (nullptr == pbFound) return MUX_E_INVALIDARG;
    *pbFound = set_doing_least_idle(target, doing, len);
    return MUX_S_OK;
}

// --- Quota ---

MUX_RESULT CConnectionManager::UpdateAllDescQuotas(int nExtra, int nMax)
{
    update_all_desc_quotas(nExtra, nMax);
    return MUX_S_OK;
}

// --- Connection lifecycle ---

MUX_RESULT CConnectionManager::BootOff(dbref target, const UTF8 *message, int *pCount)
{
    int n = boot_off(target, message);
    if (pCount) *pCount = n;
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::BootByPort(SOCKET port, bool bGod, const UTF8 *message, int *pCount)
{
    int n = boot_by_port(port, bGod, message);
    if (pCount) *pCount = n;
    return MUX_S_OK;
}

// --- Idle check ---

MUX_RESULT CConnectionManager::CheckIdle(void)
{
    check_idle();
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::EmergencyShutdown(void)
{
    emergency_shutdown();
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescQueueWrite(DESC *d, const UTF8 *data,
    size_t len)
{
    if (nullptr == d || nullptr == data)
    {
        return MUX_E_INVALIDARG;
    }
    queue_write_LEN(d, data, len);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescQueueString(DESC *d, const UTF8 *text)
{
    if (nullptr == d || nullptr == text)
    {
        return MUX_E_INVALIDARG;
    }
    queue_string(d, text);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CConnectionManagerFactory
// ---------------------------------------------------------------------------

CConnectionManagerFactory::CConnectionManagerFactory(void) : m_cRef(1)
{
}

CConnectionManagerFactory::~CConnectionManagerFactory()
{
}

MUX_RESULT CConnectionManagerFactory::QueryInterface(MUX_IID iid, void **ppv)
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

uint32_t CConnectionManagerFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CConnectionManagerFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CConnectionManagerFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    CConnectionManager *pConnMgr = nullptr;
    try
    {
        pConnMgr = new CConnectionManager;
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

MUX_RESULT CConnectionManagerFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CDriverControl — driver-side implementation of mux_IDriverControl.
// Provides non-connection driver operations (shutdown requests, etc.).
// ---------------------------------------------------------------------------

class CDriverControl : public mux_IDriverControl
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT ShutdownRequest(void);
    virtual MUX_RESULT GetRestarting(bool *pbRestarting);
    virtual MUX_RESULT SiteUpdate(const UTF8 *subnetStr,
        dbref player, UTF8 *cmd, int operation);

    CDriverControl(void);
    virtual ~CDriverControl();

private:
    uint32_t m_cRef;
};

CDriverControl::CDriverControl(void) : m_cRef(1)
{
}

CDriverControl::~CDriverControl()
{
}

MUX_RESULT CDriverControl::QueryInterface(MUX_IID iid, void **ppv)
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

uint32_t CDriverControl::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CDriverControl::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CDriverControl::ShutdownRequest(void)
{
    g_shutdown_flag = true;
    return MUX_S_OK;
}

MUX_RESULT CDriverControl::GetRestarting(bool *pbRestarting)
{
    if (nullptr == pbRestarting)
    {
        return MUX_E_INVALIDARG;
    }
    *pbRestarting = g_restarting;
    return MUX_S_OK;
}

MUX_RESULT CDriverControl::SiteUpdate(const UTF8 *subnetStr,
    dbref player, UTF8 *cmd, int operation)
{
    if (nullptr == subnetStr)
    {
        return MUX_E_INVALIDARG;
    }

    // parse_subnet needs a mutable copy.
    //
    UTF8 buf[LBUF_SIZE];
    mux_strncpy(buf, subnetStr, sizeof(buf) - 1);

    mux_subnet *pSubnet = parse_subnet(buf, player, cmd);
    if (nullptr == pSubnet)
    {
        return MUX_E_FAIL;
    }

    bool bOK = false;
    switch (operation)
    {
    case HC_PERMIT:     bOK = g_access_list.permit(pSubnet);     break;
    case HC_REGISTER:   bOK = g_access_list.registered(pSubnet); break;
    case HC_FORBID:     bOK = g_access_list.forbid(pSubnet);     break;
    case HC_NOSITEMON:  bOK = g_access_list.nositemon(pSubnet);  break;
    case HC_SITEMON:    bOK = g_access_list.sitemon(pSubnet);    break;
    case HC_NOGUEST:    bOK = g_access_list.noguest(pSubnet);    break;
    case HC_GUEST:      bOK = g_access_list.guest(pSubnet);      break;
    case HC_SUSPECT:    bOK = g_access_list.suspect(pSubnet);    break;
    case HC_TRUST:      bOK = g_access_list.trust(pSubnet);      break;
    case HC_RESET:
        if (!g_access_list.reset(pSubnet))
        {
            delete pSubnet;
            return MUX_E_FAIL;
        }
        bOK = true;
        break;
    default:
        delete pSubnet;
        return MUX_E_INVALIDARG;
    }

    return bOK ? MUX_S_OK : MUX_E_FAIL;
}

// ---------------------------------------------------------------------------
// CDriverControlFactory
// ---------------------------------------------------------------------------

CDriverControlFactory::CDriverControlFactory(void) : m_cRef(1)
{
}

CDriverControlFactory::~CDriverControlFactory()
{
}

MUX_RESULT CDriverControlFactory::QueryInterface(MUX_IID iid, void **ppv)
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

uint32_t CDriverControlFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CDriverControlFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CDriverControlFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    CDriverControl *pDC = nullptr;
    try
    {
        pDC = new CDriverControl;
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

MUX_RESULT CDriverControlFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}
