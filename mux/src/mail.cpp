/*! \file mail.cpp
 * \brief In-game \@mail system.
 *
 * This code was taken from Kalkin's DarkZone code, which was
 * originally taken from PennMUSH 1.50 p10, and has been heavily modified
 * since being included in MUX.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "interface.h"

#include "attrs.h"
#include "command.h"
#include "mail.h"
#include "mathutil.h"
#include "powers.h"

const UTF8 *DASH_LINE =
    T("\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93");

const char *MAIL_LINE =
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93"
      "   MAIL: %s   "
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93";

const char *FOLDER_LINE =
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
        "   MAIL: Folder %d   "
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"
      "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93";

#define SIZEOF_MALIAS 13
#define WIDTHOF_MALIASDESC 40
#define SIZEOF_MALIASDESC (WIDTHOF_MALIASDESC*2)

#define MAX_MALIAS_MEMBERSHIP 100
typedef struct malias
{
    int  owner;
    int  numrecep;
    UTF8 *name;
    UTF8 *desc;
    size_t desc_width; // The visual width of the Mail Alias Description.
    dbref list[MAX_MALIAS_MEMBERSHIP];
} malias_t;

static int ma_size = 0;
static int ma_top = 0;

static malias_t **malias   = nullptr;
static MAILBODY *mail_list = nullptr;

// Handling functions for the database of mail messages.
//

// mail_db_grow - We keep a database of mail text, so if we send a
// message to more than one player, we won't have to duplicate the
// text.
//
#define MAIL_FUDGE 1
static void mail_db_grow(int newtop)
{
    if (newtop <= mudstate.mail_db_top)
    {
        return;
    }
    if (mudstate.mail_db_size <= newtop)
    {
        // We need to make the mail bag bigger.
        //
        int newsize = mudstate.mail_db_size + 100;
        if (newtop > newsize)
        {
            newsize = newtop;
        }

        MAILBODY *newdb = (MAILBODY *)MEMALLOC((newsize + MAIL_FUDGE) * sizeof(MAILBODY));
        ISOUTOFMEMORY(newdb);
        if (mail_list)
        {
            mail_list -= MAIL_FUDGE;
            memcpy( newdb,
                    mail_list,
                    (mudstate.mail_db_top + MAIL_FUDGE) * sizeof(MAILBODY));
            MEMFREE(mail_list);
            mail_list = nullptr;
        }
        mail_list = newdb + MAIL_FUDGE;
        newdb = nullptr;
        mudstate.mail_db_size = newsize;
    }

    // Initialize new parts of the mail bag.
    //
    for (int i = mudstate.mail_db_top; i < newtop; i++)
    {
        mail_list[i].m_nRefs = 0;
        mail_list[i].m_nMessage = 0;
        mail_list[i].m_pMessage = nullptr;
    }
    mudstate.mail_db_top = newtop;
}

// MessageReferenceInc - Increments the reference count for any
// particular message.
//
static inline void MessageReferenceInc(int number)
{
    mail_list[number].m_nRefs++;
}

// MessageReferenceCheck - Checks whether the reference count for
// any particular message indicates that the message body should be
// freed. Also checks that if a message pointer is null, that the
// reference count is zero.
//
static void MessageReferenceCheck(int number)
{
    MAILBODY &m = mail_list[number];
    if (m.m_nRefs <= 0)
    {
        if (m.m_pMessage)
        {
            MEMFREE(m.m_pMessage);
            m.m_pMessage = nullptr;
            m.m_nMessage = 0;
        }
    }

    if (m.m_pMessage == nullptr)
    {
        m.m_nRefs = 0;
        m.m_nMessage = 0;
    }
}

// MessageReferenceDec - Decrements the reference count for a message, and
// will also delete the message if the counter reaches 0.
//
static void MessageReferenceDec(int number)
{
    mail_list[number].m_nRefs--;
    MessageReferenceCheck(number);
}

// MessageFetch - returns the text for a particular message number. This
// text should not be modified.
//
const UTF8 *MessageFetch(int number)
{
    MessageReferenceCheck(number);
    if (mail_list[number].m_pMessage)
    {
        return mail_list[number].m_pMessage;
    }
    else
    {
        return T("MAIL: This mail message does not exist in the database. Please alert your admin.");
    }
}

size_t MessageFetchSize(int number)
{
    MessageReferenceCheck(number);
    if (mail_list[number].m_pMessage)
    {
        return mail_list[number].m_nMessage;
    }
    else
    {
        return 0;
    }
}

// This function returns a reference to the message and the the
// reference count is increased to reflect that.
//
static int MessageAdd(UTF8 *pMessage)
{
    int i;
    MAILBODY *pm;
    bool bFound = false;
    for (i = 0; i < mudstate.mail_db_top; i++)
    {
        pm = &mail_list[i];
        if (nullptr == pm->m_pMessage)
        {
            pm->m_nRefs = 0;
            bFound = true;
            break;
        }
    }

    if (!bFound)
    {
        mail_db_grow(i + 1);
    }

    pm = &mail_list[i];
    pm->m_nMessage = strlen((char *)pMessage);
    pm->m_pMessage = StringCloneLen(pMessage, pm->m_nMessage);
    MessageReferenceInc(i);
    return i;
}

// add_mail_message - adds a new text message to the mail database, and returns
// a unique number for that message.
//
// IF return value is !NOTHING, you have a reference to the message,
// and the reference count reflects that.
//
static int add_mail_message(dbref player, UTF8 *message)
{
    if (!mux_stricmp(message, T("clear")))
    {
        // Using \230 instead \x98 because \x98c is making gcc choke.
        //
        raw_notify(player, T("MAIL: You probably did not intend to send a @mail saying \xE2\x80\230clear\xE2\x80\x99."));
        return NOTHING;
    }

    // Evaluate signature.
    //
    int   aflags;
    dbref aowner;
    UTF8 *bp = alloc_lbuf("add_mail_message");
    UTF8 *atrstr = atr_get("add_mail_message.216", player, A_SIGNATURE, &aowner, &aflags);
    UTF8 *execstr = bp;
    mux_exec(atrstr, LBUF_SIZE-1, execstr, &bp, player, player, player,
         AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL),
         nullptr, 0);
    *bp = '\0';

    // Save message body and return a reference to it.
    //
    int number = MessageAdd(tprintf(T("%s %s"), message, execstr));
    free_lbuf(atrstr);
    free_lbuf(execstr);
    return number;
}

// This function is -only- used from reading from the disk, and so
// it does -not- manage the reference counts.
//
static bool MessageAddWithNumber(int i, UTF8 *pMessage)
{
    mail_db_grow(i+1);

    MAILBODY *pm = &mail_list[i];
    pm->m_nMessage = strlen((char *)pMessage);
    pm->m_pMessage = StringCloneLen(pMessage, pm->m_nMessage);
    return true;
}

// new_mail_message - used for reading messages in from disk which
// already have a number assigned to them.
//
// This function is -only- used from reading from the disk, and so
// it does -not- manage the reference counts.
//
static void new_mail_message(UTF8 *message, int number)
{
    bool bTruncated = false;
    if (strlen((char *)message) > LBUF_SIZE-1)
    {
        bTruncated = true;
        message[LBUF_SIZE-1] = '\0';
    }
    MessageAddWithNumber(number, message);
    if (bTruncated)
    {
        STARTLOG(LOG_BUGS, "BUG", "MAIL");
        log_printf(T("new_mail_message: Mail message %d truncated."), number);
        ENDLOG;
    }
}

/*-------------------------------------------------------------------------*
 *   User mail functions (these are called from game.c)
 *
 * do_mail - cases
 * do_mail_read - read messages
 * do_mail_list - list messages
 * do_mail_flags - tagging, untagging, clearing, unclearing of messages
 * do_mail_file - files messages into a new folder
 * do_mail_fwd - forward messages to another player(s)
 * do_mail_reply - reply to a message
 * do_mail_count - count messages
 * do_mail_purge - purge cleared messages
 * do_mail_change_folder - change current folder
 *-------------------------------------------------------------------------*/

static void set_player_folder(dbref player, int fnum)
{
    // Set a player's folder to fnum.
    //
    UTF8 *tbuf1 = alloc_sbuf("set_player_folder");
    mux_ltoa(fnum, tbuf1);
    ATTR *a = atr_num(A_MAILCURF);
    if (a)
    {
        atr_add(player, A_MAILCURF, tbuf1, GOD, a->flags);
    }
    else
    {
        // Shouldn't happen, but...
        //
        atr_add(player, A_MAILCURF, tbuf1, GOD, AF_ODARK | AF_WIZARD | AF_NOPROG | AF_LOCK);
    }
    free_sbuf(tbuf1);
}

static void add_folder_name(dbref player, int fld, UTF8 *name)
{
    // Fetch current list of folders
    //
    int aflags;
    size_t nFolders;
    dbref aowner;
    UTF8 *aFolders = alloc_lbuf("add_folder_name.str");
    atr_get_str_LEN(aFolders, player, A_MAILFOLDERS, &aowner, &aflags,
        &nFolders);

    // Build new record ("%d:%s:%d", fld, uppercase(name), fld) upper-casing
    // the provided folder name.
    //
    mux_string *sRecord = new mux_string;
    sRecord->append((long)fld);
    sRecord->append(T(":"));
    sRecord->append(name);
    sRecord->append(T(":"));
    sRecord->append((long)fld);
    sRecord->UpperCase();

    UTF8 *aNew = alloc_lbuf("add_folder_name.new");
    sRecord->export_TextPlain(aNew);
    delete sRecord;
    size_t nNew = strlen((char *)aNew);

    UTF8 *p, *q;
    if (0 != nFolders)
    {
        // Build pattern ("%d:", fld)
        //
        UTF8 *aPattern = alloc_lbuf("add_folder_name.pat");
        q = aPattern;
        q += mux_ltoa(fld, q);
        safe_chr(':', aPattern, &q);
        *q = '\0';
        size_t nPattern = q - aPattern;

        BMH_State bmhs;
        BMH_Prepare(&bmhs, nPattern, aPattern);
        for (;;)
        {
            size_t i;
            if (!BMH_Execute(&bmhs, &i, nPattern, aPattern, nFolders, aFolders))
            {
                break;
            }

            // Remove old record.
            //
            q = aFolders + i;
            p = q + nPattern;

            // Eat leading spaces.
            //
            while (  aFolders < q
                  && mux_isspace(q[-1]))
            {
                q--;
            }

            // Skip past old record and trailing spaces.
            //
            while (  *p
                  && *p != ':')
            {
                p++;
            }
            while (  *p
                  && !mux_isspace(*p))
            {
                p++;
            }
            while (mux_isspace(*p))
            {
                p++;
            }

            if (q != aFolders)
            {
                *q++ = ' ';
            }
            while (*p)
            {
                safe_chr(*p, aFolders, &q);
                p++;
            }
            *q = '\0';
            nFolders = q - aFolders;
        }
        free_lbuf(aPattern);
    }

    if (nFolders + 1 + nNew < LBUF_SIZE)
    {
        // It will fit. Append new record.
        //
        q = aFolders + nFolders;
        if (nFolders)
        {
            *q++ = ' ';
        }
        memcpy(q, aNew, nNew);
        q += nNew;
        *q = '\0';

        atr_add(player, A_MAILFOLDERS, aFolders, player,
            AF_MDARK | AF_WIZARD | AF_NOPROG | AF_LOCK);
    }
    free_lbuf(aFolders);
    free_lbuf(aNew);
}

static UTF8 *get_folder_name(dbref player, int fld)
{
    // Get the name of the folder, or return "unnamed".
    //
    int aflags;
    size_t nFolders;
    dbref aowner;
    static UTF8 aFolders[LBUF_SIZE];
    atr_get_str_LEN(aFolders, player, A_MAILFOLDERS, &aowner, &aflags,
        &nFolders);
    UTF8 *p;
    if (nFolders != 0)
    {
        UTF8 *aPattern = alloc_lbuf("get_folder_name");
        p = aPattern;
        p += mux_ltoa(fld, p);
        *p++ = ':';
        *p = '\0';
        size_t nPattern = p - aPattern;

        size_t i;
        bool bSucceeded = BMH_StringSearch(&i, nPattern, aPattern, nFolders, aFolders);
        free_lbuf(aPattern);

        if (bSucceeded)
        {
            p = aFolders + i + nPattern;
            UTF8 *q = p;
            while (  *q
                  && *q != ':')
            {
                q++;
            }
            *q = '\0';
            return p;
        }
    }
    p = (UTF8 *)"unnamed";
    return p;
}

static int get_folder_number(dbref player, UTF8 *name)
{
    // Look up a folder name and return the corresponding folder number.
    //
    int aflags;
    size_t nFolders;
    dbref aowner;
    UTF8 *aFolders = alloc_lbuf("get_folder_num_str");
    atr_get_str_LEN(aFolders, player, A_MAILFOLDERS, &aowner, &aflags,
        &nFolders);
    if (nFolders != 0)
    {
        // Convert the folder name provided into upper-case characters.
        //
        mux_string *sRecord = new mux_string;
        sRecord->append(T(":"));
        sRecord->append(name);
        sRecord->append(T(":"));
        sRecord->UpperCase();

        UTF8 *aPattern = alloc_lbuf("add_folder_num_pat");
        sRecord->export_TextPlain(aPattern);
        delete sRecord;
        size_t nPattern = strlen((char *)aPattern);

        size_t i;
        bool bSucceeded = BMH_StringSearch(&i, nPattern, aPattern, nFolders, aFolders);
        free_lbuf(aPattern);

        UTF8 *p, *q;
        if (bSucceeded)
        {
            p = aFolders + i + nPattern;
            q = p;
            while (  *q
                  && !mux_isspace(*q))
            {
                q++;
            }
            *q = '\0';

            // A bug in TinyMUX 2.7 (Jan 22, 2008 through Feb 1, 2009)
            // generated a leading '#' in folder numbers.  The following
            // workaround can eventually be removed.
            //
            if ('#' == *p)
            {
                p++;
            }

            int iFolderNumber = mux_atol(p);
            free_lbuf(aFolders);
            return iFolderNumber;
        }
    }
    free_lbuf(aFolders);
    return -1;
}

static int parse_folder(dbref player, UTF8 *folder_string)
{
    // Given a string, return a folder #, or -1.
    //
    if (  !folder_string
       || !*folder_string)
    {
        return -1;
    }
    if (mux_isdigit(*folder_string))
    {
        int fnum = mux_atol(folder_string);
        if (  fnum < 0
           || fnum > MAX_FOLDERS)
        {
            return -1;
        }
        else
        {
            return fnum;
        }
    }

    // Handle named folders here
    //
    return get_folder_number(player, folder_string);
}

#define MAIL_INVALID_RANGE  0
#define MAIL_INVALID_NUMBER 1
#define MAIL_INVALID_AGE    2
#define MAIL_INVALID_DBREF  3
#define MAIL_INVALID_PLAYER 4
#define MAIL_INVALID_SPEC   5
#define MAIL_INVALID_PLAYER_OR_USING_MALIAS 6

static const UTF8 *mailmsg[] =
{
    T("MAIL: Invalid message range"),
    T("MAIL: Invalid message number"),
    T("MAIL: Invalid age"),
    T("MAIL: Invalid dbref #"),
    T("MAIL: Invalid player"),
    T("MAIL: Invalid message specification"),
    T("MAIL: Invalid player or trying to send @mail to a @malias without a subject"),
};

