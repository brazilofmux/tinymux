/*! \file driver.cpp
 * \brief Program entry point, CLI parsing, and driver-side orchestration.
 *
 * This file contains main(), dbconvert(), command-line option parsing,
 * and other driver-level startup/shutdown code.  Game engine logic
 * (notification, matching, dumps, loading) is in engine.cpp.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "interface.h"
#include "sqlite_backend.h"

#include "ganl_adapter.h"
#include "modules.h"
#include "driverstate.h"
#include "driver_log.h"
#include "driver_bridge.h"

#if defined(HAVE_WORKING_FORK)
#include <sys/wait.h>
#endif

mux_ILog *g_pILog = nullptr;
mux_IPlatform *g_pIPlatform = nullptr;
DRIVER_CONFIG g_dc;

#if defined(INLINESQL)
#include <mysql.h>

MYSQL *mush_database = nullptr;
#endif // INLINESQL

static const UTF8 *standalone_infile = nullptr;
static const UTF8 *standalone_outfile = nullptr;
static const UTF8 *standalone_basename = nullptr;
static bool standalone_check = false;
static bool standalone_load = false;
static bool standalone_unload = false;
static const UTF8 *standalone_comsys_file = nullptr;
static const UTF8 *standalone_mail_file = nullptr;
static bool standalone_force = false;

// dbconvert delegates to engine via mux_IGameEngine::DbConvert.
//
static void dbconvert(void)
{
    MUX_RESULT mr = init_modules();
    if (MUX_FAILED(mr))
    {
        mux_fprintf(stderr, T("Failed to initialize modules.\n"));
        exit(1);
    }

    mux_IGameEngine *pEngine = nullptr;
    mr = mux_CreateInstance(CID_GameEngine, nullptr, UseSameProcess,
                            IID_IGameEngine,
                            reinterpret_cast<void **>(&pEngine));
    if (MUX_FAILED(mr) || nullptr == pEngine)
    {
        mux_fprintf(stderr, T("Failed to create game engine.\n"));
        exit(1);
    }

    mr = pEngine->DbConvert(standalone_infile, standalone_outfile,
        standalone_basename, standalone_check, standalone_load,
        standalone_unload, standalone_comsys_file, standalone_mail_file,
        standalone_force);
    pEngine->Release();
    exit(MUX_SUCCEEDED(mr) ? 0 : 1);
}

static void write_pidfile(const UTF8 *pFilename)
{
    FILE *fp;
    if (mux_fopen(&fp, pFilename, T("wb")))
    {
        mux_fprintf(fp, T("%d" ENDLINE), game_pid);
        mux_fclose(fp);
    }
    else
    {
        STARTLOG(LOG_ALWAYS, "PID", "FAIL");
        g_pILog->WriteString(tprintf(T("Failed to write pidfile %s\n"), pFilename));
        ENDLOG;
    }
}

// #1798: required driver→engine COM interfaces must fail closed before
// listeners/main loop.  Partial acquisition is released so module
// shutdown stays coherent.
//
static void driver_release_interfaces(void)
{
    if (nullptr != g_pIPlayerSession)
    {
        g_pIPlayerSession->Release();
        g_pIPlayerSession = nullptr;
    }
    if (nullptr != g_pIObjectInfo)
    {
        g_pIObjectInfo->Release();
        g_pIObjectInfo = nullptr;
    }
    if (nullptr != g_pINotify)
    {
        g_pINotify->Release();
        g_pINotify = nullptr;
    }
    if (nullptr != g_pIGameEngine)
    {
        g_pIGameEngine->Release();
        g_pIGameEngine = nullptr;
    }
    if (nullptr != g_pIPlatform)
    {
        g_pIPlatform->Release();
        g_pIPlatform = nullptr;
    }
    if (nullptr != g_pILog)
    {
        g_pILog->Release();
        g_pILog = nullptr;
    }
}

static void driver_fatal_iface(const char *name, MUX_RESULT mr)
{
    // Always name the interface and result on stderr — even when CID_Log
    // itself is the failure (no logger yet).
    //
    fprintf(stderr, "FATAL: Failed to create %s interface (%d).\n",
        name, static_cast<int>(mr));
    if (nullptr != g_pILog)
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        g_pILog->log_text(tprintf(T("Failed to create %s interface (%d)."),
            reinterpret_cast<const UTF8 *>(name), mr));
        ENDLOG;
    }
}

#ifdef INLINESQL
void init_sql(void)
{
    if ('\0' != mudconf.sql_server[0])
    {
        STARTLOG(LOG_STARTUP,"SQL","CONN");
        g_pILog->log_text(T("Connecting: "));
        g_pILog->log_text(mudconf.sql_database);
        g_pILog->log_text(T("@"));
        g_pILog->log_text(mudconf.sql_server);
        g_pILog->log_text(T(" as "));
        g_pILog->log_text(mudconf.sql_user);
        ENDLOG;

        mush_database = mysql_init(nullptr);

        if (mush_database)
        {
#ifdef MYSQL_OPT_RECONNECT
            // As of MySQL 5.0.3, the default is no longer to reconnect.
            //
            my_bool reconnect = 1;
            mysql_options(mush_database, MYSQL_OPT_RECONNECT, reinterpret_cast<const char *>(&reconnect));
#endif
            mysql_options(mush_database, MYSQL_SET_CHARSET_NAME, "utf8");

            if (mysql_real_connect(mush_database,
                       reinterpret_cast<char *>(mudconf.sql_server), reinterpret_cast<char *>(mudconf.sql_user),
                       reinterpret_cast<char *>(mudconf.sql_password),
                       reinterpret_cast<char *>(mudconf.sql_database), 0, nullptr, 0))
            {
#ifdef MYSQL_OPT_RECONNECT
                // Before MySQL 5.0.19, mysql_real_connect sets the option
                // back to default, so we set it again.
                //
                mysql_options(mush_database, MYSQL_OPT_RECONNECT, reinterpret_cast<const char *>(&reconnect));
#endif
                STARTLOG(LOG_STARTUP,"SQL","CONN");
                g_pILog->log_text(T("Connected to MySQL"));
                ENDLOG;
            }
            else
            {
                STARTLOG(LOG_STARTUP,"SQL","CONN");
                g_pILog->log_text(T("Unable to connect"));
                ENDLOG;
                mysql_close(mush_database);
                mush_database = nullptr;
            }
        }
        else
        {
            STARTLOG(LOG_STARTUP,"SQL","CONN");
            g_pILog->log_text(T("MySQL Library unavailable"));
            ENDLOG;
        }
    }
}

#endif // INLINESQL

#define CLI_DO_CONFIG_FILE CLI_USER+0
#define CLI_DO_MINIMAL     CLI_USER+1
#define CLI_DO_VERSION     CLI_USER+2
#define CLI_DO_USAGE       CLI_USER+3
#define CLI_DO_INFILE      CLI_USER+4
#define CLI_DO_OUTFILE     CLI_USER+5
#define CLI_DO_CHECK       CLI_USER+6
#define CLI_DO_LOAD        CLI_USER+7
#define CLI_DO_UNLOAD      CLI_USER+8
#define CLI_DO_BASENAME    CLI_USER+9
#define CLI_DO_PID_FILE    CLI_USER+10
#define CLI_DO_ERRORPATH   CLI_USER+11
#define CLI_DO_COMSYS_FILE CLI_USER+12
#define CLI_DO_MAIL_FILE   CLI_USER+13
#define CLI_DO_FORCE       CLI_USER+14

static bool bMinDB = false;
static bool bSyntaxError = false;
static const UTF8 *conffile = nullptr;
static bool bVersion = false;
static const UTF8 *pErrorBasename = T("");
static bool bServerOption = false;
static const UTF8 *driver_pid_file = T("netmux.pid");

#define NUM_CLI_OPTIONS (sizeof(OptionTable)/sizeof(OptionTable[0]))
static CLI_OptionEntry OptionTable[] =
{
    { "c", CLI_REQUIRED, CLI_DO_CONFIG_FILE },
    { "s", CLI_NONE,     CLI_DO_MINIMAL     },
    { "v", CLI_NONE,     CLI_DO_VERSION     },
    { "h", CLI_NONE,     CLI_DO_USAGE       },
    { "i", CLI_REQUIRED, CLI_DO_INFILE      },
    { "o", CLI_REQUIRED, CLI_DO_OUTFILE     },
    { "k", CLI_NONE,     CLI_DO_CHECK       },
    { "l", CLI_NONE,     CLI_DO_LOAD        },
    { "u", CLI_NONE,     CLI_DO_UNLOAD      },
    { "d", CLI_REQUIRED, CLI_DO_BASENAME    },
    { "C", CLI_REQUIRED, CLI_DO_COMSYS_FILE },
    { "m", CLI_REQUIRED, CLI_DO_MAIL_FILE   },
    { "p", CLI_REQUIRED, CLI_DO_PID_FILE    },
    { "e", CLI_REQUIRED, CLI_DO_ERRORPATH   },
    { "f", CLI_NONE,     CLI_DO_FORCE       }
};

static void CLI_CallBack(CLI_OptionEntry *p, const char *pValue)
{
    if (p)
    {
        switch (p->m_Unique)
        {
        case CLI_DO_PID_FILE:
            bServerOption = true;
            driver_pid_file = reinterpret_cast<const UTF8 *>(pValue);
            break;

        case CLI_DO_CONFIG_FILE:
            bServerOption = true;
            conffile = reinterpret_cast<const UTF8 *>(pValue);
            break;

        case CLI_DO_MINIMAL:
            bServerOption = true;
            bMinDB = true;
            break;

        case CLI_DO_VERSION:
            bServerOption = true;
            bVersion = true;
            break;

        case CLI_DO_ERRORPATH:
            bServerOption = true;
            pErrorBasename = reinterpret_cast<const UTF8 *>(pValue);
            break;

        case CLI_DO_INFILE:
            g_bStandAlone = true;
            standalone_infile = reinterpret_cast<const UTF8 *>(pValue);
            break;

        case CLI_DO_OUTFILE:
            g_bStandAlone = true;
            standalone_outfile = reinterpret_cast<const UTF8 *>(pValue);
            break;

        case CLI_DO_CHECK:
            g_bStandAlone = true;
            standalone_check = true;
            break;

        case CLI_DO_LOAD:
            g_bStandAlone = true;
            standalone_load = true;
            break;

        case CLI_DO_UNLOAD:
            g_bStandAlone = true;
            standalone_unload = true;
            break;

        case CLI_DO_BASENAME:
            g_bStandAlone = true;
            standalone_basename = reinterpret_cast<const UTF8 *>(pValue);
            break;

        case CLI_DO_COMSYS_FILE:
            g_bStandAlone = true;
            standalone_comsys_file = reinterpret_cast<const UTF8 *>(pValue);
            break;

        case CLI_DO_MAIL_FILE:
            g_bStandAlone = true;
            standalone_mail_file = reinterpret_cast<const UTF8 *>(pValue);
            break;

        case CLI_DO_FORCE:
            g_bStandAlone = true;
            standalone_force = true;
            break;

        case CLI_DO_USAGE:
        default:
            bSyntaxError = true;
            break;
        }
    }
    else
    {
        bSyntaxError = true;
    }
}

#define DBCONVERT_NAME1 T("dbconvert")
#define DBCONVERT_NAME2 T("dbconvert.exe")

int DCL_CDECL main(int argc, char *argv[])
{
    FLOAT_Initialize();

    build_version();

    // Look for dbconvert[.exe] in the program name.
    //
    size_t nProg = strlen(argv[0]);
    const char *pProg = argv[0] + nProg - 1;
    while (  nProg
          && (  mux_isalpha(*pProg)
             || *pProg == '.'))
    {
        nProg--;
        pProg--;
    }
    pProg++;
    g_bStandAlone = false;
    if (  mux_stricmp(reinterpret_cast<const UTF8 *>(pProg), DBCONVERT_NAME1) == 0
       || mux_stricmp(reinterpret_cast<const UTF8 *>(pProg), DBCONVERT_NAME2) == 0)
    {
        g_bStandAlone = true;
    }

    // pid_file is driver-owned — set from CLI or default.
    //

    // Parse the command line
    //
    CLI_Process(argc, argv, OptionTable, NUM_CLI_OPTIONS, CLI_CallBack);

    if (g_bStandAlone)
    {
        int n = 0;
        if (standalone_check)
        {
            n++;
        }
        if (standalone_load)
        {
            n++;
        }
        if (standalone_unload)
        {
            n++;
        }
        if (  !standalone_basename
           || (!standalone_infile && !standalone_unload)
           || (!standalone_outfile && standalone_unload)
           || n != 1
           || bServerOption)
        {
            bSyntaxError = true;
        }
        else
        {
            dbconvert();
            return 0;
        }
    }
    else

    if (bVersion)
    {
        mux_fprintf(stderr, T("Version: %s" ENDLINE), g_version);
        return 1;
    }
    if (  bSyntaxError
       || conffile == nullptr
       || !bServerOption)
    {
        mux_fprintf(stderr, T("Version: %s" ENDLINE), g_version);
        if (g_bStandAlone)
        {
            mux_fprintf(stderr, T("Usage: %s -d <dbname> [-i <infile>] [-o <outfile>] [-l|-u|-k] [-f] [-C <comsys>] [-m <mail>]" ENDLINE), pProg);
            mux_fprintf(stderr, T("  -d  Basename (the SQLite file is <basename>.sqlite, relative to the current directory)." ENDLINE));
            mux_fprintf(stderr, T("  -i  Input file." ENDLINE));
            mux_fprintf(stderr, T("  -k  Check." ENDLINE));
            mux_fprintf(stderr, T("  -l  Load (import flatfile into SQLite)." ENDLINE));
            mux_fprintf(stderr, T("  -o  Output file." ENDLINE));
            mux_fprintf(stderr, T("  -u  Unload (export SQLite to flatfile)." ENDLINE));
            mux_fprintf(stderr, T("  -f  Force load over an existing SQLite database (replaces it)." ENDLINE));
            mux_fprintf(stderr, T("  -C  Comsys flatfile (import/export)." ENDLINE));
            mux_fprintf(stderr, T("  -m  Mail flatfile (import/export)." ENDLINE));
        }
        else
        {
            mux_fprintf(stderr, T("Usage: %s [-c <filename>] [-p <filename>] [-h] [-s] [-v]" ENDLINE), pProg);
            mux_fprintf(stderr, T("  -c  Specify configuration file." ENDLINE));
            mux_fprintf(stderr, T("  -e  Specify logfile basename (or '-' for stderr)." ENDLINE));
            mux_fprintf(stderr, T("  -h  Display this help." ENDLINE));
            mux_fprintf(stderr, T("  -p  Specify process ID file." ENDLINE));
            mux_fprintf(stderr, T("  -s  Start with a minimal database." ENDLINE));
            mux_fprintf(stderr, T("  -v  Display version string." ENDLINE ENDLINE));
        }
        return 1;
    }

    g_bStandAlone = false;

    // Initialize Modules very, very early.
    //
    MUX_RESULT mr = init_modules();
    if (MUX_FAILED(mr))
    {
        fprintf(stderr, "FATAL: Failed to initialize module subsystem (%d).\n",
            static_cast<int>(mr));
        return 2;
    }

    // Acquire required driver→engine COM interfaces.  Fail closed before
    // pidfile/signals/listeners so a missing interface is not a later
    // null-deref or silent drop (#1798).  Each CreateInstance result is
    // checked on its own — do not overwrite mr and ignore it.
    //
    mr = mux_CreateInstance(CID_Log, nullptr, UseSameProcess,
                            IID_ILog,
                            reinterpret_cast<void **>(&g_pILog));
    if (MUX_FAILED(mr) || nullptr == g_pILog)
    {
        driver_fatal_iface("Log (CID_Log)", mr);
        final_modules();
        return 2;
    }
    g_pILog->SetBasename(pErrorBasename);
    g_pILog->StartLogging();

    mr = mux_CreateInstance(CID_Platform, nullptr, UseSameProcess,
                            IID_IPlatform,
                            reinterpret_cast<void **>(&g_pIPlatform));
    if (MUX_FAILED(mr) || nullptr == g_pIPlatform)
    {
        driver_fatal_iface("Platform (CID_Platform)", mr);
        driver_release_interfaces();
        final_modules();
        return 2;
    }

    TimezoneCache::initialize();
    SeedRandomNumberGenerator();

    STARTLOG(LOG_ALWAYS, "INI", "LOAD");
    g_pILog->log_text(T("Registered netmux modules."));
    ENDLOG;

    game_pid = mux_getpid();
    write_pidfile(driver_pid_file);

    build_signal_names_table();

    // Timing state is engine-owned; the driver captures the values here
    // and pushes them via COM after LoadGame.
    //
    CLinearTimeAbsolute ltaStartup;
    ltaStartup.GetUTC();
    // Caller-owned pools (libmux types + driver-specific DESC).
    // Engine-owned pools (BOOL, QENTRY, PCACHE) are initialized
    // by engine.so during LoadGame().
    //
    pool_init(POOL_LBUF, LBUF_SIZE);
    pool_init(POOL_MBUF, MBUF_SIZE);
    pool_init(POOL_SBUF, SBUF_SIZE);
    pool_init(POOL_DESC, sizeof(DESC));

    if (g_pIPlatform)
    {
        int fdLimit = 0;
        g_pIPlatform->MaximizeFileDescriptors(&fdLimit);
    }
    init_logout_cmdtab();
    init_version();

    // The module subsystem must be ready to go before the configuration files
    // are consumed.  However, this means that the modules can't really do
    // much until they get a notification that the part of loading they depend
    // on is complete.
    //
    // #2192: collect zombies inherited across an exec-restart.
    //
    // Children survive execve() and the new image is still their parent, so
    // a helper that exited during do_restart()'s teardown arrives here as an
    // inherited zombie.  The DNS slave is exactly that case: GANL closes its
    // pipe during prepare_for_restart(), it exits on EOF milliseconds later
    // while do_restart() is still dumping, and the SIGCHLD that would have
    // reaped it sets a flag in a main loop that never runs again.
    //
    // Nothing else would ever collect it.  The steady-state reap is driven
    // by SIGCHLD (ganl_adapter.cpp), and in steady state no child exits --
    // so with no trigger the zombie is permanent, one more per restart.
    //
    // Deliberately before any helper of our own is booted: anything reaped
    // here is by construction inherited, never something this image spawned.
    // WNOHANG means only already-exited children are taken, so a live
    // inherited child (none expected) is left alone.
    //
#if defined(HAVE_WORKING_FORK)
    {
        int nReaped = 0;
        int status = 0;
        while (waitpid(-1, &status, WNOHANG) > 0)
        {
            nReaped++;
        }
        if (0 < nReaped && g_pILog)
        {
            g_pILog->WriteString(tprintf(
                T("Reaped %d inherited zombie helper(s) from a previous image.\n"),
                nReaped));
        }
    }
#endif // HAVE_WORKING_FORK

    // Boot stubslave helper process via platform interface.
    // On Unix: fork+exec creates a child process with IPC pipe.
    // On Windows: returns MUX_E_NOTIMPLEMENTED (runs in-process).
    //
    if (g_pIPlatform)
    {
        int readFd = -1, writeFd = -1, childPid = 0;
        MUX_RESULT mrHelper = g_pIPlatform->BootHelperProcess(
            T("bin/stubslave"), &readFd, &writeFd, &childPid);
        if (MUX_SUCCEEDED(mrHelper))
        {
            g_GanlAdapter.attach_stubslave(readFd, writeFd, childPid);
            init_stubslave();
        }
    }

    // log_dir was previously written to mudconf here; it's now
    // passed via pErrorBasename to SetBasename above.

    // Create the game engine interface.  In the current in-process build
    // this is a thin wrapper; when engine.so is split out, the driver
    // creates it via mux_CreateInstance to load the engine shared library.
    //
    mr = mux_CreateInstance(CID_GameEngine, nullptr, UseSameProcess,
                            IID_IGameEngine,
                            reinterpret_cast<void **>(&g_pIGameEngine));
    mux_IGameEngine *pGameEngine = g_pIGameEngine;
    if (MUX_FAILED(mr) || nullptr == pGameEngine)
    {
        driver_fatal_iface("GameEngine (CID_GameEngine)", mr);
        driver_release_interfaces();
        final_modules();
        return 2;
    }

    // Engine loads configuration, discovers modules, opens database.
    //
    mr = pGameEngine->LoadGame(conffile, nullptr, bMinDB);

    // cf_init() inside LoadGame clears mudstate; g_pIGameEngine is now
    // driver-local and unaffected, but keep the explicit assignment.
    //
    g_pIGameEngine = pGameEngine;

    if (MUX_FAILED(mr))
    {
        // Name the config file when that is what failed (#1601).  The
        // generic "LoadGame failed (-9)" gives an operator nothing to act
        // on, and this is the one startup failure most likely to be a
        // simple typo in the path.
        //
        if (MUX_E_NOTFOUND == mr)
        {
            fprintf(stderr, "FATAL: cannot read configuration file '%s'.\n",
                reinterpret_cast<const char *>(conffile));
        }
        fprintf(stderr, "FATAL: Game engine LoadGame failed (%d).\n", mr);
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        g_pILog->log_text(tprintf(T("Game engine LoadGame failed (%d)."), mr));
        ENDLOG;
        driver_release_interfaces();
        final_modules();
        return 2;
    }

    // Query the static configuration basket from the engine.
    //
    mr = pGameEngine->GetConfig(&g_dc);
    if (MUX_FAILED(mr))
    {
        fprintf(stderr, "FATAL: Failed to get driver config basket (%d).\n", mr);
        STARTLOG(LOG_ALWAYS, "INI", "CONF");
        g_pILog->log_text(tprintf(T("Failed to get driver config basket (%d)."), mr));
        ENDLOG;
        driver_release_interfaces();
        final_modules();
        return 2;
    }

    // Let engine-side @admin of defense knobs re-pull the basket into g_dc
    // so incident-response changes take effect without a restart.  Same
    // pattern as g_pool_limit_bytes (live push), via a libmux callback so
    // engine.so does not need a driver symbol.
    //
    g_driver_config_sync_fn = []() {
        if (nullptr != g_pIGameEngine)
        {
            g_pIGameEngine->GetConfig(&g_dc);
        }
    };

    // Push initial timing state to the engine.
    //
    pGameEngine->SetStartTime(ltaStartup);
    pGameEngine->SetRestartTime(ltaStartup);
    pGameEngine->SetRestartCount(0);
    pGameEngine->SetCpuCountFrom(ltaStartup);

    // Required driver→engine bridges.  Null here used to mean silent
    // drop (Notify/ObjectInfo) or a later login-path crash (PlayerSession).
    //
    mr = mux_CreateInstance(CID_Notify, nullptr, UseSameProcess,
                            IID_INotify,
                            reinterpret_cast<void **>(&g_pINotify));
    if (MUX_FAILED(mr) || nullptr == g_pINotify)
    {
        driver_fatal_iface("Notify (CID_Notify)", mr);
        driver_release_interfaces();
        final_modules();
        return 2;
    }

    mr = mux_CreateInstance(CID_ObjectInfo, nullptr, UseSameProcess,
                            IID_IObjectInfo,
                            reinterpret_cast<void **>(&g_pIObjectInfo));
    if (MUX_FAILED(mr) || nullptr == g_pIObjectInfo)
    {
        driver_fatal_iface("ObjectInfo (CID_ObjectInfo)", mr);
        driver_release_interfaces();
        final_modules();
        return 2;
    }

    mr = mux_CreateInstance(CID_PlayerSession, nullptr, UseSameProcess,
                            IID_IPlayerSession,
                            reinterpret_cast<void **>(&g_pIPlayerSession));
    if (MUX_FAILED(mr) || nullptr == g_pIPlayerSession)
    {
        driver_fatal_iface("PlayerSession (CID_PlayerSession)", mr);
        driver_release_interfaces();
        final_modules();
        return 2;
    }

    set_signals();

#if defined(HAVE_WORKING_FORK)
    load_restart_db();
    if (!g_restarting)
#endif // HAVE_WORKING_FORK
    {
        fclose(stdout);
        fclose(stdin);
    }

    // All initialization should be complete, allow the local
    // extensions to configure themselves.
    //
    pGameEngine->Startup();

    ganl_initialize();
    ganl_main_loop();
    ganl_shutdown();

#ifdef INLINESQL
     if (mush_database)
     {
         mysql_close(mush_database);
         mush_database = nullptr;
         STARTLOG(LOG_STARTUP,"SQL","DISC");
         g_pILog->log_text(T("SQL shut down"));
         ENDLOG;
     }
#endif // INLINESQL

    pGameEngine->DumpDatabase();

    // All shutdown, barring logfiles, should be done, shutdown the
    // local extensions.
    //
    pGameEngine->Shutdown();
    drv_CacheClose();
    pGameEngine = nullptr;  // released inside driver_release_interfaces
    driver_release_interfaces();
#if defined(STUB_SLAVE)
    final_stubslave();
#endif // STUB_SLAVE
    final_modules();

#if defined(HAVE_WORKING_FORK) && defined(STUB_SLAVE)
    g_GanlAdapter.shutdown_stubslave();
#endif // HAVE_WORKING_FORK && STUB_SLAVE

#ifdef SELFCHECK
    // Go ahead and explicitly free the memory for these things so
    // that it's easy to spot unintentional memory leaks.
    //
    int i;
    for (i = 0; i < mudstate.nHelpDesc; i++)
    {
        helpindex_clean(i);
    }

    finish_mail();
    finish_cmdtab();
    db_free();
#endif

    return 0;
}

// init_rlimit removed — replaced by g_pIPlatform->MaximizeFileDescriptors()

