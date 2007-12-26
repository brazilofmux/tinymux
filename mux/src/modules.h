/*! \file modules.h
 * \brief netmux-provided modules.
 *
 * Interfaces and classes declared here are built into the netmux server and
 * are available to netmux itself and to dynamically-loaded external modules.
 *
 * $Id$
 *
 */

#ifndef MODULES_H
#define MODULES_H
#if defined(HAVE_DLOPEN) || defined(WIN32)

const MUX_CID CID_Log                   = UINT64_C(0x000000020CE18E7A);
const MUX_CID CID_StubSlave             = UINT64_C(0x00000002267CA586);
const MUX_IID IID_ISlaveControl         = UINT64_C(0x0000000250C158E9);
const MUX_CID CID_QueryControlProxy     = UINT64_C(0x00000002683E889A);
const MUX_IID IID_IServerEventsControl  = UINT64_C(0x000000026EE5256E);
const MUX_CID CID_QuerySinkProxy        = UINT64_C(0x00000002746B93B9);
const MUX_IID IID_ILog                  = UINT64_C(0x000000028B9DC13A);
const MUX_CID CID_QueryServer           = UINT64_C(0x000000028FEA49AD);
const MUX_CID CID_ServerEventsSource    = UINT64_C(0x00000002A5080812);
const MUX_IID IID_IQuerySink            = UINT64_C(0x00000002CBBCE24E);
const MUX_CID CID_StubSlaveProxy        = UINT64_C(0x00000002D2453099);
const MUX_IID IID_IQueryControl         = UINT64_C(0x00000002ECD689FC);
const MUX_IID IID_IServerEventsSink     = UINT64_C(0x00000002F0F2753F);
const MUX_IID CID_QueryClient           = UINT64_C(0x00000002F571AB88);

interface mux_ILog : public mux_IUnknown
{
public:
    virtual bool start_log(int key, const UTF8 *primary, const UTF8 *secondary) = 0;

    virtual void log_perror(const UTF8 *primary, const UTF8 *secondary, const UTF8 *extra, const UTF8 *failing_object) = 0;
    virtual void log_text(const UTF8 *text) = 0;
    virtual void log_number(int num) = 0;
    virtual void DCL_CDECL log_printf(const char *fmt, ...) = 0;
    virtual void log_name(dbref target) = 0;
    virtual void log_name_and_loc(dbref player) = 0;
    virtual void log_type_and_name(dbref thing) = 0;

    virtual void end_log(void) = 0;
};

class CLog : public mux_ILog
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_ILog
    //
    virtual bool start_log(int key, const UTF8 *primary, const UTF8 *secondary);
    virtual void log_perror(const UTF8 *primary, const UTF8 *secondary, const UTF8 *extra, const UTF8 *failing_object);
    virtual void log_text(const UTF8 *text);
    virtual void log_number(int num);
    virtual void DCL_CDECL log_printf(const char *fmt, ...);
    virtual void log_name(dbref target);
    virtual void log_name_and_loc(dbref player);
    virtual void log_type_and_name(dbref thing);
    virtual void end_log(void);

    CLog(void);
    virtual ~CLog();

private:
    UINT32 m_cRef;
};

class CLogFactory : public mux_IClassFactory
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IClassFactory
    //
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);

    CLogFactory(void);
    virtual ~CLogFactory();

private:
    UINT32 m_cRef;
};

interface mux_IServerEventsSink : public mux_IUnknown
{
public:
    // Called after all normal MUX initialization is complete.
    //
    virtual void startup(void) = 0;

    // This is called prior to the game syncronizing its own state to its own
    // database.  If you depend on the the core database to store your data,
    // you need to checkpoint your changes here. The write-protection
    // mechanism in MUX is not turned on at this point.  You are guaranteed
    // to not be a fork()-ed dumping process.
    //
    virtual void presync_database(void) = 0;

    // Like the above routine except that it called from the SIGSEGV handler.
    // At this point, your choices are limited. You can attempt to use the core
    // database. The core won't stop you, but it is risky.
    //
    virtual void presync_database_sigsegv(void) = 0;

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
    virtual void dump_database(int dump_type) = 0;

    // The function is called when the dumping process has completed.
    // Typically, this will be called from within a signal handler. Your
    // ability to do anything interesting from within a signal handler is
    // severly limited.  This is also called at the end of the dumping process
    // if either no dumping child was created or if the child finished
    // quickly. In fact, this may be called twice at the end of the same dump.
    //
    virtual void dump_complete_signal(void) = 0;

    // Called when the game is shutting down, after the game database has
    // been saved but prior to the logfiles being closed.
    //
    virtual void shutdown(void) = 0;

