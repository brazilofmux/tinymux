/*! \file sample.cpp
 * \brief Sample Module
 *
 */

#include "../autoconf.h"
#include "../config.h"
#include "../libmux.h"
#include "../modules.h"
#include "sample.h"

static INT32 g_cComponents  = 0;
static INT32 g_cServerLocks = 0;

static ISample *g_pISample = NULL;

#define NUM_CLASSES 2
static MUX_CLASS_INFO sample_classes[NUM_CLASSES] =
{
    { CID_Sample },
    { CID_SumProxy }
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

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
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
    else if (CID_SumProxy == cid)
    {
        CSumProxyFactory *pSumProxyFactory = NULL;
        try
        {
            pSumProxyFactory = new CSumProxyFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pSumProxyFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pSumProxyFactory->QueryInterface(iid, ppv);
        pSumProxyFactory->Release();
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Register(void)
{
    MUX_RESULT mr = MUX_E_UNEXPECTED;

    if (NULL == g_pISample)
    {
        // Advertise our components.
        //
        mr = mux_RegisterClassObjects(NUM_CLASSES, sample_classes, NULL);
        if (MUX_FAILED(mr))
        {
            return mr;
        }

        // Create an instance of our CSample component.
        //
        ISample *pISample = NULL;
        mr = mux_CreateInstance(CID_Sample, NULL, UseSameProcess, IID_ISample, (void **)&pISample);
        if (MUX_SUCCEEDED(mr))
        {
            g_pISample = pISample;
            pISample = NULL;
        }
        else
        {
            (void)mux_RevokeClassObjects(NUM_CLASSES, sample_classes);
            mr = MUX_E_OUTOFMEMORY;
        }
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Unregister(void)
{
    // Destroy our CSample component.
    //
    if (NULL != g_pISample)
    {
        g_pISample->Unregistering();
        g_pISample->Release();
        g_pISample = NULL;
    }

    return mux_RevokeClassObjects(NUM_CLASSES, sample_classes);
}

// Sample component which is not directly accessible.
//
CSample::CSample(void) : m_cRef(1)
{
    g_cComponents++;
    m_pILog = NULL;
    m_pIServerEventsControl = NULL;
}

MUX_RESULT CSample::FinalConstruct(void)
{
    MUX_RESULT mr;

    // Use CLog provided by netmux.
    //
    mr = mux_CreateInstance(CID_Log, NULL, UseSameProcess, IID_ILog, (void **)&m_pILog);
    if (MUX_SUCCEEDED(mr))
    {
        bool fStarted;
        mr = m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("INFO")); 
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            mr = m_pILog->log_text(T("CSample::CSample()." ENDLINE));
            mr = m_pILog->end_log();
        }
    }

    // Use CServerEventsSource to hook up our IServerEventsSink callback interface.
    //
    mux_IServerEventsSink *pIServerEventsSink = NULL;
    mr = QueryInterface(IID_IServerEventsSink, (void **)&pIServerEventsSink);
    if (MUX_SUCCEEDED(mr))
    {
        mr = mux_CreateInstance(CID_ServerEventsSource, NULL, UseSameProcess, IID_IServerEventsControl, (void **)&m_pIServerEventsControl);
        if (MUX_SUCCEEDED(mr))
        {
            m_pIServerEventsControl->Advise(pIServerEventsSink);
        }
        pIServerEventsSink->Release();
    }
    return mr;
}

CSample::~CSample()
{
    if (NULL != m_pILog)
    {
        bool fStarted;
        MUX_RESULT mr = m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("INFO"));
        if (MUX_SUCCEEDED(mr) && fStarted)
        {
            mr = m_pILog->log_text(T("CSample::~CSample()." ENDLINE));
            mr = m_pILog->end_log();
        }

        m_pILog->Release();
        m_pILog = NULL;
    }

    if (NULL != m_pIServerEventsControl)
    {
        m_pIServerEventsControl->Release();
        m_pIServerEventsControl = NULL;
    }

    g_cComponents--;
}

MUX_RESULT CSample::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<ISample *>(this);
    }
    else if (IID_ISample == iid)
    {
        *ppv = static_cast<ISample *>(this);
    }
    else if (IID_IServerEventsSink == iid)
    {
        *ppv = static_cast<mux_IServerEventsSink *>(this);
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

void CSample::Unregistering(void)
{
    // The ServerEventsSource and we are holding references to each other.
    // We need to release our reference before he will release his.
    //
    if (NULL != m_pIServerEventsControl)
    {
        m_pIServerEventsControl->Release();
        m_pIServerEventsControl = NULL;
    }
}

// Factory for Sample component which is not directly accessible.
//
CSampleFactory::CSampleFactory(void) : m_cRef(1)
{
}

CSampleFactory::~CSampleFactory()
{
}

MUX_RESULT CSampleFactory::QueryInterface(MUX_IID iid, void **ppv)
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

MUX_RESULT CSampleFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
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

    MUX_RESULT mr;
    if (NULL == pSample)
    {
        return MUX_E_OUTOFMEMORY;
    }
    else
    {
        mr = pSample->FinalConstruct();
        if (MUX_FAILED(mr))
        {
            pSample->Release();
            return mr;
        }
    }

    mr = pSample->QueryInterface(iid, ppv);
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

// Called after all normal MUX initialization is complete.
//
void CSample::startup(void)
{
    MUX_RESULT mr = m_pILog->log_text(T("Sample module sees CSample::startup event." ENDLINE));

#if 1
    ISum *pISum = NULL;
    mr = mux_CreateInstance(CID_Sum, NULL, UseSlaveProcess, IID_ISum, (void **)&pISum);
    if (MUX_SUCCEEDED(mr))
    {
        int sum;
        mr = pISum->Add(1,1, &sum);
        if (MUX_SUCCEEDED(mr))
        {
            mr = m_pILog->log_text(T("ISum::Add(1,1) is:"));
            mr = m_pILog->log_number(sum);
        }
        else
        {
            mr = m_pILog->log_text(T("Call to pISum->Add() failed with:."));
            mr = m_pILog->log_number(mr);
        }
        pISum->Release();
        pISum = NULL;
    }
    else
    {
        mr = m_pILog->log_text(T("CreateInstance returned:"));
        mr = m_pILog->log_number(mr);
    }
#endif
}

// This is called prior to the game syncronizing its own state to its own
// database.  If you depend on the the core database to store your data,
// you need to checkpoint your changes here. The write-protection
// mechanism in MUX is not turned on at this point.  You are guaranteed
// to not be a fork()-ed dumping process.
//
void CSample::presync_database(void)
{
    m_pILog->log_text(T("Sample module sees CSample::presync_database event." ENDLINE));
}

// Like the above routine except that it called from the SIGSEGV handler.
// At this point, your choices are limited. You can attempt to use the core
// database. The core won't stop you, but it is risky.
//
void CSample::presync_database_sigsegv(void)
{
    m_pILog->log_text(T("Sample module sees CSample::presync_database_sigsegv event." ENDLINE));
}

// This is called prior to the game database writing out it's own
// database.  This is typically only called from the fork()-ed process so
// write-protection is in force and you will be unable to modify the
// game's database for you own needs.  You can however, use this point to
// maintain your own dump file.
//
// The caveat is that it is possible the game will crash while you are
// doing this, or it is already in the process of crashing.  You may be
// called reentrantly.  Therefore, it is recommended that you follow the
// pattern in dump_database_internal() and write your database to a
// temporary file, and then if completed successfully, move your temporary
// over the top of your old database.
//
// The argument dump_type is one of the 5 DUMP_I_x defines declared in
// externs.h
//
void CSample::dump_database(int dump_type)
{
    UNUSED_PARAMETER(dump_type);

    m_pILog->log_text(T("Sample module sees CSample::dump_database event." ENDLINE));
}

// The function is called when the dumping process has completed.
// Typically, this will be called from within a signal handler. Your
// ability to do anything interesting from within a signal handler is
// severly limited.  This is also called at the end of the dumping process
// if either no dumping child was created or if the child finished
// quickly. In fact, this may be called twice at the end of the same dump.
//
void CSample::dump_complete_signal(void)
{
    m_pILog->log_text(T("Sample module sees CSample::dump_complete_signal event." ENDLINE));
}

// Called when the game is shutting down, after the game database has
// been saved but prior to the logfiles being closed.
//
void CSample::shutdown(void)
{
    m_pILog->log_text(T("Sample module sees CSample::shutdown event." ENDLINE));
}

// Called after the database consistency check is completed.   Add
// checks for local data consistency here.
//
void CSample::dbck(void)
{
    m_pILog->log_text(T("Sample module sees CSample::dbck event." ENDLINE));
}

// Called when a player connects or creates at the connection screen.
// isnew of 1 indicates it was a creation, 0 is for a connection.
// num indicates the number of current connections for player.
//
void CSample::connect(dbref player, int isnew, int num)
{
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(isnew);
    UNUSED_PARAMETER(num);

    m_pILog->log_text(T("Sample module sees CSample::connect event." ENDLINE));
}

// Called when player disconnects from the game.  The parameter 'num' is
// the number of connections the player had upon being disconnected.
// Any value greater than 1 indicates multiple connections.
//
void CSample::disconnect(dbref player, int num)
{
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(num);

    m_pILog->log_text(T("Sample module sees CSample::disconnect event." ENDLINE));
}

// Called after any object type is created.
//
void CSample::data_create(dbref object)
{
    UNUSED_PARAMETER(object);

    m_pILog->log_text(T("Sample module sees CSample::data_create event." ENDLINE));
}

// Called when an object is cloned.  clone is the new object created
// from source.
//
void CSample::data_clone(dbref clone, dbref source)
{
    UNUSED_PARAMETER(clone);
    UNUSED_PARAMETER(source);

    m_pILog->log_text(T("Sample module sees CSample::data_clone event." ENDLINE));
}

// Called when the object is truly destroyed, not just set GOING
//
void CSample::data_free(dbref object)
{
    UNUSED_PARAMETER(object);

    m_pILog->log_text(T("Sample module sees CSample::data_free event." ENDLINE));
}

// SumProxy component which is not directly accessible.
//
CSumProxy::CSumProxy(void) : m_cRef(1), m_nChannel(CHANNEL_INVALID)
{
    g_cComponents++;
}

MUX_RESULT CSumProxy::FinalConstruct(void)
{
    return MUX_S_OK;
}

CSumProxy::~CSumProxy()
{
    g_cComponents--;
}

MUX_RESULT CSumProxy::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<ISum *>(this);
    }
    else if (IID_ISum == iid)
    {
        *ppv = static_cast<ISum *>(this);
    }
    else if (mux_IID_IMarshal == iid)
    {
        *ppv = static_cast<mux_IMarshal *>(this);
    }
    else
    {
        *ppv = NULL;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CSumProxy::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CSumProxy::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        // The last reference to the proxy was released, we need to clean up
        // the connection as well.
        //
        QUEUE_INFO qiFrame;
        Pipe_InitializeQueueInfo(&qiFrame);
        (void)Pipe_SendDiscPacket(m_nChannel, &qiFrame);
        m_nChannel = CHANNEL_INVALID;
        Pipe_EmptyQueue(&qiFrame);

        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CSumProxy::GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid)
{
    UNUSED_PARAMETER(riid);
    UNUSED_PARAMETER(ctx);
    UNUSED_PARAMETER(pcid);

    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CSumProxy::MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void *pv, marshal_context ctx)
{
    UNUSED_PARAMETER(pqi);
    UNUSED_PARAMETER(riid);
    UNUSED_PARAMETER(pv);
    UNUSED_PARAMETER(ctx);

    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CSumProxy::UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv)
{
    // Use the channel number in the marshal packet from the remote component
    // to support a proxy ISum.
    //
    size_t nWanted = sizeof(m_nChannel);
    if (  Pipe_GetBytes(pqi, &nWanted, &m_nChannel)
       && nWanted == sizeof(m_nChannel))
    {
        return QueryInterface(riid, ppv);
    }
    return MUX_E_NOINTERFACE;
}

MUX_RESULT CSumProxy::ReleaseMarshalData(QUEUE_INFO *pqi)
{
    UNUSED_PARAMETER(pqi);

    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CSumProxy::DisconnectObject(void)
{
    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CSumProxy::Add(int a, int b, int *sum)
{
    // Communicate with the remote component to service this request.
    //
    MUX_RESULT mr = MUX_S_OK;

    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    UINT32 iMethod = 3;
    Pipe_AppendBytes(&qiFrame, sizeof(iMethod), &iMethod);

    struct FRAME
    {
        int    a;
        int    b;
    } CallFrame;

    CallFrame.a       = a;
    CallFrame.b       = b;

    Pipe_AppendBytes(&qiFrame, sizeof(CallFrame), &CallFrame);

    mr = Pipe_SendCallPacketAndWait(m_nChannel, &qiFrame);

    if (MUX_SUCCEEDED(mr))
    {
        struct RETURN
        {
            int sum;
        } ReturnFrame;

        size_t nWanted = sizeof(ReturnFrame);
        if (  Pipe_GetBytes(&qiFrame, &nWanted, &ReturnFrame)
           && nWanted == sizeof(ReturnFrame))
        {
            *sum = ReturnFrame.sum;
        }
        else
        {
            mr = MUX_E_FAIL;
        }
    }
    Pipe_EmptyQueue(&qiFrame);
    return mr;
}

// Factory for SumProxy component which is not directly accessible.
//
CSumProxyFactory::CSumProxyFactory(void) : m_cRef(1)
{
}

CSumProxyFactory::~CSumProxyFactory()
{
}

MUX_RESULT CSumProxyFactory::QueryInterface(MUX_IID iid, void **ppv)
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

UINT32 CSumProxyFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CSumProxyFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CSumProxyFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (NULL != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CSumProxy *pSumProxy = NULL;
    try
    {
        pSumProxy = new CSumProxy;
    }
    catch (...)
    {
        ; // Nothing.
    }

    MUX_RESULT mr;
    if (NULL == pSumProxy)
    {
        return MUX_E_OUTOFMEMORY;
    }
    else
    {
        mr = pSumProxy->FinalConstruct();
        if (MUX_FAILED(mr))
        {
            pSumProxy->Release();
            return mr;
        }
    }

    mr = pSumProxy->QueryInterface(iid, ppv);
    pSumProxy->Release();
    return mr;
}

MUX_RESULT CSumProxyFactory::LockServer(bool bLock)
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
