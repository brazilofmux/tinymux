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

#ifdef WIN32
const UINT64 CID_Log                   = 0x000000020CE18E7Ai64;
const UINT64 IID_ILog                  = 0x000000028B9DC13Ai64;
const UINT64 CID_ServerEventsSource    = 0x00000002A5080812i64;
const UINT64 IID_IServerEventsSink     = 0x00000002F0F2753Fi64;
const UINT64 IID_IServerEventsControl  = 0x000000026EE5256Ei64;
#else
const UINT64 CID_Log                   = 0x000000020CE18E7Aull;
const UINT64 IID_ILog                  = 0x000000028B9DC13Aull;
const UINT64 CID_ServerEventsSource    = 0x00000002A5080812ull;
const UINT64 IID_IServerEventsSink     = 0x00000002F0F2753Full;
const UINT64 IID_IServerEventsControl  = 0x000000026EE5256Eull;
#endif

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
    virtual MUX_RESULT QueryInterface(UINT64 iid, void **ppv);
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
    virtual MUX_RESULT QueryInterface(UINT64 iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IClassFactory
    //
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, UINT64 iid, void **ppv);
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
    virtual MUX_RESULT QueryInterface(UINT64 iid, void **ppv);
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
    virtual MUX_RESULT QueryInterface(UINT64 iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IClassFactory
    //
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, UINT64 iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);

    CServerEventsSourceFactory(void);
    virtual ~CServerEventsSourceFactory();

private:
    UINT32 m_cRef;
};

extern void init_modules(void);
extern void final_modules(void);

#endif
#endif // MODULES_H