static bool parse_msglist(UTF8 *msglist, struct mail_selector *ms, dbref player)
{
    // Take a message list, and return the appropriate mail_selector setup.
    // For now, msglists are quite restricted. That'll change once all this
    // is working. Returns 0 if couldn't parse, and also notifies the player
    // why.

    // Initialize the mail selector - this matches all messages.
    //
    ms->low = 0;
    ms->high = 0;
    ms->flags = 0x0FFF | M_MSUNREAD;
    ms->player = 0;
    ms->days = -1;
    ms->day_comp = 0;

    // Now, parse the message list.
    //
    if (!msglist || !*msglist)
    {
        // All messages
        //
        return true;
    }

    UTF8 *p = msglist;
    while (mux_isspace(*p))
    {
        p++;
    }

    if (*p == '\0')
    {
        return true;
    }

    if (mux_isdigit(*p))
    {
        // Message or range.
        //
        UTF8 *q = (UTF8 *)strchr((char *)p, '-');
        if (q)
        {
            // We have a subrange, split it up and test to see if it is valid.
            //
            q++;
            ms->low = mux_atol(p);
            if (ms->low <= 0)
            {
                raw_notify(player, mailmsg[MAIL_INVALID_RANGE]);
                return false;
            }
            if (*q == '\0')
            {
                // Unbounded range.
                //
                ms->high = 0;
            }
            else
            {
                ms->high = mux_atol(q);
                if (ms->low > ms->high)
                {
                    raw_notify(player, mailmsg[MAIL_INVALID_RANGE]);
                    return false;
                }
            }
        }
        else
        {
            // A single message.
            //
            ms->low = ms->high = mux_atol(p);
            if (ms->low <= 0)
            {
                raw_notify(player, mailmsg[MAIL_INVALID_NUMBER]);
                return false;
            }
        }
    }
    else
    {
        switch (*p)
        {
        case '-':

            // Range with no start.
            //
            p++;
            if (*p == '\0')
            {
                raw_notify(player, mailmsg[MAIL_INVALID_RANGE]);
                return false;
            }
            ms->high = mux_atol(p);
            if (ms->high <= 0)
            {
                raw_notify(player, mailmsg[MAIL_INVALID_RANGE]);
                return false;
            }
            break;

        case '~':

            // Exact # of days old.
            //
            p++;
            if (*p == '\0')
            {
                raw_notify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            ms->day_comp = 0;
            ms->days = mux_atol(p);
            if (ms->days < 0)
            {
                raw_notify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            break;

        case '<':

            // Less than # of days old.
            //
            p++;
            if (*p == '\0')
            {
                raw_notify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            ms->day_comp = -1;
            ms->days = mux_atol(p);
            if (ms->days < 0)
            {
                raw_notify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            break;

        case '>':

            // Greater than # of days old.
            //
            p++;
            if (*p == '\0')
            {
                raw_notify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            ms->day_comp = 1;
            ms->days = mux_atol(p);
            if (ms->days < 0)
            {
                raw_notify(player, mailmsg[MAIL_INVALID_AGE]);
                return false;
            }
            break;

        case '#':

            // From db#.
            //
            p++;
            if (*p == '\0')
            {
                raw_notify(player, mailmsg[MAIL_INVALID_DBREF]);
                return false;
            }
            ms->player = mux_atol(p);
            if (!Good_obj(ms->player) || !(ms->player))
            {
                raw_notify(player, mailmsg[MAIL_INVALID_DBREF]);
                return false;
            }
            break;

        case '*':

            // From player name.
            //
            p++;
            if (*p == '\0')
            {
                raw_notify(player, mailmsg[MAIL_INVALID_PLAYER]);
                return false;
            }
            ms->player = lookup_player(player, p, true);
            if (ms->player == NOTHING)
            {
                raw_notify(player, mailmsg[MAIL_INVALID_PLAYER_OR_USING_MALIAS]);
                return false;
            }
            break;

        case 'a':
        case 'A':

            // All messages, all folders
            //
            p++;
            switch (*p)
            {
            case '\0':
                raw_notify(player, T("MAIL: A isn\xE2\x80\x99t enough (all?)"));
                return false;

            case 'l':
            case 'L':

                // All messages, all folders
                //
                p++;
                switch (*p)
                {
                case '\0':
                    raw_notify(player, T("MAIL: AL isn\xE2\x80\x99t enough (all?)"));
                    return false;

                case 'l':
                case 'L':

                    // All messages, all folders
                    //
                    p++;
                    if (*p == '\0')
                    {
                        ms->flags = M_ALL;
                    }
                    else
                    {
                        raw_notify(player, mailmsg[MAIL_INVALID_SPEC]);
                        return false;
                    }
                    break;

                default:

                    // Bad
                    //
                    raw_notify(player, mailmsg[MAIL_INVALID_SPEC]);
                    return false;
                }
                break;

            default:

                // Bad
                //
                raw_notify(player, mailmsg[MAIL_INVALID_SPEC]);
                return false;
            }
            break;

        case 'u':
        case 'U':

            // Urgent, Unread
            //
            p++;
            if (*p == '\0')
            {
                raw_notify(player, T("MAIL: U is ambiguous (urgent or unread?)"));
                return false;
            }

            switch (*p)
            {
            case 'r':
            case 'R':

                // Urgent
                //
                ms->flags = M_URGENT;
                break;

            case 'n':
            case 'N':

                // Unread
                //
                ms->flags = M_MSUNREAD;
                break;

            default:

                // Bad
                //
                raw_notify(player, mailmsg[MAIL_INVALID_SPEC]);
                return false;
            }
            break;

        case 'r':
        case 'R':

            // Read
            //
            ms->flags = M_ISREAD;
            break;

        case 'c':
        case 'C':

            // Cleared.
            //
            ms->flags = M_CLEARED;
            break;

        case 't':
        case 'T':

            // Tagged.
            //
            ms->flags = M_TAG;
            break;

        case 'm':
        case 'M':

            // Mass, me.
            //
            p++;
            if (*p == '\0')
            {
                raw_notify(player, T("MAIL: M is ambiguous (mass or me?)"));
                return false;
            }

            switch (*p)
            {
            case 'a':
            case 'A':

                ms->flags = M_MASS;
                break;

            case 'e':
            case 'E':

                ms->player = player;
                break;

            default:

                raw_notify(player, mailmsg[MAIL_INVALID_SPEC]);
                return false;
            }
            break;

        default:

            // Bad news.
            //
            raw_notify(player, mailmsg[MAIL_INVALID_SPEC]);
            return false;
        }
    }
    return true;
}

static int player_folder(dbref player)
{
    // Return the player's current folder number. If they don't have one, set
    // it to 0.
    //
    int flags;
    UTF8 *atrstr = atr_pget(player, A_MAILCURF, &player, &flags);
    if (!*atrstr)
    {
        free_lbuf(atrstr);
        set_player_folder(player, 0);
        return 0;
    }
    int number = mux_atol(atrstr);
    free_lbuf(atrstr);
    return number;
}


// List mail stats for all current folders
//
static void DoListMailBrief(dbref player)
{
    for(int folder = 0; folder < MAX_FOLDERS; folder++)
    {
        check_mail(player, folder, true);
    }

    int current_folder = player_folder(player);

    raw_notify(player, tprintf(T("MAIL: Current folder is %d [%s]."),
                current_folder, get_folder_name(player, current_folder)));
}


// Change or rename a folder
//
static void do_mail_change_folder(dbref player, UTF8 *fld, UTF8 *newname)
{
    int pfld;

    if (!fld || !*fld)
    {
        // Check mail in all folders
        //
        DoListMailBrief(player);
        return;
    }

    pfld = parse_folder(player, fld);
    if (pfld < 0)
    {
        raw_notify(player, T("MAIL: What folder is that?"));
        return;
    }
    if (newname && *newname)
    {
        // We're changing a folder name here
        //
        if (strlen((char *)newname) > FOLDER_NAME_LEN)
        {
            raw_notify(player, T("MAIL: Folder name too long"));
            return;
        }
        UTF8 *p;
        for (p = newname; mux_isalnum(*p); p++) ;
        if (*p != '\0')
        {
            raw_notify(player, T("MAIL: Illegal folder name"));
            return;
        }

        add_folder_name(player, pfld, newname);
        raw_notify(player, tprintf(T("MAIL: Folder %d now named \xE2\x80\x98%s\xE2\x80\x99"), pfld,
                    newname));
    }
    else
    {
        // Set a new folder
        //
        set_player_folder(player, pfld);
        raw_notify(player, tprintf(T("MAIL: Current folder set to %d [%s]."),
                       pfld, get_folder_name(player, pfld)));
    }
}

static int sign(int x)
{
    if (x == 0)
    {
        return 0;
    }
    else if (x < 0)
    {
        return -1;
    }
    else
    {
        return 1;
    }
}

static bool mail_match(struct mail *mp, struct mail_selector ms, int num)
{
    // Does a piece of mail match the mail_selector?
    //
    if (ms.low && num < ms.low)
    {
        return false;
    }
    if (ms.high && ms.high < num)
    {
        return false;
    }
    if (ms.player && mp->from != ms.player)
    {
        return false;
    }

    mail_flag mpflag = Read(mp)
        ? (mp->read | M_ALL)
        : (mp->read | M_ALL | M_MSUNREAD);

    if ((ms.flags & mpflag) == 0)
    {
        return false;
    }

    if (ms.days == -1)
    {
        return true;
    }

    // Get the time now, subtract mp->time, and compare the results with
    // ms.days (in manner of ms.day_comp)
    //
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();

    const UTF8 *pMailTimeStr = mp->time;

    CLinearTimeAbsolute ltaMail;
    if (ltaMail.SetString(pMailTimeStr))
    {
        CLinearTimeDelta ltd(ltaMail, ltaNow);
        int iDiffDays = ltd.ReturnDays();
        if (sign(iDiffDays - ms.days) == ms.day_comp)
        {
            return true;
        }
    }
    return false;
}

// Adjust the flags of a set of messages.
// If negate is true, clear the flag.
static void do_mail_flags(dbref player, UTF8 *msglist, mail_flag flag, bool negate)
{
    struct mail_selector ms;

    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }
    int i = 0, j = 0;
    int folder = player_folder(player);

    MailList ml(player);
    struct mail *mp;
    for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
    {
        if (  All(ms)
           || Folder(mp) == folder)
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                j++;
                if (negate)
                {
                    mp->read &= ~flag;
                }
                else
                {
                    mp->read |= flag;
                }

                switch (flag)
                {
                case M_TAG:
                    raw_notify(player, tprintf(T("MAIL: Msg #%d %s."), i,
                                negate ? "untagged" : "tagged"));
                    break;

                case M_CLEARED:
                    if (Unread(mp) && !negate)
                    {
                        raw_notify(player, tprintf(T("MAIL: Unread Msg #%d cleared! Use @mail/unclear %d to recover."), i, i));
                    }
                    else
                    {
                        raw_notify(player, tprintf(T("MAIL: Msg #%d %s."), i, negate ? "uncleared" : "cleared"));
                    }
                    break;

                case M_SAFE:
                    raw_notify(player, tprintf(T("MAIL: Msg #%d marked safe."), i));
                    break;
                }
            }
        }
    }

    if (!j)
    {
        // Ran off the end of the list without finding anything.
        //
        raw_notify(player, T("MAIL: You don\xE2\x80\x99t have any matching messages!"));
    }
}

static void do_mail_tag(dbref player, UTF8 *msglist)
{
    do_mail_flags(player, msglist, M_TAG, false);
}

static void do_mail_safe(dbref player, UTF8 *msglist)
{
    do_mail_flags(player, msglist, M_SAFE, false);
}

void do_mail_clear(dbref player, UTF8 *msglist)
{
    do_mail_flags(player, msglist, M_CLEARED, false);
}

static void do_mail_untag(dbref player, UTF8 *msglist)
{
    do_mail_flags(player, msglist, M_TAG, true);
}

static void do_mail_unclear(dbref player, UTF8 *msglist)
{
    do_mail_flags(player, msglist, M_CLEARED, true);
}

// Change a message's folder.
//
static void do_mail_file(dbref player, UTF8 *msglist, UTF8 *folder)
{
    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }
    int foldernum;
    if ((foldernum = parse_folder(player, folder)) == -1)
    {
        raw_notify(player, T("MAIL: Invalid folder specification"));
        return;
    }
    int i = 0, j = 0;
    int origfold = player_folder(player);

    MailList ml(player);
    struct mail *mp;
    for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
    {
        if (  All(ms)
           || (Folder(mp) == origfold))
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                j++;

                // Clear the folder.
                //
                mp->read &= M_FMASK;
                mp->read |= FolderBit(foldernum);
                raw_notify(player, tprintf(T("MAIL: Msg %d filed in folder %d"), i,
                            foldernum));
            }
        }
    }

    if (!j)
    {
        // Ran off the end of the list without finding anything.
        //
        raw_notify(player, T("MAIL: You don\xE2\x80\x99t have any matching messages!"));
    }
}

// A mail alias can be any combination of upper-case letters, lower-case
// letters, and digits. No leading digits. No symbols. No ANSI. Length is
// limited to SIZEOF_MALIAS-1. Case is preserved.
//
UTF8 *MakeCanonicalMailAlias
(
    UTF8   *pMailAlias,
    size_t *pnValidMailAlias,
    bool   *pbValidMailAlias
)
{
    static UTF8 Buffer[SIZEOF_MALIAS];
    size_t nLeft = sizeof(Buffer)-1;
    UTF8 *q = Buffer;
    UTF8 *p = pMailAlias;

    if (  !p
       || !mux_isalpha(*p))
    {
        *pnValidMailAlias = 0;
        *pbValidMailAlias = false;
        return nullptr;
    }
    *q++ = *p++;
    nLeft--;

    while (  *p
          && nLeft)
    {
        if (  !mux_isalpha(*p)
           && !mux_isdigit(*p)
           && *p != '_')
        {
            break;
        }
        *q++ = *p++;
        nLeft--;
    }
    *q = '\0';

    *pnValidMailAlias = q - Buffer;
    *pbValidMailAlias = true;
    return Buffer;
}

#define GMA_NOTFOUND    1
#define GMA_FOUND       2
#define GMA_INVALIDFORM 3

static malias_t *get_malias(dbref player, UTF8 *alias, int *pnResult)
{
    *pnResult = GMA_INVALIDFORM;
    if (!alias)
    {
        return nullptr;
    }
    if (alias[0] == '#')
    {
        if (ExpMail(player))
        {
            int x = mux_atol(alias + 1);
            if (x < 0 || x >= ma_top)
            {
                *pnResult = GMA_NOTFOUND;
                return nullptr;
            }
            *pnResult = GMA_FOUND;
            return malias[x];
        }
    }
    else if (alias[0] == '*')
    {
        size_t nValidMailAlias;
        bool   bValidMailAlias;
        UTF8 *pValidMailAlias = MakeCanonicalMailAlias
                                (   alias+1,
                                    &nValidMailAlias,
                                    &bValidMailAlias
                                );

        if (bValidMailAlias)
        {
            for (int i = 0; i < ma_top; i++)
            {
                malias_t *m = malias[i];
                if (  m->owner == player
                   || m->owner == GOD
                   || ExpMail(player))
                {
                    if (!strcmp((char *)pValidMailAlias, (char *)m->name))
                    {
                        // Found it!
                        //
                        *pnResult = GMA_FOUND;
                        return m;
                    }
                }
            }
            *pnResult = GMA_NOTFOUND;
        }
    }
    if (*pnResult == GMA_INVALIDFORM)
    {
        if (ExpMail(player))
        {
            raw_notify(player, T("MAIL: Mail aliases must be of the form *<name> or #<num>."));
        }
        else
        {
            raw_notify(player, T("MAIL: Mail aliases must be of the form *<name>."));
        }
    }
    return nullptr;
}

