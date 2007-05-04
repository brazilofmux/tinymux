// dbconvert.cpp -- Convert databases to various MUX formats.
//
// $Id: dbconvert.cpp,v 1.15 2001-11-20 05:17:54 sdennis Exp $ 
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#undef MEMORY_BASED
#include "externs.h"

#include "db.h"
#include "vattr.h"
#include "_build.h"

#ifdef RADIX_COMPRESSION
#ifndef COMPRESSOR
#define COMPRESSOR
#endif
#include "radix.h"
#endif

extern void NDECL(cf_init);
extern void FDECL(do_dbck, (dbref, dbref, int));
extern void NDECL(init_attrtab);

/*
 * ---------------------------------------------------------------------------
 * * info: display info about the file being read or written.
 */

void info(int fmt, int flags, int ver)
{
    const char *cp;

    if (fmt == F_MUX)
    {
        cp = "TinyMUX";
    }
    else
    {
        cp = "*unknown*";
    }
    Log.tinyprintf("%s version %d:", cp, ver);
    if ((flags & MANDFLAGS) != MANDFLAGS)
    {
        Log.WriteString(" Unsupported flags");
    }
    if (flags & V_DATABASE)
        Log.WriteString(" Database");
    if (flags & V_ATRNAME)
        Log.WriteString(" AtrName");
    if (flags & V_ATRKEY)
        Log.WriteString(" AtrKey");
    if (flags & V_ATRMONEY)
        Log.WriteString(" AtrMoney");
    Log.WriteString("\n");
}

void usage(char *prog)
{
#ifdef WIN32
#ifdef BETA
    Log.tinyprintf("%s from MUX %s for Win32 #%s [BETA]\n", prog, MUX_VERSION,
        MUX_BUILD_NUM);
#else // BETA
    Log.tinyprintf("%s from MUX %s for Win32 #%s [%s]\n", prog, MUX_VERSION,
        MUX_BUILD_NUM, MUX_RELEASE_DATE);
#endif // BETA
#else // WIN32
#ifdef BETA
    Log.tinyprintf("%s from MUX %s #%s [BETA]\n", prog, MUX_VERSION,
        MUX_BUILD_NUM);
#else // BETA
    Log.tinyprintf("%s from MUX %s #%s [%s]\n", prog, MUX_VERSION, MUX_BUILD_NUM,
        MUX_RELEASE_DATE);
#endif // BETA
#endif // WIN32
    Log.tinyprintf("Usage: %s gamedb-basename [flags] [<in-file] [>out-file]\n", prog);
    Log.WriteString("   Available flags are:\n");
    Log.WriteString("      C - Perform consistency check\n");
    Log.WriteString("      X - Load into attribute database.\n");
    Log.WriteString("      x - Unload to a flat file db\n");
}

long DebugTotalFiles = 3;
long DebugTotalThreads = 1;
long DebugTotalSemaphores = 0;
long DebugTotalSockets = 0;

int DCL_CDECL main(int argc, char *argv[])
{
    int setflags, clrflags, ver;
    int db_ver, db_format, db_flags;
    char *fp;

    if ((argc < 2) || (argc > 3))
    {
        usage(argv[0]);
        exit(1);
    }

    SeedRandomNumberGenerator();

    cf_init();
#ifdef RADIX_COMPRESSION
    init_string_compress();
#endif

    // Decide what conversions to do and how to format the output file.
    //
    setflags = clrflags = ver = 0;
    BOOL do_check = FALSE;
    BOOL do_write = TRUE;
    BOOL do_redirect = FALSE;

    if (argc >= 3)
    {
        for (fp = argv[2]; *fp; fp++)
        {
            switch (*fp)
            {
            case 'C':
                do_check = TRUE;
                do_write = FALSE;
                break;

            case 'X':
                clrflags = 0xffffffff;
                setflags = OUTPUT_FLAGS;
                ver = OUTPUT_VERSION;
                do_redirect = 1;
                break;

            case 'x':
                clrflags = 0xffffffff;
                setflags = UNLOAD_FLAGS;
                ver = UNLOAD_VERSION;
                break;

            default:
                Log.tinyprintf("Unknown flag: '%c'\n", *fp);
                usage(argv[0]);
                exit(1);
            }
        }
    }
#ifdef WIN32
    _setmode(fileno(stdin),O_BINARY);
    _setmode(fileno(stdout),O_BINARY);
#endif // WIN32

    // Open the database
    //
    init_attrtab();

    char dirfile[SIZEOF_PATHNAME];
    char pagfile[SIZEOF_PATHNAME];
    strcpy(dirfile, argv[1]);
    strcat(dirfile, ".dir");
    strcpy(pagfile, argv[1]);
    strcat(pagfile, ".pag");

    int cc = init_dbfile(dirfile, pagfile);
    if (cc == HF_OPEN_STATUS_ERROR)
    {
        Log.tinyprintf("Can't open database in (%s, %s) files\n", dirfile, pagfile);
        exit(1);
    }
    else if (cc == HF_OPEN_STATUS_OLD)
    {
        if (setflags == OUTPUT_FLAGS)
        {
            Log.tinyprintf("Would overwrite existing database (%s, %s)\n", dirfile, pagfile);
            CLOSE;
            exit(1);
        }
    }
    else if (cc == HF_OPEN_STATUS_NEW)
    {
        if (setflags == UNLOAD_FLAGS)
        {
            Log.tinyprintf("Database (%s, %s) is empty.\n", dirfile, pagfile);
            CLOSE;
            exit(1);
        }
    }

    // Go do it.
    //
    if (do_redirect)
    {
        extern void cache_redirect(void);
        cache_redirect();
    }
    setvbuf(stdin, NULL, _IOFBF, 16384);
    db_read(stdin, &db_format, &db_ver, &db_flags);
    if (do_redirect)
    {
        extern void cache_pass2(void);
        cache_pass2();
    }
    Log.WriteString("Input: ");
    info(db_format, db_flags, db_ver);

    if (do_check)
        do_dbck(NOTHING, NOTHING, DBCK_FULL);

    if (do_write)
    {
        db_flags = (db_flags & ~clrflags) | setflags;
        if (db_format != F_MUX)
            db_ver = 3;
        if (ver != 0)
            db_ver = ver;
        Log.WriteString("Output: ");
        info(F_MUX, db_flags, db_ver);
        setvbuf(stdout, NULL, _IOFBF, 16384);
        db_write(stdout, F_MUX, db_ver | db_flags);
    }
    CLOSE;
    db_free();
    return 0;
}
