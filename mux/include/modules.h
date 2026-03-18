/*! \file modules.h
 * \brief netmux-provided modules.
 *
 * Interfaces and classes declared here are built into the netmux server and
 * are available to netmux itself and to dynamically-loaded external modules.
 *
 */

#ifndef MODULES_H
#define MODULES_H

// Forward declarations for types used by COM interfaces below.
// These are defined in core.h, alloc.h, mudconf.h, and externs.h;
// modules.h must be self-contained for external module compilation.
//
class CLinearTimeAbsolute;
class CLinearTimeDelta;
struct descriptor_data;
typedef struct descriptor_data DESC;
enum class SocketState;
struct program_data;
struct reg_ref;
struct NamedRegsMap;
struct name_table;
typedef struct name_table NAMETAB;

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
const MUX_CID CID_MailDelivery          = UINT64_C(0x00000002B3F5D721);
const MUX_CID CID_HelpSystem           = UINT64_C(0x00000002C4D6E832);
const MUX_IID IID_IHelpSystem          = UINT64_C(0x0000000238A9F157);

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

    // Direct CLogFile operations for driver startup/shutdown/signals.
    //
    virtual MUX_RESULT WriteString(const UTF8 *text) = 0;
    virtual MUX_RESULT SetBasename(const UTF8 *pBasename) = 0;
    virtual MUX_RESULT StartLogging(void) = 0;
    virtual MUX_RESULT Flush(void) = 0;
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

    // Flag/power accessors — word is FLAG_WORD1/2/3.
    //
    virtual MUX_RESULT GetFlags(dbref obj, int word, unsigned int *pFlags) = 0;
    virtual MUX_RESULT SetFlags(dbref obj, int word, unsigned int flags) = 0;
    virtual MUX_RESULT GetPowers(dbref obj, unsigned int *pPowers) = 0;

    // Pennies accessor.
    //
    virtual MUX_RESULT GetPennies(dbref obj, int *pPennies) = 0;

    // PureName — returns the unadorned name (no ANSI).
    //
    virtual MUX_RESULT GetPureName(dbref obj, const UTF8 **ppName) = 0;

    // DecodeFlags — printable flag string (caller must free_sbuf).
    //
    virtual MUX_RESULT DecodeFlags(dbref player, dbref obj, UTF8 **ppStr) = 0;

    // Compound permission queries — these wrap the complex macro chains
    // (Owner/Inherits/Flags) so the driver doesn't need db[] access.
    //
    virtual MUX_RESULT IsWizard(dbref obj, bool *pResult) = 0;
    virtual MUX_RESULT IsWizRoy(dbref obj, bool *pResult) = 0;
    virtual MUX_RESULT CanIdle(dbref obj, bool *pResult) = 0;
    virtual MUX_RESULT WizardWho(dbref obj, bool *pResult) = 0;
    virtual MUX_RESULT SeeHidden(dbref obj, bool *pResult) = 0;

    // Raw attribute access (by attrnum) for driver-side connection
    // lifecycle operations (A_REASON, A_LAST, A_PROGCMD, etc.).
    //
    virtual MUX_RESULT AtrAddRaw(dbref obj, int attrnum,
        const UTF8 *value) = 0;
    virtual MUX_RESULT AtrClr(dbref obj, int attrnum) = 0;
    virtual MUX_RESULT AtrGet(dbref obj, int attrnum, UTF8 *pValue,
        size_t nValueMax, dbref *pOwner, int *pFlags) = 0;
    virtual MUX_RESULT AtrPGet(dbref obj, int attrnum, UTF8 *pValue,
        size_t nValueMax, dbref *pOwner, int *pFlags) = 0;

    // Player lookup by name.
    //
    virtual MUX_RESULT LookupPlayer(dbref executor, const UTF8 *pName,
        bool bConnected, dbref *pResult) = 0;

    // ConnectionInfo fields (A_CONNINFO attribute).
    //
    virtual MUX_RESULT FetchConnectionInfoFields(dbref player,
        long anFields[4]) = 0;
    virtual MUX_RESULT PutConnectionInfoFields(dbref player,
        long anFields[4], CLinearTimeAbsolute &ltaNow) = 0;
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

// Storage interfaces — engine-provided access to the shared SQLite
// database.  Modules call these instead of linking sqlite3 directly.
//
const MUX_CID CID_ComsysStorage         = UINT64_C(0x00000002A3B4C5D6);
const MUX_IID IID_IComsysStorage        = UINT64_C(0x00000002B4C5D6E7);
const MUX_CID CID_MailStorage           = UINT64_C(0x00000002C5D6E7F8);
const MUX_IID IID_IMailStorage          = UINT64_C(0x00000002D6E7F809);

// Callback typedefs for bulk-load operations (C function pointers for
// ABI safety across .so boundaries).
//
typedef void (*PFN_CHANNEL_CB)(void *context,
    const UTF8 *name, const UTF8 *header,
    int type, int temp1, int temp2, int charge, int charge_who,
    int amount_col, int num_messages, int chan_obj);

