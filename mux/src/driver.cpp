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

#if defined(INLINESQL)
#include <mysql.h>

MYSQL *mush_database = nullptr;
#endif // INLINESQL

/*
 * ---------------------------------------------------------------------------
 * * info: display info about the file being read or written.
 */

static void info(int fmt, int flags, int ver)
{
    const UTF8 *cp;

    if (fmt == F_MUX)
    {
        cp = T("MUX");
    }
    else
    {
        cp = T("*unknown*");
    }
    Log.tinyprintf(T("%s version %d:"), cp, ver);
    if (  ver < MIN_SUPPORTED_VERSION
       || MAX_SUPPORTED_VERSION < ver)
    {
        Log.WriteString(T(" Unsupported version"));
        exit(1);
    }
    else if (  (  (  1 == ver
                  || 2 == ver)
               && (flags & MANDFLAGS_V2) != MANDFLAGS_V2)
            || (  3 == ver
               && (flags & MANDFLAGS_V3) != MANDFLAGS_V3)
            || (  4 == ver
               && (flags & MANDFLAGS_V4) != MANDFLAGS_V4))
    {
        Log.WriteString(T(" Unsupported flags"));
        exit(1);
    }
    if (flags & V_DATABASE)
        Log.WriteString(T(" Database"));
    if (flags & V_ATRNAME)
        Log.WriteString(T(" AtrName"));
    if (flags & V_ATRKEY)
        Log.WriteString(T(" AtrKey"));
    if (flags & V_ATRMONEY)
        Log.WriteString(T(" AtrMoney"));
    Log.WriteString(T(ENDLINE));
}

static const UTF8 *standalone_infile = nullptr;
static const UTF8 *standalone_outfile = nullptr;
static const UTF8 *standalone_basename = nullptr;
static bool standalone_check = false;
static bool standalone_load = false;
static bool standalone_unload = false;
static const UTF8 *standalone_comsys_file = nullptr;
static const UTF8 *standalone_mail_file = nullptr;

