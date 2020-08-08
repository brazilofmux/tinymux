/*! \file sqlslave.cpp
 * \brief SQLSlave Module
 *
 */

#include "../autoconf.h"
#include "../config.h"
#include "../libmux.h"
#include "../modules.h"
#include "autoconf.h"
#if defined(HAVE_MYSQL_H)
#include <mysql.h>
#endif // HAVE_MYSQL_H
#include "sql.h"

class CQueryServer : public mux_IQueryControl, public mux_IMarshal
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IMarshal
    //
    virtual MUX_RESULT GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid);
    virtual MUX_RESULT MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void *pv, marshal_context ctx);
    virtual MUX_RESULT UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv);
    virtual MUX_RESULT ReleaseMarshalData(QUEUE_INFO *pqi);
    virtual MUX_RESULT DisconnectObject(void);

    // mux_IQueryControl
    //
    virtual MUX_RESULT Connect(const UTF8 *pServer, const UTF8 *pDatabase, const UTF8 *pUser, const UTF8 *pPassword);
    virtual MUX_RESULT Advise(mux_IQuerySink *pIQuerySink);
    virtual MUX_RESULT Query(UINT32 iQueryHandle, const UTF8 *pDatabaseName, const UTF8 *pQuery);

    CQueryServer(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CQueryServer();

private:
    UINT32          m_cRef;
    mux_IQuerySink *m_pIQuerySink;
#if defined(HAVE_MYSQL)
    MYSQL          *m_database;
#endif // HAVE_MYSQL
    const UTF8     *m_pServer;
    const UTF8     *m_pDatabase;
    const UTF8     *m_pUser;
    const UTF8     *m_pPassword;

    void ConnectionHelper();
};

static INT32 g_cComponents  = 0;
static INT32 g_cServerLocks = 0;