typedef void (*PFN_CHANNEL_USER_CB)(void *context,
    const UTF8 *channel_name, int who,
    bool is_on, bool comtitle_status, bool gag_join_leave,
    const UTF8 *title);

typedef void (*PFN_PLAYER_CHANNEL_CB)(void *context,
    int who, const UTF8 *alias, const UTF8 *channel_name);

typedef void (*PFN_MAIL_HEADER_CB)(void *context,
    int64_t rowid, int to_player, int from_player, int body_number,
    const UTF8 *tolist, const UTF8 *time_str, const UTF8 *subject,
    int read_flags);

typedef void (*PFN_MAIL_BODY_CB)(void *context,
    int number, const UTF8 *message);

typedef void (*PFN_MAIL_ALIAS_CB)(void *context,
    int owner, const UTF8 *name, const UTF8 *desc,
    int desc_width, const UTF8 *members);

interface mux_IComsysStorage : public mux_IUnknown
{
public:
    // Bulk load (called once at module initialization).
    //
    virtual MUX_RESULT LoadAllChannels(PFN_CHANNEL_CB pfn, void *context) = 0;
    virtual MUX_RESULT LoadAllChannelUsers(PFN_CHANNEL_USER_CB pfn, void *context) = 0;
    virtual MUX_RESULT LoadAllPlayerChannels(PFN_PLAYER_CHANNEL_CB pfn, void *context) = 0;

    // Write-through.
    //
    virtual MUX_RESULT SyncChannel(const UTF8 *name, const UTF8 *header,
        int type, int temp1, int temp2, int charge, int charge_who,
        int amount_col, int num_messages, int chan_obj) = 0;
    virtual MUX_RESULT SyncChannelUser(const UTF8 *channel_name, int who,
        bool is_on, bool comtitle_status, bool gag_join_leave,
        const UTF8 *title) = 0;
    virtual MUX_RESULT SyncPlayerChannel(int who, const UTF8 *alias,
        const UTF8 *channel_name) = 0;

    // Delete.
    //
    virtual MUX_RESULT DeleteChannel(const UTF8 *name) = 0;
    virtual MUX_RESULT DeleteChannelUser(const UTF8 *channel_name, int who) = 0;
    virtual MUX_RESULT DeletePlayerChannel(int who, const UTF8 *alias) = 0;
    virtual MUX_RESULT DeleteAllPlayerChannels(int who) = 0;
    virtual MUX_RESULT ClearComsysTables(void) = 0;
};

interface mux_IMailStorage : public mux_IUnknown
{
public:
    // Bulk load.
    //
    virtual MUX_RESULT LoadAllMailHeaders(PFN_MAIL_HEADER_CB pfn, void *context) = 0;
    virtual MUX_RESULT LoadAllMailBodies(PFN_MAIL_BODY_CB pfn, void *context) = 0;
    virtual MUX_RESULT LoadAllMailAliases(PFN_MAIL_ALIAS_CB pfn, void *context) = 0;
    virtual MUX_RESULT GetMeta(const UTF8 *key, int *pValue) = 0;

    // Write-through: headers.
    //
    virtual MUX_RESULT InsertMailHeader(int to_player, int from_player,
        int body_number, const UTF8 *tolist, const UTF8 *time_str,
        const UTF8 *subject, int read_flags, int64_t *pRowid) = 0;
    virtual MUX_RESULT UpdateMailReadFlags(int64_t rowid, int read_flags) = 0;
    virtual MUX_RESULT DeleteMailHeader(int64_t rowid) = 0;
    virtual MUX_RESULT DeleteAllMailHeaders(int to_player) = 0;

    // Write-through: bodies.
    //
    virtual MUX_RESULT SyncMailBody(int number, const UTF8 *message) = 0;
    virtual MUX_RESULT DeleteMailBody(int number) = 0;