static UTF8 *make_namelist(dbref player, UTF8 *arg)
{
    UNUSED_PARAMETER(player);

    UTF8 *p;
    UTF8 *oldarg = alloc_lbuf("make_namelist.oldarg");
    UTF8 *names = alloc_lbuf("make_namelist.names");
    UTF8 *bp = names;

    mux_strncpy(oldarg, arg, LBUF_SIZE-1);

    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, oldarg);
    mux_strtok_ctl(&tts, T(" "));
    bool bFirst = true;
    for (p = mux_strtok_parse(&tts); p; p = mux_strtok_parse(&tts))
    {
        if (!bFirst)
        {
            safe_str(T(", "), names, &bp);
        }
        bFirst = false;

        if (  mux_isdigit(p[0])
           || (  p[0] == '!'
              && mux_isdigit(p[1])))
        {
            UTF8 ch = p[0];
            if (ch == '!')
            {
                p++;
            }
            dbref target = mux_atol(p);
            if (  Good_obj(target)
               && isPlayer(target))
            {
                if (ch == '!')
                {
                    safe_chr('!', names, &bp);
                }
                safe_str(Moniker(target), names, &bp);
            }
        }
        else
        {
            safe_str(p, names, &bp);
        }
    }
    *bp = '\0';
    free_lbuf(oldarg);
    return names;
}

#define NUM_MAILSTATUSTABLE 7
static struct tag_mailstatusentry
{
    int nMask;
    const UTF8 *pYes;
    int   nYes;
    const UTF8 *pNo;
    int   nNo;
}
aMailStatusTable[NUM_MAILSTATUSTABLE] =
{
    { M_ISREAD,  T("Read"),    4, T("Unread"), 6 },
    { M_CLEARED, T("Cleared"), 7,  0,       0 },
    { M_URGENT,  T("Urgent"),  6,  0,       0 },
    { M_MASS,    T("Mass"),    4,  0,       0 },
    { M_FORWARD, T("Fwd"),     3,  0,       0 },
    { M_TAG,     T("Tagged"),  6,  0,       0 },
    { M_SAFE,    T("Safe"),    4,  0,       0 }
};

static UTF8 *status_string(struct mail *mp)
{
    // Return a longer description of message flags.
    //
    UTF8 *tbuf1 = alloc_lbuf("status_string");
    UTF8 *p = tbuf1;
    struct tag_mailstatusentry *mse = aMailStatusTable;
    for (int i = 0; i < NUM_MAILSTATUSTABLE; i++, mse++)
    {
        if (mp->read & mse->nMask)
        {
            if (p != tbuf1) *p++ = ' ';
            memcpy(p, mse->pYes, mse->nYes);
            p += mse->nYes;
        }
        else if (mse->pNo)
        {
            if (p != tbuf1) *p++ = ' ';
            memcpy(p, mse->pNo, mse->nNo);
            p += mse->nNo;
        }
    }
    *p++ = '\0';
    return tbuf1;
}

static void do_mail_read(dbref player, UTF8 *arg1, UTF8 *arg2)
{
    UTF8 *msglist;
    int folder = player_folder(player);
    int original_folder = folder;

    // Check the argument list, if arg2 is present and valid, then lookup
    // mail in the arg1 folder rather than the default folder.
    //
    if (  nullptr == arg2
       || '\0' == arg2[0])
    {
        msglist = arg1;
    }
    else
    {
        folder = parse_folder(player, arg1);

        if (-1 == folder)
        {
            raw_notify(player, T("MAIL: No such folder."));
            return;
        }
        set_player_folder(player, folder);
        msglist = arg2;
    }

    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }

    UTF8 *status, *names;
    int i = 0, j = 0;
    UTF8 *buff = alloc_lbuf("do_mail_read.1");

    MailList ml(player);
    struct mail *mp;
    for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
    {
        if (Folder(mp) == folder)
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                // Read it.
                //
                j++;

                UTF8 *bp = buff;
                safe_str(MessageFetch(mp->number), buff, &bp);
                *bp = '\0';

                raw_notify(player, (UTF8 *)DASH_LINE);
                status = status_string(mp);
                names = make_namelist(player, mp->tolist);

                UTF8 szFromName[MBUF_SIZE];
                trimmed_name(mp->from, szFromName, 16, 16, 0);

                UTF8 szSubjectBuffer[MBUF_SIZE];
                StripTabsAndTruncate(mp->subject, szSubjectBuffer, MBUF_SIZE-1, 65);

                raw_notify(player, tprintf(T("%-3d         From:  %s  At: %-25s  %s\r\nFldr   : %-2d Status: %s\r\nTo     : %-65s\r\nSubject: %s"),
                               i, szFromName,
                               mp->time,
                               (Connected(mp->from) &&
                               (!Hidden(mp->from) || See_Hidden(player))) ?
                               " (Conn)" : "      ", folder,
                               status,
                               names,
                               szSubjectBuffer));
                free_lbuf(names);
                free_lbuf(status);
                raw_notify(player, (UTF8 *)DASH_LINE);
                raw_notify(player, buff);
                raw_notify(player, (UTF8 *)DASH_LINE);
                if (Unread(mp))
                {
                    // Mark message as read.
                    //
                    mp->read |= M_ISREAD;
                }
            }
        }
    }
    free_lbuf(buff);

    // If the folder was changed, restore the original folder setting
    //
    if (folder != original_folder)
    {
        set_player_folder(player, original_folder);
    }

    if (!j)
    {
        // Ran off the end of the list without finding anything.
        //
        raw_notify(player,
                T("MAIL: You don\xE2\x80\x99t have that many matching messages!"));
    }
}

static UTF8 *status_chars(struct mail *mp)
{
    // Return a short description of message flags.
    //
    static UTF8 res[10];

    UTF8 *p = res;
    *p++ = Read(mp)     ? '-' : 'N';
    *p++ = M_Safe(mp)   ? 'S' : '-';
    *p++ = Cleared(mp)  ? 'C' : '-';
    *p++ = Urgent(mp)   ? 'U' : '-';
    *p++ = Mass(mp)     ? 'M' : '-';
    *p++ = Forward(mp)  ? 'F' : '-';
    *p++ = Tagged(mp)   ? '+' : '-';
    *p = '\0';
    return res;
}

static void do_mail_review(dbref player, UTF8 *name, UTF8 *msglist)
{
    dbref target = lookup_player(player, name, true);
    if (target == NOTHING)
    {
        raw_notify(player, T("MAIL: No such player."));
        return;
    }

    struct mail *mp;
    struct mail_selector ms;
    int i = 0, j = 0;
    UTF8 szSubjectBuffer[MBUF_SIZE];
    UTF8 szFromName[MBUF_SIZE];

    if (  !msglist
       || !*msglist)
    {
        trimmed_name(target, szFromName, 25, 25, 0);
        raw_notify(player, tprintf(T(MAIL_LINE), szFromName));
        MailList ml(target);
        for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
        {
            if (mp->from == player)
            {
                i++;

                trimmed_name(mp->from, szFromName, 16, 16, 0);

                StripTabsAndTruncate(mp->subject, szSubjectBuffer, MBUF_SIZE-1, 25);
                size_t nSize = MessageFetchSize(mp->number);
                raw_notify(player, tprintf(T("[%s] %-3d (%4d) From: %s Sub: %s"),
                               status_chars(mp),
                               i, nSize,
                               szFromName,
                               szSubjectBuffer));
            }
        }
        raw_notify(player, (UTF8 *)DASH_LINE);
    }
    else
    {
        if (!parse_msglist(msglist, &ms, target))
        {
            return;
        }
        MailList ml(target);
        for (mp = ml.FirstItem(); !ml.IsEnd() && !alarm_clock.alarmed; mp = ml.NextItem())
        {
            if (mp->from == player)
            {
                i++;
                if (mail_match(mp, ms, i))
                {
                    j++;
                    UTF8 *status = status_string(mp);
                    const UTF8 *str = MessageFetch(mp->number);

                    trimmed_name(mp->from, szFromName, 16, 16, 0);

                    StripTabsAndTruncate(mp->subject, szSubjectBuffer, MBUF_SIZE-1, 65);

                    raw_notify(player, (UTF8 *)DASH_LINE);
                    raw_notify(player, tprintf(T("%-3d         From:  %s  At: %-25s  %s\r\nFldr   : %-2d Status: %s\r\nSubject: %s"),
                                   i, szFromName,
                                   mp->time,
                                   (Connected(mp->from) &&
                                   (!Hidden(mp->from) || See_Hidden(player))) ?
                                   " (Conn)" : "      ", 0,
                                   status, szSubjectBuffer));
                    free_lbuf(status);
                    raw_notify(player, (UTF8 *)DASH_LINE);
                    raw_notify(player, str);
                    raw_notify(player, (UTF8 *)DASH_LINE);
                }
            }
        }

        if (!j)
        {
            // Ran off the end of the list without finding anything.
            //
            raw_notify(player,
                    T("MAIL: You don\xE2\x80\x99t have that many matching messages!"));
        }
    }
}

static UTF8 *mail_list_time(const UTF8 *the_time)
{
    const UTF8 *p = the_time;
    UTF8 *new0 = alloc_lbuf("mail_list_time");
    UTF8 *q = new0;
    if (!p || !*p)
    {
        *new0 = '\0';
        return new0;
    }

    // Format of the_time is: day mon dd hh:mm:ss yyyy
    // Chop out :ss
    //
    int i;
    for (i = 0; i < 16; i++)
    {
        if (*p)
        {
            *q++ = *p++;
        }
    }

    for (i = 0; i < 3; i++)
    {
        if (*p)
        {
            p++;
        }
    }

    for (i = 0; i < 5; i++)
    {
        if (*p)
        {
            *q++ = *p++;
        }
    }

    *q = '\0';
    return new0;
}

static void do_mail_list(dbref player, UTF8 *arg1, UTF8 *arg2, bool sub)
{
    UTF8 *msglist;
    int folder = player_folder(player);
    int original_folder = folder;

    // Check the argument list, if arg2 is present and valid, then lookup
    // mail in the arg1 folder rather than the default folder.
    //
    if (  nullptr == arg2
       || '\0' == arg2[0])
    {
        msglist = arg1;
    }
    else
    {
        folder = parse_folder(player, arg1);

        if (-1 == folder)
        {
            raw_notify(player, T("MAIL: No such folder."));
            return;
        }
        set_player_folder(player, folder);
        msglist = arg2;
        if (  nullptr != msglist
           && '*' == msglist[0])
        {
            msglist[0] = '\0';
        }
        sub = true;
    }

    struct mail_selector ms;

    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }
    int i = 0;
    UTF8 *time;
    UTF8 szSubjectBuffer[MBUF_SIZE];

    raw_notify(player, tprintf(T(FOLDER_LINE), folder));

    MailList ml(player);
    struct mail *mp;
    for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
    {
        if (Folder(mp) == folder)
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                time = mail_list_time(mp->time);
                size_t nSize = MessageFetchSize(mp->number);

                UTF8 szFromName[MBUF_SIZE];
                trimmed_name(mp->from, szFromName, 16, 16, 0);

                if (sub)
                {
                    StripTabsAndTruncate(mp->subject, szSubjectBuffer, MBUF_SIZE-1, 25);

                    raw_notify(player, tprintf(T("[%s] %-3d (%4d) From: %s Sub: %s"),
                        status_chars(mp), i, nSize, szFromName, szSubjectBuffer));
                }
                else
                {
                    raw_notify(player, tprintf(T("[%s] %-3d (%4d) From: %s At: %s %s"),
                        status_chars(mp), i, nSize, szFromName, time,
                            ((Connected(mp->from) && (!Hidden(mp->from) || See_Hidden(player))) ? "Conn" : " ")));
                }
                free_lbuf(time);
            }
        }
    }
    raw_notify(player, (UTF8 *)DASH_LINE);

    if (folder != original_folder)
    {
        set_player_folder(player, original_folder);
    }
}

void do_mail_purge(dbref player)
{
    // Go through player's mail, and remove anything marked cleared.
    //
    MailList ml(player);
    struct mail *mp;
    for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
    {
        if (Cleared(mp))
        {
            ml.RemoveItem();
        }
    }
    raw_notify(player, T("MAIL: Mailbox purged."));
}

static UTF8 *make_numlist(dbref player, UTF8 *arg, bool bBlind)
{
    UTF8 *tail, spot;
    malias_t *m;
    dbref target;
    int nRecip = 0;
    dbref aRecip[(LBUF_SIZE+1)/2];

    UTF8 *head = arg;

    while (  head
          && *head)
    {
        while (*head == ' ')
        {
            head++;
        }

        tail = head;
        while (  *tail
              && *tail != ' ')
        {
            if (*tail == '"')
            {
                head++;
                tail++;
                while (  *tail
                      && *tail != '"')
                {
                    tail++;
                }
            }
            if (*tail)
            {
                tail++;
            }
        }
        tail--;
        if (*tail != '"')
        {
            tail++;
        }
        spot = *tail;
        *tail = '\0';

        if (*head == '*')
        {
            int nResult;
            m = get_malias(player, head, &nResult);
            if (nResult == GMA_NOTFOUND)
            {
                raw_notify(player,
                        tprintf(T("MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 does not exist."), head));
                return nullptr;
            }
            else if (nResult == GMA_INVALIDFORM)
            {
                raw_notify(player,
                        tprintf(T("MAIL: \xE2\x80\x98%s\xE2\x80\x99 is a badly-formed alias."), head));
                return nullptr;
            }
            for (int i = 0; i < m->numrecep; i++)
            {
                 aRecip[nRecip++] = m->list[i];
            }
        }
        else
        {
            target = lookup_player(player, head, true);
            if (Good_obj(target))
            {
                aRecip[nRecip++] = target;
            }
            else
            {
                raw_notify(player, tprintf(T("MAIL: \xE2\x80\x98%s\xE2\x80\x99 does not exist."), head));
                return nullptr;
            }
        }

        // Get the next recip.
        //
        *tail = spot;
        head = tail;
        if (*head == '"')
        {
            head++;
        }
    }

    if (nRecip <= 0)
    {
        raw_notify(player, T("MAIL: No players specified."));
        return nullptr;
    }
    else
    {
        ITL itl;
        UTF8 *numbuf, *numbp;
        numbp = numbuf = alloc_lbuf("mail.make_numlist");
        ItemToList_Init(&itl, numbuf, &numbp, bBlind ? '!' : '\0');
        int i;
        for (i = 0; i < nRecip; i++)
        {
            if (aRecip[i] != NOTHING)
            {
                for (int j = i + 1; j < nRecip; j++)
                {
                    if (aRecip[i] == aRecip[j])
                    {
                        aRecip[j] = NOTHING;
                    }
                }
                if (Good_obj(aRecip[i]))
                {
                    ItemToList_AddInteger(&itl, aRecip[i]);
                }
            }
        }
        ItemToList_Final(&itl);
        return numbuf;
    }
}

static void do_expmail_start(dbref player, UTF8 *arg, UTF8 *subject)
{
    if (!arg || !*arg)
    {
        raw_notify(player, T("MAIL: I do not know whom you want to mail."));
        return;
    }
    if (!subject || !*subject)
    {
        raw_notify(player, T("MAIL: No subject."));
        return;
    }
    if (Flags2(player) & PLAYER_MAILS)
    {
        raw_notify(player, T("MAIL: Mail message already in progress."));
        return;
    }
    if (  !Wizard(player)
       && ThrottleMail(player))
    {
        raw_notify(player, T("MAIL: Too much @mail sent recently."));
        return;
    }
    UTF8 *tolist = make_numlist(player, arg, false);
    if (!tolist)
    {
        return;
    }

    atr_add_raw(player, A_MAILTO, tolist);
    atr_add_raw(player, A_MAILSUB, subject);
    atr_add_raw(player, A_MAILFLAGS, T("0"));
    atr_clr(player, A_MAILMSG);
    Flags2(player) |= PLAYER_MAILS;
    UTF8 *names = make_namelist(player, tolist);
    raw_notify(player, tprintf(T("MAIL: You are sending mail to \xE2\x80\x98%s\xE2\x80\x99."), names));
    free_lbuf(names);
    free_lbuf(tolist);
}