    // Called after the database consistency check is completed.   Add
    // checks for local data consistency here.
    //
    virtual void dbck(void) = 0;

    // Called when a player connects or creates at the connection screen.
    // isnew of 1 indicates it was a creation, 0 is for a connection.
    // num indicates the number of current connections for player.
    //
    virtual void connect(dbref player, int isnew, int num) = 0;

    // Called when player disconnects from the game.  The parameter 'num' is
    // the number of connections the player had upon being disconnected.
    // Any value greater than 1 indicates multiple connections.
    //
    virtual void disconnect(dbref player, int num) = 0;

    // Called after any object type is created.
    //
    virtual void data_create(dbref object) = 0;

    // Called when an object is cloned.  clone is the new object created
    // from source.
    //
    virtual void data_clone(dbref clone, dbref source) = 0;

    // Called when the object is truly destroyed, not just set GOING
    //
    virtual void data_free(dbref object) = 0;
};

interface mux_IServerEventsControl : public mux_IUnknown
{
public:
    virtual MUX_RESULT Advise(mux_IServerEventsSink *pIServerEvents) = 0;
};

typedef struct ServerEventsSinkNode
{
    mux_IServerEventsSink        *pSink;
    struct ServerEventsSinkNode  *pNext;
} ServerEventsSinkNode;

extern ServerEventsSinkNode *g_pServerEventsSinkListHead;

class CServerEventsSource : public mux_IServerEventsControl
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IServerEventsControl
    //
    virtual MUX_RESULT Advise(mux_IServerEventsSink *pIServerEvents);

    CServerEventsSource(void);
    virtual ~CServerEventsSource();

private:
    UINT32 m_cRef;
    mux_IServerEventsSink *m_pSink;
};

class CServerEventsSourceFactory : public mux_IClassFactory
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IClassFactory
    //
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);

    CServerEventsSourceFactory(void);
    virtual ~CServerEventsSourceFactory();

private:
    UINT32 m_cRef;
};

interface mux_ISlaveControl : public mux_IUnknown
{
public:
#ifdef WIN32
    virtual MUX_RESULT AddModule(const UTF8 aModuleName[], const UTF16 aFileName[]) = 0;
#else
    virtual MUX_RESULT AddModule(const UTF8 aModuleName[], const UTF8 aFileName[]) = 0;
#endif // WIN32
    virtual MUX_RESULT RemoveModule(const UTF8 aModuleName[]) = 0;
    virtual MUX_RESULT ModuleInfo(int iModule, MUX_MODULE_INFO *pModuleInfo) = 0;
    virtual MUX_RESULT ModuleMaintenance(void) = 0;
    virtual MUX_RESULT ShutdownSlave(void) = 0;
};

class CStubSlave : public mux_ISlaveControl, public mux_IMarshal
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
    virtual MUX_RESULT MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, marshal_context ctx);
    virtual MUX_RESULT UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv);
    virtual MUX_RESULT ReleaseMarshalData(QUEUE_INFO *pqi);
    virtual MUX_RESULT DisconnectObject(void);

    // mux_ISlaveControl
    //
#ifdef WIN32
    virtual MUX_RESULT AddModule(const UTF8 aModuleName[], const UTF16 aFileName[]);
#else
    virtual MUX_RESULT AddModule(const UTF8 aModuleName[], const UTF8 aFileName[]);
#endif // WIN32
    virtual MUX_RESULT RemoveModule(const UTF8 aModuleName[]);
    virtual MUX_RESULT ModuleInfo(int iModule, MUX_MODULE_INFO *pModuleInfo);
    virtual MUX_RESULT ModuleMaintenance(void);
    virtual MUX_RESULT ShutdownSlave(void);

    CStubSlave(void);
    virtual ~CStubSlave();

private:
    UINT32 m_cRef;
};

class CStubSlaveFactory : public mux_IClassFactory
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IClassFactory
    //
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);

    CStubSlaveFactory(void);
    virtual ~CStubSlaveFactory();

private:
    UINT32 m_cRef;
};

class CStubSlaveProxy : public mux_ISlaveControl, public mux_IMarshal
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
    virtual MUX_RESULT MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, marshal_context ctx);
    virtual MUX_RESULT UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv);
    virtual MUX_RESULT ReleaseMarshalData(QUEUE_INFO *pqi);
    virtual MUX_RESULT DisconnectObject(void);

    // mux_ISlaveControl
    //
#ifdef WIN32
    virtual MUX_RESULT AddModule(const UTF8 aModuleName[], const UTF16 aFileName[]);