    // Write-through: aliases.
    //
    virtual MUX_RESULT SyncMailAlias(int owner, const UTF8 *name,
        const UTF8 *desc, int desc_width, const UTF8 *members) = 0;
    virtual MUX_RESULT ClearMailAliases(void) = 0;
    virtual MUX_RESULT ClearMailTables(void) = 0;
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
    // One-time initialization: the engine passes a storage interface
    // so the module accesses SQLite through the engine's connection.
    //
    virtual MUX_RESULT Initialize(mux_IComsysStorage *pStorage) = 0;

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
    // One-time initialization: the engine passes a storage interface
    // so the module accesses SQLite through the engine's connection.
    //
    virtual MUX_RESULT Initialize(mux_IMailStorage *pStorage,
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

// Mail delivery — server-provided interface for mail permission checks,
// notifications, and composition state.  The module owns all mail data;
// the server handles lock evaluation, attribute triggers, flag management,
// and throttling.
//
const MUX_IID IID_IMailDelivery        = UINT64_C(0x00000002A1B7C3D4);

interface mux_IMailDelivery : public mux_IUnknown
{
public:
    // Check if player can mail target (evaluates A_LMAIL lock).
    // On failure, sends the MFAIL message to player.
    //
    virtual MUX_RESULT MailCheck(dbref player, dbref target,
        bool *pResult) = 0;

    // Post-delivery notification and attribute triggers.
    // Sends "You have new mail" to target, "You sent mail" to sender,
    // and triggers A_MAIL/A_AMAIL on target.
    //
    virtual MUX_RESULT NotifyDelivery(dbref sender, dbref target,
        const UTF8 *subject, bool silent) = 0;

    // Composition state (PLAYER_MAILS flag in Flags2).
    //
    virtual MUX_RESULT IsComposing(dbref player, bool *pResult) = 0;
    virtual MUX_RESULT SetComposing(dbref player, bool bComposing) = 0;

    // Throttle check (has player sent too much mail recently?).
    //
    virtual MUX_RESULT ThrottleCheck(dbref player, bool *pResult) = 0;
};

// Help system — server-provided interface exposing the in-game help
// file lookup to modules.  The help system (files, indexes, command
// registration) stays in netmux; modules call through this interface
// to look up topics.
//
interface mux_IHelpSystem : public mux_IUnknown
{
public:
    // Look up a help topic and return the rendered text.
    // iHelpfile identifies which help file to search (-1 for default).
    // Evaluates softcode in help text if the file was registered with
    // bEval=true.
    //
    virtual MUX_RESULT LookupTopic(dbref executor, int iHelpfile,
        const UTF8 *pTopic, UTF8 *pResult, size_t nResultMax,
        size_t *pnResultLen) = 0;

    // Find the help file index for a given command name (e.g., "help",
    // "news", "+help").  Returns MUX_S_OK and the index, or
    // MUX_E_NOTFOUND if no such help file is registered.
    //
    virtual MUX_RESULT FindHelpFile(const UTF8 *pCommandName,
        int *pIndex) = 0;

    // Return the number of registered help files.
    //
    virtual MUX_RESULT GetHelpFileCount(int *pCount) = 0;

    // Reload all help file indexes.
    //
    virtual MUX_RESULT ReloadIndexes(dbref player) = 0;
};

// Static configuration basket: one-time snapshot of mudconf values that
// the driver queries after LoadGame.  All fixed-size arrays; no pointers
// cross the .so boundary.  Time deltas are raw 100-nanosecond ticks
// (CLinearTimeDelta underlying representation).
//
#define DRIVER_CONFIG_MAX_PORTS 20

struct DRIVER_CONFIG
{
    // Ports and networking.
    //
    int     ports[DRIVER_CONFIG_MAX_PORTS];
    int     nPorts;
    int     sslPorts[DRIVER_CONFIG_MAX_PORTS];
    int     nSslPorts;
    UTF8    ip_address[128];
    bool    use_hostname;
    int     retry_limit;
    int     idle_timeout;
    int     conn_timeout;
    int     cmd_quota_max;
    int     output_limit;
    int     default_charset;
    int     max_players;
    int     control_flags;

    // Guest configuration.
    //
    UTF8    guest_prefix[32];
    int     number_guests;
    dbref   guest_char;

    // SSL/TLS.
    //
    UTF8    ssl_certificate_file[128];
    UTF8    ssl_certificate_key[128];
    UTF8    ssl_certificate_password[128];

    // Game identity.
    //
    UTF8    mud_name[32];

    // File paths (copied from pointer fields).
    //
    UTF8    pid_file[128];
    UTF8    log_dir[128];
    UTF8    config_file[128];

    // Messages.
    //
    UTF8    crash_msg[1024];
    UTF8    downmotd_msg[1024];
    UTF8    fullmotd_msg[1024];
    UTF8    pueblo_msg[1024];

    // Timing (raw 100ns ticks).
    //
    int64_t max_cmdsecs;
    int64_t rpt_cmdsecs;
    int64_t timeslice;
    int64_t start_time_utc;     // Server start time (UTC seconds since epoch)

    // Behavior flags and limits.
    //
    int     sig_action;
    bool    fork_dump;
    bool    name_spaces;
    bool    idle_wiz_dark;
    bool    reset_players;
    unsigned int site_chars;
    dbref   start_room;

    // SQL.
    //
    UTF8    sql_server[128];
    UTF8    sql_user[128];
    UTF8    sql_password[128];
    UTF8    sql_database[128];

    // Mail relay.
    //
    UTF8    mail_server[128];
    UTF8    mail_sendaddr[128];
    UTF8    mail_sendname[128];
    UTF8    mail_ehlo[128];
};

// SOLE EXCEPTION to the "engine.so exports only 4 COM front-door
// functions" rule.  debug_cmd is a crash diagnostic read by the signal
// handler during SIGSEGV/SIGBUS — a context where COM vtable calls are
// unsafe.  Both engine and driver write this pointer; the signal handler
// reads it.  When the engine moves out-of-process (stubslave), this
// export goes away and debug_cmd becomes process-local.
//
extern LIBMUX_API const UTF8 *g_debug_cmd;

// Game engine — the interface the driver uses to call into the engine.
// In the current in-process build, CGameEngine wraps direct function calls.
// When the engine is split into engine.so, the driver creates this via
// mux_CreateInstance(CID_GameEngine) to get the engine's front door.
//
const MUX_CID CID_GameEngine           = UINT64_C(0x00000002D4E5F6A7);
const MUX_IID IID_IGameEngine          = UINT64_C(0x0000000247B8C9D1);

interface mux_IGameEngine : public mux_IUnknown
{
public:
    // Load configuration and game database.
    // Returns MUX_S_OK on success.
    //
    virtual MUX_RESULT LoadGame(const UTF8 *configFile, const UTF8 *inputDb,
        bool bMinDB) = 0;

    // Post-load initialization: local_startup, module startup, init_timer.
    //
    virtual MUX_RESULT Startup(void) = 0;

    // Run scheduled tasks up to the given time.
    //
    virtual MUX_RESULT RunTasks(CLinearTimeAbsolute &ltaNow) = 0;

    // Update command quotas between time intervals.
    //
    virtual MUX_RESULT UpdateQuotas(CLinearTimeAbsolute &ltaLast,
        const CLinearTimeAbsolute &ltaCurrent) = 0;

    // Query when the next scheduled task is due.
    //
    virtual MUX_RESULT WhenNext(CLinearTimeAbsolute *pltaWhen) = 0;

    // Save the game database (WAL checkpoint in SQLite mode).
    //
    virtual MUX_RESULT DumpDatabase(void) = 0;

    // Clean shutdown: local_shutdown, module shutdown, cleanup.
    //
    virtual MUX_RESULT Shutdown(void) = 0;

    // Standalone database conversion (dbconvert mode).
    //
    virtual MUX_RESULT DbConvert(const UTF8 *infile, const UTF8 *outfile,
        const UTF8 *basename, bool bCheck, bool bLoad, bool bUnload,
        const UTF8 *comsys_file, const UTF8 *mail_file) = 0;

    // Query the static configuration basket.  The driver calls this once
    // after LoadGame to get a snapshot of mudconf values it needs.
    //
    virtual MUX_RESULT GetConfig(DRIVER_CONFIG *pConfig) = 0;

    // Notify the engine that a forked dump child process has exited.
    // Called from the driver's SIGCHLD handler.
    //
    virtual MUX_RESULT DumpChildExited(int child_pid) = 0;

    // --- Driver ↔ Engine state ---
    // Timing/restart state owned by the engine, set by the driver.
    //
    virtual MUX_RESULT SetStartTime(const CLinearTimeAbsolute &time) = 0;
    virtual MUX_RESULT GetStartTime(CLinearTimeAbsolute *pTime) = 0;
    virtual MUX_RESULT SetRestartTime(const CLinearTimeAbsolute &time) = 0;
    virtual MUX_RESULT SetRestartCount(unsigned int count) = 0;
    virtual MUX_RESULT GetRestartCount(unsigned int *pCount) = 0;
    virtual MUX_RESULT SetCpuCountFrom(const CLinearTimeAbsolute &time) = 0;
    virtual MUX_RESULT SetRecordPlayers(int count) = 0;

    // Engine state getters/setters for driver.
    //
    virtual MUX_RESULT GetDoingHdr(UTF8 *buf, size_t bufSize) = 0;
    virtual MUX_RESULT SetDoingHdr(const UTF8 *hdr, size_t len) = 0;
    virtual MUX_RESULT GetRecordPlayers(int *pCount) = 0;
    virtual MUX_RESULT GetBCanRestart(bool *pbCanRestart) = 0;

    // Scheduler — driver delegates task management to engine's CScheduler.
    // Uses raw function pointer type since FTASK isn't declared yet.
    //
    virtual MUX_RESULT CancelTask(void (*fpTask)(void *, int),
        void *arg_voidptr, int arg_Integer) = 0;
    virtual MUX_RESULT DeferImmediateTask(int iPriority,
        void (*fpTask)(void *, int), void *arg_voidptr,
        int arg_Integer) = 0;
    virtual MUX_RESULT DeferTask(const CLinearTimeAbsolute &ltWhen,
        int iPriority, void (*fpTask)(void *, int),
        void *arg_voidptr, int arg_Integer) = 0;

    // Command execution — the driver's do_command block delegates these.
    //
    virtual MUX_RESULT PrepareForCommand(dbref player) = 0;
    virtual MUX_RESULT ProcessCommand(dbref executor, dbref caller,
        dbref enactor, int eval, bool bHasCmdArg, UTF8 *command,
        const UTF8 *cargs[], int ncargs, UTF8 **ppLogBuf) = 0;
    virtual MUX_RESULT FinishCommand(void) = 0;
    virtual MUX_RESULT HaltQueue(dbref executor, dbref target) = 0;
    virtual MUX_RESULT WaitQueue(dbref executor, dbref caller,
        dbref enactor, int eval, bool bTimed,
        const CLinearTimeAbsolute &ltaWhen, dbref sem, int attr,
        UTF8 *command, int ncargs, const UTF8 *cargs[],
        reg_ref *regs[], NamedRegsMap *named) = 0;

    // Object movement — for newly created players.
    //
    virtual MUX_RESULT MoveObject(dbref thing, dbref dest) = 0;

    // Queries for WHO/INFO display.
    //
    virtual MUX_RESULT WhereRoom(dbref what, dbref *pRoom) = 0;
    virtual MUX_RESULT TimeFormat1(int seconds, size_t maxWidth,
        const UTF8 **ppResult) = 0;
    virtual MUX_RESULT TimeFormat2(int seconds,
        const UTF8 **ppResult) = 0;
    virtual MUX_RESULT GetDbTop(int *pDbTop) = 0;
    virtual MUX_RESULT GetInfoTable(const UTF8 ***pppTable) = 0;

    // Emergency/signal operations.
    //
    virtual MUX_RESULT Report(void) = 0;
    virtual MUX_RESULT PresyncDatabaseSigsegv(void) = 0;
    virtual MUX_RESULT DoRestart(dbref executor, dbref caller,
        dbref enactor, int eval, int key) = 0;
    virtual MUX_RESULT CacheClose(void) = 0;
};

// Player session — the interface the driver uses for player authentication,
// creation, and connect/disconnect lifecycle.  The engine implements this;
// the driver acquires it via mux_CreateInstance(CID_PlayerSession).
//
// All methods operate on players (engine domain).  The driver handles
// networking (DESC binding, socket I/O) and calls these when the network
// state changes.
//
const MUX_CID CID_PlayerSession        = UINT64_C(0x00000002F1A2B3C4);
const MUX_IID IID_IPlayerSession       = UINT64_C(0x00000002F2B3C4D5);

interface mux_IPlayerSession : public mux_IUnknown
{
public:
    // Authenticate a player by name and password.
    // Returns the player dbref in *pPlayer, or NOTHING on failure.
    //
    virtual MUX_RESULT ConnectPlayer(const UTF8 *name, const UTF8 *password,
        const UTF8 *host, const UTF8 *username, const UTF8 *ipaddr,
        dbref *pPlayer) = 0;

    // Create a new player.
    // Returns the player dbref in *pPlayer, or NOTHING on failure.
    // On failure, *ppMsg points to a static error message string.
    //
    virtual MUX_RESULT CreatePlayer(const UTF8 *name, const UTF8 *password,
        dbref creator, bool isRobot, dbref *pPlayer,
        const UTF8 **ppMsg) = 0;

    // Add a newly-created player to the public channel and configured
    // player channels.
    //
    virtual MUX_RESULT AddToPublicChannel(dbref player) = 0;
    virtual MUX_RESULT AddToPlayerChannels(dbref player) = 0;

    // Engine-side connect announcement: set flags, MOTD, ACONNECT triggers,
    // record_login, check_mail, look_in, do_comconnect.
    // The driver has already bound the DESC and passes derived state:
    //   numConnections: total connections for this player after binding.
    //   isPueblo:       true if the DESC negotiated Pueblo.
    //   isSuspect:      true if the connection address is suspect.
    //   pTimeout:       out — engine reads A_TIMEOUT; driver sets on DESC.
    //
    virtual MUX_RESULT AnnounceConnect(dbref player, int numConnections,
        bool isPueblo, bool isSuspect, const UTF8 *host,
        const UTF8 *username, const UTF8 *ipaddr, int *pTimeout,
        int64_t *pConnlogId) = 0;

    // Engine-side disconnect announcement: room/monitor messages,
    // ADISCONNECT triggers, do_comdisconnect, mail purge, flag cleanup.
    //   numConnections: total connections BEFORE this one drops.
    //   wasAutoDark:    true if the DESC had DS_AUTODARK set.
    //   connlogId:      connlog row id from AnnounceConnect (0 if none).
    //
    virtual MUX_RESULT AnnounceDisconnect(dbref player, int numConnections,
        bool isSuspect, bool wasAutoDark, const UTF8 *reason,
        int64_t connlogId) = 0;

    // Send a cached text file (welcome, MOTD, etc.) to a descriptor.
    // num is an FC_* constant.  The engine reads its fcache and queues
    // the output through mux_IConnectionManager::DescQueueWrite.
    //
    virtual MUX_RESULT FcacheSend(DESC *d, int num) = 0;

    // Send a cached text file using raw socket writes (emergency/pre-login).
    //
    virtual MUX_RESULT FcacheRawSend(SOCKET fd, int num) = 0;

    // Guest management — engine owns the CGuests object.
    //
    virtual MUX_RESULT CreateGuest(DESC *d, const UTF8 **ppName) = 0;
    virtual MUX_RESULT CheckGuest(dbref player, bool *pResult) = 0;
};

// Driver control — the interface the engine uses for non-connection
// driver operations (shutdown requests, etc.).  The driver implements this;
// the engine acquires it via mux_CreateInstance(CID_DriverControl).
//
const MUX_CID CID_DriverControl        = UINT64_C(0x00000002E4A5B6C7);
const MUX_IID IID_IDriverControl       = UINT64_C(0x00000002F2E3D4C5);

interface mux_IDriverControl : public mux_IUnknown
{
public:
    // Request a graceful shutdown.  The driver sets its shutdown flag
    // and the networking loop exits on its next iteration.
    //
    virtual MUX_RESULT ShutdownRequest(void) = 0;

    // Query whether the driver is in a @restart sequence.
    //
    virtual MUX_RESULT GetRestarting(bool *pbRestarting) = 0;

    // Update the site access list.  The engine calls this when a wizard
    // uses @register_site, @forbid_site, etc.  The driver parses the
    // subnet string and applies it to its local access list.
    //
    virtual MUX_RESULT SiteUpdate(const UTF8 *subnetStr,
        dbref player, UTF8 *cmd, int operation) = 0;

    // Process ID.
    //
    virtual MUX_RESULT GetPid(int *pPid) = 0;

    // Config nametables owned by the driver.
    //
    virtual MUX_RESULT GetCharsetNametab(NAMETAB **ppTable) = 0;
    virtual MUX_RESULT GetSigactionsNametab(NAMETAB **ppTable) = 0;
    virtual MUX_RESULT GetLogoutCmdtable(NAMETAB **ppTable) = 0;

    // Login-screen command handlers (logged_out0 / logged_out1).
    //
    virtual MUX_RESULT LoggedOut0(dbref executor, dbref caller,
        dbref enactor, int eval, int key) = 0;
    virtual MUX_RESULT LoggedOut1(dbref executor, dbref caller,
        dbref enactor, int eval, int key, UTF8 *arg,
        const UTF8 *cargs[], int ncargs) = 0;

    // Driver-side command handlers.
    //
    virtual MUX_RESULT DoVersion(dbref executor, dbref caller,
        dbref enactor, int eval, int key) = 0;
    virtual MUX_RESULT DoStartSlave(dbref executor, dbref caller,
        dbref enactor, int eval, int key) = 0;

    // Task_ProcessCommand function pointer (for scheduler callbacks).
    //
    virtual MUX_RESULT GetTaskProcessCommand(
        void (**ppfTask)(void *, int)) = 0;

    // Restart / dump helpers.
    //
    virtual MUX_RESULT DumpRestartDb(void) = 0;
    virtual MUX_RESULT PrepareNetworkForRestart(void) = 0;

    // Email send (GANL adapter).
    //
    virtual MUX_RESULT StartEmailSend(dbref executor, const UTF8 *recipient,
        const UTF8 *subject, const UTF8 *body, bool *pResult) = 0;
};

// Connection manager — the interface the engine uses to interact with
// connections owned by the driver.  The driver implements this; the engine
// acquires it via mux_CreateInstance(CID_ConnectionManager) during startup.
//
// This replaces all direct calls from engine files to net.cpp accessor
// functions (find_desc_by_socket, send_text_to_player, etc.).
//
const MUX_CID CID_ConnectionManager    = UINT64_C(0x00000002E3F4A5B6);
const MUX_IID IID_IConnectionManager   = UINT64_C(0x00000002F1D2C3E4);

interface mux_IConnectionManager : public mux_IUnknown
{
public:
    // --- Output ---

    // Send encoded UTF-8 text to all of a player's connections.
    //
    virtual MUX_RESULT SendText(dbref target, const UTF8 *text) = 0;

    // Send raw bytes to all of a player's connections.
    //
    virtual MUX_RESULT SendRaw(dbref target, const UTF8 *data, size_t len) = 0;

    // Broadcast text to all connected players matching flag mask, then flush.
    //
    virtual MUX_RESULT BroadcastAndFlush(int inflags, const UTF8 *text) = 0;

    // Send @program prompt to all of a player's descriptors.
    //
    virtual MUX_RESULT SendProgPrompt(dbref target) = 0;

    // Send Telnet NOP keepalives to all connections with KeepAlive set.
    //
    virtual MUX_RESULT SendKeepaliveNops(void) = 0;

    // --- Queries by dbref ---

    // Query total number of active connections.
    //
    virtual MUX_RESULT GetTotalConnections(int *pCount) = 0;

    // Query number of descriptors for a given player.
    //
    virtual MUX_RESULT CountPlayerDescs(dbref target, int *pCount) = 0;

    // Query sum of command_count for a given player (-1 if not connected).
    //
    virtual MUX_RESULT SumPlayerCommandCount(dbref target, int *pCount) = 0;

    // Query height of a player's least-idle connection.
    //
    virtual MUX_RESULT FetchHeight(dbref target, int *pHeight) = 0;

    // Query width of a player's least-idle connection.
    //
    virtual MUX_RESULT FetchWidth(dbref target, int *pWidth) = 0;

    // Query smallest idle time for a player (-1 if not connected).
    //
    virtual MUX_RESULT FetchIdle(dbref target, int *pIdle) = 0;

    // Query largest connect time for a player (-1 if not connected).
    //
    virtual MUX_RESULT FetchConnect(dbref target, int *pConnect) = 0;

    // --- Queries by opaque connection handle (DESC*) ---
    // Engine code holds DESC* as an opaque pointer and queries fields here.

    // Find connected descriptor by socket number, or nullptr if not found.
    //
    virtual MUX_RESULT FindDescBySocket(SOCKET s, DESC **ppDesc) = 0;

    // Find first connected descriptor for a player, or nullptr.
    //
    virtual MUX_RESULT FindDescByPlayer(dbref target, DESC **ppDesc) = 0;

    // Query individual fields of an opaque descriptor handle.
    //
    virtual MUX_RESULT DescPlayer(const DESC *d, dbref *pPlayer) = 0;
    virtual MUX_RESULT DescHeight(const DESC *d, int *pHeight) = 0;
    virtual MUX_RESULT DescWidth(const DESC *d, int *pWidth) = 0;
    virtual MUX_RESULT DescEncoding(const DESC *d, int *pEncoding) = 0;
    virtual MUX_RESULT DescCommandCount(const DESC *d, int *pCount) = 0;
    virtual MUX_RESULT DescTtype(const DESC *d, const UTF8 **ppTtype) = 0;
    virtual MUX_RESULT DescLastTime(const DESC *d,
        CLinearTimeAbsolute *pTime) = 0;
    virtual MUX_RESULT DescConnectedAt(const DESC *d,
        CLinearTimeAbsolute *pTime) = 0;
    virtual MUX_RESULT DescNvtHimState(const DESC *d,
        unsigned char chOption, int *pState) = 0;
    virtual MUX_RESULT DescSocketState(const DESC *d,
        SocketState *pState) = 0;

    // Send a GMCP message to all GMCP-enabled descriptors for a player.
    // Builds IAC SB GMCP <payload> IAC SE and sends to each.
    //
    virtual MUX_RESULT SendGmcp(dbref target, const UTF8 *pkg, const UTF8 *json) = 0;

    // --- Iteration ---

    // Call a function for each connected player dbref.
    //
    virtual MUX_RESULT ForEachConnectedPlayer(
        void (*callback)(dbref player, void *context), void *context) = 0;

    // Call a function for each connected descriptor (player + socket).
    //
    virtual MUX_RESULT ForEachConnectedDesc(
        void (*callback)(dbref player, SOCKET sock, void *context),
        void *context) = 0;

    // --- @program state ---

    virtual MUX_RESULT PlayerHasProgram(dbref target, bool *pbHas) = 0;
    virtual MUX_RESULT DetachPlayerProgram(dbref target,
        program_data **ppProgram) = 0;
    virtual MUX_RESULT SetPlayerProgram(dbref target,
        program_data *program) = 0;

    // --- Encoding / Display ---

    virtual MUX_RESULT SetPlayerEncoding(dbref target, int encoding) = 0;
    virtual MUX_RESULT ResetPlayerEncoding(dbref target) = 0;
    virtual MUX_RESULT SetDoingAll(dbref target, const UTF8 *doing,
        size_t len) = 0;
    virtual MUX_RESULT SetDoingLeastIdle(dbref target, const UTF8 *doing,
        size_t len, bool *pbFound) = 0;

    // --- Quota ---

    virtual MUX_RESULT UpdateAllDescQuotas(int nExtra, int nMax) = 0;

    // --- Connection lifecycle ---

    virtual MUX_RESULT BootOff(dbref target, const UTF8 *message,
        int *pCount) = 0;
    virtual MUX_RESULT BootByPort(SOCKET port, bool bGod,
        const UTF8 *message, int *pCount) = 0;

    // --- Idle check ---

    virtual MUX_RESULT CheckIdle(void) = 0;

    // --- Emergency shutdown ---

    // Close all sockets immediately (panic shutdown).
    //
    virtual MUX_RESULT EmergencyShutdown(void) = 0;

    // --- Low-level descriptor I/O ---

    // Queue raw bytes on a specific descriptor (opaque handle).
    // Used by engine code (e.g., fcache_dump) that writes to a specific
    // connection rather than broadcasting to all of a player's sessions.
    //
    virtual MUX_RESULT DescQueueWrite(DESC *d, const UTF8 *data,
        size_t len) = 0;

    // Queue encoded text on a specific descriptor (color/charset aware).
    //
    virtual MUX_RESULT DescQueueString(DESC *d, const UTF8 *text) = 0;

    // Reload descriptors after dbread (reconnect cached player refs).
    //
    virtual MUX_RESULT DescReload(dbref player) = 0;

    // Trimmed name for WHO/mail display.
    //
    virtual MUX_RESULT TrimmedName(dbref player, UTF8 *cbuff,
        size_t cbuffSize, unsigned short nMin, unsigned short nMax,
        unsigned short nPad, unsigned short *pResult) = 0;

    // Port list for ports() function.
    //
    virtual MUX_RESULT MakePortlist(dbref player, dbref target,
        UTF8 *buff, UTF8 **bufc) = 0;

    // Per-descriptor iteration (for file cache sends, etc.).
    //
    virtual MUX_RESULT ForEachPlayerDesc(dbref target,
        void (*callback)(DESC *d, void *context), void *context) = 0;

    // Softcode function implementations that query connection state.
    // These write results into buff/bufc in the standard MUX way.
    //
    virtual MUX_RESULT FunHost(dbref executor, dbref caller, dbref enactor,
        int eval, UTF8 *fargs[], int nfargs, UTF8 *buff,
        UTF8 **bufc) = 0;
    virtual MUX_RESULT FunDoing(dbref executor, dbref caller, dbref enactor,
        int eval, UTF8 *fargs[], int nfargs, UTF8 *buff,
        UTF8 **bufc) = 0;
    virtual MUX_RESULT FunSiteinfo(dbref executor, dbref caller,
        dbref enactor, int eval, UTF8 *fargs[], int nfargs,
        UTF8 *buff, UTF8 **bufc) = 0;
};

// Lua JIT compilation — engine-side bytecode → native compilation.
//
const MUX_CID CID_JITCompile            = UINT64_C(0x00000002A8C3D4E5);
const MUX_IID IID_IJITCompile           = UINT64_C(0x00000002B9D4E5F6);

interface mux_IJITCompile : public mux_IUnknown
{
public:
    // Compile a Lua 5.4 bytecode blob (output of lua_dump).
    // On success, stores compiled program and returns key in *pKey.
    //
    virtual MUX_RESULT CompileLuaBytecode(const uint8_t *pData, size_t nData,
        uint64_t *pKey) = 0;

    // Run a previously compiled program.
    // executor/caller/enactor provide the softcode context.
    // pArgs/nArgs are the Lua mux.args[].
    //
    virtual MUX_RESULT RunCompiled(uint64_t key,
        dbref executor, dbref caller, dbref enactor,
        const UTF8 *pArgs[], int nArgs,
        UTF8 *pResult, size_t nResultMax, size_t *pnResultLen) = 0;

    // Check if a key has a compiled program.
    //
    virtual MUX_RESULT IsCompiled(uint64_t key, bool *pCompiled) = 0;

    // Invalidate a compiled program (e.g., source changed).
    //
    virtual MUX_RESULT Invalidate(uint64_t key) = 0;
};

// Lua scripting module — server-side Lua 5.4 integration.
//
const MUX_CID CID_LuaMod                = UINT64_C(0x00000002E1A3B5C7);
const MUX_IID IID_ILuaControl           = UINT64_C(0x00000002F2B4C6D8);

interface mux_ILuaControl : public mux_IUnknown
{
public:
    // Execute a Lua chunk stored on an object attribute.
    // Resolves obj/attrnum, reads source, compiles (with cache), executes
    // in a sandboxed environment with the given arguments.
    //
    virtual MUX_RESULT CallAttr(dbref executor, dbref caller, dbref enactor,
        dbref obj, const UTF8 *pAttrName,
        const UTF8 *pArgs[], int nArgs,
        UTF8 *pResult, size_t nResultMax, size_t *pnResultLen) = 0;

    // Execute inline Lua source (wizard-only).
    //
    virtual MUX_RESULT Eval(dbref executor, dbref caller, dbref enactor,
        const UTF8 *pSource, size_t nSource,
        UTF8 *pResult, size_t nResultMax, size_t *pnResultLen) = 0;

    // Resource statistics for @list lua.
    //
    virtual MUX_RESULT GetStats(size_t *pnCalls, size_t *pnErrors,
        size_t *pnInsnLimitHits, size_t *pnMemLimitHits,
        size_t *pnBytesUsed,
        size_t *pnCacheHits, size_t *pnCacheMisses,
        size_t *pnCacheEntries) = 0;

    // Configure resource limits (called after module discovery).
    //
    virtual MUX_RESULT SetLimits(int nInsnLimit, int nMemLimit) = 0;
};

#endif // MODULES_H
