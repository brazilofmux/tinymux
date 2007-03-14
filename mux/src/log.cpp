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
    {(UTF8 *)"flags",           1,  0,  LOGOPT_FLAGS},
    {(UTF8 *)"location",        1,  0,  LOGOPT_LOC},
    {(UTF8 *)"owner",           1,  0,  LOGOPT_OWNER},
    {(UTF8 *)"timestamp",       1,  0,  LOGOPT_TIMESTAMP},
    {(UTF8 *) NULL,             0,  0,  0}
};

NAMETAB logoptions_nametab[] =
{
    {(UTF8 *)"accounting",      2,  0,  LOG_ACCOUNTING},
    {(UTF8 *)"all_commands",    2,  0,  LOG_ALLCOMMANDS},
    {(UTF8 *)"bad_commands",    2,  0,  LOG_BADCOMMANDS},
    {(UTF8 *)"buffer_alloc",    3,  0,  LOG_ALLOCATE},
    {(UTF8 *)"bugs",            3,  0,  LOG_BUGS},
    {(UTF8 *)"checkpoints",     2,  0,  LOG_DBSAVES},
    {(UTF8 *)"config_changes",  2,  0,  LOG_CONFIGMODS},
    {(UTF8 *)"create",          2,  0,  LOG_PCREATES},
    {(UTF8 *)"killing",         1,  0,  LOG_KILLS},
    {(UTF8 *)"logins",          1,  0,  LOG_LOGIN},
    {(UTF8 *)"network",         1,  0,  LOG_NET},
    {(UTF8 *)"problems",        1,  0,  LOG_PROBLEMS},
    {(UTF8 *)"security",        2,  0,  LOG_SECURITY},
    {(UTF8 *)"shouts",          2,  0,  LOG_SHOUTS},
    {(UTF8 *)"startup",         2,  0,  LOG_STARTUP},
    {(UTF8 *)"suspect",         2,  0,  LOG_SUSPECTCMDS},
    {(UTF8 *)"time_usage",      1,  0,  LOG_TIMEUSE},
    {(UTF8 *)"wizard",          1,  0,  LOG_WIZARD},
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
        Log.WriteString((UTF8 *)"Recursive logging request." ENDLINE);
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
        log_text((UTF8 *)"(");
        log_text(extra);
        log_text((UTF8 *)") ");
    }

    // <Failing_object text>: <strerror() text>
    //
    Log.WriteString(failing_object);
    Log.WriteString((UTF8 *)": ");
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
    Log.WriteString(strip_ansi(text));
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
        Log.WriteString(strip_ansi(tp));
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
            Log.tinyprintf("[%s]", strip_ansi(tp));
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
        log_text((UTF8 *)" in ");
        log_name(Location(player));
    }
    return;
}

static const UTF8 *OBJTYP(dbref thing)
{
    if (!Good_dbref(thing))
    {
        return (UTF8 *)"??OUT-OF-RANGE??";
    }
    switch (Typeof(thing))
    {
    case TYPE_PLAYER:
        return (UTF8 *)"PLAYER";
    case TYPE_THING:
        return (UTF8 *)"THING";
    case TYPE_ROOM:
        return (UTF8 *)"ROOM";
    case TYPE_EXIT:
        return (UTF8 *)"EXIT";
    case TYPE_GARBAGE:
        return (UTF8 *)"GARBAGE";
    default:
        return (UTF8 *)"??ILLEGAL??";
    }
}

void log_type_and_name(dbref thing)
{
    Log.tinyprintf("%s #%d(%s)", OBJTYP(thing), thing, Good_obj(thing) ? PureName(thing) : (UTF8 *)"");
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
    UTF8 *pFilename = strip_ansi(whichlog);

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
    UTF8 *pMessage = (UTF8 *)"";
    if (bValid)
    {
        pFullName = alloc_lbuf("do_log_filename");
        mux_sprintf(pFullName, LBUF_SIZE, "logs/M-%s.log", pFilename);

        // Strip the message of all ANSI.
        //
        pMessage = strip_ansi(logtext);

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
        notify(executor, (UTF8 *)"Syntax: @log file=message");
        return;
    }

    FILE *hFile;
    if (mux_fopen(&hFile, pFullName, (UTF8 *)"r"))
    {
        fclose(hFile);
        if (mux_fopen(&hFile, pFullName, (UTF8 *)"a"))
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

    notify(executor, (UTF8 *)"Not a valid log file.");
    return;
}