static void dbconvert(void)
{
    int setflags, clrflags, ver;
    int db_ver, db_format, db_flags;

    Log.SetBasename(T("-"));
    Log.StartLogging();

    SeedRandomNumberGenerator();

    pool_init(POOL_LBUF, LBUF_SIZE);
    pool_init(POOL_MBUF, MBUF_SIZE);
    pool_init(POOL_SBUF, SBUF_SIZE);
    pool_init(POOL_BOOL, sizeof(struct boolexp));
    pool_init(POOL_STRING, sizeof(mux_string));

    cf_init();

    // Decide what conversions to do and how to format the output file.
    //
    setflags = clrflags = ver = 0;
    bool do_redirect = false;

    bool do_write = true;
    if (standalone_check || standalone_load)
    {
        do_write = false;
    }
    if (standalone_load)
    {
        clrflags = 0xffffffff;
        setflags = OUTPUT_FLAGS;
        ver = OUTPUT_VERSION;
        do_redirect = true;
    }
    else if (standalone_unload)
    {
        clrflags = 0xffffffff;
        setflags = UNLOAD_FLAGS;
        ver = UNLOAD_VERSION;
    }

    // Open the database
    //
    init_attrtab();

    int cc = init_dbfile(standalone_basename);
    if (cc == HF_OPEN_STATUS_ERROR)
    {
        Log.tinyprintf(T("Can\xE2\x80\x99t open SQLite database.\n"));
        exit(1);
    }
    else if (cc == HF_OPEN_STATUS_OLD)
    {
        if (setflags == OUTPUT_FLAGS)
        {
            Log.tinyprintf(T("Would overwrite existing SQLite database.\n"));
            CLOSE;
            exit(1);
        }
    }
    else if (cc == HF_OPEN_STATUS_NEW)
    {
        if (setflags == UNLOAD_FLAGS)
        {
            Log.tinyprintf(T("SQLite database is empty.\n"));
            CLOSE;
            exit(1);
        }
    }

    bool bLoadedFromSQLite = false;
    if (nullptr == standalone_infile && HF_OPEN_STATUS_OLD == cc)
    {
        int sqlite_load_rc = sqlite_load_game();
        if (sqlite_load_rc < 0)
        {
            Log.WriteString(T("Input: SQLite database load failed.\n"));
            exit(1);
        }
        bLoadedFromSQLite = (sqlite_load_rc > 0);
    }

    if (bLoadedFromSQLite)
    {
        // No input flatfile given, but SQLite database exists.
        // Load from SQLite for export.
        //
        Log.WriteString(T("Input: SQLite database\n"));
        db_format = F_MUX;
        db_ver = OUTPUT_VERSION;
        db_flags = OUTPUT_FLAGS;
    }
    else
    {
        if (nullptr == standalone_infile)
        {
            Log.WriteString(T("No input flatfile provided and SQLite has no loadable game data.\n"));
            exit(1);
        }
        FILE *fpIn;
        if (!mux_fopen(&fpIn, standalone_infile, T("rb")))
        {
            exit(1);
        }

        // Go do it.
        //
        setvbuf(fpIn, nullptr, _IOFBF, 16384);
        CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();
        if (!sqldb.Begin() || !sqldb.ClearAttributes())
        {
            sqldb.Rollback();
            Log.WriteString(T("SQLite attribute clear failed before flatfile import.\n"));
            fclose(fpIn);
            exit(1);
        }
        mudstate.bSQLiteLoading = true;
        if (db_read(fpIn, &db_format, &db_ver, &db_flags) < 0)
        {
            mudstate.bSQLiteLoading = false;
            sqldb.Rollback();
            exit(1);
        }
        mudstate.bSQLiteLoading = false;
        if (!sqldb.Commit())
        {
            sqldb.Rollback();
            Log.WriteString(T("SQLite attribute import commit failed.\n"));
            fclose(fpIn);
            exit(1);
        }
        if (!sqlite_sync_runtime())
        {
            if (!clear_sqlite_after_sync_failure(sqldb))
            {
                Log.WriteString(T("SQLite cleanup failed after sync failure.\n"));
            }
            Log.WriteString(T("SQLite metadata sync failed.\n"));
            exit(1);
        }
        Log.WriteString(T("Input: "));
        info(db_format, db_flags, db_ver);

        if (standalone_check)
        {
            do_dbck(NOTHING, NOTHING, NOTHING, 0, DBCK_FULL);
        }
        fclose(fpIn);
    }

    // Import comsys from flatfile into SQLite.
    //
    if (standalone_load && standalone_comsys_file)
    {
        load_comsys(const_cast<UTF8 *>(standalone_comsys_file));
        if (!sqlite_sync_comsys())
        {
            Log.WriteString(T("Import comsys into SQLite failed.\n"));
            exit(1);
        }
        Log.WriteString(T("Imported comsys into SQLite.\n"));
    }

    // Import mail from flatfile into SQLite.
    //
    if (standalone_load && standalone_mail_file)
    {
        FILE *fpMail;
        if (mux_fopen(&fpMail, standalone_mail_file, T("rb")))
        {
            setvbuf(fpMail, nullptr, _IOFBF, 16384);
            load_mail(fpMail);
            fclose(fpMail);
            if (!sqlite_sync_mail())
            {
                Log.WriteString(T("Import mail into SQLite failed.\n"));
                exit(1);
            }
            Log.WriteString(T("Imported mail into SQLite.\n"));
        }
    }

    // Export comsys from SQLite to flatfile.
    //
    if (standalone_unload && standalone_comsys_file)
    {
        int sqlite_comsys_rc = sqlite_load_comsys();
        if (sqlite_comsys_rc > 0)
        {
            save_comsys(const_cast<UTF8 *>(standalone_comsys_file));
            Log.WriteString(T("Exported comsys from SQLite.\n"));
        }
        else if (sqlite_comsys_rc < 0)
        {
            Log.WriteString(T("Export comsys from SQLite failed.\n"));
            exit(1);
        }
    }

    // Export mail from SQLite to flatfile.
    //
    if (standalone_unload && standalone_mail_file)
    {
        int sqlite_mail_rc = sqlite_load_mail();
        if (sqlite_mail_rc > 0)
        {
            FILE *fpMail;
            if (mux_fopen(&fpMail, standalone_mail_file, T("wb")))
            {
                dump_mail(fpMail);
                fclose(fpMail);
                Log.WriteString(T("Exported mail from SQLite.\n"));
            }
        }
        else if (sqlite_mail_rc < 0)
        {
            Log.WriteString(T("Export mail from SQLite failed.\n"));
            exit(1);
        }
    }

    if (do_write)
    {
        FILE *fpOut;
        if (!mux_fopen(&fpOut, standalone_outfile, T("wb")))
        {
            exit(1);
        }

        db_flags = (db_flags & ~clrflags) | setflags;
        if (db_format != F_MUX)
        {
            db_ver = 3;
        }
        if (ver != 0)
        {
            db_ver = ver;
        }
        Log.WriteString(T("Output: "));
        info(F_MUX, db_flags, db_ver);
        setvbuf(fpOut, nullptr, _IOFBF, 16384);
        db_write(fpOut, F_MUX, db_ver | db_flags);
        fclose(fpOut);
    }
    CLOSE;
#ifdef SELFCHECK
    db_free();
#endif
    exit(0);
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
        Log.tinyprintf(T("Failed to write pidfile %s\n"), pFilename);
        ENDLOG;
    }
}