static void do_mail_fwd(dbref player, UTF8 *msg, UTF8 *tolist)
{
    if (Flags2(player) & PLAYER_MAILS)
    {
        raw_notify(player, T("MAIL: Mail message already in progress."));
        return;
    }
    if (!msg || !*msg)
    {
        raw_notify(player, T("MAIL: No message list."));
        return;
    }
    if (!tolist || !*tolist)
    {
        raw_notify(player, T("MAIL: To whom should I forward?"));
        return;
    }
    if (  !Wizard(player)
       && ThrottleMail(player))
    {
        raw_notify(player, T("MAIL: Too much @mail sent recently."));
        return;
    }
    int num = mux_atol(msg);
    if (!num)
    {
        raw_notify(player, T("MAIL: I don\xE2\x80\x99t understand that message number."));
        return;
    }
    struct mail *mp = mail_fetch(player, num);
    if (!mp)
    {
        raw_notify(player, T("MAIL: You can\xE2\x80\x99t forward non-existent messages."));
        return;
    }
    do_expmail_start(player, tolist, tprintf(T("%s (fwd from %s)"), mp->subject, Moniker(mp->from)));
    atr_add_raw(player, A_MAILMSG, MessageFetch(mp->number));
    const UTF8 *pValue = atr_get_raw(player, A_MAILFLAGS);
    int iFlag = M_FORWARD;
    if (pValue)
    {
        iFlag |= mux_atol(pValue);
    }
    atr_add_raw(player, A_MAILFLAGS, mux_ltoa_t(iFlag));
}

static void do_mail_reply(dbref player, UTF8 *msg, bool all, int key)
{
    if (Flags2(player) & PLAYER_MAILS)
    {
        raw_notify(player, T("MAIL: Mail message already in progress."));
        return;
    }
    if (!msg || !*msg)
    {
        raw_notify(player, T("MAIL: No message list."));
        return;
    }
    if (  !Wizard(player)
       && ThrottleMail(player))
    {
        raw_notify(player, T("MAIL: Too much @mail sent recently."));
        return;
    }
    int num = mux_atol(msg);
    if (!num)
    {
        raw_notify(player, T("MAIL: I don\xE2\x80\x99t understand that message number."));
        return;
    }
    struct mail *mp = mail_fetch(player, num);
    if (!mp)
    {
        raw_notify(player, T("MAIL: You can\xE2\x80\x99t reply to non-existent messages."));
        return;
    }
    UTF8 *tolist = alloc_lbuf("do_mail_reply.tolist");
    UTF8 *bp = tolist;
    if (all)
    {
        UTF8 *names = alloc_lbuf("do_mail_reply.names");
        UTF8 *oldlist = alloc_lbuf("do_mail_reply.oldlist");
        bp = names;
        *bp = '\0';

        mux_strncpy(oldlist, mp->tolist, LBUF_SIZE-1);

        MUX_STRTOK_STATE tts;
        mux_strtok_src(&tts, oldlist);
        mux_strtok_ctl(&tts, T(" "));
        UTF8 *p;
        for (p = mux_strtok_parse(&tts); p; p = mux_strtok_parse(&tts))
        {
            if (mux_atol(p) != mp->from)
            {
                safe_chr('#', names, &bp);
                safe_str(p, names, &bp);
                safe_chr(' ', names, &bp);
            }
        }
        free_lbuf(oldlist);
        safe_chr('#', names, &bp);
        safe_ltoa(mp->from, names, &bp);
        *bp = '\0';
        mux_strncpy(tolist, names, LBUF_SIZE-1);
        free_lbuf(names);
    }
    else
    {
        safe_chr('#', tolist, &bp);
        safe_ltoa(mp->from, tolist, &bp);
        *bp = '\0';
    }

    const UTF8 *pSubject = mp->subject;
    const UTF8 *pMessage = MessageFetch(mp->number);
    const UTF8 *pTime = mp->time;
    if (strncmp((char *)pSubject, "Re:", 3))
    {
        do_expmail_start(player, tolist, tprintf(T("Re: %s"), pSubject));
    }
    else
    {
        do_expmail_start(player, tolist, tprintf(T("%s"), pSubject));
    }
    if (key & MAIL_QUOTE)
    {
        const UTF8 *pFromName = Moniker(mp->from);
        UTF8 *pMessageBody =
            tprintf(T("On %s, %s wrote:\r\n\r\n%s\r\n\r\n********** End of included message from %s\r\n"),
                pTime, pFromName, pMessage, pFromName);
        atr_add_raw(player, A_MAILMSG, pMessageBody);
    }

    // The following combination of atr_get_raw() with atr_add_raw() is OK
    // because we are not passing a pointer to atr_add_raw() that came
    // directly from atr_get_raw().
    //
    const UTF8 *pValue = atr_get_raw(player, A_MAILFLAGS);
    int iFlag = M_REPLY;
    if (pValue)
    {
        iFlag |= mux_atol(pValue);
    }
    atr_add_raw(player, A_MAILFLAGS, mux_ltoa_t(iFlag));

    free_lbuf(tolist);
}

/*-------------------------------------------------------------------------*
 *   Admin mail functions
 *
 * do_mail_nuke - clear & purge mail for a player, or all mail in db.
 * do_mail_stat - stats on mail for a player, or for all db.
 * do_mail_debug - fix mail with a sledgehammer
 *-------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*
 *   Basic mail functions
 *-------------------------------------------------------------------------*/
struct mail *mail_fetch(dbref player, int num)
{
    int i = 0;
    MailList ml(player);
    struct mail *mp;
    for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
    {
        if (Folder(mp) == player_folder(player))
        {
            i++;
            if (i == num)
            {
                return mp;
            }
        }
    }
    return nullptr;
}

const UTF8 *mail_fetch_message(dbref player, int num)
{
    struct mail *mp = mail_fetch(player, num);
    if (mp)
    {
        return MessageFetch(mp->number);
    }
    return nullptr;
}

int mail_fetch_from(dbref player, int num)
{
    struct mail *mp = mail_fetch(player, num);
    if (mp)
    {
        return mp->from;
    }
    return NOTHING;
}

// Returns count of read, unread, and cleared messages as rcount, ucount, ccount.
//
void count_mail(dbref player, int folder, int *rcount, int *ucount, int *ccount)
{
    int rc = 0;
    int uc = 0;
    int cc = 0;

    MailList ml(player);
    struct mail *mp;
    for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
    {
        if (Folder(mp) == folder)
        {
            if (Read(mp))
            {
                rc++;
            }
            else
            {
                uc++;
            }

            if (Cleared(mp))
            {
                cc++;
            }
        }
    }
    *rcount = rc;
    *ucount = uc;
    *ccount = cc;
}

static void urgent_mail(dbref player, int folder, int *ucount)
{
    int uc = 0;

    MailList ml(player);
    struct mail *mp;
    for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
    {
        if (Folder(mp) == folder)
        {
            if (Unread(mp) && Urgent(mp))
            {
                uc++;
            }
        }
    }
    *ucount = uc;
}

static void mail_return(dbref player, dbref target)
{
    dbref aowner;
    int aflags;

    UTF8 *str = atr_pget(target, A_MFAIL, &aowner, &aflags);
    if (*str)
    {
        UTF8 *str2, *bp;
        str2 = bp = alloc_lbuf("mail_return");
        mux_exec(str, LBUF_SIZE-1, str2, &bp, target, player, player,
             AttrTrace(aflags, EV_FCHECK|EV_EVAL|EV_TOP|EV_NO_LOCATION),
             nullptr, 0);
        *bp = '\0';
        if (*str2)
        {
            CLinearTimeAbsolute ltaNow;
            ltaNow.GetLocal();
            FIELDEDTIME ft;
            ltaNow.ReturnFields(&ft);

            raw_notify(player, tprintf(T("MAIL: Reject message from %s: %s"),
                Moniker(target), str2));
            raw_notify(target, tprintf(T("[%d:%02d] MAIL: Reject message sent to %s."),
                ft.iHour, ft.iMinute, Moniker(player)));
        }
        free_lbuf(str2);
    }
    else
    {
        raw_notify(player, tprintf(T("Sorry, %s is not accepting mail."), Moniker(target)));
    }
    free_lbuf(str);
}

static bool mail_check(dbref player, dbref target)
{
    if (!could_doit(player, target, A_LMAIL))
    {
        mail_return(player, target);
    }
    else if (!could_doit(target, player, A_LMAIL))
    {
        if (Wizard(player))
        {
            raw_notify(player, tprintf(T("Warning: %s can\xE2\x80\x99t return your mail."), Moniker(target)));
            return true;
        }
        else
        {
            raw_notify(player, tprintf(T("Sorry, %s can\xE2\x80\x99t return your mail."), Moniker(target)));
            return false;
        }
    }
    else
    {
        return true;
    }
    return false;
}

static void send_mail
(
    dbref player,
    dbref target,
    const UTF8 *tolist,
    const UTF8 *subject,
    int number,
    mail_flag flags,
    bool silent
)
{
    if (!isPlayer(target))
    {
        raw_notify(player, T("MAIL: You cannot send mail to non-existent people."));
        return;
    }
    if (!mail_check(player, target))
    {
        return;
    }
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();

    UTF8 *pTimeStr = ltaNow.ReturnDateString(0);

    // Initialize the appropriate fields.
    //
    struct mail *newp = nullptr;
    try
    {
        newp = new struct mail;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == newp)
    {
        raw_notify(player, T("MAIL: Out of memory."));
        return;
    }

    newp->to = target;

    // HACK: Allow @mail/quick, if player is an object, then the
    // object's owner is the sender, if the owner is a wizard, then
    // we allow the object to be the sender.
    //
    if (isPlayer(player))
    {
        newp->from = player;
    }
    else
    {
        dbref mailbag = Owner(player);
        if (Wizard(mailbag))
        {
            newp->from = player;
        }
        else
        {
            newp->from = mailbag;
        }
    }
    if (  !tolist
       || tolist[0] == '\0')
    {
        newp->tolist = StringClone(T("*HIDDEN*"));
    }
    else
    {
        newp->tolist = StringClone(tolist);
    }

    newp->number = number;
    MessageReferenceInc(number);
    newp->time = StringClone(pTimeStr);
    newp->subject = StringClone(subject);

    // Send to folder 0
    //
    newp->read = flags & M_FMASK;

    // If this is the first message, it is the head and the tail.
    //
    MailList ml(target);
    ml.AppendItem(newp);

    // Notify people.
    //
    if (!silent)
    {
        raw_notify(player,
                tprintf(T("MAIL: You sent your message to %s."), Moniker(target)));
    }

    raw_notify(target,
            tprintf(T("MAIL: You have a new message from %s. Subject: %s"),
                Moniker(player), subject));

    did_it(player, target, A_MAIL, nullptr, 0, nullptr, A_AMAIL, 0, nullptr, NOTHING);
}

static void do_mail_nuke(dbref player)
{
    if (!God(player))
    {
        raw_notify(player,
                T("The postal service issues a warrant for your arrest."));
        return;
    }

    // Walk the list.
    //
    dbref thing;
    DO_WHOLE_DB(thing)
    {
        MailList ml(thing);
        ml.RemoveAll();
    }
    log_printf(T("** MAIL PURGE ** done by %s(#%d)." ENDLINE), PureName(player), player);
    raw_notify(player, T("You annihilate the post office. All messages cleared."));
}

#ifdef SELFCHECK
void finish_mail()
{
    dbref thing;
    DO_WHOLE_DB(thing)
    {
        MailList ml(thing);
        ml.RemoveAll();
    }

    if (nullptr != mail_list)
    {
        mail_list -= MAIL_FUDGE;
        MEMFREE(mail_list);
        mail_list = nullptr;
    }
}
#endif

static void do_mail_debug(dbref player, UTF8 *action, UTF8 *victim)
{
    if (!ExpMail(player))
    {
        raw_notify(player, T("Go get some bugspray."));
        return;
    }

    dbref thing;
    if (string_prefix(T("clear"), action))
    {
        dbref target = lookup_player(player, victim, true);
        if (target == NOTHING)
        {
            init_match(player, victim, NOTYPE);
            match_absolute();
            target = match_result();
        }
        if (target == NOTHING)
        {
            raw_notify(player, tprintf(T("%s: no such player."), victim));
            return;
        }
        if (Wizard(target))
        {
            raw_notify(player, tprintf(T("Let %s clear their own @mail."), Moniker(target)));
            return;
        }
        do_mail_clear(target, nullptr);
        do_mail_purge(target);
        raw_notify(player, tprintf(T("Mail cleared for %s(#%d)."), Moniker(target), target));
        return;
    }
    else if (string_prefix(T("sanity"), action))
    {
        int *ai = nullptr;
        try
        {
            ai = new int[mudstate.mail_db_top];
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == ai)
        {
            raw_notify(player, T("Out of memory."));
            return;
        }

        memset(ai, 0, mudstate.mail_db_top * sizeof(int));

        DO_WHOLE_DB(thing)
        {
            MailList ml(thing);
            struct mail *mp;
            for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
            {
                bool bGoodReference;
                if (0 <= mp->number && mp->number < mudstate.mail_db_top)
                {
                    ai[mp->number]++;
                    bGoodReference = true;
                }
                else
                {
                    bGoodReference = false;
                }
                if (!Good_obj(mp->to))
                {
                    if (bGoodReference)
                    {
                        raw_notify(player, tprintf(T("Bad object #%d has mail."), mp->to));
                    }
                    else
                    {
                        raw_notify(player, tprintf(T("Bad object #%d has mail which refers to a non-existent mailbag item."), mp->to));
                    }
                }
                else if (!isPlayer(mp->to))
                {
                    if (bGoodReference)
                    {
                        raw_notify(player, tprintf(T("%s(#%d) has mail, but is not a player."),
                                 Moniker(mp->to), mp->to));
                    }
                    else
                    {
                        raw_notify(player, tprintf(T("%s(#%d) is not a player, but has mail which refers to a non-existent mailbag item."),
                             Moniker(mp->to), mp->to));
                    }
                }
                else if (!bGoodReference)
                {
                    raw_notify(player, tprintf(T("%s(#%d) has mail which refers to a non-existent mailbag item."), Moniker(mp->to), mp->to));
                }
            }
        }

        // Check ref counts.
        //
        if (mail_list)
        {
            int i;
            int nCountHigher = 0;
            int nCountLower  = 0;
            for (i = 0; i < mudstate.mail_db_top; i++)
            {
                if (mail_list[i].m_nRefs < ai[i])
                {
                    nCountLower++;
                }
                else if (mail_list[i].m_nRefs > ai[i])
                {
                    nCountHigher++;
                }
            }
            if (nCountLower)
            {
                raw_notify(player, T("Some mailbag items are referred to more often than the mailbag item indicates."));
            }
            if (nCountHigher)
            {
                raw_notify(player, T("Some mailbag items are referred to less often than the mailbag item indicates."));
            }
        }

        delete [] ai;
        ai = nullptr;
        raw_notify(player, T("Mail sanity check completed."));
    }
    else if (string_prefix(T("fix"), action))
    {
        // First, we should fixup the reference counts.
        //
        if (mail_list)
        {
            raw_notify(player, tprintf(T("Re-counting mailbag reference counts.")));
            int *ai = nullptr;
            try
            {
                ai = new int[mudstate.mail_db_top];
            }
            catch (...)
            {
                ; // Nothing.
            }

            if (nullptr == ai)
            {
                raw_notify(player, T("Out of memory."));
                return;
            }

            memset(ai, 0, mudstate.mail_db_top * sizeof(int));

            DO_WHOLE_DB(thing)
            {
                MailList ml(thing);
                struct mail *mp;
                for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
                {
                    if (  0 <= mp->number
                       && mp->number < mudstate.mail_db_top)
                    {
                        ai[mp->number]++;
                    }
                    else
                    {
                        mp->number = NOTHING;
                    }
                }
            }
            int i;
            int nCountWrong = 0;
            for (i = 0; i < mudstate.mail_db_top; i++)
            {
                if (mail_list[i].m_nRefs != ai[i])
                {
                    mail_list[i].m_nRefs = ai[i];
                    nCountWrong++;
                }
            }
            if (nCountWrong)
            {
                raw_notify(player, T("Some reference counts were wrong [FIXED]."));
            }

            delete [] ai;
            ai = nullptr;
        }

        raw_notify(player, tprintf(T("Removing @mail that is associated with non-players.")));

        // Now, remove all mail to non-good or non-players, or mail that
        // points to non-existent mailbag items.
        //
        DO_WHOLE_DB(thing)
        {
            MailList ml(thing);
            struct mail *mp;
            for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
            {
                if (  !Good_obj(mp->to)
                   || !isPlayer(mp->to)
                   || NOTHING == mp->number)
                {
                    // Delete this item.
                    //
                    raw_notify(player, tprintf(T("Fixing mail for #%d."), mp->to));
                    ml.RemoveItem();
                }
            }
        }
        raw_notify(player, T("Mail sanity fix completed."));
    }
    else
    {
        raw_notify(player, T("That is not a debugging option."));
        return;
    }
}