#define NUM_CLASSES 2
static MUX_CLASS_INFO sum_classes[NUM_CLASSES] =
{
    { CID_QueryServer    },
    { CID_QuerySinkProxy }
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

    if (CID_QueryServer == cid)
    {
        CQueryServerFactory *pQueryServerFactory = NULL;
        try
        {
            pQueryServerFactory = new CQueryServerFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pQueryServerFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pQueryServerFactory->QueryInterface(iid, ppv);
        pQueryServerFactory->Release();
    }
    else if (CID_QuerySinkProxy == cid)
    {
        CQuerySinkProxyFactory *pQuerySinkProxyFactory = NULL;
        try
        {
            pQuerySinkProxyFactory = new CQuerySinkProxyFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pQuerySinkProxyFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pQuerySinkProxyFactory->QueryInterface(iid, ppv);
        pQuerySinkProxyFactory->Release();
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Register(void)
{
    // Advertise our components.
    //
    MUX_RESULT mr = mux_RegisterClassObjects(NUM_CLASSES, sum_classes, NULL);
#if defined(HAVE_MYSQL)
    if (MUX_SUCCEEDED(mr))
    {
        if (mysql_library_init(0, NULL, NULL))
        {
            mr = MUX_E_FAIL;
        }
    }
#endif
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Unregister(void)
{
#if defined(HAVE_MYSQL)
    mysql_library_end();
#endif
    return mux_RevokeClassObjects(NUM_CLASSES, sum_classes);
}

// QueryServer component which is not directly accessible.
//
CQueryServer::CQueryServer(void) : m_cRef(1), m_pIQuerySink(NULL)
{
#if defined(HAVE_MYSQL)
    m_database = NULL;
#endif // HAVE_MYSQL
    m_pServer = NULL;
    m_pDatabase = NULL;
    m_pUser = NULL;
    m_pPassword = NULL;

    g_cComponents++;
}

MUX_RESULT CQueryServer::FinalConstruct(void)
{
    MUX_RESULT mr = MUX_S_OK;
    return mr;
}

CQueryServer::~CQueryServer()
{
    if (NULL != m_pIQuerySink)
    {
        m_pIQuerySink->Release();
        m_pIQuerySink = NULL;
    }

#if defined(HAVE_MYSQL)
    if (NULL != m_database)
    {
        mysql_close(m_database);
        m_database = NULL;
    }
    delete [] m_pServer;
    m_pServer = NULL;
    delete [] m_pDatabase;
    m_pDatabase = NULL;
    delete [] m_pUser;
    m_pUser = NULL;
    delete [] m_pPassword;
    m_pPassword = NULL;
#endif // HAVE_MYSQL

    g_cComponents--;
}

MUX_RESULT CQueryServer::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IQueryControl *>(this);
    }
    else if (IID_IQueryControl == iid)
    {
        *ppv = static_cast<mux_IQueryControl *>(this);
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

UINT32 CQueryServer::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CQueryServer::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CQueryServer::GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid)
{
    UNUSED_PARAMETER(ctx);

    if (NULL == pcid)
    {
        return MUX_E_INVALIDARG;
    }
    else if (  IID_IQueryControl == riid
            && CrossProcess == ctx)
    {
        // We only support cross-process at the moment.
        //
        *pcid = CID_QueryControlProxy;
        return MUX_S_OK;
    }
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryControl_Disconnect(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    UNUSED_PARAMETER(pqi);

    // Get our interface pointer from the channel.
    //
    mux_IUnknown *pIUnknown= static_cast<mux_IUnknown *>(pci->pInterface);
    pci->pInterface = NULL;

    // Tear down our side of the communication.  Our callback functions will
    // no longer be called.
    //
    Pipe_FreeChannel(pci);

    if (NULL != pIUnknown)
    {
        pIUnknown->Release();
        return MUX_S_OK;
    }
    else
    {
        return MUX_E_NOINTERFACE;
    }
}

MUX_RESULT CQueryControl_Call(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    mux_IQueryControl *pIQueryControl = static_cast<mux_IQueryControl *>(pci->pInterface);
    if (NULL == pIQueryControl)
    {
        return MUX_E_NOINTERFACE;
    }

    UINT32 iMethod;
    size_t nWanted = sizeof(iMethod);
    if (  !Pipe_GetBytes(pqi, &nWanted, &iMethod)
       || nWanted != sizeof(iMethod))
    {
        return MUX_E_INVALIDARG;
    }

    // The IUnknown methods (0, 1, and 2) do not make it across, so we don't
    // attempt to handle them here.  Instead, when the reference count on
    // CQueryServerProxy goes to zero, it drops the connection and destroys
    // itself.  We see that as a call to CQueryControl_Disconnect.
    //
    switch (iMethod)
    {
    case 3:  // MUX_RESULT Connect(UTF8 *pServer, UTF8 *pDatabase, UTF8 *pUser, UTF8 *pPassword);
        {
            struct FRAME
            {
                size_t nServer;
                size_t nDatabase;
                size_t nUser;
                size_t nPassword;
            } CallFrame;

            struct RETURN
            {
                MUX_RESULT mr;
            } ReturnFrame = { MUX_S_OK };

            nWanted = sizeof(CallFrame);
            if (  !Pipe_GetBytes(pqi, &nWanted, &CallFrame)
               || nWanted != sizeof(CallFrame))
            {
                ReturnFrame.mr = MUX_E_INVALIDARG;
            }

            if (MUX_SUCCEEDED(ReturnFrame.mr))
            {
                UTF8 *pServer = NULL;
                UTF8 *pDatabase = NULL;
                UTF8 *pUser = NULL;
                UTF8 *pPassword = NULL;
                try
                {
                    pServer   = new UTF8[CallFrame.nServer];
                    pDatabase = new UTF8[CallFrame.nDatabase];
                    pUser     = new UTF8[CallFrame.nUser];
                    pPassword = new UTF8[CallFrame.nPassword];
                }
                catch (...)
                {
                    ; // Nothing.
                }

                if (  NULL != pServer
                   && NULL != pDatabase
                   && NULL != pUser
                   && NULL != pPassword)
                {
                    nWanted = CallFrame.nServer;
                    if (  Pipe_GetBytes(pqi, &nWanted, pServer)
                       && nWanted == CallFrame.nServer)
                    {
                        nWanted = CallFrame.nDatabase;
                        if (  Pipe_GetBytes(pqi, &nWanted, pDatabase)
                           && nWanted == CallFrame.nDatabase)
                        {
                            nWanted = CallFrame.nUser;
                            if (  Pipe_GetBytes(pqi, &nWanted, pUser)
                               && nWanted == CallFrame.nUser)
                            {
                                nWanted = CallFrame.nPassword;
                                if (  Pipe_GetBytes(pqi, &nWanted, pPassword)
                                   && nWanted == CallFrame.nPassword)
                                {
                                    ReturnFrame.mr = pIQueryControl->Connect(pServer, pDatabase, pUser, pPassword);
                                }
                                else
                                {
                                    ReturnFrame.mr = MUX_E_INVALIDARG;
                                }
                            }
                            else
                            {
                                ReturnFrame.mr = MUX_E_INVALIDARG;
                            }
                        }
                        else
                        {
                            ReturnFrame.mr = MUX_E_INVALIDARG;
                        }
                    }
                    else
                    {
                        ReturnFrame.mr = MUX_E_INVALIDARG;
                    }
                }
                else
                {
                    ReturnFrame.mr = MUX_E_OUTOFMEMORY;
                }
            }
            Pipe_EmptyQueue(pqi);
            Pipe_AppendBytes(pqi, sizeof(ReturnFrame), &ReturnFrame);
            return MUX_S_OK;
        }
        break;

    case 4:  // MUX_RESULT Advise(mux_IQuerySink *pIQuerySink);
        {
            struct RETURN
            {
                MUX_RESULT mr;
            } ReturnFrame = { MUX_S_OK };

            mux_IQuerySink *pIQuerySink = NULL;
            ReturnFrame.mr = mux_UnmarshalInterface(pqi, IID_IQuerySink, (void **)&pIQuerySink);

            if (MUX_SUCCEEDED(ReturnFrame.mr))
            {
                ReturnFrame.mr = pIQueryControl->Advise(pIQuerySink);
            }

            Pipe_EmptyQueue(pqi);
            Pipe_AppendBytes(pqi, sizeof(ReturnFrame), &ReturnFrame);
            return MUX_S_OK;
        }
        break;

    case 5:  // MUX_RESULT Query(UINT32 iQueryHandle, UTF8 *pDatabaseName, UTF8 *pQuery);
        {
            struct FRAME
            {
                UINT32 iQueryHandle;
                size_t nDatabaseName;
                size_t nQuery;
            } CallFrame;

            struct RETURN
            {
                MUX_RESULT mr;
            } ReturnFrame = { MUX_S_OK };

            nWanted = sizeof(CallFrame);
            if (  !Pipe_GetBytes(pqi, &nWanted, &CallFrame)
               || nWanted != sizeof(CallFrame))
            {
                ReturnFrame.mr = MUX_E_INVALIDARG;
            }

            if (MUX_SUCCEEDED(ReturnFrame.mr))
            {
                UTF8 *pDatabaseName = NULL;
                UTF8 *pQuery = NULL;
                try
                {
                    pDatabaseName = new UTF8[CallFrame.nDatabaseName];
                    pQuery        = new UTF8[CallFrame.nQuery];
                }
                catch (...)
                {
                    ; // Nothing.
                }

                if (  NULL != pDatabaseName
                   && NULL != pQuery)
                {
                    nWanted = CallFrame.nDatabaseName;
                    if (  Pipe_GetBytes(pqi, &nWanted, pDatabaseName)
                       && nWanted == CallFrame.nDatabaseName)
                    {
                        nWanted = CallFrame.nQuery;
                        if (  Pipe_GetBytes(pqi, &nWanted, pQuery)
                           && nWanted == CallFrame.nQuery)
                        {
                            ReturnFrame.mr = pIQueryControl->Query(CallFrame.iQueryHandle, pDatabaseName, pQuery);
                        }
                        else
                        {
                            ReturnFrame.mr = MUX_E_INVALIDARG;
                        }
                    }
                    else
                    {
                        ReturnFrame.mr = MUX_E_INVALIDARG;
                    }
                }
                else
                {
                    ReturnFrame.mr = MUX_E_OUTOFMEMORY;
                }

                if (NULL != pDatabaseName)
                {
                    delete [] pDatabaseName;
                    pDatabaseName = NULL;
                }

                if (NULL != pQuery)
                {
                    delete [] pQuery;
                    pQuery = NULL;
                }
            }
            Pipe_EmptyQueue(pqi);
            Pipe_AppendBytes(pqi, sizeof(ReturnFrame), &ReturnFrame);
            return MUX_S_OK;
        }
        break;
    }
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryControl_Msg(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    // The same as CQueryControl_Call except that the caller is no longer
    // available to receive the ReturnFrame.
    //
    return CQueryControl_Call(pci, pqi);
}

MUX_RESULT CQueryServer::MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void *pv, marshal_context ctx)
{
    // Parameter validation and initialization.
    //
    MUX_RESULT mr = MUX_S_OK;
    if (NULL == pqi)
    {
        mr = MUX_E_INVALIDARG;
    }
    else if (IID_IQueryControl != riid)
    {
        mr = MUX_E_FAIL;
    }
    else if (CrossProcess != ctx)
    {
        mr = MUX_E_NOTIMPLEMENTED;
    }
    else
    {
        mux_IQueryControl *pIQueryControl = NULL;
        if (NULL == pv)
        {
            mr = QueryInterface(IID_IQueryControl, (void **)&pIQueryControl);
        }
        else
        {
            mux_IUnknown *pIUnknown = static_cast<mux_IUnknown *>(pv);
            mr = pIUnknown->QueryInterface(IID_IQueryControl, (void **)&pIQueryControl);
        }
        if (MUX_SUCCEEDED(mr))
        {
            // Construct a packet sufficient to allow the proxy to communicate with us.
            //
            CHANNEL_INFO *pChannel = Pipe_AllocateChannel(CQueryControl_Call, CQueryControl_Msg, CQueryControl_Disconnect);
            if (NULL != pChannel)
            {
                pChannel->pInterface = pIQueryControl;
                Pipe_AppendBytes(pqi, sizeof(pChannel->nChannel), (UTF8*)(&pChannel->nChannel));
                mr =  MUX_S_OK;
            }
            else
            {
                pIQueryControl->Release();
                pIQueryControl = NULL;
                mr = MUX_E_OUTOFMEMORY;
            }
        }
    }
    return mr;
}

MUX_RESULT CQueryServer::UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv)
{
    UNUSED_PARAMETER(pqi);
    UNUSED_PARAMETER(riid);
    UNUSED_PARAMETER(ppv);

    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryServer::ReleaseMarshalData(QUEUE_INFO *pqi)
{
    // Since the Marshal Data is like an extra reference on an object, if the
    // Marshaled Data is never unmarshalled, libmux should use this function
    // to release the reference to the component.  This is only implemented on
    // the server side -- not the proxy.
    //
    UINT32 nChannel;
    size_t nWanted = sizeof(nChannel);
    if (  Pipe_GetBytes(pqi, &nWanted, &nChannel)
       && sizeof(nChannel) == nWanted)
    {
        CHANNEL_INFO *pChannel = Pipe_FindChannel(nChannel);
        if (NULL != pChannel)
        {
            CQueryControl_Disconnect(pChannel, pqi);
        }
    }
    return MUX_S_OK;
}

MUX_RESULT CQueryServer::DisconnectObject(void)
{
    // This is called when the hosting process is about to go down to give the
    // component a chance to notify its proxy that it is about to shut down.
    // This is only implemented on the server side -- not the proxy.
    //
    // TODO: There isn't a mechanism for sending such a notification, yet.
    //
    return MUX_S_OK;
}

MUX_RESULT CQueryServer::Connect(const UTF8 *pServer, const UTF8 *pDatabase, const UTF8 *pUser, const UTF8 *pPassword)
{
    // Free any previous Server/Database/User/Password values.
    //
    delete [] m_pServer;
    m_pServer = NULL;
    delete [] m_pDatabase;
    m_pDatabase = NULL;
    delete [] m_pUser;
    m_pUser = NULL;
    delete [] m_pPassword;
    m_pPassword = NULL;

    // Save new Server/Database/User/Password values.  These are used later if reconnection is necessary.
    //
    m_pServer = pServer;
    delete [] m_pDatabase;
    m_pDatabase = pDatabase;
    delete [] m_pUser;
    m_pUser = pUser;
    delete [] m_pPassword;
    m_pPassword = pPassword;

#if defined(HAVE_MYSQL)
    // Close any existing session.
    //
    if (NULL != m_database)
    {
        mysql_close(m_database);
        m_database = NULL;
    }

    m_database = mysql_init(NULL);

    if (NULL != m_database)
    {
        ConnectionHelper();
    }
#endif // HAVE_MYSQL
    return MUX_S_OK;
}

void CQueryServer::ConnectionHelper()
{
#if defined(HAVE_MYSQL)
    if ('\0' != m_pServer[0])
    {
#ifdef MYSQL_OPT_RECONNECT
        // As of MySQL 5.0.3, the default is no longer to reconnect.
        //
        my_bool reconnect = 1;
        mysql_options(m_database, MYSQL_OPT_RECONNECT, (const char *)&reconnect);
#endif
        mysql_options(m_database, MYSQL_SET_CHARSET_NAME, "utf8");

        if (mysql_real_connect(m_database, (char *)m_pServer, (char *)m_pUser,
             (char *)m_pPassword, (char *)m_pDatabase, 0, NULL, 0) != 0)
        {
#ifdef MYSQL_OPT_RECONNECT
            // Before MySQL 5.0.19, mysql_real_connect sets the option
            // back to default, so we set it again.
            //
            mysql_options(m_database, MYSQL_OPT_RECONNECT, (const char *)&reconnect);
#endif
        }
    }
#endif
}

MUX_RESULT CQueryServer::Advise(mux_IQuerySink *pIQuerySink)
{
    if (NULL != m_pIQuerySink)
    {
        m_pIQuerySink->Release();
        m_pIQuerySink = NULL;
    }

    if (NULL == pIQuerySink)
    {
        return MUX_E_INVALIDARG;
    }

    m_pIQuerySink = pIQuerySink;
    return MUX_S_OK;
}

MUX_RESULT CQueryServer::Query(UINT32 iQueryHandle, const UTF8 *pDatabaseName, const UTF8 *pQuery)
{
    UNUSED_PARAMETER(pDatabaseName);

    if (NULL == m_pIQuerySink)
    {
        return MUX_E_NOTREADY;
    }

    UINT32 iError = QS_SUCCESS;

    QUEUE_INFO qiResultsSet;
    Pipe_InitializeQueueInfo(&qiResultsSet);

#if defined(HAVE_MYSQL)
    if (NULL == m_database)
    {
        iError = QS_NO_SESSION;
    }
    else
    {
        unsigned long lThreadId_before = mysql_thread_id(m_database);
        if (mysql_ping(m_database) != 0)
        {
            // Attempt our own reconnection.
            //
            ConnectionHelper();
            if (mysql_ping(m_database) != 0)
            {
                iError = QS_SQL_UNAVAILABLE;
            }
        }
        else
        {
            unsigned long lThreadId_after = mysql_thread_id(m_database);
            if (lThreadId_before != lThreadId_after)
            {
                // Respond to detected reconnection.
                //
            }
        }
    }

    if (  QS_SUCCESS == iError
       && mysql_real_query(m_database, (char *)pQuery, strlen((char *)pQuery)) != 0)
    {
        iError = QS_QUERY_ERROR;
    }

    MYSQL_RES *result = NULL;
    MYSQL_ROW  row;

    int nFields = 0;
    if (iError == QS_SUCCESS)
    {
        size_t nRows = 0;
        result = mysql_store_result(m_database);
        if (NULL == result)
        {
            Pipe_AppendBytes(&qiResultsSet, sizeof(nFields), &nFields);
            Pipe_AppendBytes(&qiResultsSet, sizeof(nRows), &nRows);
        }
        else
        {
            nFields = mysql_num_fields(result);
            Pipe_AppendBytes(&qiResultsSet, sizeof(nFields), &nFields);

            row = mysql_fetch_row(result);
            while (row)
            {
                nRows++;

                int loop;
                for (loop = 0; loop < nFields; loop++)
                {
                    const char *p;
                    if (NULL != row[loop])
                    {
                        p = row[loop];
                    }
                    else
                    {
                        p = "";
                    }
                    size_t n = strlen(p)+1;
                    Pipe_AppendBytes(&qiResultsSet, sizeof(n), &n);
                    Pipe_AppendBytes(&qiResultsSet, n, p);
                }
                row = mysql_fetch_row(result);
            }
            mysql_free_result(result);
            Pipe_AppendBytes(&qiResultsSet, sizeof(nRows), &nRows);
        }
    }
#else // HAVE_MYSQL
    iError = QS_NO_SESSION;
#endif // HAVE_MYSQL

    MUX_RESULT mr = m_pIQuerySink->Result(iQueryHandle, iError, &qiResultsSet);
    Pipe_EmptyQueue(&qiResultsSet);
    return mr;
}

// Factory for CQueryServer component which is not directly accessible.
//
CQueryServerFactory::CQueryServerFactory(void) : m_cRef(1)
{
}

CQueryServerFactory::~CQueryServerFactory()
{
}

MUX_RESULT CQueryServerFactory::QueryInterface(MUX_IID iid, void **ppv)
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

UINT32 CQueryServerFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CQueryServerFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CQueryServerFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (NULL != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CQueryServer *pQueryServer = NULL;
    try
    {
        pQueryServer = new CQueryServer;
    }
    catch (...)
    {
        ; // Nothing.
    }

    MUX_RESULT mr;
    if (NULL == pQueryServer)
    {
        return MUX_E_OUTOFMEMORY;
    }
    else
    {
        mr = pQueryServer->FinalConstruct();
        if (MUX_FAILED(mr))
        {
            pQueryServer->Release();
            return mr;
        }
    }

    mr = pQueryServer->QueryInterface(iid, ppv);
    pQueryServer->Release();
    return mr;
}

MUX_RESULT CQueryServerFactory::LockServer(bool bLock)
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

// QuerySinkProxy component which is not directly accessible.
//
CQuerySinkProxy::CQuerySinkProxy(void) : m_cRef(1), m_nChannel(CHANNEL_INVALID)
{
    g_cComponents++;
}

MUX_RESULT CQuerySinkProxy::FinalConstruct(void)
{
    return MUX_S_OK;
}

CQuerySinkProxy::~CQuerySinkProxy()
{
    g_cComponents--;
}

MUX_RESULT CQuerySinkProxy::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IQuerySink *>(this);
    }
    else if (IID_IQuerySink == iid)
    {
        *ppv = static_cast<mux_IQuerySink *>(this);
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

UINT32 CQuerySinkProxy::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CQuerySinkProxy::Release(void)
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
MUX_RESULT CQuerySinkProxy::GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid)
{
    UNUSED_PARAMETER(riid);
    UNUSED_PARAMETER(ctx);
    UNUSED_PARAMETER(pcid);

    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQuerySinkProxy::MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void *pv, marshal_context ctx)
{
    UNUSED_PARAMETER(pqi);
    UNUSED_PARAMETER(riid);
    UNUSED_PARAMETER(pv);
    UNUSED_PARAMETER(ctx);

    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQuerySinkProxy::UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv)
{
    // Use the channel number in the marshal packet from the remote component
    // to support a proxy IQueryControl.
    //
    size_t nWanted = sizeof(m_nChannel);
    if (  Pipe_GetBytes(pqi, &nWanted, &m_nChannel)
       && nWanted == sizeof(m_nChannel))
    {
        return QueryInterface(riid, ppv);
    }
    return MUX_E_NOINTERFACE;
}

MUX_RESULT CQuerySinkProxy::ReleaseMarshalData(QUEUE_INFO *pqi)
{
    UNUSED_PARAMETER(pqi);

    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQuerySinkProxy::DisconnectObject(void)
{
    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQuerySinkProxy::Result(UINT32 iQueryHandle, UINT32 iError, QUEUE_INFO *pqiResultsSet)
{
    UNUSED_PARAMETER(iQueryHandle);

    // Communicate with the remote component to service this request.
    //
    MUX_RESULT mr = MUX_S_OK;

    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    UINT32 iMethod = 3;
    Pipe_AppendBytes(&qiFrame, sizeof(iMethod), &iMethod);

    struct FRAME
    {
        UINT32 iQueryHandle;
        UINT32 iError;
    } CallFrame;

    CallFrame.iQueryHandle = iQueryHandle;
    CallFrame.iError = iError;

    Pipe_AppendBytes(&qiFrame, sizeof(CallFrame), &CallFrame);
    Pipe_AppendQueue(&qiFrame, pqiResultsSet);

    mr = Pipe_SendMsgPacket(m_nChannel, &qiFrame);

    if (MUX_SUCCEEDED(mr))
    {
        struct RETURN
        {
            MUX_RESULT mr;
        } ReturnFrame;

        size_t nWanted = sizeof(ReturnFrame);
        if (  Pipe_GetBytes(&qiFrame, &nWanted, &ReturnFrame)
           && nWanted == sizeof(ReturnFrame))
        {
            mr = ReturnFrame.mr;
        }
        else
        {
            mr = MUX_E_FAIL;
        }
    }
    Pipe_EmptyQueue(&qiFrame);
    return mr;
}

// Factory for QuerySinkProxy component which is not directly accessible.
//
CQuerySinkProxyFactory::CQuerySinkProxyFactory(void) : m_cRef(1)
{
}

CQuerySinkProxyFactory::~CQuerySinkProxyFactory()
{
}

MUX_RESULT CQuerySinkProxyFactory::QueryInterface(MUX_IID iid, void **ppv)
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

UINT32 CQuerySinkProxyFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CQuerySinkProxyFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CQuerySinkProxyFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (NULL != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CQuerySinkProxy *pQuerySinkProxy = NULL;
    try
    {
        pQuerySinkProxy = new CQuerySinkProxy;
    }
    catch (...)
    {
        ; // Nothing.
    }

    MUX_RESULT mr;
    if (NULL == pQuerySinkProxy)
    {
        return MUX_E_OUTOFMEMORY;
    }
    else
    {
        mr = pQuerySinkProxy->FinalConstruct();
        if (MUX_FAILED(mr))
        {
            pQuerySinkProxy->Release();
            return mr;
        }
    }

    mr = pQuerySinkProxy->QueryInterface(iid, ppv);
    pQuerySinkProxy->Release();
    return mr;
}

MUX_RESULT CQuerySinkProxyFactory::LockServer(bool bLock)
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
