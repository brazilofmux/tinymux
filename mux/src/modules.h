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
#define LOG_DEBUG       0x00080000  // Log extra debugging information.
#define LOG_ALWAYS      0x80000000  /* Always log it */

/* Evaluation directives */

#define EV_TRACE        0x00200000  /* Request a trace of this call to eval */

const MUX_IID IID_IFunctionSinkControl   = UINT64_C(0x000000020560E6D5);
const MUX_CID CID_Log                    = UINT64_C(0x000000020CE18E7A);
const MUX_CID CID_StubSlave              = UINT64_C(0x00000002267CA586);
const MUX_IID IID_IFunctionsControl      = UINT64_C(0x000000022E28F8FA);
const MUX_IID IID_ISlaveControl          = UINT64_C(0x0000000250C158E9);
const MUX_IID IID_IServerEventsControl   = UINT64_C(0x000000026EE5256E);
const MUX_CID CID_QuerySinkPSFactory      = UINT64_C(0x00000002746B93B9);
const MUX_CID CID_QueryControlPSFactory   = UINT64_C(0x00000002683E889A);
const MUX_IID IID_ILog                   = UINT64_C(0x000000028B9DC13B);
const MUX_CID CID_QueryServer            = UINT64_C(0x000000028FEA49AD);
const MUX_CID CID_ServerEventsSource     = UINT64_C(0x00000002A5080812);
const MUX_IID IID_IQuerySink             = UINT64_C(0x00000002CBBCE24E);
const MUX_IID IID_IFunction              = UINT64_C(0x00000002D34910C1);
const MUX_IID IID_IQueryControl          = UINT64_C(0x00000002ECD689FC);
const MUX_IID IID_IServerEventsSink      = UINT64_C(0x00000002F0F2753F);
const MUX_IID CID_QueryClient            = UINT64_C(0x00000002F571AB88);
const MUX_CID CID_LogPSFactory           = UINT64_C(0x00000002BC269C88);
const MUX_CID CID_SlaveControlPSFactory  = UINT64_C(0x00000002FD75363F);
const MUX_CID CID_Functions              = UINT64_C(0x00000002FE32BEA1);
const MUX_CID CID_Notify                 = UINT64_C(0x00000002B880897B);
const MUX_IID IID_INotify                = UINT64_C(0x00000002621F4385);
const MUX_CID CID_ObjectInfo             = UINT64_C(0x00000002251565F1);
const MUX_IID IID_IObjectInfo            = UINT64_C(0x00000002722A6C49);
const MUX_CID CID_AttributeAccess       = UINT64_C(0x000000024A3E71B5);
const MUX_IID IID_IAttributeAccess      = UINT64_C(0x00000002D89F42C3);
const MUX_CID CID_Evaluator             = UINT64_C(0x00000002E7B3A51D);
const MUX_IID IID_IEvaluator            = UINT64_C(0x000000023C6D8F72);
const MUX_CID CID_Permissions           = UINT64_C(0x00000002A4C7E831);
const MUX_IID IID_IPermissions          = UINT64_C(0x0000000257B1D946);

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
    virtual MUX_RESULT Result(uint32_t iQueryHandle, uint32_t iError, QUEUE_INFO *pqiResultsSet) = 0;
};

interface mux_IQueryControl : public mux_IUnknown
{
public:
    virtual MUX_RESULT Connect(const UTF8 *pServer, const UTF8 *pDatabase, const UTF8 *pUser, const UTF8 *pPassword) = 0;
    virtual MUX_RESULT Advise(mux_IQuerySink *pIQuerySink) = 0;
    virtual MUX_RESULT Query(uint32_t iQueryHandle, const UTF8 *pDatabaseName, const UTF8 *pQuery) = 0;
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
    virtual MUX_RESULT Call(unsigned int nKey, UTF8 *buff, UTF8 **bufc, dbref executor, dbref caller, dbref enactor,
        int eval, UTF8 *fargs[], int nfargs, const UTF8 *cargs[], int ncargs) = 0;
};

interface mux_IFunctionsControl : public mux_IUnknown
{
public:
    virtual MUX_RESULT Add(unsigned int nKey, const UTF8 *name, mux_IFunction *pIFun, int maxArgsParsed, int minArgs, int maxArgs, int flags, int perms) = 0;
};

// Player/object notification.
//
interface mux_INotify : public mux_IUnknown
{
public:
    virtual MUX_RESULT Notify(dbref target, const UTF8 *msg) = 0;
    virtual MUX_RESULT RawNotify(dbref target, const UTF8 *msg) = 0;
    virtual MUX_RESULT NotifyCheck(dbref target, dbref sender,
        const UTF8 *msg, int key) = 0;
};

// Object property queries.
//
interface mux_IObjectInfo : public mux_IUnknown
{
public:
    virtual MUX_RESULT IsValid(dbref obj, bool *pValid) = 0;
    virtual MUX_RESULT GetName(dbref obj, const UTF8 **ppName) = 0;
    virtual MUX_RESULT GetOwner(dbref obj, dbref *pOwner) = 0;
    virtual MUX_RESULT GetLocation(dbref obj, dbref *pLocation) = 0;
    virtual MUX_RESULT GetType(dbref obj, int *pType) = 0;
    virtual MUX_RESULT IsConnected(dbref obj, bool *pConnected) = 0;
    virtual MUX_RESULT IsPlayer(dbref obj, bool *pPlayer) = 0;
    virtual MUX_RESULT IsGoing(dbref obj, bool *pGoing) = 0;
    virtual MUX_RESULT GetMoniker(dbref obj, const UTF8 **ppMoniker) = 0;
    virtual MUX_RESULT MatchThing(dbref executor, const UTF8 *pName,
        dbref *pResult) = 0;
};

