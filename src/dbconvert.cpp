// dbconvert.cpp - Convert databases to various MUX formats.
//
// $Id: dbconvert.cpp,v 1.12 2001-10-17 15:58:59 sdennis Exp $ 
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

    switch (fmt) {
    case F_MUX:
        cp = "TinyMUX";
        break;
    default:
        cp = "*unknown*";
        break;
    }
    Log.tinyprintf("%s version %d:", cp, ver);
    if (flags & V_ZONE)
        Log.WriteString(" Zone");
    if (flags & V_LINK)
        Log.WriteString(" Link");
    if (flags & V_GDBM)
        Log.WriteString(" GDBM");
    if (flags & V_ATRNAME)
        Log.WriteString(" AtrName");
    if (flags & V_ATRKEY)
        Log.WriteString(" AtrKey");
    if (flags & V_PARENT)
        Log.WriteString(" Parent");
    if (flags & V_COMM)
        Log.WriteString(" Comm");
    if (flags & V_ATRMONEY)
        Log.WriteString(" AtrMoney");
    if (flags & V_XFLAGS)
        Log.WriteString(" ExtFlags");
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
    Log.WriteString("      G - Write in dir/pag format     g - Write in flat file format\n");
    Log.WriteString("      K - Store key as an attribute   k - Store key in the header\n");
    Log.WriteString("      L - Include link information    l - Don't include link information\n");
    Log.WriteString("      M - Store attr map if DIRPAG    m - Don't store attr map if DIRPAG\n");
    Log.WriteString("      N - Store name as an attribute  n - Store name in the header\n");
    Log.WriteString("      P - Include parent information  p - Don't include parent information\n");
    Log.WriteString("      W - Write the output file  b    w - Don't write the output file.\n");
    Log.WriteString("      X - Create a default DIRPAG db  x - Create a default flat file db\n");
    Log.WriteString("      Z - Include zone information    z - Don't include zone information\n");
    Log.WriteString("      <number> - Set output version number\n");
}

long DebugTotalFiles = 3;
long DebugTotalThreads = 1;
long DebugTotalSemaphores = 0;
long DebugTotalSockets = 0;

int DCL_CDECL main(int argc, char *argv[])
{
    int setflags, clrflags, ver;
    int db_ver, db_format, db_flags, do_check, do_write;
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
    setflags = clrflags = ver = do_check = 0;
    do_write = 1;
    int do_redirect = 0;

    if (argc >= 3)
    {
        for (fp = argv[2]; *fp; fp++)
        {
            switch (*fp)
            {
            case 'C':
                do_check = 1;
                break;
            case 'G':
                setflags |= V_GDBM;
                break;
            case 'g':
                clrflags |= V_GDBM;
                break;
            case 'Z':
                setflags |= V_ZONE;
                break;
            case 'z':
                clrflags |= V_ZONE;
                break;
            case 'L':
                setflags |= V_LINK;
                break;
            case 'l':
                clrflags |= V_LINK;
                break;
            case 'N':
                setflags |= V_ATRNAME;
                break;
            case 'n':
                clrflags |= V_ATRNAME;
                break;
            case 'K':
                setflags |= V_ATRKEY;
                break;
            case 'k':
                clrflags |= V_ATRKEY;
                break;
            case 'P':
                setflags |= V_PARENT;
                break;
            case 'p':
                clrflags |= V_PARENT;
                break;
            case 'W':
                do_write = 1;
                break;
            case 'w':
                do_write = 0;
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
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                ver = ver * 10 + (*fp - '0');
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

    if (do_write) {
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
