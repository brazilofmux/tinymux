//
// log.cpp - logging routines
//
// $Id: log.cpp,v 1.5 2001-06-30 17:44:18 morgan Exp $
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <sys/types.h>

#include "db.h"
#include "mudconf.h"
#include "flags.h"
#include "powers.h"
#include "alloc.h"
#include "htab.h"
#include "ansi.h"

#ifndef STANDALONE

NAMETAB logdata_nametab[] =
{
    {(char *)"flags",       1,  0,  LOGOPT_FLAGS},
    {(char *)"location",        1,  0,  LOGOPT_LOC},
    {(char *)"owner",       1,  0,  LOGOPT_OWNER},
    {(char *)"timestamp",       1,  0,  LOGOPT_TIMESTAMP},
    { NULL,             0,  0,  0}
};

NAMETAB logoptions_nametab[] =
{
    {(char *)"accounting",      2,  0,  LOG_ACCOUNTING},
    {(char *)"all_commands",    2,  0,  LOG_ALLCOMMANDS},
    {(char *)"bad_commands",    2,  0,  LOG_BADCOMMANDS},
    {(char *)"buffer_alloc",    3,  0,  LOG_ALLOCATE},
    {(char *)"bugs",            3,  0,  LOG_BUGS},
    {(char *)"checkpoints",     2,  0,  LOG_DBSAVES},
    {(char *)"config_changes",  2,  0,  LOG_CONFIGMODS},
    {(char *)"create",          2,  0,  LOG_PCREATES},
    {(char *)"killing",         1,  0,  LOG_KILLS},
    {(char *)"logins",          1,  0,  LOG_LOGIN},
    {(char *)"network",         1,  0,  LOG_NET},
    {(char *)"problems",        1,  0,  LOG_PROBLEMS},
    {(char *)"security",        2,  0,  LOG_SECURITY},
    {(char *)"shouts",          2,  0,  LOG_SHOUTS},
    {(char *)"startup",         2,  0,  LOG_STARTUP},
    {(char *)"time_usage",      1,  0,  LOG_TIMEUSE},
    {(char *)"wizard",          1,  0,  LOG_WIZARD},
    { NULL,                     0,  0,  0}
};

#endif // !STANDALONE

/*
 * ---------------------------------------------------------------------------
 * * start_log: see if it is OK to log something, and if so, start writing the
 * * log entry.
 */

int start_log(const char *primary, const char *secondary)
{
    /*
     * Format the timestamp
     */

    char buffer[256];
    buffer[0] = '\0';
    if ((mudconf.log_info & LOGOPT_TIMESTAMP) != 0)
    {
        CLinearTimeAbsolute ltaNow;
        ltaNow.GetLocal();
        FIELDEDTIME ft;
        ltaNow.ReturnFields(&ft);
        sprintf(buffer, "%d.%02d%02d:%02d%02d%02d ",ft.iYear, ft.iMonth,
                ft.iDayOfMonth, ft.iHour, ft.iMinute, ft.iSecond);
    }

#ifndef STANDALONE
    /*
     * Write the header to the log
     */

    if (secondary && *secondary)
        Log.tinyprintf("%s%s %3s/%-5s: ", buffer, mudconf.mud_name, primary, secondary);
    else
        Log.tinyprintf("%s%s %-9s: ", buffer, mudconf.mud_name, primary);
#endif // !STANDALONE

    return 1;
}

/*
 * ---------------------------------------------------------------------------
 * * end_log: Finish up writing a log entry
 */

void NDECL(end_log)
{
    Log.WriteString(ENDLINE);
    Log.Flush();
}

/*
 * ---------------------------------------------------------------------------
 * * log_perror: Write perror message to the log
 */

void log_perror(const char *primary, const char *secondary, const char *extra, const char *failing_object)
{
    start_log(primary, secondary);
    if (extra && *extra)
    {
        log_text((char *)"(");
        log_text((char *)extra);
        log_text((char *)") ");
    }

    // <Failing_object text>: <strerror() text>
    //
    Log.WriteString(failing_object);
    Log.WriteString(": ");
    Log.WriteString(strerror(errno));
#ifndef WIN32
    Log.WriteString(ENDLINE);
#endif // !WIN32
    Log.Flush();
}

/*
 * ---------------------------------------------------------------------------
 * * log_text, log_number: Write text or number to the log file.
 */

void log_text(const char *text)
{
    Log.WriteString(strip_ansi(text));
}