static void do_mail_stats(dbref player, UTF8 *name, int full)
{
    dbref target, thing;
    int fc, fr, fu, tc, tr, tu, count;
    size_t cchars = 0;
    size_t fchars = 0;
    size_t tchars = 0;
    fc = fr = fu = tc = tr = tu = count = 0;

    // Find player.
    //
    if (  !name
       || *name == '\0')
    {
        if (Wizard(player))
        {
            target = AMBIGUOUS;
        }
        else
        {
            target = player;
        }
    }
    else if (*name == NUMBER_TOKEN)
    {
        target = mux_atol(&name[1]);
        if (!Good_obj(target) || !isPlayer(target))
        {
            target = NOTHING;
        }
    }
    else if (!mux_stricmp(name, T("me")))
    {
        target = player;
    }
    else
    {
        target = lookup_player(player, name, true);
    }

    if (target == NOTHING)
    {
        init_match(player, name, NOTYPE);
        match_absolute();
        target = match_result();
    }
    if (target == NOTHING)
    {
        raw_notify(player, tprintf(T("%s: No such player."), name));
        return;
    }
    if (!ExpMail(player) && (target != player))
    {
        raw_notify(player, T("The post office protects privacy!"));
        return;
    }

    // This comand is computationally expensive.
    //
    if (!payfor(player, mudconf.searchcost))
    {
        raw_notify(player, tprintf(T("Finding mail stats costs %d %s."),
                       mudconf.searchcost,
                       (mudconf.searchcost == 1) ? mudconf.one_coin : mudconf.many_coins));
        return;
    }
    if (AMBIGUOUS == target)
    {
        // Stats for all.
        //
        if (full == 0)
        {
            DO_WHOLE_DB(thing)
            {
                MailList ml(thing);
                for ((void)ml.FirstItem(); !ml.IsEnd(); (void)ml.NextItem())
                {
                    count++;
                }
            }
            raw_notify(player, tprintf(T("There are %d messages in the mail spool."), count));
            return;
        }
        else if (full == 1)
        {
            DO_WHOLE_DB(thing)
            {
                MailList ml(thing);
                struct mail *mp;
                for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
                {
                    if (Cleared(mp))
                    {
                        fc++;
                    }
                    else if (Read(mp))
                    {
                        fr++;
                    }
                    else
                    {
                        fu++;
                    }
                }
            }
            raw_notify(player,
                   tprintf(T("MAIL: There are %d msgs in the mail spool, %d unread, %d cleared."),
                       fc + fr + fu, fu, fc));
            return;
        }
        else
        {
            DO_WHOLE_DB(thing)
            {
                MailList ml(thing);
                struct mail *mp;
                for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
                {
                    if (Cleared(mp))
                    {
                        fc++;
                        cchars += MessageFetchSize(mp->number) + 1;
                    }
                    else if (Read(mp))
                    {
                        fr++;
                        fchars += MessageFetchSize(mp->number) + 1;
                    }
                    else
                    {
                        fu++;
                        tchars += MessageFetchSize(mp->number) + 1;
                    }
                }
            }
            raw_notify(player, tprintf(T("MAIL: There are %d old msgs in the mail spool, totalling %d characters."), fr, fchars));
            raw_notify(player, tprintf(T("MAIL: There are %d new msgs in the mail spool, totalling %d characters."), fu, tchars));
            raw_notify(player, tprintf(T("MAIL: There are %d cleared msgs in the mail spool, totalling %d characters."), fc, cchars));
            return;
        }
    }

    // individual stats
    //
    if (full == 0)
    {
        // Just count the number of messages.
        //
        DO_WHOLE_DB(thing)
        {
            MailList ml(thing);
            struct mail *mp;
            for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
            {
                if (mp->from == target)
                {
                    fr++;
                }
                if (mp->to == target)
                {
                    tr++;
                }
            }
        }
        raw_notify(player, tprintf(T("%s sent %d messages."), Moniker(target), fr));
        raw_notify(player, tprintf(T("%s has %d messages."), Moniker(target), tr));
        return;
    }

    // More detailed message count.
    //
    UTF8 last[50];
    DO_WHOLE_DB(thing)
    {
        MailList ml(thing);
        struct mail *mp;
        for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
        {
            if (mp->from == target)
            {
                if (Cleared(mp))
                {
                    fc++;
                }
                else if (Read(mp))
                {
                    fr++;
                }
                else
                {
                    fu++;
                }
                if (full == 2)
                {
                    fchars += MessageFetchSize(mp->number) + 1;
                }
            }
            if (mp->to == target)
            {
                if (!tr && !tu)
                {
                    mux_strncpy(last, mp->time, sizeof(last)-1);
                }
                if (Cleared(mp))
                {
                    tc++;
                }
                else if (Read(mp))
                {
                    tr++;
                }
                else
                {
                    tu++;
                }
                if (full == 2)
                {
                    tchars += MessageFetchSize(mp->number) + 1;
                }
            }
        }
    }

    raw_notify(player, tprintf(T("Mail statistics for %s:"), Moniker(target)));

    if (full == 1)
    {
        raw_notify(player, tprintf(T("%d messages sent, %d unread, %d cleared."),
                       fc + fr + fu, fu, fc));
        raw_notify(player, tprintf(T("%d messages received, %d unread, %d cleared."),
                       tc + tr + tu, tu, tc));
    }
    else
    {
        raw_notify(player,
               tprintf(T("%d messages sent, %d unread, %d cleared, totalling %d characters."),
                   fc + fr + fu, fu, fc, fchars));
        raw_notify(player,
               tprintf(T("%d messages received, %d unread, %d cleared, totalling %d characters."),
                   tc + tr + tu, tu, tc, tchars));
    }

    if (tc + tr + tu > 0)
    {
        raw_notify(player, tprintf(T("Last is dated %s"), last));
    }
}

/*-------------------------------------------------------------------------*
 *   Main mail routine for @mail w/o a switch
 *-------------------------------------------------------------------------*/

static void do_mail_stub(dbref player, UTF8 *arg1, UTF8 *arg2)
{
    if (!arg1 || !*arg1)
    {
        if (arg2 && *arg2)
        {
            raw_notify(player, T("MAIL: Invalid mail command."));
            return;
        }

        // Just the "@mail" command.
        //
        do_mail_list(player, arg1, nullptr, true);
        return;
    }

    // purge a player's mailbox
    //
    if (!mux_stricmp(arg1, T("purge")))
    {
        do_mail_purge(player);
        return;
    }

    // clear message
    //
    if (!mux_stricmp(arg1, T("clear")))
    {
        do_mail_clear(player, arg2);
        return;
    }
    if (!mux_stricmp(arg1, T("unclear")))
    {
        do_mail_unclear(player, arg2);
        return;
    }
    if (arg2 && *arg2)
    {
        // Sending mail
        //
        do_expmail_start(player, arg1, arg2);
        return;
    }
    else
    {
        // Must be reading or listing mail - no arg2
        //
        if (  mux_isdigit(*arg1)
           && !strchr((char *)arg1, '-'))
        {
            do_mail_read(player, arg1, nullptr);
        }
        else
        {
            do_mail_list(player, arg1, nullptr, true);
        }
        return;
    }
}

static void malias_write(FILE *fp)
{
    int i, j;
    malias_t *m;

    putref(fp, ma_top);
    for (i = 0; i < ma_top; i++)
    {
        m = malias[i];
        mux_fprintf(fp, T("%d %d\n"), m->owner, m->numrecep);
        mux_fprintf(fp, T("N:%s\n"), m->name);
        mux_fprintf(fp, T("D:%s\n"), m->desc);
        for (j = 0; j < m->numrecep; j++)
        {
            putref(fp, m->list[j]);
        }
    }
}

static void save_malias(FILE *fp)
{
    mux_fprintf(fp, T("*** Begin MALIAS ***\n"));
    malias_write(fp);
}

int dump_mail(FILE *fp)
{
    dbref thing;
    int count = 0, i;

    // Write out version number
    //
    mux_fprintf(fp, T("+V6\n"));
    putref(fp, mudstate.mail_db_top);
    DO_WHOLE_DB(thing)
    {
        if (isPlayer(thing))
        {
            MailList ml(thing);
            struct mail *mp;
            for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
            {
                putref(fp, mp->to);
                putref(fp, mp->from);
                putref(fp, mp->number);
                putstring(fp, mp->tolist);
                putstring(fp, mp->time);
                putstring(fp, mp->subject);
                putref(fp, mp->read);
                count++;
            }
        }
    }

    mux_fprintf(fp, T("*** END OF DUMP ***\n"));

    // Add the db of mail messages
    //
    for (i = 0; i < mudstate.mail_db_top; i++)
    {
        if (0 < mail_list[i].m_nRefs)
        {
            putref(fp, i);
            putstring(fp, MessageFetch(i));
        }
    }
    mux_fprintf(fp, T("+++ END OF DUMP +++\n"));
    save_malias(fp);

    return count;
}

static void malias_read(FILE *fp, bool bConvert);

static void load_mail_V6(FILE *fp)
{
    int mail_top = getref(fp);
    mail_db_grow(mail_top + 1);

    size_t nBuffer;
    UTF8  *pBuffer;
    UTF8 nbuf1[200];
    UTF8 *p = (UTF8 *)fgets((char *)nbuf1, sizeof(nbuf1), fp);
    while (  nullptr != p
          && strncmp((char *)nbuf1, "***", 3) != 0)
    {
        struct mail *mp = nullptr;
        try
        {
            mp = new struct mail;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == mp)
        {
            STARTLOG(LOG_BUGS, "BUG", "MAIL");
            log_text(T("Out of memory."));
            ENDLOG;
            return;
        }

        mp->to      = mux_atol(nbuf1);
        mp->from    = getref(fp);

        mp->number  = getref(fp);
        MessageReferenceInc(mp->number);

        pBuffer = (UTF8 *)getstring_noalloc(fp, true, &nBuffer);
        mp->tolist  = StringCloneLen(pBuffer, nBuffer);
        pBuffer = (UTF8 *)getstring_noalloc(fp, true, &nBuffer);
        mp->time    = StringCloneLen(pBuffer, nBuffer);
        pBuffer = (UTF8 *)getstring_noalloc(fp, true, &nBuffer);
        mp->subject = StringCloneLen(pBuffer, nBuffer);
        mp->read    = getref(fp);

        MailList ml(mp->to);
        ml.AppendItem(mp);

        p = (UTF8 *)fgets((char *)nbuf1, sizeof(nbuf1), fp);
    }

    p = (UTF8 *)fgets((char *)nbuf1, sizeof(nbuf1), fp);
    while (  nullptr != p
          && strncmp((char *)nbuf1, "+++", 3) != 0)
    {
        int number = mux_atol(nbuf1);
        pBuffer = (UTF8 *)getstring_noalloc(fp, true, &nBuffer);
        new_mail_message(pBuffer, number);
        p = (UTF8 *)fgets((char *)nbuf1, sizeof(nbuf1), fp);
    }

    p = (UTF8 *)fgets((char *)nbuf1, sizeof(nbuf1), fp);
    if (  nullptr != p
       && strcmp((char *)nbuf1, "*** Begin MALIAS ***\n") == 0)
    {
        malias_read(fp, false);
    }
    else
    {
        Log.WriteString(T("ERROR: Couldn\xE2\x80\x99t find Begin MALIAS." ENDLINE));
    }
}

static void load_mail_V5(FILE *fp)
{
    int mail_top = getref(fp);
    mail_db_grow(mail_top + 1);

    size_t nBufferLatin1;
    char  *pBufferLatin1;
    size_t nBufferUnicode;
    UTF8  *pBufferUnicode;

    char nbuf1[200];
    char *p = fgets(nbuf1, sizeof(nbuf1), fp);
    while (  nullptr != p
          && strncmp(nbuf1, "***", 3) != 0)
    {
        struct mail *mp = nullptr;
        try
        {
            mp = new struct mail;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == mp)
        {
            STARTLOG(LOG_BUGS, "BUG", "MAIL");
            log_text(T("Out of memory."));
            ENDLOG;
            return;
        }

        pBufferUnicode = (UTF8 *)nbuf1;

        mp->to      = mux_atol(pBufferUnicode);
        mp->from    = getref(fp);

        mp->number  = getref(fp);
        MessageReferenceInc(mp->number);

        pBufferLatin1 = (char *)getstring_noalloc(fp, true, &nBufferLatin1);
        pBufferUnicode = ConvertToUTF8(pBufferLatin1, &nBufferUnicode);
        mp->tolist  = StringCloneLen(pBufferUnicode, nBufferUnicode);

        pBufferLatin1 = (char *)getstring_noalloc(fp, true, &nBufferLatin1);
        pBufferUnicode = ConvertToUTF8(pBufferLatin1, &nBufferUnicode);
        mp->time    = StringCloneLen(pBufferUnicode, nBufferUnicode);

        pBufferLatin1 = (char *)getstring_noalloc(fp, true, &nBufferLatin1);
        pBufferUnicode = ConvertToUTF8(pBufferLatin1, &nBufferUnicode);
        mp->subject = StringCloneLen(pBufferUnicode, nBufferUnicode);

        mp->read    = getref(fp);

        MailList ml(mp->to);
        ml.AppendItem(mp);

        p = fgets(nbuf1, sizeof(nbuf1), fp);
    }

    p = fgets(nbuf1, sizeof(nbuf1), fp);
    while (  nullptr != p
          && strncmp(nbuf1, "+++", 3) != 0)
    {
        pBufferUnicode = (UTF8 *)nbuf1;
        int number = mux_atol(pBufferUnicode);
        pBufferLatin1 = (char *)getstring_noalloc(fp, true, &nBufferLatin1);
        pBufferUnicode = ConvertToUTF8(pBufferLatin1, &nBufferUnicode);
        new_mail_message(pBufferUnicode, number);
        p = fgets(nbuf1, sizeof(nbuf1), fp);
    }

    p = fgets(nbuf1, sizeof(nbuf1), fp);
    if (  nullptr != p
       && strcmp(nbuf1, "*** Begin MALIAS ***\n") == 0)
    {
        malias_read(fp, true);
    }
    else
    {
        Log.WriteString(T("ERROR: Couldn\xE2\x80\x99t find Begin MALIAS." ENDLINE));
    }
}

