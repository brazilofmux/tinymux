/*! \file log.cpp
 * \brief Logging routines.
 *
 * $Id$
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <sys/types.h>

#include "command.h"

NAMETAB logdata_nametab[] =
{
    {T("flags"),           1,  0,  LOGOPT_FLAGS},
    {T("location"),        1,  0,  LOGOPT_LOC},
    {T("owner"),           1,  0,  LOGOPT_OWNER},
    {T("timestamp"),       1,  0,  LOGOPT_TIMESTAMP},
    {(UTF8 *) NULL,             0,  0,  0}
};

NAMETAB logoptions_nametab[] =
{
    {T("accounting"),      2,  0,  LOG_ACCOUNTING},
    {T("all_commands"),    2,  0,  LOG_ALLCOMMANDS},
    {T("bad_commands"),    2,  0,  LOG_BADCOMMANDS},
    {T("buffer_alloc"),    3,  0,  LOG_ALLOCATE},
    {T("bugs"),            3,  0,  LOG_BUGS},
    {T("checkpoints"),     2,  0,  LOG_DBSAVES},
    {T("config_changes"),  2,  0,  LOG_CONFIGMODS},
    {T("create"),          2,  0,  LOG_PCREATES},
    {T("killing"),         1,  0,  LOG_KILLS},
    {T("logins"),          1,  0,  LOG_LOGIN},
    {T("network"),         1,  0,  LOG_NET},
    {T("problems"),        1,  0,  LOG_PROBLEMS},
    {T("security"),        2,  0,  LOG_SECURITY},
    {T("shouts"),          2,  0,  LOG_SHOUTS},
    {T("startup"),         2,  0,  LOG_STARTUP},
    {T("suspect"),         2,  0,  LOG_SUSPECTCMDS},
    {T("time_usage"),      1,  0,  LOG_TIMEUSE},
    {T("wizard"),          1,  0,  LOG_WIZARD},
    {(UTF8 *) NULL,                     0,  0,  0}
};

/* ---------------------------------------------------------------------------
 * start_log: see if it is OK to log something, and if so, start writing the
 * log entry.
 */

bool start_log(const UTF8 *primary, const UTF8 *secondary)
{
    mudstate.logging++;
    if (  1 <= mudstate.logging
       && mudstate.logging <= 2)
    {
        if (!mudstate.bStandAlone)
        {
            // Format the timestamp.
            //
            UTF8 buffer[256];
            buffer[0] = '\0';
            if (mudconf.log_info & LOGOPT_TIMESTAMP)
            {
                CLinearTimeAbsolute ltaNow;
                ltaNow.GetLocal();
                FIELDEDTIME ft;
                ltaNow.ReturnFields(&ft);
                mux_sprintf(buffer, sizeof(buffer), "%d.%02d%02d:%02d%02d%02d ", ft.iYear,
                    ft.iMonth, ft.iDayOfMonth, ft.iHour, ft.iMinute,
                    ft.iSecond);
            }

            // Write the header to the log.
            //
            if (  secondary
               && *secondary)
            {
                Log.tinyprintf("%s%s %3s/%-5s: ", buffer, mudconf.mud_name,
                    primary, secondary);
            }
            else
            {
                Log.tinyprintf("%s%s %-9s: ", buffer, mudconf.mud_name,
                    primary);
            }
        }

        // If a recursive call, log it and return indicating no log.
        //
        if (mudstate.logging == 1)
        {
            return true;
        }
        Log.WriteString(T("Recursive logging request." ENDLINE));
    }
    mudstate.logging--;
    return false;
}

/* ---------------------------------------------------------------------------
 * end_log: Finish up writing a log entry
 */

void end_log(void)
{
    Log.WriteString((UTF8 *)ENDLINE);
    Log.Flush();
    mudstate.logging--;
}

/* ---------------------------------------------------------------------------
 * log_perror: Write perror message to the log
 */

void log_perror(const UTF8 *primary, const UTF8 *secondary, const UTF8 *extra, const UTF8 *failing_object)
{
    start_log(primary, secondary);
    if (extra && *extra)
    {
        log_text(T("("));
        log_text(extra);
        log_text(T(") "));
    }

    // <Failing_object text>: <strerror() text>
    //
    Log.WriteString(failing_object);
    Log.WriteString(T(": "));
    Log.WriteString(mux_strerror(errno));
#ifndef WIN32
    Log.WriteString((UTF8 *)ENDLINE);
#endif // !WIN32
    Log.Flush();
    mudstate.logging--;
}

/* ---------------------------------------------------------------------------
 * log_text, log_number: Write text or number to the log file.
 */

void log_text(const UTF8 *text)
{
    Log.WriteString(strip_color(text));
}

void log_number(int num)
{
    Log.WriteInteger(num);
}