void log_number(int num)
{
    Log.WriteInteger(num);
}

/*
 * ---------------------------------------------------------------------------
 * * log_name: write the name, db number, and flags of an object to the log.
 * * If the object does not own itself, append the name, db number, and flags
 * * of the owner.
 */

void log_name(dbref target)
{
#ifdef STANDALONE
    Log.tinyprintf("%s(#%d)", Name(target), target);
#else // STANDALONE
    char *tp;

    if ((mudconf.log_info & LOGOPT_FLAGS) != 0)
        tp = unparse_object((dbref) GOD, target, 0);
    else
        tp = unparse_object_numonly(target);
    Log.WriteString(strip_ansi(tp));
    free_lbuf(tp);
    if (((mudconf.log_info & LOGOPT_OWNER) != 0) &&
        (target != Owner(target))) {
        if ((mudconf.log_info & LOGOPT_FLAGS) != 0)
            tp = unparse_object((dbref) GOD, Owner(target), 0);
        else
            tp = unparse_object_numonly(Owner(target));
        Log.tinyprintf("[%s]", strip_ansi(tp));
        free_lbuf(tp);
    }
#endif // STANDALONE
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * log_name_and_loc: Log both the name and location of an object
 */

void log_name_and_loc(dbref player)
{
    log_name(player);
    if ((mudconf.log_info & LOGOPT_LOC) && Has_location(player)) {
        log_text((char *)" in ");
        log_name(Location(player));
    }
    return;
}

char *OBJTYP(dbref thing)
{
    if (!Good_obj(thing)) {
        return (char *)"??OUT-OF-RANGE??";
    }
    switch (Typeof(thing)) {
    case TYPE_PLAYER:
        return (char *)"PLAYER";
    case TYPE_THING:
        return (char *)"THING";
    case TYPE_ROOM:
        return (char *)"ROOM";
    case TYPE_EXIT:
        return (char *)"EXIT";
    case TYPE_GARBAGE:
        return (char *)"GARBAGE";
    default:
        return (char *)"??ILLEGAL??";
    }
}

void log_type_and_name(dbref thing)
{
    char nbuf[16];

    log_text(OBJTYP(thing));
    sprintf(nbuf, " #%d(", thing);
    log_text(nbuf);
    if (Good_obj(thing))
        log_text(Name(thing));
    log_text((char *)")");
    return;
}

void log_type_and_num(dbref thing)
{
    char nbuf[16];

    log_text(OBJTYP(thing));
    sprintf(nbuf, " #%d", thing);
    log_text(nbuf);
    return;
}

#ifndef STANDALONE
void do_log(dbref player, dbref cause, int key, char *whichlog, char *logtext)
{
    BOOL bValid = TRUE;

    // Strip the filename of all ANSI.
    //
    char *pFilename = strip_ansi(whichlog);

    // Restrict filename to a subdirectory to reduce the possibility
    // of a security hole.
    //
    char *temp_ptr = strrchr(pFilename, '/');
    if (temp_ptr)
    {
        pFilename = ++temp_ptr;
    }
    temp_ptr = strrchr(pFilename, '\\');
    if (temp_ptr)
    {
        pFilename = ++temp_ptr;
    }

    // Check for and disallow leading periods, empty strings
    // and filenames over 30 characters.
    //
    if (  pFilename[0] == '\0'
       || pFilename[0] == '.'
       || strlen(pFilename) > 30)
    {
        bValid = FALSE;
    }

    char *pFullName = 0;
    char *pMessage;
    if (bValid)
    {
        pFullName = alloc_lbuf("do_log_filename");
        sprintf(pFullName, "logs/%s", pFilename);

        // Strip the message of all ANSI.
        //
        pMessage = strip_ansi(logtext);

        // Check for and disallow empty messages.
        //
        if (pMessage[0] == '\0')
        {
            bValid = FALSE;
        }
    }
    if (!bValid)
    {
        if (pFullName) free_lbuf(pFullName);
        notify(player, "Syntax: @log file=message");
        return;
    }

    FILE *hFile = fopen(pFullName, "a");
    if (hFile == NULL)
    {
        notify(player, "Not a valid log file.");
        if (pFullName) free_lbuf(pFullName);
        return;
    }

    // Okay, at this point, the file exists.
    //
    fprintf(hFile, "%s" ENDLINE, pMessage);
    fclose(hFile);                        /* Send and close... */
    free_lbuf(pFullName);
}
#endif // !STANDALONE