// A mail alias description can be any combination of upper-case letters,
// lower-case letters, digits, blanks, and symbols. ANSI is permitted.
// Length is limited to SIZEOF_MALIASDESC-1. Visual width is limited to
// WIDTHOF_MALIASDESC. Case is preserved.
//
UTF8 *MakeCanonicalMailAliasDesc
(
    UTF8   *pMailAliasDesc,
    size_t *pnValidMailAliasDesc,
    bool   *pbValidMailAliasDesc,
    size_t *pnVisualWidth
)
{
    *pnValidMailAliasDesc = 0;
    *pbValidMailAliasDesc = false;
    *pnVisualWidth = 0;
    if (!pMailAliasDesc)
    {
        return nullptr;
    }

    // Remove all '\r\n\t' from the string.
    // Terminate any ANSI in the string.
    //
    static UTF8 szFittedMailAliasDesc[SIZEOF_MALIASDESC+1];
    mux_field nValidMailAliasDesc = StripTabsAndTruncate
                                     ( pMailAliasDesc,
                                       szFittedMailAliasDesc,
                                       SIZEOF_MALIASDESC,
                                       WIDTHOF_MALIASDESC
                                     );
    *pnValidMailAliasDesc = nValidMailAliasDesc.m_byte;
    *pbValidMailAliasDesc = true;
    *pnVisualWidth = nValidMailAliasDesc.m_column;
    return szFittedMailAliasDesc;
}

static void malias_read(FILE *fp, bool bConvert)
{
    int i, j;

    i = getref(fp);
    if (i <= 0)
    {
        return;
    }
    UTF8 buffer[LBUF_SIZE];

    ma_size = ma_top = i;

    malias = nullptr;
    try
    {
        malias = new malias_t *[ma_size];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == malias)
    {
        STARTLOG(LOG_BUGS, "BUG", "MAIL");
        log_text(T("Out of memory."));
        ENDLOG;
        return;
    }

    for (i = 0; i < ma_top; i++)
    {
        // Format is: "%d %d\n", &(m->owner), &(m->numrecep)
        //
        if (!fgets((char *)buffer, sizeof(buffer), fp))
        {
            // We've hit the end of the file. Set the last recognized
            // @malias, and give up.
            //
            STARTLOG(LOG_BUGS, "BUG", "MAIL");
            log_text(T("Unexpected end of file. Mail bag truncated."));
            ENDLOG;

            ma_top = i;
            return;
        }

        malias_t *m = nullptr;
        try
        {
            m = new malias_t;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == m)
        {
            STARTLOG(LOG_BUGS, "BUG", "MAIL");
            log_text(T("Out of memory. Mail bag truncated."));
            ENDLOG;

            ma_top = i;
            return;
        }

        malias[i] = m;

        UTF8 *p = (UTF8 *)strchr((char *)buffer, ' ');
        m->owner = m->numrecep = 0;
        if (p)
        {
            m->owner = mux_atol(buffer);
            m->numrecep = mux_atol(p+1);
        }

        // The format of @malias name is "N:<name>\n".
        //
        size_t nLen = GetLineTrunc(buffer, sizeof(buffer), fp);
        buffer[nLen-1] = '\0'; // Get rid of trailing '\n'.

        UTF8 *pBufferUnicode;
        if (bConvert)
        {
            size_t nBufferUnicode;
            pBufferUnicode = ConvertToUTF8((char *)buffer, &nBufferUnicode);
        }
        else
        {
            pBufferUnicode = buffer;
        }

        size_t nMailAlias;
        bool bMailAlias;
        UTF8 *pMailAlias = MakeCanonicalMailAlias( pBufferUnicode+2,
                                                   &nMailAlias,
                                                   &bMailAlias);
        if (bMailAlias)
        {
            m->name = StringCloneLen(pMailAlias, nMailAlias);
        }
        else
        {
            m->name = StringCloneLen(T("Invalid"), 7);
        }

        // The format of the description is "D:<description>\n"
        //
        nLen = GetLineTrunc(buffer, sizeof(buffer), fp);
        if (bConvert)
        {
            size_t nBufferUnicode;
            pBufferUnicode = ConvertToUTF8((char *)buffer, &nBufferUnicode);
        }
        else
        {
            pBufferUnicode = buffer;
        }

        size_t  nMailAliasDesc;
        bool bMailAliasDesc;
        size_t nVisualWidth;
        UTF8 *pMailAliasDesc = MakeCanonicalMailAliasDesc( pBufferUnicode+2,
                                                           &nMailAliasDesc,
                                                           &bMailAliasDesc,
                                                           &nVisualWidth);
        if (bMailAliasDesc)
        {
            m->desc = StringCloneLen(pMailAliasDesc, nMailAliasDesc);
            m->desc_width = nVisualWidth;
        }
        else
        {
            m->desc = StringCloneLen(T("Invalid Desc"), 12);
            m->desc_width = 12;
        }

        if (m->numrecep > 0)
        {
            for (j = 0; j < m->numrecep; j++)
            {
                int k = getref(fp);
                if (j < MAX_MALIAS_MEMBERSHIP)
                {
                    m->list[j] = k;
                }
            }
        }
        else
        {
            m->list[0] = 0;
        }
    }
}

static void load_malias(FILE *fp, bool bConvert)
{
    UTF8 buffer[200];

    getref(fp);
    if (  fgets((char *)buffer, sizeof(buffer), fp)
       && strcmp((char *)buffer, "*** Begin MALIAS ***\n") == 0)
    {
        malias_read(fp, bConvert);
    }
    else
    {
        Log.WriteString(T("ERROR: Couldn\xE2\x80\x99t find Begin MALIAS." ENDLINE));
        return;
    }
}

void load_mail(FILE *fp)
{
    UTF8 nbuf1[8];

    // Read the version number.
    //
    if (!fgets((char *)nbuf1, sizeof(nbuf1), fp))
    {
        return;
    }

    if (strncmp((char *)nbuf1, "+V6", 3) == 0)
    {
        // Started v6 on 2007-MAR-13.
        //
        load_mail_V6(fp);
    }
    else if (strncmp((char *)nbuf1, "+V5", 3) == 0)
    {
        load_mail_V5(fp);
    }
}

void check_mail_expiration(void)
{
    // Negative values for expirations never expire.
    //
    if (0 > mudconf.mail_expiration)
    {
        return;
    }

    dbref thing;
    int expire_secs = mudconf.mail_expiration * 86400;

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();

    CLinearTimeAbsolute ltaMail;
    DO_WHOLE_DB(thing)
    {
        MailList ml(thing);
        struct mail *mp;
        for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
        {
            if (M_Safe(mp))
            {
                continue;
            }

            const UTF8 *pMailTimeStr = mp->time;
            if (!ltaMail.SetString(pMailTimeStr))
            {
                continue;
            }

            CLinearTimeDelta ltd(ltaMail, ltaNow);
            if (ltd.ReturnSeconds() <= expire_secs)
            {
                continue;
            }

            // Delete this one.
            //
            ml.RemoveItem();
        }
    }
}

void check_mail(dbref player, int folder, bool silent)
{
    // Check for new @mail
    //
    int rc;     // Read messages.
    int uc;     // Unread messages.
    int cc;     // Cleared messages.
    int gc;     // urgent messages.

    // Just count messages
    //
    count_mail(player, folder, &rc, &uc, &cc);
    urgent_mail(player, folder, &gc);
#ifdef MAIL_ALL_FOLDERS
    raw_notify(player,
           tprintf(T("MAIL: %d messages in folder %d [%s] (%d unread, %d cleared).\r\n"),
               rc + uc, folder, get_folder_name(player, folder), uc, cc));
#else // MAIL_ALL_FOLDERS
    if (rc + uc > 0)
    {
        raw_notify(player, tprintf(T("MAIL: %d messages in folder %d [%s] (%d unread, %d cleared)."), rc + uc, folder, get_folder_name(player, folder), uc, cc));
    }
    else if (!silent)
    {
        raw_notify(player, tprintf(T("\r\nMAIL: You have no mail.\r\n")));
    }
    if (gc > 0)
    {
        raw_notify(player, tprintf(T("URGENT MAIL: You have %d urgent messages in folder %d [%s]."), gc, folder, get_folder_name(player, folder)));
    }
#endif // MAIL_ALL_FOLDERS
}

static void do_malias_send
(
    dbref player,
    UTF8 *tolist,
    UTF8 *listto,
    UTF8 *subject,
    int   number,
    mail_flag flags,
    bool  silent
)
{
    int nResult;
    malias_t *m = get_malias(player, tolist, &nResult);
    if (nResult == GMA_INVALIDFORM)
    {
        raw_notify(player, tprintf(T("MAIL: I can\xE2\x80\x99t figure out from \xE2\x80\x98%s\xE2\x80\x99 who you want to mail to."), tolist));
        return;
    }
    else if (nResult == GMA_NOTFOUND)
    {
        raw_notify(player, tprintf(T("MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 not found."), tolist));
        return;
    }

    // Parse the player list.
    //
    dbref vic;
    int k;
    for (k = 0; k < m->numrecep; k++)
    {
        vic = m->list[k];

        if (isPlayer(vic))
        {
            send_mail(player, m->list[k], listto, subject, number, flags, silent);
        }
        else
        {
            // Complain about it.
            //
            UTF8 *pMail = tprintf(T("Alias Error: Bad Player %d for %s"), vic, tolist);
            int iMail = add_mail_message(player, pMail);
            if (iMail != NOTHING)
            {
                send_mail(GOD, GOD, listto, subject, iMail, 0, silent);
                MessageReferenceDec(iMail);
            }
        }
    }
}

static void do_malias_create(dbref player, UTF8 *alias, UTF8 *tolist)
{
    malias_t **nm;
    int nResult;
    get_malias(player, alias, &nResult);

    if (nResult == GMA_INVALIDFORM)
    {
        raw_notify(player, T("MAIL: What alias do you want to create?."));
        return;
    }
    else if (nResult == GMA_FOUND)
    {
        raw_notify(player,
                tprintf(T("MAIL: Mail Alias \xE2\x80\x98%s\xE2\x80\x99 already exists."), alias));
        return;
    }

    malias_t *pt = nullptr;
    try
    {
        pt = new malias_t;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pt)
    {
        raw_notify(player, T("MAIL: Out of memory."));
        return;
    }

    int i = 0;
    if (!ma_size)
    {
        ma_size = MA_INC;
        malias = nullptr;
        try
        {
            malias = new malias_t *[ma_size];
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == malias)
        {
            raw_notify(player, T("MAIL: Out of memory."));
            delete pt;
            return;
        }
    }
    else if (ma_top >= ma_size)
    {
        ma_size += MA_INC;
        nm = nullptr;
        try
        {
            nm = new malias_t *[ma_size];
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == nm)
        {
            raw_notify(player, T("MAIL: Out of memory."));
            delete pt;
            return;
        }

        for (i = 0; i < ma_top; i++)
        {
            nm[i] = malias[i];
        }

        delete [] malias;
        malias = nm;
    }

    malias[ma_top] = pt;

    // Parse the player list.
    //
    UTF8 *head = tolist;
    UTF8 *tail, spot;
    UTF8 *buff;
    dbref target;
    i = 0;
    while (  head
          && *head
          && i < (MAX_MALIAS_MEMBERSHIP - 1))
    {
        while (*head == ' ')
        {
            head++;
        }
        tail = head;
        while (  *tail
              && *tail != ' ')
        {
            if (*tail == '"')
            {
                head++;
                tail++;
                while (  *tail
                      && *tail != '"')
                {
                    tail++;
                }
            }
            if (*tail)
            {
                tail++;
            }
        }
        tail--;
        if (*tail != '"')
        {
            tail++;
        }
        spot = *tail;
        *tail = '\0';

        // Now locate a target.
        //
        if (!mux_stricmp(head, T("me")))
        {
            target = player;
        }
        else if (*head == '#')
        {
            target = mux_atol(head + 1);
        }
        else
        {
            target = lookup_player(player, head, true);
        }

        if (  !Good_obj(target)
           || !isPlayer(target))
        {
            raw_notify(player, T("MAIL: No such player."));
        }
        else
        {
            buff = unparse_object(player, target, false);
            raw_notify(player,
                    tprintf(T("MAIL: %s added to alias %s"), buff, alias));
            malias[ma_top]->list[i] = target;
            i++;
            free_lbuf(buff);
        }

        // Get the next recip.
        //
        *tail = spot;
        head = tail;
        if (*head == '"')
        {
            head++;
        }
    }
    size_t nValidMailAlias;
    bool   bValidMailAlias;
    UTF8 *pValidMailAlias = MakeCanonicalMailAlias
                            (   alias+1,
                                &nValidMailAlias,
                                &bValidMailAlias
                            );

    if (!bValidMailAlias)
    {
        raw_notify(player, T("MAIL: Invalid mail alias."));
        return;
    }

    // The Mail Alias Description is a superset of the Mail Alias,
    // so, the following code is not necessary unless the specification
    // of the Mail Alias Description becomes more restrictive at some
    // future time.
    //
    UTF8 *pValidMailAliasDesc = pValidMailAlias;
    size_t nValidMailAliasDesc = nValidMailAlias;

    malias[ma_top]->list[i] = NOTHING;
    malias[ma_top]->name = StringCloneLen(pValidMailAlias, nValidMailAlias);
    malias[ma_top]->numrecep = i;
    malias[ma_top]->owner = player;
    malias[ma_top]->desc = StringCloneLen(pValidMailAliasDesc, nValidMailAliasDesc);
    malias[ma_top]->desc_width = nValidMailAliasDesc;
    ma_top++;

    raw_notify(player, tprintf(T("MAIL: Alias set \xE2\x80\x98%s\xE2\x80\x99 defined."), alias));
}

static void do_malias_list(dbref player, UTF8 *alias)
{
    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        raw_notify(player, tprintf(T("MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 not found."), alias));
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    if (!ExpMail(player) && (player != m->owner) && !(God(m->owner)))
    {
        raw_notify(player, T("MAIL: Permission denied."));
        return;
    }
    UTF8 *buff = alloc_lbuf("do_malias_list");
    UTF8 *bp = buff;

    safe_tprintf_str(buff, &bp, T("MAIL: Alias *%s: "), m->name);
    for (int i = m->numrecep - 1; i > -1; i--)
    {
        const UTF8 *p = Moniker(m->list[i]);
        if (strchr((char *)p, ' '))
        {
            safe_chr('"', buff, &bp);
            safe_str(p, buff, &bp);
            safe_chr('"', buff, &bp);
        }
        else
        {
            safe_str(p, buff, &bp);
        }
        safe_chr(' ', buff, &bp);
    }
    *bp = '\0';

    raw_notify(player, buff);
    free_lbuf(buff);
}

static const UTF8 *Spaces(size_t n)
{
    static const UTF8 buffer[42] = "                                         ";

    if (n < sizeof(buffer)-1)
    {
        return buffer + (sizeof(buffer)-1) - n;
    }
    else
    {
        return T("");
    }
}

static int DCL_CDECL malias_compare(const void *first, const void *second)
{
    const malias_t* alias1 = (const malias_t*)first;
    const malias_t* alias2 = (const malias_t*)second;

    return mux_stricmp(alias1->name, alias2->name);
}

static void do_malias_list_all(dbref player)
{
    int actual_entries = 0;
    malias_t* alias_array = nullptr;
    try
    {
        alias_array = (malias_t*)MEMALLOC(sizeof(malias_t)*ma_top);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == alias_array)
    {
        return;
    }

    int i;
    for (i = 0; i < ma_top; i++)
    {
        malias_t *m = malias[i];
        if (  GOD == m->owner
           || m->owner == player
           || God(player))
        {
            alias_array[actual_entries].name = m->name;
            alias_array[actual_entries].desc = m->desc;
            alias_array[actual_entries].desc_width = m->desc_width;
            alias_array[actual_entries].owner = m->owner;
            ++actual_entries;
        }
    }
    qsort(alias_array, actual_entries, sizeof(malias_t), malias_compare);

    bool notified = false;
    for (i = 0; i < actual_entries; i++)
    {
        malias_t *m = &alias_array[i];
        if (!notified)
        {
            raw_notify(player, T("Name         Description                              Owner"));
            notified = true;
        }

        const UTF8 *pSpaces = Spaces(40 - m->desc_width);

        UTF8 *p = tprintf(T("%-12s %s%s %-15.15s"),
            m->name, m->desc, pSpaces, Moniker(m->owner));
        raw_notify(player, p);
    }
    raw_notify(player, T("*****  End of Mail Aliases *****"));
    MEMFREE(alias_array);
}

