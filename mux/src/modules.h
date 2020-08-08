/*! \file modules.h
 * \brief netmux-provided modules.
 *
 * Interfaces and classes declared here are built into the netmux server and
 * are available to netmux itself and to dynamically-loaded external modules.
 *
 */

#ifndef MODULES_H
#define MODULES_H

/* Logging options */

#define LOG_ALLCOMMANDS 0x00000001  /* Log all commands */
#define LOG_ACCOUNTING  0x00000002  /* Write accounting info on logout */
#define LOG_BADCOMMANDS 0x00000004  /* Log bad commands */
#define LOG_BUGS        0x00000008  /* Log program bugs found */
#define LOG_DBSAVES     0x00000010  /* Log database dumps */
#define LOG_CONFIGMODS  0x00000020  /* Log changes to configuration */
#define LOG_PCREATES    0x00000040  /* Log character creations */
#define LOG_KILLS       0x00000080  /* Log KILLs */
#define LOG_LOGIN       0x00000100  /* Log logins and logouts */
#define LOG_NET         0x00000200  /* Log net connects and disconnects */
#define LOG_SECURITY    0x00000400  /* Log security-related events */
#define LOG_SHOUTS      0x00000800  /* Log shouts */
#define LOG_STARTUP     0x00001000  /* Log nonfatal errors in startup */
#define LOG_WIZARD      0x00002000  /* Log dangerous things */
#define LOG_ALLOCATE    0x00004000  /* Log alloc/free from buffer pools */
#define LOG_PROBLEMS    0x00008000  /* Log runtime problems */
#define LOG_SUSPECTCMDS 0x00020000  // Log SUSPECT player keyboard commands.
#define LOG_TIMEUSE     0x00040000  // Log CPU time usage.
#define LOG_ALWAYS      0x80000000  /* Always log it */

/* Evaluation directives */

#define EV_TRACE        0x00200000  /* Request a trace of this call to eval */

const MUX_IID IID_IFunctionSinkControl   = UINT64_C(0x000000020560E6D5);
const MUX_CID CID_Log                    = UINT64_C(0x000000020CE18E7A);
const MUX_CID CID_StubSlave              = UINT64_C(0x00000002267CA586);
const MUX_IID IID_IFunctionsControl      = UINT64_C(0x000000022E28F8FA);
const MUX_IID IID_ISlaveControl          = UINT64_C(0x0000000250C158E9);
const MUX_IID IID_IServerEventsControl   = UINT64_C(0x000000026EE5256E);
const MUX_CID CID_QuerySinkProxy         = UINT64_C(0x00000002746B93B9);
const MUX_IID IID_ILog                   = UINT64_C(0x000000028B9DC13B);
const MUX_CID CID_QueryServer            = UINT64_C(0x000000028FEA49AD);
const MUX_CID CID_ServerEventsSource     = UINT64_C(0x00000002A5080812);
const MUX_IID IID_IQuerySink             = UINT64_C(0x00000002CBBCE24E);
const MUX_CID CID_StubSlaveProxy         = UINT64_C(0x00000002D2453099);
const MUX_IID IID_IFunction              = UINT64_C(0x00000002D34910C1);
const MUX_IID IID_IQueryControl          = UINT64_C(0x00000002ECD689FC);
const MUX_IID IID_IServerEventsSink      = UINT64_C(0x00000002F0F2753F);
const MUX_IID CID_QueryClient            = UINT64_C(0x00000002F571AB88);
const MUX_CID CID_Functions              = UINT64_C(0x00000002FE32BEA1);

interface mux_ILog : public mux_IUnknown
{
public:
    virtual MUX_RESULT start_log(bool *pStarted, int key, const UTF8 *primary, const UTF8 *secondary) = 0;
    virtual MUX_RESULT log_perror(const UTF8 *primary, const UTF8 *secondary, const UTF8 *extra, const UTF8 *failing_object) = 0;
    virtual MUX_RESULT log_text(const UTF8 *text) = 0;
    virtual MUX_RESULT log_number(int num) = 0;
    virtual MUX_RESULT log_name(dbref target) = 0;
    virtual MUX_RESULT log_name_and_loc(dbref player) = 0;
    virtual MUX_RESULT log_type_and_name(dbref thing) = 0;
    virtual MUX_RESULT end_log(void) = 0;
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

interface mux_ISlaveControl : public mux_IUnknown
{
public:
#if defined(WINDOWS_FILES)
    virtual MUX_RESULT AddModule(const UTF8 aModuleName[], const UTF16 aFileName[]) = 0;
#elif defined(UNIX_FILES)
    virtual MUX_RESULT AddModule(const UTF8 aModuleName[], const UTF8 aFileName[]) = 0;
#endif // UNIX_FILES
    virtual MUX_RESULT RemoveModule(const UTF8 aModuleName[]) = 0;
    virtual MUX_RESULT ModuleInfo(int iModule, MUX_MODULE_INFO *pModuleInfo) = 0;
    virtual MUX_RESULT ModuleMaintenance(void) = 0;
    virtual MUX_RESULT ShutdownSlave(void) = 0;
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

extern MUX_RESULT init_modules(void);
extern MUX_RESULT init_stubslave(void);
extern void final_stubslave(void);
extern void final_modules(void);

#define QS_SUCCESS         (0)
#define QS_NO_SESSION      (1)
#define QS_SQL_UNAVAILABLE (2)
#define QS_QUERY_ERROR     (3)

#define RS_TOP             (0)

// Functions
//
interface mux_IFunction : public mux_IUnknown
{
public:
    virtual MUX_RESULT Call(unsigned int nKey, __in UTF8 *buff, __deref_inout UTF8 **bufc, dbref executor, dbref caller, dbref enactor,
        int eval, __in UTF8 *fargs[], int nfargs, __in const UTF8 *cargs[], int ncargs) = 0;
};

interface mux_IFunctionsControl : public mux_IUnknown
{
public:
    virtual MUX_RESULT Add(unsigned int nKey, const UTF8 *name, mux_IFunction *pIFun, int maxArgsParsed, int minArgs, int maxArgs, int flags, int perms) = 0;
};

#endif // MODULES_H