void DCL_CDECL log_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    UTF8 aTempBuffer[SIZEOF_LOG_BUFFER];
    size_t nString = mux_vsnprintf(aTempBuffer, SIZEOF_LOG_BUFFER, fmt, ap);
    va_end(ap);
    Log.WriteBuffer(nString, aTempBuffer);
}

/* ---------------------------------------------------------------------------
 * log_name: write the name, db number, and flags of an object to the log.
 * If the object does not own itself, append the name, db number, and flags
 * of the owner.
 */

void log_name(dbref target)
{
    if (mudstate.bStandAlone)
    {
        Log.tinyprintf("%s(#%d)", PureName(target), target);
    }
    else
    {
        UTF8 *tp;

        if (mudconf.log_info & LOGOPT_FLAGS)
        {
            tp = unparse_object(GOD, target, false);
        }
        else
        {
            tp = unparse_object_numonly(target);
        }
        Log.WriteString(strip_color(tp));
        free_lbuf(tp);
        if (  (mudconf.log_info & LOGOPT_OWNER)
           && target != Owner(target))
        {
            if (mudconf.log_info & LOGOPT_FLAGS)
            {
                tp = unparse_object(GOD, Owner(target), false);
            }
            else
            {
                tp = unparse_object_numonly(Owner(target));
            }
            Log.tinyprintf("[%s]", strip_color(tp));
            free_lbuf(tp);
        }
    }
}

/* ---------------------------------------------------------------------------
 * log_name_and_loc: Log both the name and location of an object
 */

void log_name_and_loc(dbref player)
{
    log_name(player);
    if (  (mudconf.log_info & LOGOPT_LOC)
       && Has_location(player))
    {
        log_text(T(" in "));
        log_name(Location(player));
    }
    return;
}

static const UTF8 *OBJTYP(dbref thing)
{
    if (!Good_dbref(thing))
    {
        return T("??OUT-OF-RANGE??");
    }
    switch (Typeof(thing))
    {
    case TYPE_PLAYER:
        return T("PLAYER");
    case TYPE_THING:
        return T("THING");
    case TYPE_ROOM:
        return T("ROOM");
    case TYPE_EXIT:
        return T("EXIT");
    case TYPE_GARBAGE:
        return T("GARBAGE");
    default:
        return T("??ILLEGAL??");
    }
}

void log_type_and_name(dbref thing)
{
    Log.tinyprintf("%s #%d(%s)", OBJTYP(thing), thing, Good_obj(thing) ? PureName(thing) : T(""));
    return;
}

void do_log
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    UTF8 *whichlog,
    UTF8 *logtext
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nargs);

    bool bValid = true;

    // Strip the filename of all ANSI.
    //
    UTF8 *pFilename = strip_color(whichlog);

    // Restrict filename to a subdirectory to reduce the possibility
    // of a security hole.
    //
    UTF8 *temp_ptr = (UTF8 *)strrchr((char *)pFilename, '/');
    if (temp_ptr)
    {
        pFilename = ++temp_ptr;
    }
    temp_ptr = (UTF8 *)strrchr((char *)pFilename, '\\');
    if (temp_ptr)
    {
        pFilename = ++temp_ptr;
    }

    // Check for and disallow leading periods, empty strings
    // and filenames over 30 characters.
    //
    size_t n = strlen((char *)pFilename);
    if (  n == 0
       || n > 30)
    {
        bValid = false;
    }
    else
    {
        unsigned int i;
        for (i = 0; i < n; i++)
        {
            if (!mux_isalnum(pFilename[i]))
            {
                bValid = false;
                break;
            }
        }
    }

    UTF8 *pFullName = NULL;
    const UTF8 *pMessage = T("");
    if (bValid)
    {
        pFullName = alloc_lbuf("do_log_filename");
        mux_sprintf(pFullName, LBUF_SIZE, "logs/M-%s.log", pFilename);

        // Strip the message of all ANSI.
        //
        pMessage = strip_color(logtext);

        // Check for and disallow empty messages.
        //
        if (pMessage[0] == '\0')
        {
            bValid = false;
        }
    }

    if (!bValid)
    {
        if (pFullName)
        {
            free_lbuf(pFullName);
        }
        notify(executor, T("Syntax: @log file=message"));
        return;
    }

    FILE *hFile;
    if (mux_fopen(&hFile, pFullName, T("r")))
    {
        fclose(hFile);
        if (mux_fopen(&hFile, pFullName, T("a")))
        {
            // Okay, at this point, the file exists.
            //
            free_lbuf(pFullName);
            fprintf(hFile, "%s" ENDLINE, pMessage);
            fclose(hFile);
            return;
        }
    }
    free_lbuf(pFullName);

    notify(executor, T("Not a valid log file."));
    return;
}