// Softcode evaluator.
//
interface mux_IEvaluator : public mux_IUnknown
{
public:
    virtual MUX_RESULT Eval(dbref executor, dbref caller, dbref enactor,
        const UTF8 *pExpr, UTF8 *pResult, size_t nResultMax,
        size_t *pnResultLen) = 0;
};

// Permission queries.
//
interface mux_IPermissions : public mux_IUnknown
{
public:
    virtual MUX_RESULT IsWizard(dbref obj, bool *pWizard) = 0;
    virtual MUX_RESULT IsGod(dbref obj, bool *pGod) = 0;
    virtual MUX_RESULT HasControl(dbref who, dbref what, bool *pControls) = 0;
    virtual MUX_RESULT HasCommAll(dbref obj, bool *pCommAll) = 0;
};

// Comsys module — channel system provided by loadable module.
//
// mux_IComsysControl is obtained by the server at startup.
// It calls into the comsys module for command dispatch and events.
//
const MUX_CID CID_Comsys                = UINT64_C(0x00000002C5A2F193);
const MUX_IID IID_IComsysControl        = UINT64_C(0x000000028E4B63D7);

interface mux_IComsysControl : public mux_IUnknown
{
public:
    // One-time initialization: pass the SQLite database path so the
    // module can open its own connection.
    //
    virtual MUX_RESULT Initialize(const UTF8 *pDatabasePath) = 0;

    // Connection events.
    //
    virtual MUX_RESULT PlayerConnect(dbref player) = 0;
    virtual MUX_RESULT PlayerDisconnect(dbref player) = 0;
    virtual MUX_RESULT PlayerNuke(dbref player) = 0;

    // Alias management.
    //
    virtual MUX_RESULT AddAlias(dbref executor, const UTF8 *pAlias,
        const UTF8 *pChannel) = 0;
    virtual MUX_RESULT DelAlias(dbref executor, const UTF8 *pAlias) = 0;
    virtual MUX_RESULT ClearAliases(dbref executor) = 0;

    // Channel creation/destruction.
    //
    virtual MUX_RESULT CreateChannel(dbref executor, const UTF8 *pName) = 0;
    virtual MUX_RESULT DestroyChannel(dbref executor, const UTF8 *pName) = 0;

    // Channel operations.
    //
    virtual MUX_RESULT AllCom(dbref executor, const UTF8 *pAction) = 0;
    virtual MUX_RESULT ComList(dbref executor, const UTF8 *pPattern) = 0;
    virtual MUX_RESULT ComTitle(dbref executor, const UTF8 *pAlias,
        const UTF8 *pTitle, int key) = 0;
    virtual MUX_RESULT ChanList(dbref executor, const UTF8 *pPattern,
        int key) = 0;
    virtual MUX_RESULT ChanWho(dbref executor, const UTF8 *pArg) = 0;
    virtual MUX_RESULT CEmit(dbref executor, const UTF8 *pChannel,
        const UTF8 *pText, int key) = 0;

    // Channel administration.
    //
    virtual MUX_RESULT CSet(dbref executor, const UTF8 *pChannel,
        const UTF8 *pValue, int key) = 0;
    virtual MUX_RESULT EditChannel(dbref executor, const UTF8 *pChannel,
        const UTF8 *pValue, int flag) = 0;
    virtual MUX_RESULT CBoot(dbref executor, const UTF8 *pChannel,
        const UTF8 *pVictim, int key) = 0;

    // Alias-based command dispatch (returns true to stop further matching).
    //
    virtual MUX_RESULT ProcessCommand(dbref executor, const UTF8 *pCmd,
        bool *pbHandled) = 0;
};

// Attribute read/write with built-in permission checks.
//
interface mux_IAttributeAccess : public mux_IUnknown
{
public:
    virtual MUX_RESULT GetAttribute(dbref executor, dbref obj,
        const UTF8 *pAttrName, UTF8 *pValue, size_t nValueMax,
        size_t *pnValueLen) = 0;
    virtual MUX_RESULT SetAttribute(dbref executor, dbref obj,
        const UTF8 *pAttrName, const UTF8 *pValue) = 0;
};

// Mail module — @mail system provided by loadable module.
//
const MUX_CID CID_Mail                  = UINT64_C(0x00000002D7A3E1B5);
const MUX_IID IID_IMailControl          = UINT64_C(0x00000002F9C84D62);

interface mux_IMailControl : public mux_IUnknown
{
public:
    // One-time initialization: pass the SQLite database path and config.
    //
    virtual MUX_RESULT Initialize(const UTF8 *pDatabasePath,
        int mail_expiration, int mail_per_player) = 0;

    // Connection events.
    //
    virtual MUX_RESULT PlayerConnect(dbref player) = 0;
    virtual MUX_RESULT PlayerNuke(dbref player) = 0;

    // Main command dispatcher.
    //
    virtual MUX_RESULT MailCommand(dbref executor, int key,
        const UTF8 *pArg1, const UTF8 *pArg2) = 0;
    virtual MUX_RESULT MaliasCommand(dbref executor, int key,
        const UTF8 *pArg1, const UTF8 *pArg2) = 0;
    virtual MUX_RESULT FolderCommand(dbref executor, int key, int nargs,
        const UTF8 *pArg1, const UTF8 *pArg2) = 0;

    // Queries used by server code outside @mail.
    //
    virtual MUX_RESULT CheckMail(dbref player, int folder, bool silent) = 0;
    virtual MUX_RESULT ExpireMail(void) = 0;
    virtual MUX_RESULT CountMail(dbref player, int folder,
        int *pRead, int *pUnread, int *pCleared) = 0;
    virtual MUX_RESULT DestroyPlayerMail(dbref player) = 0;
};

#endif // MODULES_H