static void do_malias_switch(dbref player, UTF8 *a1, UTF8 *a2)
{
    if (a1 && *a1)
    {
        if (a2 && *a2)
        {
            do_malias_create(player, a1, a2);
        }
        else
        {
            do_malias_list(player, a1);
        }
    }
    else
    {
        do_malias_list_all(player);
    }
}

static void do_mail_cc(dbref player, UTF8 *arg, bool bBlind)
{
    if (!(Flags2(player) & PLAYER_MAILS))
    {
        raw_notify(player, T("MAIL: No mail message in progress."));
        return;
    }
    if (!arg || !*arg)
    {
        raw_notify(player, T("MAIL: I do not know whom you want to mail."));
        return;
    }

    UTF8 *tolist = make_numlist(player, arg, bBlind);
    if (!tolist)
    {
        return;
    }
    UTF8 *fulllist = alloc_lbuf("do_mail_cc");
    UTF8 *bp = fulllist;

    safe_str(tolist, fulllist, &bp);
    const UTF8 *pPlayerMailTo = atr_get_raw(player, A_MAILTO);
    if (pPlayerMailTo)
    {
        safe_chr(' ', fulllist, &bp);
        safe_str(pPlayerMailTo, fulllist, &bp);
    }
    *bp = '\0';

    atr_add_raw(player, A_MAILTO, fulllist);
    UTF8 *names = make_namelist(player, fulllist);
    raw_notify(player, tprintf(T("MAIL: You are sending mail to \xE2\x80\x98%s\xE2\x80\x99."), names));
    free_lbuf(names);
    free_lbuf(tolist);
    free_lbuf(fulllist);
}

static void mail_to_list(dbref player, UTF8 *list, UTF8 *subject, UTF8 *message, int flags, bool silent)
{
    if (!list)
    {
        return;
    }
    if (!*list)
    {
        free_lbuf(list);
        return;
    }

    // Construct a tolist which excludes all the Blind Carbon Copy (BCC)
    // recipients.
    //
    UTF8 *tolist = alloc_lbuf("mail_to_list");
    UTF8 *p = tolist;
    UTF8 *tail;
    UTF8 *head = list;
    while (*head)
    {
        while (*head == ' ')
        {
            head++;
        }

        tail = head;
        while (  *tail
              && *tail != ' ')
        {
            if (*tail == '"')
            {
                head++;
                tail++;
                while (  *tail
                      && *tail != '"')
                {
                    tail++;
                }
            }
            if (*tail)
            {
                tail++;
            }
        }
        tail--;
        if (*tail != '"')
        {
            tail++;
        }

        if (*head != '!')
        {
            if (p != tolist)
            {
                safe_chr(' ', tolist, &p);
            }
            memcpy(p, head, tail-head);
            p += tail-head;
        }

        // Get the next recipient.
        //
        head = tail;
        if (*head == '"')
        {
            head++;
        }
    }
    *p = '\0';

    int number = add_mail_message(player, message);
    if (number != NOTHING)
    {
        UTF8 spot;
        head = list;
        while (*head)
        {
            while (' ' == *head)
            {
                head++;
            }

            tail = head;
            while (  *tail
                  && *tail != ' ')
            {
                if (*tail == '"')
                {
                    head++;
                    tail++;
                    while (  *tail
                          && *tail != '"')
                    {
                        tail++;
                    }
                }
                if (*tail)
                {
                    tail++;
                }
            }
            tail--;
            if (*tail != '"')
            {
                tail++;
            }
            spot = *tail;
            *tail = '\0';

            if (*head == '!')
            {
                head++;
            }

            if (*head == '*')
            {
                do_malias_send(player, head, tolist, subject, number, flags, silent);
            }
            else
            {
                dbref target = mux_atol(head);
                if (  Good_obj(target)
                   && isPlayer(target))
                {
                    send_mail(player, target, tolist, subject, number, flags, silent);
                }
            }

            // Get the next recipient.
            //
            *tail = spot;
            head = tail;
            if (*head == '"')
            {
                head++;
            }
        }
        MessageReferenceDec(number);
    }
    free_lbuf(tolist);
    free_lbuf(list);
}

static void do_mail_quick(dbref player, UTF8 *arg1, UTF8 *arg2)
{
    if (!arg1 || !*arg1)
    {
        raw_notify(player, T("MAIL: I don\xE2\x80\x99t know who you want to mail."));
        return;
    }
    if (!arg2 || !*arg2)
    {
        raw_notify(player, T("MAIL: No message."));
        return;
    }
    if (Flags2(player) & PLAYER_MAILS)
    {
        raw_notify(player, T("MAIL: Mail message already in progress."));
        return;
    }
    if (  !Wizard(player)
       && ThrottleMail(player))
    {
        raw_notify(player, T("MAIL: Too much @mail sent recently."));
        return;
    }
    UTF8 *bufDest = alloc_lbuf("do_mail_quick");
    UTF8 *bpSubject = bufDest;

    mux_strncpy(bpSubject, arg1, LBUF_SIZE-1);
    parse_to(&bpSubject, '/', 1);

    if (!bpSubject)
    {
        raw_notify(player, T("MAIL: No subject."));
        free_lbuf(bufDest);
        return;
    }

    mail_to_list(player, make_numlist(player, bufDest, false), bpSubject, arg2, 0, false);
    free_lbuf(bufDest);
}

static void do_expmail_stop(dbref player, int flags)
{
    if ((Flags2(player) & PLAYER_MAILS) != PLAYER_MAILS)
    {
        raw_notify(player, T("MAIL: No message started."));
        return;
    }

    dbref aowner;
    dbref aflags;
    UTF8 *tolist = atr_get("do_expmail_stop.3854", player, A_MAILTO, & aowner, &aflags);
    if (*tolist == '\0')
    {
        raw_notify(player, T("MAIL: No recipients."));
        free_lbuf(tolist);
    }
    else
    {
        UTF8 *pMailMsg = atr_get("do_expmail_stop.3862", player, A_MAILMSG, &aowner, &aflags);
        if (*pMailMsg == '\0')
        {
            raw_notify(player, T("MAIL: The body of this message is empty.  Use - to add to the message."));
            free_lbuf(tolist);
        }
        else
        {
            UTF8 *mailsub   = atr_get("do_expmail_stop.3870", player, A_MAILSUB, &aowner, &aflags);
            UTF8 *mailflags = atr_get("do_expmail_stop.3871", player, A_MAILFLAGS, &aowner, &aflags);
            mail_to_list(player, tolist, mailsub, pMailMsg, flags | mux_atol(mailflags), false);
            free_lbuf(mailflags);
            free_lbuf(mailsub);

            Flags2(player) &= ~PLAYER_MAILS;
        }
        free_lbuf(pMailMsg);
    }
}

static void do_expmail_abort(dbref player)
{
    Flags2(player) &= ~PLAYER_MAILS;
    raw_notify(player, T("MAIL: Message aborted."));
}

void do_prepend(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *text, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!mudconf.have_mailer)
    {
        return;
    }

    if (Flags2(executor) & PLAYER_MAILS)
    {
        if (  !text
           || !*text)
        {
            raw_notify(executor, T("No text prepended."));
            return;
        }

        UTF8 *bufText = alloc_lbuf("do_prepend");
        UTF8 *bpText = bufText;
        mux_exec(text+1, LBUF_SIZE-1, bufText, &bpText, executor, caller, enactor,
                 eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, nullptr, 0);
        *bpText = '\0';

        dbref aowner;
        int aflags;

        UTF8 *oldmsg = atr_get("do_prepend.3915", executor, A_MAILMSG, &aowner, &aflags);
        if (*oldmsg)
        {
            UTF8 *newmsg = alloc_lbuf("do_prepend");
            UTF8 *bp = newmsg;
            safe_str(bufText, newmsg, &bp);
            safe_chr(' ', newmsg, &bp);
            safe_str(oldmsg, newmsg, &bp);
            *bp = '\0';
            atr_add_raw(executor, A_MAILMSG, newmsg);
            free_lbuf(newmsg);
        }
        else
        {
            atr_add_raw(executor, A_MAILMSG, bufText);
        }

        free_lbuf(bufText);
        free_lbuf(oldmsg);
        size_t nLen;

        atr_get_raw_LEN(executor, A_MAILMSG, &nLen);
        raw_notify(executor, tprintf(T("%d/%d characters prepended."), nLen, LBUF_SIZE-1));
    }
    else
    {
        raw_notify(executor, T("MAIL: No message in progress."));
    }
}

void do_postpend(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *text, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!mudconf.have_mailer)
    {
        return;
    }
    if (  text[1] == '-'
       && text[2] == '\0')
    {
        do_expmail_stop(executor, 0);
        return;
    }

    if (Flags2(executor) & PLAYER_MAILS)
    {
        if (  !text
           || !*text)
        {
            raw_notify(executor, T("No text added."));
            return;
        }

        UTF8 *bufText = alloc_lbuf("do_prepend");
        UTF8 *bpText = bufText;
        mux_exec(text+1, LBUF_SIZE-1, bufText, &bpText, executor, caller, enactor,
                 eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, nullptr, 0);
        *bpText = '\0';

        dbref aowner;
        int aflags;

        UTF8 *oldmsg = atr_get("do_postpend.3978", executor, A_MAILMSG, &aowner, &aflags);
        if (*oldmsg)
        {
            UTF8 *newmsg = alloc_lbuf("do_postpend");
            UTF8 *bp = newmsg;
            safe_str(oldmsg, newmsg, &bp);
            safe_chr(' ', newmsg, &bp);
            safe_str(bufText, newmsg, &bp);
            *bp = '\0';
            atr_add_raw(executor, A_MAILMSG, newmsg);
            free_lbuf(newmsg);
        }
        else
        {
            atr_add_raw(executor, A_MAILMSG, bufText);
        }

        free_lbuf(bufText);
        free_lbuf(oldmsg);
        size_t nLen;

        atr_get_raw_LEN(executor, A_MAILMSG, &nLen);
        raw_notify(executor, tprintf(T("%d/%d characters added."), nLen, LBUF_SIZE-1));
    }
    else
    {
        raw_notify(executor, T("MAIL: No message in progress."));
    }
}

static void do_edit_msg(dbref player, UTF8 *from, UTF8 *to)
{
    if (Flags2(player) & PLAYER_MAILS)
    {
        dbref aowner;
        int aflags;
        UTF8 *msg = atr_get("do_edit_msg.4014", player, A_MAILMSG, &aowner, &aflags);
        UTF8 *result = replace_string(from, to, msg);
        atr_add(player, A_MAILMSG, result, aowner, aflags);
        raw_notify(player, T("Text edited."));
        free_lbuf(result);
        free_lbuf(msg);
    }
    else
    {
        raw_notify(player, T("MAIL: No message in progress."));
    }
}

static void do_mail_proof(dbref player)
{
    if (!(Flags2(player) & PLAYER_MAILS))
    {
        raw_notify(player, T("MAIL: No message in progress."));
        return;
    }

    dbref aowner;
    int aflags;

    UTF8 *mailto   = atr_get("do_mail_proof.4038", player, A_MAILTO, &aowner, &aflags);
    UTF8 *pMailMsg = atr_get("do_mail_proof.4039", player, A_MAILMSG, &aowner, &aflags);
    UTF8 *names    = make_namelist(player, mailto);

    UTF8 szSubjectBuffer[MBUF_SIZE];
    StripTabsAndTruncate( atr_get_raw(player, A_MAILSUB), szSubjectBuffer,
                          MBUF_SIZE-1, 35);

    UTF8 szFromName[MBUF_SIZE];
    trimmed_name(player, szFromName, 16, 16, 0);

    raw_notify(player, (UTF8 *)DASH_LINE);
    raw_notify(player, tprintf(T("From:  %s  Subject: %s\nTo: %s"),
            szFromName, szSubjectBuffer, names));
    raw_notify(player, (UTF8 *)DASH_LINE);
    raw_notify(player, pMailMsg);
    raw_notify(player, (UTF8 *)DASH_LINE);
    free_lbuf(pMailMsg);
    free_lbuf(names);
    free_lbuf(mailto);
}

static void do_malias_desc(dbref player, UTF8 *alias, UTF8 *desc)
{
    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        raw_notify(player, tprintf(T("MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 not found."), alias));
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    if (  m->owner != GOD
       || ExpMail(player))
    {
        size_t nValidMailAliasDesc;
        bool   bValidMailAliasDesc;
        size_t nVisualWidth;
        UTF8 *pValidMailAliasDesc = MakeCanonicalMailAliasDesc
                                    (   desc,
                                        &nValidMailAliasDesc,
                                        &bValidMailAliasDesc,
                                        &nVisualWidth
                                    );

        if (bValidMailAliasDesc)
        {
            MEMFREE(m->desc);
            m->desc = StringCloneLen(pValidMailAliasDesc, nValidMailAliasDesc);
            m->desc_width = nVisualWidth;
            raw_notify(player, T("MAIL: Description changed."));
        }
        else
        {
            raw_notify(player, T("MAIL: Description is not valid."));
        }
    }
    else
    {
        raw_notify(player, T("MAIL: Permission denied."));
    }
}

static void do_malias_chown(dbref player, UTF8 *alias, UTF8 *owner)
{
    if (!ExpMail(player))
    {
        raw_notify(player, T("MAIL: You cannot do that!"));
        return;
    }

    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        raw_notify(player, tprintf(T("MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 not found."), alias));
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    dbref no = lookup_player(player, owner, true);
    if (no == NOTHING)
    {
        raw_notify(player, T("MAIL: I do not see that here."));
        return;
    }
    m->owner = no;
    raw_notify(player, T("MAIL: Owner changed for alias."));
}

static void do_malias_add(dbref player, UTF8 *alias, UTF8 *person)
{
    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        raw_notify(player, tprintf(T("MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 not found."), alias));
        return;
    }
    else if (nResult != GMA_FOUND)
    {
        return;
    }
    dbref thing = NOTHING;
    if (*person == '#')
    {
        thing = parse_dbref(person + 1);
        if (!isPlayer(thing))
        {
            raw_notify(player, T("MAIL: Only players may be added."));
            return;
        }
    }

    if (thing == NOTHING)
    {
        thing = lookup_player(player, person, true);
    }

    if (thing == NOTHING)
    {
        raw_notify(player, T("MAIL: I do not see that person here."));
        return;
    }

    if ((m->owner == GOD) && !ExpMail(player))
    {
        raw_notify(player, T("MAIL: Permission denied."));
        return;
    }
    int i;
    for (i = 0; i < m->numrecep; i++)
    {
        if (m->list[i] == thing)
        {
            raw_notify(player, T("MAIL: That person is already on the list."));
            return;
        }
    }

    if (i >= (MAX_MALIAS_MEMBERSHIP - 1))
    {
        raw_notify(player, T("MAIL: The list is full."));
        return;
    }

    m->list[m->numrecep] = thing;
    m->numrecep = m->numrecep + 1;
    raw_notify(player, tprintf(T("MAIL: %s added to %s"), Moniker(thing), m->name));
}