#ifdef INLINESQL
void init_sql(void)
{
    if ('\0' != mudconf.sql_server[0])
    {
        STARTLOG(LOG_STARTUP,"SQL","CONN");
        log_text(T("Connecting: "));
        log_text(mudconf.sql_database);
        log_text(T("@"));
        log_text(mudconf.sql_server);
        log_text(T(" as "));
        log_text(mudconf.sql_user);
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
                log_text(T("Connected to MySQL"));
                ENDLOG;
            }
            else
            {
                STARTLOG(LOG_STARTUP,"SQL","CONN");
                log_text(T("Unable to connect"));
                ENDLOG;
                mysql_close(mush_database);
                mush_database = nullptr;
            }
        }
        else
        {
            STARTLOG(LOG_STARTUP,"SQL","CONN");
            log_text(T("MySQL Library unavailable"));
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

    // TODO: Create platform interface

    TimezoneCache::initialize();
    SeedRandomNumberGenerator();

    Log.SetBasename(pErrorBasename);
    Log.StartLogging();

    STARTLOG(LOG_ALWAYS, "INI", "LOAD");
    if (MUX_SUCCEEDED(mr))
    {
        log_printf(T("Registered netmux modules."));
    }
    else
    {
        log_printf(T("Failed either to initialize module subsystem or register netmux modules (%d)."), mr);
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
    mux_IGameEngine *pGameEngine = nullptr;
    mr = mux_CreateInstance(CID_GameEngine, nullptr, UseSameProcess,
                            IID_IGameEngine,
                            reinterpret_cast<void **>(&pGameEngine));
    if (MUX_FAILED(mr) || nullptr == pGameEngine)
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_printf(T("Failed to create game engine interface (%d)."), mr);
        ENDLOG;
        return 2;
    }

    // Engine loads configuration, discovers modules, opens database.
    //
    mr = pGameEngine->LoadGame(conffile, nullptr, bMinDB);
    if (MUX_FAILED(mr))
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_printf(T("Game engine LoadGame failed (%d)."), mr);
        ENDLOG;
        pGameEngine->Release();
        return 2;
    }

    set_signals();

    // Initialize the connection bridge so engine.so's free-function
    // stubs (conn_bridge.cpp) can delegate to the driver's real
    // implementations via mux_IConnectionManager COM interface.
    //
    conn_bridge_init();

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
         log_text(T("SQL shut down"));
         ENDLOG;
     }
#endif // INLINESQL

    pGameEngine->DumpDatabase();

    // All shutdown, barring logfiles, should be done, shutdown the
    // local extensions.
    //
    pGameEngine->Shutdown();
    conn_bridge_final();
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
        log_perror(T("RLM"), T("FAIL"), nullptr, T("getrlimit()"));
        free_lbuf(rlp);
        return;
    }
    rlp->rlim_cur = rlp->rlim_max;
    if (setrlimit(RLIMIT_NOFILE, rlp))
    {
        log_perror(T("RLM"), T("FAIL"), nullptr, T("setrlimit()"));
    }
    free_lbuf(rlp);

}
#endif // HAVE_SETRLIMIT RLIMIT_NOFILE

