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
#include "driver_log.h"

mux_ILog *g_pILog = nullptr;

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
        standalone_unload, standalone_comsys_file, standalone_mail_file);
    pEngine->Release();
    exit(MUX_SUCCEEDED(mr) ? 0 : 1);
}

static void write_pidfile(const UTF8 *pFilename)
{
    FILE *fp;
    if (mux_fopen(&fp, pFilename, T("wb")))
    {
        mux_fprintf(fp, T("%d" ENDLINE), game_pid);
        fclose(fp);
    }
    else
    {
        STARTLOG(LOG_ALWAYS, "PID", "FAIL");
        g_pILog->WriteString(tprintf(T("Failed to write pidfile %s\n"), pFilename));
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

static bool bMinDB = false;
static bool bSyntaxError = false;
static const UTF8 *conffile = nullptr;
static bool bVersion = false;
static const UTF8 *pErrorBasename = T("");
static bool bServerOption = false;

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
    { "e", CLI_REQUIRED, CLI_DO_ERRORPATH   }
};

static void CLI_CallBack(CLI_OptionEntry *p, const char *pValue)
{
    if (p)
    {
        switch (p->m_Unique)
        {
        case CLI_DO_PID_FILE:
            bServerOption = true;
            mudconf.pid_file = reinterpret_cast<const UTF8 *>(pValue);
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
            mudstate.bStandAlone = true;
            standalone_infile = reinterpret_cast<const UTF8 *>(pValue);
            break;

        case CLI_DO_OUTFILE:
            mudstate.bStandAlone = true;
            standalone_outfile = reinterpret_cast<const UTF8 *>(pValue);
            break;

        case CLI_DO_CHECK:
            mudstate.bStandAlone = true;
            standalone_check = true;
            break;

        case CLI_DO_LOAD:
            mudstate.bStandAlone = true;
            standalone_load = true;
            break;

        case CLI_DO_UNLOAD:
            mudstate.bStandAlone = true;
            standalone_unload = true;
            break;

        case CLI_DO_BASENAME:
            mudstate.bStandAlone = true;
            standalone_basename = reinterpret_cast<const UTF8 *>(pValue);
            break;

        case CLI_DO_COMSYS_FILE:
            mudstate.bStandAlone = true;
            standalone_comsys_file = reinterpret_cast<const UTF8 *>(pValue);
            break;

        case CLI_DO_MAIL_FILE:
            mudstate.bStandAlone = true;
            standalone_mail_file = reinterpret_cast<const UTF8 *>(pValue);
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
    mudstate.bStandAlone = false;
    if (  mux_stricmp(reinterpret_cast<const UTF8 *>(pProg), DBCONVERT_NAME1) == 0
       || mux_stricmp(reinterpret_cast<const UTF8 *>(pProg), DBCONVERT_NAME2) == 0)
    {
        mudstate.bStandAlone = true;
    }

    mudconf.pid_file = T("netmux.pid");

    // Parse the command line
    //
    CLI_Process(argc, argv, OptionTable, NUM_CLI_OPTIONS, CLI_CallBack);

    if (mudstate.bStandAlone)
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
        mux_fprintf(stderr, T("Version: %s" ENDLINE), mudstate.version);
        return 1;
    }
    if (  bSyntaxError
       || conffile == nullptr
       || !bServerOption)
    {
        mux_fprintf(stderr, T("Version: %s" ENDLINE), mudstate.version);
        if (mudstate.bStandAlone)
        {
            mux_fprintf(stderr, T("Usage: %s -d <dbname> [-i <infile>] [-o <outfile>] [-l|-u|-k] [-C <comsys>] [-m <mail>]" ENDLINE), pProg);
            mux_fprintf(stderr, T("  -d  Basename." ENDLINE));
            mux_fprintf(stderr, T("  -i  Input file." ENDLINE));
            mux_fprintf(stderr, T("  -k  Check." ENDLINE));
            mux_fprintf(stderr, T("  -l  Load (import flatfile into SQLite)." ENDLINE));
            mux_fprintf(stderr, T("  -o  Output file." ENDLINE));
            mux_fprintf(stderr, T("  -u  Unload (export SQLite to flatfile)." ENDLINE));
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

    mudstate.bStandAlone = false;

    // Initialize Modules very, very early.
    //
    MUX_RESULT mr = init_modules();

    // Acquire the logging interface from engine.so immediately after
    // module registration so all driver code can log through COM.
    //
    mr = mux_CreateInstance(CID_Log, nullptr, UseSameProcess,
                            IID_ILog,
                            reinterpret_cast<void **>(&g_pILog));

    // TODO: Create platform interface

    TimezoneCache::initialize();
    SeedRandomNumberGenerator();

    if (g_pILog)
    {
        g_pILog->SetBasename(pErrorBasename);
        g_pILog->StartLogging();
    }

    STARTLOG(LOG_ALWAYS, "INI", "LOAD");
    if (MUX_SUCCEEDED(mr))
    {
        g_pILog->log_text(T("Registered netmux modules."));
    }
    else
    {
        g_pILog->log_text(tprintf(T("Failed either to initialize module subsystem or register netmux modules (%d)."), mr));
    }
    ENDLOG;

    game_pid = mux_getpid();
    write_pidfile(mudconf.pid_file);

    build_signal_names_table();

    mudstate.restart_time.GetUTC();
    mudstate.start_time = mudstate.restart_time;
    mudstate.restart_count= 0;

    mudstate.cpu_count_from.GetUTC();
    pool_init(POOL_LBUF, LBUF_SIZE);
    pool_init(POOL_MBUF, MBUF_SIZE);
    pool_init(POOL_SBUF, SBUF_SIZE);
    pool_init(POOL_BOOL, sizeof(struct boolexp));

    pool_init(POOL_DESC, sizeof(DESC));
    pool_init(POOL_QENTRY, sizeof(BQUE));
    pool_init(POOL_LBUFREF, sizeof(lbuf_ref));
    pool_init(POOL_REGREF, sizeof(reg_ref));
    pool_init(POOL_STRING, sizeof(mux_string));

#if defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE)
    init_rlimit();
#endif // HAVE_SETRLIMIT RLIMIT_NOFILE
    init_logout_cmdtab();
    init_version();

    // The module subsystem must be ready to go before the configuration files
    // are consumed.  However, this means that the modules can't really do
    // much until they get a notification that the part of loading they depend
    // on is complete.
    //
#if defined(HAVE_WORKING_FORK) && defined(STUB_SLAVE)
    g_GanlAdapter.boot_stubslave();
    init_stubslave();
#endif // HAVE_WORKING_FORK && STUB_SLAVE

    mudconf.log_dir = StringClone(pErrorBasename);

    // Create the game engine interface.  In the current in-process build
    // this is a thin wrapper; when engine.so is split out, the driver
    // creates it via mux_CreateInstance to load the engine shared library.
    //
    mr = mux_CreateInstance(CID_GameEngine, nullptr, UseSameProcess,
                            IID_IGameEngine,
                            reinterpret_cast<void **>(&mudstate.pIGameEngine));
    mux_IGameEngine *pGameEngine = mudstate.pIGameEngine;
    if (MUX_FAILED(mr) || nullptr == pGameEngine)
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        g_pILog->log_text(tprintf(T("Failed to create game engine interface (%d)."), mr));
        ENDLOG;
        return 2;
    }

    // Engine loads configuration, discovers modules, opens database.
    //
    mr = pGameEngine->LoadGame(conffile, nullptr, bMinDB);

    // cf_init() inside LoadGame clears mudstate.pIGameEngine; restore it.
    //
    mudstate.pIGameEngine = pGameEngine;

    if (MUX_FAILED(mr))
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        g_pILog->log_text(tprintf(T("Game engine LoadGame failed (%d)."), mr));
        ENDLOG;
        pGameEngine->Release();
        return 2;
    }

    set_signals();

#if defined(HAVE_WORKING_FORK)
    load_restart_db();
    if (!mudstate.restarting)
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
    pGameEngine->Release();
    pGameEngine = nullptr;
#if defined(STUB_SLAVE)
    final_stubslave();
#endif // STUB_SLAVE
    final_modules();
    CLOSE;

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

#if defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE)
void init_rlimit(void)
{
    struct rlimit *rlp;

    rlp = reinterpret_cast<struct rlimit *>(alloc_lbuf("rlimit"));

    if (getrlimit(RLIMIT_NOFILE, rlp))
    {
        g_pILog->log_perror(T("RLM"), T("FAIL"), nullptr, T("getrlimit()"));
        free_lbuf(rlp);
        return;
    }
    rlp->rlim_cur = rlp->rlim_max;
    if (setrlimit(RLIMIT_NOFILE, rlp))
    {
        g_pILog->log_perror(T("RLM"), T("FAIL"), nullptr, T("setrlimit()"));
    }
    free_lbuf(rlp);

}
#endif // HAVE_SETRLIMIT RLIMIT_NOFILE