#else
    virtual MUX_RESULT AddModule(const UTF8 aModuleName[], const UTF8 aFileName[]);
#endif // WIN32
    virtual MUX_RESULT RemoveModule(const UTF8 aModuleName[]);
    virtual MUX_RESULT ModuleInfo(int iModule, MUX_MODULE_INFO *pModuleInfo);
    virtual MUX_RESULT ModuleMaintenance(void);
    virtual MUX_RESULT ShutdownSlave(void);

    CStubSlaveProxy(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CStubSlaveProxy();

private:
    UINT32 m_cRef;
    UINT32 m_nChannel;
    UTF8  *m_pModuleName;
};

class CStubSlaveProxyFactory : public mux_IClassFactory
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IClassFactory
    //
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);

    CStubSlaveProxyFactory(void);
    virtual ~CStubSlaveProxyFactory();

private:
    UINT32 m_cRef;
};

interface mux_IQuerySink : public mux_IUnknown
{
public:
    virtual MUX_RESULT Result(UINT32 iQueryHandle, UINT32 iError, QUEUE_INFO *pqiResultsSet) = 0;
};

interface mux_IQueryControl : public mux_IUnknown
{
public:
    virtual MUX_RESULT Connect(const UTF8 *pServer, const UTF8 *pDatabase, const UTF8 *pUser, const UTF8 *pPassword) = 0;
    virtual MUX_RESULT Advise(mux_IQuerySink *pIQuerySink) = 0;
    virtual MUX_RESULT Query(UINT32 iQueryHandle, const UTF8 *pDatabaseName, const UTF8 *pQuery) = 0;
};

class CQueryClient : public mux_IQuerySink, public mux_IMarshal
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
    virtual MUX_RESULT MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, marshal_context ctx);
    virtual MUX_RESULT UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv);
    virtual MUX_RESULT ReleaseMarshalData(QUEUE_INFO *pqi);
    virtual MUX_RESULT DisconnectObject(void);

    // mux_IQuerySink
    //
    virtual MUX_RESULT Result(UINT32 iQueryHandle, UINT32 iError, QUEUE_INFO *pqiResultsSet);

    CQueryClient(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CQueryClient();

private:
    UINT32 m_cRef;
};

class CQueryClientFactory : public mux_IClassFactory
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IClassFactory
    //
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);

    CQueryClientFactory(void);
    virtual ~CQueryClientFactory();

private:
    UINT32 m_cRef;
};

class CQueryControlProxy : public mux_IQueryControl, public mux_IMarshal
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
    virtual MUX_RESULT MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, marshal_context ctx);
    virtual MUX_RESULT UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv);
    virtual MUX_RESULT ReleaseMarshalData(QUEUE_INFO *pqi);
    virtual MUX_RESULT DisconnectObject(void);

    // mux_IQueryControl
    //
    virtual MUX_RESULT Connect(const UTF8 *pServer, const UTF8 *pDatabase, const UTF8 *pUser, const UTF8 *pPassword);
    virtual MUX_RESULT Advise(mux_IQuerySink *pIQuerySink);
    virtual MUX_RESULT Query(UINT32 iQueryHandle, const UTF8 *pDatabaseName, const UTF8 *pQuery);

    CQueryControlProxy(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CQueryControlProxy();

private:
    UINT32 m_cRef;
    UINT32 m_nChannel;
};

class CQueryControlProxyFactory : public mux_IClassFactory
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IClassFactory
    //
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);

    CQueryControlProxyFactory(void);
    virtual ~CQueryControlProxyFactory();

private:
    UINT32 m_cRef;
};

extern void init_modules(void);
extern void final_modules(void);

class CResultsSet
{
public:
    CResultsSet(QUEUE_INFO *pqi);
    ~CResultsSet(void);
    UINT32 Release(void);
    UINT32 AddRef(void);
    bool   isLoaded(void);
    void   SetError(UINT32 iError);
    UINT32 GetError(void);
    int    GetRowCount(void);
    const UTF8 *FirstField(int iRow);
    const UTF8 *NextField(void);

private:
    UINT32 m_cRef;
    UINT32 m_nFields;
    size_t m_nBlob;
    UTF8  *m_pBlob;
    bool   m_bLoaded;
    UINT32 m_iError;
    int    m_nRows;
    PUTF8 *m_pRows;

    const UTF8 *m_pCurrentField;
    int         m_iCurrentField;
};

#define QS_SUCCESS         (0)
#define QS_NO_SESSION      (1)
#define QS_SQL_UNAVAILABLE (2)
#define QS_QUERY_ERROR     (3)

#define RS_TOP             (0)

#endif
#endif // MODULES_H
