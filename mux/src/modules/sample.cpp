/*! \file sample.cpp
 * \brief Sample Module
 *
 * $Id$
 *
 */

#include "../autoconf.h"
#include "../config.h"
#include "../libmux.h"
#include "../modules.h"
#include "sample.h"

static INT32 g_cComponents  = 0;
static INT32 g_cServerLocks = 0;

class CSampleModule
{
private:
    mux_ILog *pILog;

public:
    CSampleModule(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CSampleModule();
};

static CSampleModule *pModule = NULL;

#define NUM_CIDS 1
static UINT64 sample_cids[NUM_CIDS] =
{
    CID_Sample
};

// The following four functions are for access by dlopen.
//
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_CanUnloadNow(void)
{
    if (  0 == g_cComponents
       && 0 == g_cServerLocks)
    {
        return MUX_S_OK;
    }
    else
    {
        return MUX_S_FALSE;
    }
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_GetClassObject(UINT64 cid, UINT64 iid, void **ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    if (CID_Sample == cid)
    {
        CSampleFactory *pSampleFactory = NULL;
        try
        {
            pSampleFactory = new CSampleFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }
        
        if (NULL == pSampleFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pSampleFactory->QueryInterface(iid, ppv);
        pSampleFactory->Release();
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Register(void)
{
    MUX_RESULT mr = MUX_E_UNEXPECTED;

    if (NULL == pModule)
    {
        // Advertise our components.
        //
        mr = mux_RegisterClassObjects(NUM_CIDS, sample_cids, NULL);
        if (MUX_FAILED(mr))
        {
            return mr;
        }

        try
        {
            pModule = new CSampleModule;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pModule)
        {
            // If we can't allocate a module object, revoke our other
            // components as well. This is also what happens if the
            // constructor throws an exception.
            //
            (void)mux_RevokeClassObjects(NUM_CIDS, sample_cids);
            mr = MUX_E_OUTOFMEMORY;
        }
        else
        {
            g_cComponents++;

            // FinalConstruct may throw an exception, but it may return
            // failure as well.
            //
            try
            {
                mr = pModule->FinalConstruct();
            }
            catch (...)
            {
                mr = MUX_E_FAIL;
            }

            if (MUX_FAILED(mr))
            {
                delete pModule;
                pModule = NULL;
                g_cComponents--;

                (void)mux_RevokeClassObjects(NUM_CIDS, sample_cids);
            }
        }
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Unregister(void)
{
    // Destroy our Module object.
    //
    if (NULL != pModule)
    {
        delete pModule;
        pModule = NULL;
        g_cComponents--;
    }

    return mux_RevokeClassObjects(NUM_CIDS, sample_cids);
}

// Sample component which is not directly accessible.
//
CSample::CSample(void) : m_cRef(1)
{
    g_cComponents++;
}

CSample::~CSample()
{
    g_cComponents--;
}

MUX_RESULT CSample::QueryInterface(UINT64 iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<ISample *>(this);
    }
    else if (IID_ISample == iid)
    {
        *ppv = static_cast<ISample *>(this);
    }
    else
    {
        *ppv = NULL;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CSample::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CSample::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

int CSample::Add(int a, int b)
{
    return a + b;
}

// Factory for Sample component which is not directly accessible.
//
CSampleFactory::CSampleFactory(void) : m_cRef(1)
{
}

CSampleFactory::~CSampleFactory()
{
}

MUX_RESULT CSampleFactory::QueryInterface(UINT64 iid, void **ppv)
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

UINT32 CSampleFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CSampleFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CSampleFactory::CreateInstance(mux_IUnknown *pUnknownOuter, UINT64 iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (NULL != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CSample *pSample = NULL;
    try
    {
        pSample = new CSample;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL == pSample)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pSample->QueryInterface(iid, ppv);
    pSample->Release();
    return mr;
}

MUX_RESULT CSampleFactory::LockServer(bool bLock)
{
    if (bLock)
    {
        g_cServerLocks++;
    }
    else
    {
        g_cServerLocks--;
    }
    return MUX_S_OK;
}

#define LOG_ALWAYS      0x80000000  /* Always log it */

CSampleModule::CSampleModule(void)
{
    pILog = NULL;
}

CSampleModule::~CSampleModule()
{
    if (NULL != pILog)
    {
        if (pILog->start_log(LOG_ALWAYS, T("INI"), T("INFO")))
        {
            pILog->log_printf("~CSampleModule::CSampleModule().");
            pILog->end_log();
        }

        pILog->Release();
        pILog = NULL;
    }
}

MUX_RESULT CSampleModule::FinalConstruct(void)
{
    // Use of CLog provided by netmux.
    //
    MUX_RESULT mr = mux_CreateInstance(CID_Log, NULL, UseSameProcess, IID_ILog, (void **)&pILog);
    if (MUX_SUCCEEDED(mr))
    {
        if (pILog->start_log(LOG_ALWAYS, T("INI"), T("INFO")))
        {
            pILog->log_printf("CSampleModule::FinalConstruct().");
            pILog->end_log();
        }
    }
}
