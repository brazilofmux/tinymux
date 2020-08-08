/*! \file funcs.cpp
 * \brief Funcs Module
 *
 */

#include "../autoconf.h"
#include "../config.h"
#include "../libmux.h"
#include "../modules.h"
#include "funcs.h"

static INT32 g_cComponents = 0;
static INT32 g_cServerLocks = 0;

static mux_IFunctionSinkControl* g_pIFunctionSinkControl = nullptr;

static MUX_CLASS_INFO funcs_classes[] =
{
    {CID_Funcs}
};
#define NUM_CLASSES (sizeof(funcs_classes)/sizeof(funcs_classes[0]))

// The following four functions are for access by dlopen.
//
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_CanUnloadNow()
{
    if (0 == g_cComponents
        && 0 == g_cServerLocks)
    {
        return MUX_S_OK;
    }
    return MUX_S_FALSE;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_GetClassObject(MUX_CID cid, MUX_IID iid, void** ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    if (CID_Funcs == cid)
    {
        CFuncsFactory* pFuncsFactory = nullptr;
        try
        {
            pFuncsFactory = new CFuncsFactory;
        }
        catch (...)
        {
        }

        if (nullptr == pFuncsFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pFuncsFactory->QueryInterface(iid, ppv);
        pFuncsFactory->Release();
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Register()
{
    MUX_RESULT mr = MUX_E_UNEXPECTED;

    if (nullptr == g_pIFunctionSinkControl)
    {
        // Advertise our components.
        //
        mr = mux_RegisterClassObjects(NUM_CLASSES, funcs_classes, nullptr);
        if (MUX_FAILED(mr))
        {
            return mr;
        }

        // Create an instance of our CFuncs component.
        //
        mux_IFunctionSinkControl* pIFunctionSinkControl = nullptr;
        mr = mux_CreateInstance(CID_Funcs, nullptr, UseSameProcess, IID_IFunctionSinkControl,
                                reinterpret_cast<void **>(&pIFunctionSinkControl));
        if (MUX_SUCCEEDED(mr))
        {
            g_pIFunctionSinkControl = pIFunctionSinkControl;
            pIFunctionSinkControl = nullptr;
        }
        else
        {
            (void)mux_RevokeClassObjects(NUM_CLASSES, funcs_classes);
            mr = MUX_E_OUTOFMEMORY;
        }
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Unregister()
{
    // Destroy our CFuncs component.
    //
    if (nullptr != g_pIFunctionSinkControl)
    {
        g_pIFunctionSinkControl->Unregistering();
        g_pIFunctionSinkControl->Release();
        g_pIFunctionSinkControl = nullptr;
    }

    return mux_RevokeClassObjects(NUM_CLASSES, funcs_classes);
}

// Funcs component which is not directly accessible.
//
CFuncs::CFuncs() : m_cRef(1)
{
    g_cComponents++;
    m_pILog = nullptr;
    m_pIFunctionsControl = nullptr;
}

MUX_RESULT CFuncs::FinalConstruct()
{
    // Use CLog provided by netmux.
    //
    MUX_RESULT mr = mux_CreateInstance(CID_Log, nullptr, UseSameProcess, IID_ILog, reinterpret_cast<void **>(&m_pILog));
    if (MUX_SUCCEEDED(mr))
    {
        bool fStarted;
        mr = m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            m_pILog->log_text(T("CFuncs::CFuncs()."));
            m_pILog->end_log();
        }
    }

    // Start conversation with netmux to offer softcode functions.
    //
    mr = mux_CreateInstance(CID_Functions, nullptr, UseSameProcess, IID_IFunctionsControl,
                            reinterpret_cast<void **>(&m_pIFunctionsControl));
    if (MUX_SUCCEEDED(mr))
    {
        mux_IFunction* pIFunction = nullptr;
        mr = QueryInterface(IID_IFunction, reinterpret_cast<void **>(&pIFunction));
        if (MUX_SUCCEEDED(mr))
        {
            m_pIFunctionsControl->Add(0, T("HELLO"), pIFunction, MAX_ARG, 0, MAX_ARG, 0, 0);
            mr = m_pIFunctionsControl->Add(1, T("GOODBYE"), pIFunction, MAX_ARG, 0, MAX_ARG, 0, 0);
            pIFunction->Release();
            pIFunction = nullptr;
        }
    }

    return mr;
}

CFuncs::~CFuncs()
{
    if (nullptr != m_pILog)
    {
        bool fStarted;
        const MUX_RESULT mr = m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            m_pILog->log_text(T("CFuncs::~CFuncs()"));
            m_pILog->end_log();
        }

        m_pILog->Release();
        m_pILog = nullptr;
    }

    if (nullptr != m_pIFunctionsControl)
    {
        m_pIFunctionsControl->Release();
        m_pIFunctionsControl = nullptr;
    }

    g_cComponents--;
}

MUX_RESULT CFuncs::QueryInterface(MUX_IID iid, void** ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IFunctionSinkControl *>(this);
    }
    else if (IID_IFunctionSinkControl == iid)
    {
        *ppv = static_cast<mux_IFunctionSinkControl *>(this);
    }
    else if (IID_IFunction == iid)
    {
        *ppv = static_cast<mux_IFunction *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CFuncs::AddRef()
{
    m_cRef++;
    return m_cRef;
}

UINT32 CFuncs::Release()
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

#define LBUF_SIZE   8000    // Large

void CFuncs::Unregistering()
{
    // We need to release our references before netmux will release his.
    //
    if (nullptr != m_pIFunctionsControl)
    {
        m_pIFunctionsControl->Release();
        m_pIFunctionsControl = nullptr;
    }
}

const unsigned char utf8_FirstByte[256] =
{
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 1
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 2
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 3
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 5
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 7

    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, // 8
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, // 9
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, // A
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, // B
    6, 6, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, // C
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, // D
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, // E
    4, 4, 4, 4, 4, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 // F
};

const int g_trimoffset[4][4] =
{
    {0, 1, 1, 1},
    {1, 0, 2, 2},
    {2, 1, 0, 3},
    {3, 2, 1, 0}
};

#define UTF8_CONTINUE  5

size_t trim_partial_sequence(size_t n, const UTF8* p)
{
    for (size_t i = 0; i < n; i++)
    {
        const int j = utf8_FirstByte[p[n - i - 1]];
        if (j < UTF8_CONTINUE)
        {
            if (i < 4)
            {
                return n - g_trimoffset[i][j - 1];
            }
            return n - i + j - 1;
        }
    }
    return 0;
}

void safe_copy_str_lbuf(const UTF8* src, UTF8* buff, UTF8** bufp)
{
    if (src == nullptr)
    {
        return;
    }

    auto tp = *bufp;
    const auto maxtp = buff + LBUF_SIZE - 1;
    while (tp < maxtp && *src)
    {
        *tp++ = *src++;
    }
    *bufp = buff + trim_partial_sequence(tp - buff, buff);
}

static void fun_hello(UTF8* buff, UTF8** bufc, dbref executor, dbref caller, dbref enactor, int eval, UTF8* fargs[],
                      int nfargs,
                      const UTF8* cargs[], int ncargs)
{
    safe_copy_str_lbuf(T("HELLO"), buff, bufc);
}

static void fun_goodbye(UTF8* buff, UTF8** bufc, dbref executor, dbref caller, dbref enactor, int eval, UTF8* fargs[],
                        int nfargs,
                        const UTF8* cargs[], int ncargs)
{
    safe_copy_str_lbuf(T("GOODBYE"), buff, bufc);
}

MUX_RESULT CFuncs::Call(unsigned int nKey, UTF8* buff, UTF8** bufc, dbref executor, dbref caller, dbref enactor,
                        int eval,
                        UTF8* fargs[], int nfargs, const UTF8* cargs[], int ncargs)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool fStarted;
    const MUX_RESULT mr = m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("INFO"));
    if (MUX_SUCCEEDED(mr) && fStarted)
    {
        m_pILog->log_text(T("CFuncs::Call()."));
        m_pILog->end_log();
    }

    if (0 == nKey)
    {
        fun_hello(buff, bufc, executor, caller, enactor, eval, fargs, nfargs, cargs, ncargs);
    }
    else
    {
        fun_goodbye(buff, bufc, executor, caller, enactor, eval, fargs, nfargs, cargs, ncargs);
    }
    return MUX_S_OK;
}

// Factory for Funcs component which is not directly accessible.
//
CFuncsFactory::CFuncsFactory() : m_cRef(1)
{
}

CFuncsFactory::~CFuncsFactory()
= default;

MUX_RESULT CFuncsFactory::QueryInterface(MUX_IID iid, void** ppv)
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
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CFuncsFactory::AddRef()
{
    m_cRef++;
    return m_cRef;
}

UINT32 CFuncsFactory::Release()
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CFuncsFactory::CreateInstance(mux_IUnknown* pUnknownOuter, MUX_IID iid, void** ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (nullptr != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CFuncs* p_funcs = nullptr;
    try
    {
        p_funcs = new CFuncs;
    }
    catch (...)
    {
    }

    if (nullptr == p_funcs)
    {
        return MUX_E_OUTOFMEMORY;
    }
    auto mr = p_funcs->FinalConstruct();
    if (MUX_FAILED(mr))
    {
        p_funcs->Release();
        return mr;
    }

    mr = p_funcs->QueryInterface(iid, ppv);
    p_funcs->Release();
    return mr;
}

MUX_RESULT CFuncsFactory::LockServer(bool bLock)
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
