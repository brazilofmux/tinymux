/*! \file modules.cpp
 * \brief netmux-provided modules.
 *
 * Interfaces and classes declared here are built into the netmux server and
 * are available to netmux itself and to dynamically-loaded external modules.
 *
 * $Id$
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#if defined(HAVE_DLOPEN) || defined(WIN32)

#include "libmux.h"
#include "modules.h"

#define NUM_CIDS 1
static UINT64 netmux_cids[NUM_CIDS] =
{
    CID_Log
};

extern "C" MUX_RESULT DCL_API netmux_GetClassObject(UINT64 cid, UINT64 iid, void **ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    if (CID_Log == cid)
    {
        CLogFactory *pLogFactory = NULL;
        try
        {
            pLogFactory = new CLogFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }
        
        if (NULL == pLogFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pLogFactory->QueryInterface(iid, ppv);
        pLogFactory->Release();
    }
    return mr;
}

void init_modules(void)
{
#ifdef STUB_SLAVE
    MUX_RESULT mr = mux_InitModuleLibrary(IsMainProcess, pipepump);
#else
    MUX_RESULT mr = mux_InitModuleLibrary(IsMainProcess, NULL);
#endif
    if (MUX_SUCCEEDED(mr))
    {
        mr = mux_RegisterClassObjects(NUM_CIDS, netmux_cids, netmux_GetClassObject);
    }

    if (MUX_FAILED(mr))
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_printf("Failed to register netmux modules (%d).", mr);
        ENDLOG;
    }
    else
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_printf("Registered netmux modules.", mr);
        ENDLOG;
    }
}

void final_modules(void)
{
    MUX_RESULT mr = mux_RevokeClassObjects(NUM_CIDS, netmux_cids);
    if (MUX_FAILED(mr))
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_printf("Failed to revoke netmux modules (%d).", mr);
        ENDLOG;
    }
    else
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_printf("Revoked netmux modules.", mr);
        ENDLOG;
    }
    mux_FinalizeModuleLibrary();
}

// Log component which is not directly accessible.
//
CLog::CLog(void) : m_cRef(1)
{
}

CLog::~CLog()
{
}

MUX_RESULT CLog::QueryInterface(UINT64 iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_ILog *>(this);
    }
    else if (IID_ILog == iid)
    {
        *ppv = static_cast<mux_ILog *>(this);
    }
    else
    {
        *ppv = NULL;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CLog::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CLog::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

bool CLog::start_log(int key, const UTF8 *primary, const UTF8 *secondary)
{
    if (  ((key) & mudconf.log_options) != 0
       && ::start_log(primary, secondary))
    {
        return true;
    }
    else
    {
        return false;
    }
}

void CLog::log_perror(const UTF8 *primary, const UTF8 *secondary, const UTF8 *extra, const UTF8 *failing_object)
{
    ::log_perror(primary, secondary, extra, failing_object);
}

void CLog::log_text(const UTF8 *text)
{
    ::log_text(text);
}

void CLog::log_number(int num)
{
    ::log_number(num);
}

void DCL_CDECL CLog::log_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    UTF8 aTempBuffer[SIZEOF_LOG_BUFFER];
    size_t nString = mux_vsnprintf(aTempBuffer, SIZEOF_LOG_BUFFER, fmt, ap);
    va_end(ap);
    Log.WriteBuffer(nString, aTempBuffer);
}

void CLog::log_name(dbref target)
{
    ::log_name(target);
}

void CLog::log_name_and_loc(dbref player)
{
    ::log_name_and_loc(player);
}

void CLog::log_type_and_name(dbref thing)
{
    ::log_type_and_name(thing);
}

void CLog::end_log(void)
{
    ::end_log();
}

// Factory for Log component which is not directly accessible.
//
CLogFactory::CLogFactory(void) : m_cRef(1)
{
}

CLogFactory::~CLogFactory()
{
}

MUX_RESULT CLogFactory::QueryInterface(UINT64 iid, void **ppv)
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
        *ppv = NULL;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CLogFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CLogFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CLogFactory::CreateInstance(mux_IUnknown *pUnknownOuter, UINT64 iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (NULL != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CLog *pLog = NULL;
    try
    {
        pLog = new CLog;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL == pLog)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pLog->QueryInterface(iid, ppv);
    pLog->Release();
    return mr;
}

MUX_RESULT CLogFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

#endif
