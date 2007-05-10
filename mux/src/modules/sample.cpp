/*! \file sample.cpp
 * \brief Sample Module
 *
 * $Id$
 *
 */

#include "../autoconf.h"
#include "../config.h"
#include "../modules.h"
#include "sample.h"

static INT32 g_cComponents  = 0;
static INT32 g_cServerLocks = 0;

#define NUM_CIDS 1
static UINT64 cids[NUM_CIDS] =
{
    CID_Sample
};

// The following four functions are for access by dlopen.
//
extern "C" DCL_EXPORT MUX_RESULT mux_CanUnloadNow(void)
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

extern "C" DCL_EXPORT MUX_RESULT mux_GetClassObject(UINT64 cid, UINT64 iid, void **ppv)
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

extern "C" DCL_EXPORT MUX_RESULT mux_Register(void)
{
    return mux_RegisterClassObjects(NUM_CIDS, cids, NULL);
}

extern "C" DCL_EXPORT MUX_RESULT mux_Unregister(void)
{
    return mux_RevokeClassObjects(NUM_CIDS, cids);
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

MUX_RESULT CSampleFactory::CreateInstance(UINT64 iid, void **ppv)
{
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
