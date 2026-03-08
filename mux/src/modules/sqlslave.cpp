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

class CQueryServer : public mux_IQueryControl
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t     AddRef(void);
    virtual uint32_t     Release(void);

    // mux_IQueryControl
    //
    virtual MUX_RESULT Connect(const UTF8 *pServer, const UTF8 *pDatabase, const UTF8 *pUser, const UTF8 *pPassword);
    virtual MUX_RESULT Advise(mux_IQuerySink *pIQuerySink);
    virtual MUX_RESULT Query(uint32_t iQueryHandle, const UTF8 *pDatabaseName, const UTF8 *pQuery);

    CQueryServer(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CQueryServer();

private:
    uint32_t          m_cRef;
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

static int32_t g_cComponents  = 0;
static int32_t g_cServerLocks = 0;

#define NUM_CLASSES 1
static MUX_CLASS_INFO sum_classes[NUM_CLASSES] =
{
    { CID_QueryServer }
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
    else
    {
        *ppv = NULL;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CQueryServer::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CQueryServer::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
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
    m_pDatabase = pDatabase;
    m_pUser = pUser;
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
        mysql_options(m_database, MYSQL_OPT_RECONNECT, reinterpret_cast<const char *>(&reconnect));
#endif
        mysql_options(m_database, MYSQL_SET_CHARSET_NAME, "utf8");

        if (mysql_real_connect(m_database, reinterpret_cast<char *>(m_pServer), reinterpret_cast<char *>(m_pUser),
             reinterpret_cast<char *>(m_pPassword), reinterpret_cast<char *>(m_pDatabase), 0, NULL, 0) != 0)
        {
#ifdef MYSQL_OPT_RECONNECT
            // Before MySQL 5.0.19, mysql_real_connect sets the option
            // back to default, so we set it again.
            //
            mysql_options(m_database, MYSQL_OPT_RECONNECT, reinterpret_cast<const char *>(&reconnect));
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

MUX_RESULT CQueryServer::Query(uint32_t iQueryHandle, const UTF8 *pDatabaseName, const UTF8 *pQuery)
{
    UNUSED_PARAMETER(pDatabaseName);

    if (NULL == m_pIQuerySink)
    {
        return MUX_E_NOTREADY;
    }

    uint32_t iError = QS_SUCCESS;

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
       && mysql_real_query(m_database, reinterpret_cast<char *>(pQuery), strlen(reinterpret_cast<char *>(pQuery))) != 0)
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

        // Drain any remaining result sets from stored procedures.
        //
        while (mysql_next_result(m_database) == 0)
        {
            MYSQL_RES *extra = mysql_store_result(m_database);
            if (extra)
            {
                mysql_free_result(extra);
            }
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

uint32_t CQueryServerFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CQueryServerFactory::Release(void)
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