static void do_malias_remove(dbref player, UTF8 *alias, UTF8 *person)
{
    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        raw_notify(player, tprintf(T("MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 not found."), alias));
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    if ((m->owner == GOD) && !ExpMail(player))
    {
        raw_notify(player, T("MAIL: Permission denied."));
        return;
    }

    dbref thing = NOTHING;
    if (*person == '#')
    {
        thing = parse_dbref(person + 1);
    }
    if (thing == NOTHING)
    {
        thing = lookup_player(player, person, true);
    }
    if (thing == NOTHING)
    {
        raw_notify(player, T("MAIL: I do not see that person here."));
        return;
    }

    bool ok = false;
    for (int i = 0; i < m->numrecep; i++)
    {
        if (ok)
        {
            m->list[i] = m->list[i + 1];
        }
        else if (m->list[i] == thing)
        {
            m->list[i] = m->list[i + 1];
            ok = true;
        }
    }

    if (ok)
    {
        m->numrecep--;
        raw_notify(player, tprintf(T("MAIL: %s removed from alias %s."),
                   Moniker(thing), alias));
    }
    else
    {
        raw_notify(player, tprintf(T("MAIL: %s is not a member of alias %s."),
                   Moniker(thing), alias));
    }
}

static void do_malias_rename(dbref player, UTF8 *alias, UTF8 *newname)
{
    int nResult;
    malias_t *m = get_malias(player, newname, &nResult);
    if (nResult == GMA_FOUND)
    {
        raw_notify(player, T("MAIL: That name already exists!"));
        return;
    }
    if (nResult != GMA_NOTFOUND)
    {
        return;
    }
    m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        raw_notify(player, T("MAIL: I cannot find that alias!"));
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    if (!ExpMail(player) && !(m->owner == player))
    {
        raw_notify(player, T("MAIL: Permission denied."));
        return;
    }

    size_t nValidMailAlias;
    bool   bValidMailAlias;
    UTF8 *pValidMailAlias = MakeCanonicalMailAlias
                            (   newname+1,
                                &nValidMailAlias,
                                &bValidMailAlias
                            );
    if (bValidMailAlias)
    {
        MEMFREE(m->name);
        m->name = StringCloneLen(pValidMailAlias, nValidMailAlias);
        raw_notify(player, T("MAIL: Mailing Alias renamed."));
    }
    else
    {
        raw_notify(player, T("MAIL: Alias is not valid."));
    }
}

static void do_malias_delete(dbref player, UTF8 *alias)
{
    int nResult;
    malias_t *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        raw_notify(player, tprintf(T("MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 not found."), alias));
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    bool done = false;
    for (int i = 0; i < ma_top; i++)
    {
        if (done)
        {
            malias[i] = malias[i + 1];
        }
        else
        {
            if ((m->owner == player) || ExpMail(player))
            {
                if (m == malias[i])
                {
                    done = true;
                    raw_notify(player, T("MAIL: Alias Deleted."));
                    malias[i] = malias[i + 1];
                }
            }
        }
    }

    if (!done)
    {
        raw_notify(player, tprintf(T("MAIL: Alias \xE2\x80\x98%s\xE2\x80\x99 not found."), alias));
    }
    else
    {
        ma_top--;
    }
}

static void do_malias_adminlist(dbref player)
{
    if (!ExpMail(player))
    {
        do_malias_list_all(player);
        return;
    }
    raw_notify(player,
      T("Num  Name         Description                              Owner"));

    malias_t *m;
    int i;

    for (i = 0; i < ma_top; i++)
    {
        m = malias[i];
        const UTF8 *pSpaces = Spaces(40 - m->desc_width);
        raw_notify(player, tprintf(T("%-4d %-12s %s%s %-15.15s"),
                       i, m->name, m->desc, pSpaces,
                       Moniker(m->owner)));
    }

    raw_notify(player, T("***** End of Mail Aliases *****"));
}

static void do_malias_status(dbref player)
{
    if (!ExpMail(player))
    {
        raw_notify(player, T("MAIL: Permission denied."));
    }
    else
    {
        raw_notify(player, tprintf(T("MAIL: Number of mail aliases defined: %d"), ma_top));
        raw_notify(player, tprintf(T("MAIL: Allocated slots %d"), ma_size));
    }
}

static void malias_cleanup1(malias_t *m, dbref target)
{
    int count = 0;
    dbref j;
    for (int i = 0; i < m->numrecep; i++)
    {
         j = m->list[i];
         if (  !Good_obj(j)
            || j == target)
         {
            count++;
         }
         if (count)
         {
            m->list[i] = m->list[i + count];
         }
    }
    m->numrecep -= count;
}

void malias_cleanup(dbref player)
{
    for (int i = 0; i < ma_top; i++)
    {
        malias_cleanup1(malias[i], player);
    }
}

static void do_mail_retract1(dbref player, UTF8 *name, UTF8 *msglist)
{
    dbref target = lookup_player(player, name, true);
    if (target == NOTHING)
    {
        raw_notify(player, T("MAIL: No such player."));
        return;
    }
    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, target))
    {
        return;
    }

    int i = 0, j = 0;
    MailList ml(target);
    struct mail *mp;
    for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
    {
        if (mp->from == player)
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                j++;
                if (Unread(mp))
                {
                    ml.RemoveItem();
                    raw_notify(player, T("MAIL: Mail retracted."));
                }
                else
                {
                    raw_notify(player, T("MAIL: That message has been read."));
                }
            }
        }
    }

    if (!j)
    {
        // Ran off the end of the list without finding anything.
        //
        raw_notify(player, T("MAIL: No matching messages."));
    }
}

static void do_mail_retract(dbref player, UTF8 *name, UTF8 *msglist)
{
    if (*name == '*')
    {
        int pnResult;
        malias_t *m = get_malias(player, name, &pnResult);
        if (pnResult == GMA_NOTFOUND)
        {
            raw_notify(player, tprintf(T("MAIL: Mail alias %s not found."), name));
            return;
        }
        if (pnResult == GMA_FOUND)
        {
            for (int i = 0; i < m->numrecep; i++)
            {
                do_mail_retract1(player, tprintf(T("#%d"), m->list[i]), msglist);
            }
        }
    }
    else
    {
        do_mail_retract1(player, name, msglist);
    }
}

void do_malias
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *arg1,
    UTF8 *arg2,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!mudconf.have_mailer)
    {
        raw_notify(executor, T("Mailer is disabled."));
        return;
    }
    switch (key)
    {
    case 0:
        do_malias_switch(executor, arg1, arg2);
        break;
    case MALIAS_DESC:
        do_malias_desc(executor, arg1, arg2);
        break;
    case MALIAS_CHOWN:
        do_malias_chown(executor, arg1, arg2);
        break;
    case MALIAS_ADD:
        do_malias_add(executor, arg1, arg2);
        break;
    case MALIAS_REMOVE:
        do_malias_remove(executor, arg1, arg2);
        break;
    case MALIAS_DELETE:
        do_malias_delete(executor, arg1);
        break;
    case MALIAS_RENAME:
        do_malias_rename(executor, arg1, arg2);
        break;
    case 7:
        // empty
        break;
    case MALIAS_LIST:
        do_malias_adminlist(executor);
        break;
    case MALIAS_STATUS:
        do_malias_status(executor);
    }
}

void do_mail
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *arg1,
    UTF8 *arg2,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!mudconf.have_mailer)
    {
        raw_notify(executor, T("Mailer is disabled."));
        return;
    }

    // HACK: Fix to allow @mail/quick from objects.
    //
    if (  (key & ~MAIL_QUOTE) != MAIL_QUICK
       && !isPlayer(executor))
    {
        return;
    }

    switch (key & ~MAIL_QUOTE)
    {
    case 0:
        do_mail_stub(executor, arg1, arg2);
        break;
    case MAIL_STATS:
        do_mail_stats(executor, arg1, 0);
        break;
    case MAIL_DSTATS:
        do_mail_stats(executor, arg1, 1);
        break;
    case MAIL_FSTATS:
        do_mail_stats(executor, arg1, 2);
        break;
    case MAIL_DEBUG:
        do_mail_debug(executor, arg1, arg2);
        break;
    case MAIL_NUKE:
        do_mail_nuke(executor);
        break;
    case MAIL_FOLDER:
        do_mail_change_folder(executor, arg1, arg2);
        break;
    case MAIL_LIST:
        do_mail_list(executor, arg1, arg2, false);
        break;
    case MAIL_READ:
        do_mail_read(executor, arg1, arg2);
        break;
    case MAIL_CLEAR:
        do_mail_clear(executor, arg1);
        break;
    case MAIL_UNCLEAR:
        do_mail_unclear(executor, arg1);
        break;
    case MAIL_PURGE:
        do_mail_purge(executor);
        break;
    case MAIL_FILE:
        do_mail_file(executor, arg1, arg2);
        break;
    case MAIL_TAG:
        do_mail_tag(executor, arg1);
        break;
    case MAIL_UNTAG:
        do_mail_untag(executor, arg1);
        break;
    case MAIL_FORWARD:
        do_mail_fwd(executor, arg1, arg2);
        break;
    case MAIL_REPLY:
        do_mail_reply(executor, arg1, false, key);
        break;
    case MAIL_REPLYALL:
        do_mail_reply(executor, arg1, true, key);
        break;
    case MAIL_SEND:
        do_expmail_stop(executor, 0);
        break;
    case MAIL_EDIT:
        do_edit_msg(executor, arg1, arg2);
        break;
    case MAIL_URGENT:
        do_expmail_stop(executor, M_URGENT);
        break;
    case MAIL_ALIAS:
        do_malias_create(executor, arg1, arg2);
        break;
    case MAIL_ALIST:
        do_malias_list_all(executor);
        break;
    case MAIL_PROOF:
        do_mail_proof(executor);
        break;
    case MAIL_ABORT:
        do_expmail_abort(executor);
        break;
    case MAIL_QUICK:
        do_mail_quick(executor, arg1, arg2);
        break;
    case MAIL_REVIEW:
        do_mail_review(executor, arg1, arg2);
        break;
    case MAIL_RETRACT:
        do_mail_retract(executor, arg1, arg2);
        break;
    case MAIL_CC:
        do_mail_cc(executor, arg1, false);
        break;
    case MAIL_SAFE:
        do_mail_safe(executor, arg1);
        break;
    case MAIL_BCC:
        do_mail_cc(executor, arg1, true);
        break;
    }
}

struct mail *MailList::FirstItem(void)
{
    m_miHead = (struct mail *)hashfindLEN(&m_player, sizeof(m_player), &mudstate.mail_htab);
    m_mi = m_miHead;
    m_bRemoved = false;
    return m_mi;
}

struct mail *MailList::NextItem(void)
{
    if (!m_bRemoved)
    {
        if (nullptr != m_mi)
        {
            m_mi = m_mi->next;
            if (m_mi == m_miHead)
            {
                m_mi = nullptr;
            }
        }
    }
    m_bRemoved = false;
    return m_mi;
}

bool MailList::IsEnd(void)
{
    return (nullptr == m_mi);
}

MailList::MailList(dbref player)
{
    m_mi       = nullptr;
    m_miHead   = nullptr;
    m_player   = player;
    m_bRemoved = false;
}

void MailList::RemoveItem(void)
{
    if (  nullptr == m_mi
       || NOTHING == m_player)
    {
        return;
    }

    struct mail *miNext = m_mi->next;

    if (m_mi == m_miHead)
    {
        if (miNext == m_miHead)
        {
            hashdeleteLEN(&m_player, sizeof(m_player), &mudstate.mail_htab);
            miNext   = nullptr;
        }
        else
        {
            hashreplLEN(&m_player, sizeof(m_player), miNext, &mudstate.mail_htab);
        }
        m_miHead = miNext;
    }

    // Relink the list
    //
    m_mi->prev->next = m_mi->next;
    m_mi->next->prev = m_mi->prev;

    m_mi->next = nullptr;
    m_mi->prev = nullptr;
    MessageReferenceDec(m_mi->number);
    MEMFREE(m_mi->subject);
    m_mi->subject = nullptr;
    MEMFREE(m_mi->time);
    m_mi->time = nullptr;
    MEMFREE(m_mi->tolist);
    m_mi->tolist = nullptr;
    delete m_mi;

    m_mi = miNext;
    m_bRemoved = true;
}

void MailList::AppendItem(struct mail *miNew)
{
    struct mail *miHead = (struct mail *)
        hashfindLEN(&m_player, sizeof(m_player), &mudstate.mail_htab);

    if (miHead)
    {
        // Add new item to the end of the list.
        //
        struct mail *miEnd = miHead->prev;

        miNew->next = miHead;
        miNew->prev = miEnd;

        miHead->prev = miNew;
        miEnd->next  = miNew;
    }
    else
    {
        hashaddLEN(&m_player, sizeof(m_player), miNew, &mudstate.mail_htab);
        miNew->next = miNew;
        miNew->prev = miNew;
    }
}

void MailList::RemoveAll(void)
{
    struct mail *miHead = (struct mail *)
        hashfindLEN(&m_player, sizeof(m_player), &mudstate.mail_htab);

    if (nullptr != miHead)
    {
        hashdeleteLEN(&m_player, sizeof(m_player), &mudstate.mail_htab);
    }

    struct mail *mi;
    struct mail *miNext;
    for (mi = miHead; nullptr != mi; mi = miNext)
    {
        miNext = mi->next;
        if (miNext == miHead)
        {
            miNext = nullptr;
        }
        MessageReferenceDec(mi->number);
        MEMFREE(mi->subject);
        mi->subject = nullptr;
        MEMFREE(mi->tolist);
        mi->tolist = nullptr;
        MEMFREE(mi->time);
        mi->time = nullptr;
        delete mi;
    }
    m_mi = nullptr;
}

static void ListMailInFolderNumber(dbref player, int folder_num, UTF8 *msglist)
{
    int original_folder = player_folder(player);
    set_player_folder(player, folder_num);

    struct mail_selector ms;

    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }
    int i = 0;
    UTF8 *time;
    UTF8 szSubjectBuffer[MBUF_SIZE];

    raw_notify(player, tprintf(T(FOLDER_LINE), folder_num));

    MailList ml(player);
    struct mail *mp;
    for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
    {
        if (Folder(mp) == folder_num)
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                time = mail_list_time(mp->time);
                size_t nSize = MessageFetchSize(mp->number);

                UTF8 szFromName[MBUF_SIZE];
                trimmed_name(mp->from, szFromName, 16, 16, 0);

                StripTabsAndTruncate(mp->subject, szSubjectBuffer,
                        MBUF_SIZE-1, 25);

                raw_notify(player, tprintf(T("[%s] %-3d (%4d) From: %s Sub: %s"),
                            status_chars(mp), i, nSize, szFromName,
                            szSubjectBuffer));
                free_lbuf(time);
            }
        }
    }
    raw_notify(player, (UTF8 *)DASH_LINE);

    set_player_folder(player, original_folder);

}

static void ListMailInFolder(dbref player, UTF8 *folder_name, UTF8 *msglist)
{
    int folder = 0;

    if (  nullptr == folder_name
       || '\0' == folder_name[0])
    {
        folder = player_folder(player);
    }
    else
    {
        folder = parse_folder(player, folder_name);
    }

    if (-1 == folder)
    {
        raw_notify(player, T("MAIL: No such folder."));
        return;
    }
    ListMailInFolderNumber(player, folder, msglist);
}

void do_folder
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *arg1,
    UTF8 *arg2,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    switch (key)
    {
    case FOLDER_FILE:
        do_mail_file(executor, arg1, arg2);
        break;

    case FOLDER_LIST:
        ListMailInFolder(executor, arg1, arg2);
        break;

    case FOLDER_READ:
        do_mail_read(executor, arg1, arg2);
        break;

    case FOLDER_SET:
        do_mail_change_folder(executor, arg1, arg2);
        break;

    default:
        if (  nullptr == arg1
           || '\0' == arg1[0])
        {
            DoListMailBrief(executor);
        }
        else if (2 == nargs)
        {
            do_mail_read(executor, arg1, arg2);
        }
        else
        {
            do_mail_change_folder(executor, arg1, arg2);
        }
        break;
    }
}
