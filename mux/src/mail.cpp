// mail.cpp
//
// $Id: mail.cpp,v 1.33 2002-08-03 19:34:21 sdennis Exp $
//
// This code was taken from Kalkin's DarkZone code, which was
// originally taken from PennMUSH 1.50 p10, and has been heavily modified
// since being included in MUX.
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <sys/types.h>

#include "attrs.h"
#include "powers.h"

static void do_mail_flags(dbref, char *, mail_flag, BOOL);
static int  player_folder(dbref);
static BOOL parse_msglist(char *, struct mail_selector *, dbref);
static BOOL mail_match(struct mail *, struct mail_selector, int);
static int  parse_folder(dbref, char *);
static char *status_chars(struct mail *);
static char *status_string(struct mail *);
void add_folder_name(dbref, int, char *);
static char *get_folder_name(dbref, int);
static char *mail_list_time(const char *);
static char *make_numlist(dbref, char *);
static char *make_namelist(char *);
static void mail_to_list(dbref, char *, char *, char *, int, BOOL);

#define SIZEOF_MALIAS 13
#define WIDTHOF_MALIASDESC 40
#define SIZEOF_MALIASDESC (WIDTHOF_MALIASDESC*2)

#define MAX_MALIAS_MEMBERSHIP 100
struct malias 
{
    int owner;
    char *name;
    char *desc;
    int desc_width; // The visual width of the Mail Alias Description.
    int numrecep;
    dbref list[MAX_MALIAS_MEMBERSHIP];
};

int ma_size = 0;
int ma_top = 0;

struct malias **malias = NULL;

// Handling functions for the database of mail messages.
//

// mail_db_grow - We keep a database of mail text, so if we send a
// message to more than one player, we won't have to duplicate the
// text.
//
#define MAIL_FUDGE 1
static void mail_db_grow(int newtop)
{
    int i;
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

        MENT *newdb = (MENT *)MEMALLOC((newsize + MAIL_FUDGE) * sizeof(MENT));
        (void)ISOUTOFMEMORY(newdb);
        if (mudstate.mail_list)
        {
            mudstate.mail_list -= MAIL_FUDGE;
            memcpy( newdb,
                    mudstate.mail_list,
                    (mudstate.mail_db_top + MAIL_FUDGE) * sizeof(MENT));
            MEMFREE(mudstate.mail_list);
            mudstate.mail_list = NULL;
        }
        mudstate.mail_list = newdb + MAIL_FUDGE;
        newdb = NULL;
        mudstate.mail_db_size = newsize;
    }

    // Initialize new parts of the mail bag.
    //
    for (i = mudstate.mail_db_top; i < newtop; i++)
    {
        mudstate.mail_list[i].m_nRefs = 0;
        mudstate.mail_list[i].m_pMessage = NULL;
    }
    mudstate.mail_db_top = newtop;
}

// MessageReferenceInc - Increments the reference count for any
// particular message.
//
static DCL_INLINE void MessageReferenceInc(int number)
{
    mudstate.mail_list[number].m_nRefs++;
}

// MessageReferenceCheck - Checks whether the reference count for
// any particular message indicates that the message body should be
// freed. Also checks that if a message point is null, that the
// reference count is zero.
//
static void MessageReferenceCheck(int number)
{
    MENT &m = mudstate.mail_list[number];
    if (m.m_nRefs <= 0)
    {
        if (m.m_pMessage)
        {
            MEMFREE(m.m_pMessage);
            m.m_pMessage = NULL;
        }
    }
    if (m.m_pMessage == NULL)
    {
        m.m_nRefs = 0;
    }
}

// MessageReferenceDec - Decrements the reference count for a message, and
// will also delete the message if the counter reaches 0.
//
static void MessageReferenceDec(int number)
{
    mudstate.mail_list[number].m_nRefs--;
    MessageReferenceCheck(number);
}

// MessageFetch - returns the text for a particular message number. This
// text should not be modified.
//
char *MessageFetch(int number)
{
    MessageReferenceCheck(number);
    if (mudstate.mail_list[number].m_pMessage)
    {
        return mudstate.mail_list[number].m_pMessage;
    }
    else
    {
        return "MAIL: This mail message does not exist in the database. Please alert your admin.";
    }
}

// This function returns a reference to the message and the the
// reference count is increased to reflect that.
//
static int MessageAdd(char *pMessage)
{
    if (!mudstate.mail_list)
    {
        mail_db_grow(1);
    }

    int i;
    MENT *pm;
    BOOL bFound = FALSE;
    for (i = 0; i < mudstate.mail_db_top; i++)
    {
        pm = &mudstate.mail_list[i];
        if (pm->m_pMessage == NULL)
        {
            pm->m_nRefs = 0;
            bFound = TRUE;
            break;
        }
    }
    if (!bFound)
    {
        mail_db_grow(i + 1);
    }

    pm = &mudstate.mail_list[i];
    pm->m_pMessage = StringClone(pMessage);
    MessageReferenceInc(i);
    return i;
}

// add_mail_message - adds a new text message to the mail database, and returns
// a unique number for that message.
//
// IF return value is !NOTHING, you have a reference to the message,
// and the reference count reflects that.
//
static int add_mail_message(dbref player, char *message)
{
    if (!_stricmp(message, "clear"))
    {
        notify(player, "MAIL: You probably don't wanna send mail saying 'clear'.");
        return NOTHING;
    }

    // Evaluate signature.
    //
    int   aflags;
    dbref aowner;
    char *bp = alloc_lbuf("add_mail_message");
    char *atrstr = atr_get(player, A_SIGNATURE, &aowner, &aflags);
    char *execstr = bp; 
    char *str = atrstr;
    TinyExec(execstr, &bp, player, player, player,
             EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, (char **)NULL, 0);
    *bp = '\0';
    char *msg = bp = alloc_lbuf("add_mail_message.2");
    str = message;
    TinyExec(msg, &bp, player, player, player,
             EV_EVAL | EV_FCHECK | EV_NO_COMPRESS, &str, (char **)NULL, 0);
    *bp = '\0';

    // Save message body and return a reference to it.
    //
    int number = MessageAdd(tprintf("%s %s", msg, execstr));
    free_lbuf(atrstr);
    free_lbuf(execstr);
    free_lbuf(msg);
    return number;
}


// This function is -only- used from reading from the disk, and so
// it does -not- manage the reference counts.
//
static BOOL MessageAddWithNumber(int i, char *pMessage)
{
    mail_db_grow(i+1);

    MENT *pm = &mudstate.mail_list[i];
    pm->m_pMessage = StringClone(pMessage);
    return TRUE;
}

// new_mail_message - used for reading messages in from disk which
// already have a number assigned to them.
//
// This function is -only- used from reading from the disk, and so
// it does -not- manage the reference counts.
//
static void new_mail_message(char *message, int number)
{
    BOOL bTruncated = FALSE;
    if (strlen(message) > LBUF_SIZE-1)
    {
        bTruncated = TRUE;
        message[LBUF_SIZE-1] = '\0';
    }
    MessageAddWithNumber(number, message);
    if (bTruncated)
    {
        STARTLOG(LOG_BUGS, "BUG", "MAIL");
        log_text(tprintf("new_mail_message: Mail message %d truncated.", number));
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

void set_player_folder(dbref player, int fnum)
{
    // Set a player's folder to fnum.
    //
    char *tbuf1 = alloc_lbuf("set_player_folder");
    Tiny_ltoa(fnum, tbuf1);
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
    free_lbuf(tbuf1);
}



// Change or rename a folder
//
void do_mail_change_folder(dbref player, char *fld, char *newname)
{
    int pfld;

    if (!fld || !*fld)
    {
        // Check mail in all folders
        //
        for (pfld = MAX_FOLDERS; pfld >= 0; pfld--)
        {
            check_mail(player, pfld, TRUE);
        }
        pfld = player_folder(player);
        notify(player, tprintf("MAIL: Current folder is %d [%s].",
                       pfld, get_folder_name(player, pfld)));
        return;
    }
    pfld = parse_folder(player, fld);
    if (pfld < 0)
    {
        notify(player, "MAIL: What folder is that?");
        return;
    }
    if (newname && *newname)
    {
        // We're changing a folder name here
        //
        if (strlen(newname) > FOLDER_NAME_LEN)
        {
            notify(player, "MAIL: Folder name too long");
            return;
        }
        char *p;
        for (p = newname; Tiny_IsAlphaNumeric[(unsigned char)*p]; p++) ;
        if (*p != '\0')
        {
            notify(player, "MAIL: Illegal folder name");
            return;
        }

        add_folder_name(player, pfld, newname);
        notify(player, tprintf("MAIL: Folder %d now named '%s'", pfld, newname));
    }
    else
    {
        // Set a new folder
        //
        set_player_folder(player, pfld);
        notify(player, tprintf("MAIL: Current folder set to %d [%s].",
                       pfld, get_folder_name(player, pfld)));
    }
}

void do_mail_tag(dbref player, char *msglist)
{
    do_mail_flags(player, msglist, M_TAG, FALSE);
}

void do_mail_safe(dbref player, char *msglist)
{
    do_mail_flags(player, msglist, M_SAFE, FALSE);
}

void do_mail_clear(dbref player, char *msglist)
{
    do_mail_flags(player, msglist, M_CLEARED, FALSE);
}

void do_mail_untag(dbref player, char *msglist)
{
    do_mail_flags(player, msglist, M_TAG, TRUE);
}

void do_mail_unclear(dbref player, char *msglist)
{
    do_mail_flags(player, msglist, M_CLEARED, TRUE);
}

// Adjust the flags of a set of messages.
// If negate is TRUE, clear the flag.
static void do_mail_flags(dbref player, char *msglist, mail_flag flag, BOOL negate)
{
    struct mail_selector ms;

    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }
    struct mail *mp;
    int i = 0, j = 0;
    int folder = player_folder(player);
    for (mp = (struct mail *)hashfindLEN(&player, sizeof(player), &mudstate.mail_htab); mp; mp = mp->next)
    {
        if (All(ms) || (Folder(mp) == folder))
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
                    notify(player, tprintf("MAIL: Msg #%d %s.", i, negate ? "untagged" : "tagged"));
                    break;

                case M_CLEARED:
                    if (Unread(mp) && !negate)
                    {
                        notify(player, tprintf("MAIL: Unread Msg #%d cleared! Use @mail/unclear %d to recover.", i, i));
                    }
                    else
                    {
                        notify(player, tprintf("MAIL: Msg #%d %s.", i, negate ? "uncleared" : "cleared"));
                    }
                    break;

                case M_SAFE:
                    notify(player, tprintf("MAIL: Msg #%d marked safe.", i));
                    break;
                }
            }
        }
    }

    if (!j)
    {
        // Ran off the end of the list without finding anything.
        //
        notify(player, "MAIL: You don't have any matching messages!");
    }
}

// Change a message's folder.
//
void do_mail_file(dbref player, char *msglist, char *folder)
{
    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }
    int foldernum;
    if ((foldernum = parse_folder(player, folder)) == -1)
    {
        notify(player, "MAIL: Invalid folder specification");
        return;
    }
    struct mail *mp;
    int i = 0, j = 0;
    int origfold = player_folder(player);
    for (mp = (struct mail *)hashfindLEN(&player, sizeof(player), &mudstate.mail_htab); mp; mp = mp->next)
    {
        if (All(ms) || (Folder(mp) == origfold))
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                j++;

                // Clear the folder.
                //
                mp->read &= M_FMASK;
                mp->read |= FolderBit(foldernum);
                notify(player, tprintf("MAIL: Msg %d filed in folder %d", i, foldernum));
            }
        }
    }

    if (!j)
    {
        // Ran off the end of the list without finding anything.
        //
        notify(player, "MAIL: You don't have any matching messages!");
    }
}

void do_mail_read(dbref player, char *msglist)
{
    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }

    char *status, *names;
    int i = 0, j = 0;
    char *buff = alloc_lbuf("do_mail_read.1");
    int folder = player_folder(player);
    struct mail *mp = (struct mail *)hashfindLEN( &player, sizeof(player), &mudstate.mail_htab);
    for ( ; mp; mp = mp->next)
    {
        if (Folder(mp) == folder)
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                // Read it.
                //
                j++;
                buff[LBUF_SIZE-1] = '\0';
                strncpy(buff, MessageFetch(mp->number), LBUF_SIZE);
                if (buff[LBUF_SIZE-1] != '\0')
                {
                    STARTLOG(LOG_BUGS, "BUG", "MAIL");
                    log_text(tprintf("do_mail_read: %s: Mail message %d truncated.", Name(player), mp->number));
                    ENDLOG;
                    buff[LBUF_SIZE-1] = '\0';
                }
                notify(player, DASH_LINE);
                status = status_string(mp);
                names = make_namelist(mp->tolist);
                char szSubjectBuffer[MBUF_SIZE];
                int iRealVisibleWidth;
                ANSI_TruncateToField(mp->subject, sizeof(szSubjectBuffer),
                    szSubjectBuffer, 65, &iRealVisibleWidth, ANSI_ENDGOAL_NORMAL);
                notify(player, tprintf("%-3d         From:  %-*s  At: %-25s  %s\r\nFldr   : %-2d Status: %s\r\nTo     : %-65s\r\nSubject: %s",
                               i, PLAYER_NAME_LIMIT - 6, Name(mp->from),
                               mp->time,
                               (Connected(mp->from) &&
                               (!Hidden(mp->from) || See_Hidden(player))) ?
                               " (Conn)" : "      ", folder,
                               status,
                               names,
                               szSubjectBuffer));
                free_lbuf(names);
                free_lbuf(status);
                notify(player, DASH_LINE);
                notify(player, buff);
                notify(player, DASH_LINE);
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

    if (!j)
    {
        // Ran off the end of the list without finding anything.
        //
        notify(player, "MAIL: You don't have that many matching messages!");
    }
}

void do_mail_review(dbref player, char *name, char *msglist)
{
    dbref target = lookup_player(player, name, TRUE);
    if (target == NOTHING)
    {
        notify(player, "MAIL: No such player.");
        return;
    }

    struct mail *mp;
    struct mail_selector ms;
    int i = 0, j = 0;
    char *msg, *status, *bp, *str;
    int iRealVisibleWidth;
    char szSubjectBuffer[MBUF_SIZE];

    if (!msglist || !*msglist)
    {
        notify(player, tprintf("--------------------   MAIL: %-25s   ------------------", Name(target)));
        for (mp = (struct mail *)hashfindLEN(&target, sizeof(target), &mudstate.mail_htab); mp; mp = mp->next)
        {
            if (mp->from == player)
            {
                i++;
                ANSI_TruncateToField(mp->subject, sizeof(szSubjectBuffer),
                    szSubjectBuffer, 25, &iRealVisibleWidth, ANSI_ENDGOAL_NORMAL);
                notify(player, tprintf("[%s] %-3d (%4d) From: %-*s Sub: %s",
                               status_chars(mp),
                               i, strlen(MessageFetch(mp->number)),
                               PLAYER_NAME_LIMIT - 6, Name(mp->from),
                               szSubjectBuffer));
            }
        }
        notify(player, DASH_LINE);
    }
    else
    {
        if (!parse_msglist(msglist, &ms, target))
        {
            return;
        }
        for (mp = (struct mail *)hashfindLEN(&target, sizeof(target), &mudstate.mail_htab); mp; mp = mp->next)
        {
            if (mp->from == player)
            {
                i++;
                if (mail_match(mp, ms, i))
                {
                    j++;
                    status = status_string(mp);
                    msg = bp = alloc_lbuf("do_mail_review");
                    str = MessageFetch(mp->number);
                    TinyExec(msg, &bp, player, player, player,
                             EV_EVAL | EV_FCHECK | EV_NO_COMPRESS, &str,
                             (char **)NULL, 0);
                    *bp = '\0';
                    ANSI_TruncateToField(mp->subject, sizeof(szSubjectBuffer),
                        szSubjectBuffer, 65, &iRealVisibleWidth, ANSI_ENDGOAL_NORMAL);
                    notify(player, DASH_LINE);
                    notify(player, tprintf("%-3d         From:  %-*s  At: %-25s  %s\r\nFldr   : %-2d Status: %s\r\nSubject: %s",
                                   i, PLAYER_NAME_LIMIT - 6, Name(mp->from),
                                   mp->time,
                                   (Connected(mp->from) &&
                                   (!Hidden(mp->from) || See_Hidden(player))) ?
                                   " (Conn)" : "      ", 0,
                                   status, szSubjectBuffer));
                    free_lbuf(status);
                    notify(player, DASH_LINE);
                    notify(player, msg);
                    notify(player, DASH_LINE);
                    free_lbuf(msg);
                }
            }
        }

        if (!j)
        {
            // Ran off the end of the list without finding anything.
            //
            notify(player, "MAIL: You don't have that many matching messages!");
        }
    }
}

void do_mail_list(dbref player, char *msglist, BOOL sub)
{
    struct mail_selector ms;

    if (!parse_msglist(msglist, &ms, player))
    {
        return;
    }
    struct mail *mp;
    int i = 0;
    char *time;
    int iRealVisibleWidth;
    char szSubjectBuffer[MBUF_SIZE];
    int folder = player_folder(player);

    notify(player, 
        tprintf("---------------------------   MAIL: Folder %d   ----------------------------", folder));

    for (mp = (struct mail *)hashfindLEN(&player, sizeof(player), &mudstate.mail_htab); mp; mp = mp->next)
    {
        if (Folder(mp) == folder)
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                time = mail_list_time(mp->time);
                if (sub)
                {
                    ANSI_TruncateToField(mp->subject, sizeof(szSubjectBuffer),
                        szSubjectBuffer, 25, &iRealVisibleWidth, ANSI_ENDGOAL_NORMAL);
                    notify(player, tprintf("[%s] %-3d (%4d) From: %-*s Sub: %s",
                                   status_chars(mp), i, strlen(MessageFetch(mp->number)),
                           PLAYER_NAME_LIMIT - 6, Name(mp->from), szSubjectBuffer));
                }
                else
                {
                    notify(player, tprintf("[%s] %-3d (%4d) From: %-*s At: %s %s",
                                   status_chars(mp), i, strlen(MessageFetch(mp->number)),
                           PLAYER_NAME_LIMIT - 6, Name(mp->from), time,
                            ((Connected(mp->from) && (!Hidden(mp->from) || See_Hidden(player))) ? "Conn" : " ")));
                }
                free_lbuf(time);
            }
        }
    }
    notify(player, DASH_LINE);
}

static char *mail_list_time(const char *the_time)
{
    char *p = (char *)the_time;
    char *new0 = alloc_lbuf("mail_list_time");
    char *q = new0;
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

void do_mail_purge(dbref player)
{
    struct mail *mp, *nextp;

    // Go through player's mail, and remove anything marked cleared.
    //
    for (mp = (struct mail *)hashfindLEN(&player, sizeof(player), &mudstate.mail_htab); mp; mp = nextp)
    {
        if (Cleared(mp))
        {
            // Delete this one.
            // Head and tail of the list are special.
            //
            if (mp->prev == NULL)
            {
                if (mp->next == NULL)
                {
                    hashdeleteLEN(&player, sizeof(player), &mudstate.mail_htab);
                }
                else
                {
                    hashreplLEN(&player, sizeof(player), (int *)(mp->next), &mudstate.mail_htab);
                }
            }
            else if (mp->next == NULL)
            {
                mp->prev->next = NULL;
            }

            // relink the list
            //
            if (mp->prev != NULL)
            {
                mp->prev->next = mp->next;
            }
            if (mp->next != NULL)
            {
                mp->next->prev = mp->prev;
            }

            nextp = mp->next;
            MessageReferenceDec(mp->number);
            MEMFREE(mp->subject);
            mp->subject = NULL;
            MEMFREE(mp->time);
            mp->time = NULL;
            MEMFREE(mp->tolist);
            mp->tolist = NULL;
            MEMFREE(mp);
            mp = NULL;
        }
        else
        {
            nextp = mp->next;
        }
    }
    notify(player, "MAIL: Mailbox purged.");
}

void do_expmail_start(dbref player, char *arg, char *subject)
{
    if (!arg || !*arg)
    {
        notify(player, "MAIL: I do not know whom you want to mail.");
        return;
    }
    if (!subject || !*subject)
    {
        notify(player, "MAIL: No subject.");
        return;
    }
    if (Flags2(player) & PLAYER_MAILS)
    {
        notify(player, "MAIL: Mail message already in progress.");
        return;
    }
    char *tolist = make_numlist(player, arg);
    if (!tolist) 
    {
        return;
    }

    atr_add_raw(player, A_MAILTO, tolist);
    atr_add_raw(player, A_MAILSUB, subject);
    atr_add_raw(player, A_MAILFLAGS, "0");
    atr_clr(player, A_MAILMSG);
    Flags2(player) |= PLAYER_MAILS;
    char *names = make_namelist(tolist);
    notify(player, tprintf("MAIL: You are sending mail to '%s'.", names));
    free_lbuf(names);
    free_lbuf(tolist);
}

void do_mail_fwd(dbref player, char *msg, char *tolist)
{
    if (Flags2(player) & PLAYER_MAILS)
    {
        notify(player, "MAIL: Mail message already in progress.");
        return;
    }
    if (!msg || !*msg)
    {
        notify(player, "MAIL: No message list.");
        return;
    }
    if (!tolist || !*tolist)
    {
        notify(player, "MAIL: To whom should I forward?");
        return;
    }
    int num = Tiny_atol(msg);
    if (!num)
    {
        notify(player, "MAIL: I don't understand that message number.");
        return;
    }
    struct mail *mp = mail_fetch(player, num);
    if (!mp)
    {
        notify(player, "MAIL: You can't forward non-existent messages.");
        return;
    }
    do_expmail_start(player, tolist, tprintf("%s (fwd from %s)", mp->subject, Name(mp->from)));
    atr_add_raw(player, A_MAILMSG, MessageFetch(mp->number));
    const char *pValue = atr_get_raw(player, A_MAILFLAGS);
    int iFlag = M_FORWARD;
    if (pValue)
    {
        iFlag |= Tiny_atol(pValue);
    }
    atr_add_raw(player, A_MAILFLAGS, Tiny_ltoa_t(iFlag));
}

void do_mail_reply(dbref player, char *msg, BOOL all, int key)
{
    if (Flags2(player) & PLAYER_MAILS)
    {
        notify(player, "MAIL: Mail message already in progress.");
        return;
    }
    if (!msg || !*msg)
    {
        notify(player, "MAIL: No message list.");
        return;
    }
    int num = Tiny_atol(msg);
    if (!num)
    {
        notify(player, "MAIL: I don't understand that message number.");
        return;
    }
    struct mail *mp = mail_fetch(player, num);
    if (!mp)
    {
        notify(player, "MAIL: You can't reply to non-existent messages.");
        return;
    }
    char *tolist = alloc_lbuf("do_mail_reply.tolist");
    char *bp = tolist;
    if (all)
    {
        char *names = alloc_lbuf("do_mail_reply.names");
        char *oldlist = alloc_lbuf("do_mail_reply.oldlist");
        bp = names;
        *bp = '\0';

        strcpy(oldlist, mp->tolist);

        TINY_STRTOK_STATE tts;
        Tiny_StrTokString(&tts, oldlist);
        Tiny_StrTokControl(&tts, " ");
        char *p;
        for (p = Tiny_StrTokParse(&tts); p; p = Tiny_StrTokParse(&tts))
        {
            if (Tiny_atol(p) != mp->from)
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
        strcpy(tolist, names);
        free_lbuf(names);
    }
    else
    {
        safe_chr('#', tolist, &bp);
        safe_ltoa(mp->from, tolist, &bp);
        *bp = '\0';
    }

    const char *pSubject = mp->subject;
    char *pMessage = MessageFetch(mp->number);
    const char *pTime = mp->time;
    if (strncmp(pSubject, "Re:", 3))
    {
        do_expmail_start(player, tolist, tprintf("Re: %s", pSubject));
    }
    else
    {
        do_expmail_start(player, tolist, tprintf("%s", pSubject));
    }
    if (key & MAIL_QUOTE)
    {
        char *pMessageBody =
            tprintf("On %s, %s wrote:\r\n\r\n%s\r\n\r\n********** End of included message from %s\r\n",
                pTime, Name(mp->from), pMessage, Name(mp->from));
        atr_add_raw(player, A_MAILMSG, pMessageBody);
    }

    // The following combination of atr_get_raw() with atr_add_raw() is OK
    // because we are not passing a pointer to atr_add_raw() that came
    // directly from atr_get_raw().
    //
    const char *pValue = atr_get_raw(player, A_MAILFLAGS);
    int iFlag = M_REPLY;
    if (pValue)
    {
        iFlag |= Tiny_atol(pValue);
    }
    atr_add_raw(player, A_MAILFLAGS, Tiny_ltoa_t(iFlag));

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
    struct mail *mp;
    int i = 0;

    for (mp = (struct mail *)hashfindLEN(&player, sizeof(player), &mudstate.mail_htab); mp; mp = mp->next)
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
    return NULL;
}

// Returns count of read, unread, and cleared messages as rcount, ucount, ccount.
//
void count_mail(dbref player, int folder, int *rcount, int *ucount, int *ccount)
{
    struct mail *mp;
    int rc, uc, cc;

    cc = rc = uc = 0;
    for (mp = (struct mail *)hashfindLEN(&player, sizeof(player), &mudstate.mail_htab); mp; mp = mp->next)
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

void urgent_mail(dbref player, int folder, int *ucount)
{
    struct mail *mp;
    int uc = 0;

    for (mp = (struct mail *)hashfindLEN(&player, sizeof(player), &mudstate.mail_htab); mp; mp = mp->next)
    {
        if (Folder(mp) == folder)
        {
            if (!(Read(mp)) && (Urgent(mp)))
            {
                uc++;
            }
        }
    }
    *ucount = uc;
}

static void send_mail
(
    dbref player,
    dbref target,
    const char *tolist,
    const char *subject,
    int number,
    mail_flag flags,
    BOOL silent
)
{
    if (!isPlayer(target))
    {
        notify(player, "MAIL: You cannot send mail to non-existent people.");
        return;
    }
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();

    char *pTimeStr = ltaNow.ReturnDateString(0);

    // Initialize the appropriate fields.
    //
    struct mail *newp = (struct mail *)MEMALLOC(sizeof(struct mail));
    (void)ISOUTOFMEMORY(newp);
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
    newp->tolist = StringClone(tolist);
    newp->number = number;
    MessageReferenceInc(number);
    newp->time = StringClone(pTimeStr);
    newp->subject = StringClone(subject);

    // Send to folder 0
    //
    newp->read = flags & M_FMASK;

    // If this is the first message, it is the head and the tail.
    //
    BOOL bFailed = FALSE;
    struct mail *mptr = (struct mail *)hashfindLEN(&target, sizeof(target), &mudstate.mail_htab);
    if (mptr)
    {
        while (mptr->next != NULL)
        {
            mptr = mptr->next;
        }
        mptr->next = newp;
        newp->next = NULL;
        newp->prev = mptr;
    }
    else
    {
        hashaddLEN(&target, sizeof(target), (int *)newp, &mudstate.mail_htab);
        newp->next = NULL;
        newp->prev = NULL;
    }

    if (!bFailed)
    {
        // notify people
        //
        if (!silent)
        {
            notify(player, tprintf("MAIL: You sent your message to %s.", Name(target)));
        }

        notify(target, tprintf("MAIL: You have a new message from %s.", Name(player)));
        did_it(player, target, A_MAIL, NULL, 0, NULL, A_AMAIL, NULL, NOTHING);
    }
}

void do_mail_nuke(dbref player)
{
    if (!God(player))
    {
        notify(player, "The postal service issues a warrant for your arrest.");
        return;
    }

    // Walk the list.
    //
    for (dbref thing = 0; thing < mudstate.db_top; thing++)
    {
        struct mail *mp = (struct mail *)hashfindLEN(&thing, sizeof(thing), &mudstate.mail_htab);
        while (mp)
        {
            struct mail *nextp = mp->next;
            MessageReferenceDec(mp->number);
            MEMFREE(mp->subject);
            mp->subject = NULL;
            MEMFREE(mp->tolist);
            mp->tolist = NULL;
            MEMFREE(mp->time);
            mp->time = NULL;
            MEMFREE(mp);
            mp = NULL;
            mp = nextp;
        }
        hashdeleteLEN(&thing, sizeof(thing), &mudstate.mail_htab);
    }
    log_text(tprintf("** MAIL PURGE ** done by %s(#%d).", Name(player), player));
    notify(player, "You annihilate the post office. All messages cleared.");
}

void do_mail_debug(dbref player, char *action, char *victim)
{
    if (!Wizard(player))
    {
        notify(player, "Go get some bugspray.");
        return;
    }

    struct mail *mp, *nextp;
    dbref thing;
    if (string_prefix("clear", action))
    {
        dbref target = lookup_player(player, victim, TRUE);
        if (target == NOTHING)
        {
            init_match(player, victim, NOTYPE);
            match_absolute();
            target = match_result();
        }
        if (target == NOTHING)
        {
            notify(player, tprintf("%s: no such player.", victim));
            return;
        }
        if (Wizard(target))
        {
            notify(player, tprintf("Let %s clear their own @mail.", Name(target)));
            return;
        }
        do_mail_clear(target, NULL);
        do_mail_purge(target);
        notify(player, tprintf("Mail cleared for %s(#%d).", Name(target), target));
        return;
    }
    else if (string_prefix("sanity", action))
    {
        int *ai = (int *)MEMALLOC(mudstate.mail_db_top * sizeof(int));
        (void)ISOUTOFMEMORY(ai);
        memset(ai, 0, mudstate.mail_db_top * sizeof(int));

        MAIL_ITER_ALL(mp, thing)
        {
            BOOL bGoodReference;
            if (0 <= mp->number && mp->number < mudstate.mail_db_top)
            {
                ai[mp->number]++;
                bGoodReference = TRUE;
            }
            else
            {
                bGoodReference = FALSE;
            }
            if (!Good_obj(mp->to))
            {
                if (bGoodReference)
                {
                    notify(player, tprintf("Bad object #%d has mail.", mp->to));
                }
                else
                {
                    notify(player, tprintf("Bad object #%d has mail which refers to a non-existent mailbag item.", mp->to));
                }
            }
            else if (!isPlayer(mp->to))
            {
                if (bGoodReference)
                {
                    notify(player, tprintf("%s(#%d) has mail, but is not a player.",
                             Name(mp->to), mp->to));
                }
                else
                {
                    notify(player, tprintf("%s(#%d) is not a player, but has mail which refers to a non-existent mailbag item.",
                             Name(mp->to), mp->to));
                }
            }
            else if (!bGoodReference)
            {
                notify(player, tprintf("%s(#%d) has mail which refers to a non-existent mailbag item.", Name(mp->to), mp->to));
            }
        }

        // Check ref counts.
        //
        if (mudstate.mail_list)
        {
            int i;
            int nCountHigher = 0;
            int nCountLower  = 0;
            for (i = 0; i < mudstate.mail_db_top; i++)
            {
                if (mudstate.mail_list[i].m_nRefs < ai[i])
                {
                    nCountLower++;
                }
                else if (mudstate.mail_list[i].m_nRefs > ai[i])
                {
                    nCountHigher++;
                }
            }
            if (nCountLower)
            {
                notify(player, "Some mailbag items are referred to more often than the mailbag item indicates.");
            }
            if (nCountHigher)
            {
                notify(player, "Some mailbag items are referred to less often than the mailbag item indicates.");
            }
        }
        MEMFREE(ai);
        ai = NULL;
        notify(player, "Mail sanity check completed.");
    }
    else if (string_prefix("fix", action))
    {
        // First, we should fixup the reference counts.
        //
        if (mudstate.mail_list)
        {
            notify(player, tprintf("Re-counting mailbag reference counts."));
            int *ai = (int *)MEMALLOC(mudstate.mail_db_top * sizeof(int));
            (void)ISOUTOFMEMORY(ai);
            memset(ai, 0, mudstate.mail_db_top * sizeof(int));

            MAIL_ITER_ALL(mp, thing)
            {
                if (0 <= mp->number && mp->number < mudstate.mail_db_top)
                {
                    ai[mp->number]++;
                }
                else
                {
                    mp->number = NOTHING;
                }
            }
            int i;
            int nCountWrong = 0;
            for (i = 0; i < mudstate.mail_db_top; i++)
            {
                if (mudstate.mail_list[i].m_nRefs != ai[i])
                {
                    mudstate.mail_list[i].m_nRefs = ai[i];
                    nCountWrong++;
                }
            }
            if (nCountWrong)
            {
                notify(player, "Some reference counts were wrong [FIXED].");
            }
            MEMFREE(ai);
            ai = NULL;
        }

        notify(player, tprintf("Removing @mail that is associated with non-players."));

        // Now, remove all mail to non-good or non-players, or mail that
        // points to non-existent mailbag items.
        //
        MAIL_ITER_SAFE(mp, thing, nextp)
        {
            if (!Good_obj(mp->to) || !isPlayer(mp->to) || mp->number == NOTHING)
            {
                // Delete this item.
                //
                notify(player, tprintf("Fixing mail for #%d.", mp->to));

                if (mp->prev == NULL)
                {
                    if (mp->next == NULL)
                    {
                        hashdeleteLEN(&player, sizeof(player), &mudstate.mail_htab);
                    }
                    else
                    {
                        hashreplLEN(&player, sizeof(player), (int *)(mp->next), &mudstate.mail_htab);
                    }
                }
                else if (mp->next == NULL)
                {
                    mp->prev->next = NULL;
                }

                if (mp->prev != NULL)
                {
                    mp->prev->next = mp->next;
                }
                if (mp->next != NULL)
                {
                    mp->next->prev = mp->prev;
                }

                nextp = mp->next;
                MessageReferenceDec(mp->number);
                MEMFREE(mp->subject);
                mp->subject = NULL;
                MEMFREE(mp->time);
                mp->time = NULL;
                MEMFREE(mp->tolist);
                mp->tolist = NULL;
                MEMFREE(mp);
                mp = NULL;
            }
            else
            {
                nextp = mp->next;
            }
        }
        notify(player, "Mail sanity fix completed.");
    }
    else
    {
        notify(player, "That is not a debugging option.");
        return;
    }
}

void do_mail_stats(dbref player, char *name, int full)
{
    dbref target, thing;
    int fc, fr, fu, tc, tr, tu, fchars, tchars, cchars, count;
    fc = fr = fu = tc = tr = tu = fchars = tchars = cchars = count = 0;

    // Find player.
    //
    if ((*name == '\0') || !name)
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
        target = Tiny_atol(&name[1]);
        if (!Good_obj(target) || !isPlayer(target))
        {
            target = NOTHING;
        }
    }
    else if (!_stricmp(name, "me"))
    {
        target = player;
    }
    else
    {
        target = lookup_player(player, name, TRUE);
    }

    if (target == NOTHING)
    {
        init_match(player, name, NOTYPE);
        match_absolute();
        target = match_result();
    }
    if (target == NOTHING)
    {
        notify(player, tprintf("%s: No such player.", name));
        return;
    }
    if (!Wizard(player) && (target != player))
    {
        notify(player, "The post office protects privacy!");
        return;
    }

    // This comand is computationally expensive.
    //
    if (!payfor(player, mudconf.searchcost))
    {
        notify(player, tprintf("Finding mail stats costs %d %s.",
                       mudconf.searchcost,
                       (mudconf.searchcost == 1) ? mudconf.one_coin : mudconf.many_coins));
        return;
    }
    struct mail *mp;
    if (target == AMBIGUOUS) 
    {  
        // stats for all
        if (full == 0)
        {
            MAIL_ITER_ALL(mp, thing)
            {
                count++;
            }
            notify(player, tprintf("There are %d messages in the mail spool.", count));
            return;
        }
        else if (full == 1)
        {
            MAIL_ITER_ALL(mp, thing)
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
            notify(player,
                   tprintf("MAIL: There are %d msgs in the mail spool, %d unread, %d cleared.",
                       fc + fr + fu, fu, fc));
            return;
        }
        else
        {
            MAIL_ITER_ALL(mp, thing)
            {
                if (Cleared(mp))
                {
                    fc++;
                    cchars += strlen(MessageFetch(mp->number));
                }
                else if (Read(mp))
                {
                    fr++;
                    fchars += strlen(MessageFetch(mp->number));
                }
                else
                {
                    fu++;
                    tchars += strlen(MessageFetch(mp->number));
                }
            }
            notify(player, tprintf("MAIL: There are %d old msgs in the mail spool, totalling %d characters.", fr, fchars));
            notify(player, tprintf("MAIL: There are %d new msgs in the mail spool, totalling %d characters.", fu, tchars));
            notify(player, tprintf("MAIL: There are %d cleared msgs in the mail spool, totalling %d characters.", fc, cchars));
            return;
        }
    }

    // individual stats
    //
    if (full == 0)
    {
        // Just count the number of messages.
        //
        MAIL_ITER_ALL(mp, thing)
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
        notify(player, tprintf("%s sent %d messages.", Name(target), fr));
        notify(player, tprintf("%s has %d messages.", Name(target), tr));
        return;
    }
    // more detailed message count
    char last[50];
    MAIL_ITER_ALL(mp, thing)
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
                fchars += strlen(MessageFetch(mp->number));
            }
        }
        if (mp->to == target)
        {
            if (!tr && !tu)
            {
                strcpy(last, mp->time);
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
                tchars += strlen(MessageFetch(mp->number));
            }
        }
    }

    notify(player, tprintf("Mail statistics for %s:", Name(target)));

    if (full == 1)
    {
        notify(player, tprintf("%d messages sent, %d unread, %d cleared.",
                       fc + fr + fu, fu, fc));
        notify(player, tprintf("%d messages received, %d unread, %d cleared.",
                       tc + tr + tu, tu, tc));
    }
    else
    {
        notify(player,
               tprintf("%d messages sent, %d unread, %d cleared, totalling %d characters.",
                   fc + fr + fu, fu, fc, fchars));
        notify(player,
               tprintf("%d messages received, %d unread, %d cleared, totalling %d characters.",
                   tc + tr + tu, tu, tc, tchars));
    }

    if (tc + tr + tu > 0)
    {
        notify(player, tprintf("Last is dated %s", last));
    }
}


/*-------------------------------------------------------------------------*
 *   Main mail routine for @mail w/o a switch
 *-------------------------------------------------------------------------*/

void do_mail_stub(dbref player, char *arg1, char *arg2)
{
    if (!arg1 || !*arg1)
    {
        if (arg2 && *arg2)
        {
            notify(player, "MAIL: Invalid mail command.");
            return;
        }

        // Just the "@mail" command.
        //
        do_mail_list(player, arg1, TRUE);
        return;
    }

    // purge a player's mailbox
    //
    if (!_stricmp(arg1, "purge"))
    {
        do_mail_purge(player);
        return;
    }

    // clear message
    //
    if (!_stricmp(arg1, "clear"))
    {
        do_mail_clear(player, arg2);
        return;
    }
    if (!_stricmp(arg1, "unclear"))
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
        if (Tiny_IsDigit[(unsigned char)*arg1] && !strchr(arg1, '-'))
        {
            do_mail_read(player, arg1);
        }
        else
        {
            do_mail_list(player, arg1, TRUE);
        }
        return;
    }
}

void malias_write(FILE *fp)
{
    int i, j;
    struct malias *m;

    putref(fp, ma_top);
    for (i = 0; i < ma_top; i++)
    {
        m = malias[i];
        fprintf(fp, "%d %d\n", m->owner, m->numrecep);
        fprintf(fp, "N:%s\n", m->name);
        fprintf(fp, "D:%s\n", m->desc);
        for (j = 0; j < m->numrecep; j++)
        {
            putref(fp, m->list[j]);
        }
    }
}

void save_malias(FILE *fp)
{
    fprintf(fp, "*** Begin MALIAS ***\n");
    malias_write(fp);
}

int dump_mail(FILE *fp)
{
    struct mail *mp, *mptr;
    dbref thing;
    int count = 0, i;

    // Write out version number
    //
    fprintf(fp, "+V5\n");
    putref(fp, mudstate.mail_db_top);
    DO_WHOLE_DB(thing)
    {
        if (isPlayer(thing))
        {
            mptr = (struct mail *)hashfindLEN(&thing, sizeof(thing), &mudstate.mail_htab);
            if (mptr != NULL)
            {
                for (mp = mptr; mp; mp = mp->next)
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
    }

    fprintf(fp, "*** END OF DUMP ***\n");

    // Add the db of mail messages
    //
    for (i = 0; i < mudstate.mail_db_top; i++)
    {
        if (mudstate.mail_list[i].m_nRefs > 0)
        {
            putref(fp, i);
            putstring(fp, MessageFetch(i));
        }
    }
    fprintf(fp, "+++ END OF DUMP +++\n");
    save_malias(fp);

    return count;
}

void SaveMailStruct(struct mail *mp)
{
    dbref nTo = mp->to;
    struct mail *mptr = (struct mail *)hashfindLEN(&nTo, sizeof(nTo), &mudstate.mail_htab);
    if (mptr)
    {
        // Find the end of the list the hard way.
        //
        while (mptr->next != NULL)
        {
            mptr = mptr->next;
        }
        mptr->next = mp;
        mp->prev = mptr;
    }
    else
    {
        mp->prev = NULL;
        hashaddLEN(&nTo, sizeof(nTo), (int *)mp, &mudstate.mail_htab);
    }
    mp->next = NULL;
}

void load_mail_V5(FILE *fp)
{
    int mail_top = getref(fp);
    mail_db_grow(mail_top + 1);

    char nbuf1[8];
    char *p = fgets(nbuf1, sizeof(nbuf1), fp);
    while (p && strncmp(nbuf1, "***", 3) != 0)
    {
        struct mail *mp = (struct mail *)MEMALLOC(sizeof(struct mail));
        (void)ISOUTOFMEMORY(mp);

        mp->to      = Tiny_atol(nbuf1);
        mp->from    = getref(fp);

        mp->number  = getref(fp);
        MessageReferenceInc(mp->number);
        mp->tolist  = StringClone(getstring_noalloc(fp, TRUE));

        mp->time    = StringClone(getstring_noalloc(fp, TRUE));
        mp->subject = StringClone(getstring_noalloc(fp, TRUE));
        mp->read    = getref(fp);
        SaveMailStruct(mp);

        p = fgets(nbuf1, sizeof(nbuf1), fp);
    }

    p = fgets(nbuf1, sizeof(nbuf1), fp);
    while (p && strncmp(nbuf1, "+++", 3))
    {
        int number = Tiny_atol(nbuf1);
        new_mail_message(getstring_noalloc(fp, TRUE), number);
        p = fgets(nbuf1, sizeof(nbuf1), fp);
    }
}

// A mail alias can be any combination of upper-case letters, lower-case
// letters, and digits. No leading digits. No symbols. No ANSI. Length is
// limited to SIZEOF_MALIAS-1. Case is preserved.
//
char *MakeCanonicalMailAlias
(
    char *pMailAlias,
    int *pnValidMailAlias,
    BOOL *pbValidMailAlias
)
{
    if (  !pMailAlias
       || !Tiny_IsAlpha[(unsigned char)pMailAlias[0]])
    {
        return NULL;
    }

    static char Buffer[SIZEOF_MALIAS];
    char *p = Buffer;

    *p++ = pMailAlias[0];
    pMailAlias += 1;
    int nLeft = (sizeof(Buffer)-1) - 1;

    while (*pMailAlias && nLeft)
    {
        if (  !Tiny_IsAlpha[(unsigned char)*pMailAlias]
           && !Tiny_IsDigit[(unsigned char)*pMailAlias])
        {
            return Buffer;
        }
        *p = *pMailAlias;
        p++;
        pMailAlias++;
        nLeft--;
    }
    *p = '\0';

    *pnValidMailAlias = p - Buffer;
    *pbValidMailAlias = TRUE;
    return Buffer;
}

// A mail alias description can be any combination of upper-case letters,
// lower-case letters, digits, blanks, and symbols. ANSI is permitted.
// Length is limited to SIZEOF_MALIASDESC-1. Visual width is limited to
// WIDTHOF_MALIASDESC. Case is preserved.
//
char *MakeCanonicalMailAliasDesc
(
    char *pMailAliasDesc,
    int *pnValidMailAliasDesc,
    BOOL *pbValidMailAliasDesc,
    int *pnVisualWidth
)
{
    if (!pMailAliasDesc)
    {
        return NULL;
    }

    // First, remove all '\r\n\t' from the string.
    //
    char *Buffer = RemoveSetOfCharacters(pMailAliasDesc, "\r\n\t");

    // Optimize/terminate any ANSI in the string.
    //
    *pnVisualWidth = 0;
    static char szFittedMailAliasDesc[SIZEOF_MALIASDESC];
    *pnValidMailAliasDesc = ANSI_TruncateToField
                            ( Buffer,
                              SIZEOF_MALIASDESC,
                              szFittedMailAliasDesc,
                              WIDTHOF_MALIASDESC,
                              pnVisualWidth,
                              ANSI_ENDGOAL_NORMAL
                             );
    *pbValidMailAliasDesc = TRUE;
    return szFittedMailAliasDesc;
}

void malias_read(FILE *fp)
{
    int i, j;

    i = getref(fp);
    if (i <= 0)
    {
        return;
    }
    char buffer[LBUF_SIZE];
    struct malias *m;

    ma_size = ma_top = i;

    malias = (struct malias **)MEMALLOC(sizeof(struct malias *) * ma_size);
    (void)ISOUTOFMEMORY(malias);

    for (i = 0; i < ma_top; i++)
    {
        // Format is: "%d %d\n", &(m->owner), &(m->numrecep)
        //
        if (!fgets(buffer, sizeof(buffer), fp))
        {
            // We've hit the end of the file. Set the last recognized
            // @malias, and give up.
            //
            ma_top = i;
            return;
        }

        m = (struct malias *)MEMALLOC(sizeof(struct malias));
        (void)ISOUTOFMEMORY(m);
        malias[i] = m;

        char *p = strchr(buffer, ' ');
        m->owner = m->numrecep = 0;
        if (p)
        {
            m->owner = Tiny_atol(buffer);
            m->numrecep = Tiny_atol(p+1);
        }

        // The format of @malias name is "N:<name>\n".
        //
        int nLen = GetLineTrunc(buffer, sizeof(buffer), fp);
        buffer[nLen-1] = '\0'; // Get rid of trailing '\n'.
        int  nMailAlias;
        BOOL bMailAlias;
        char *pMailAlias = MakeCanonicalMailAlias( buffer+2,
                                                   &nMailAlias,
                                                   &bMailAlias);
        if (bMailAlias)
        {
            m->name = StringCloneLen(pMailAlias, nMailAlias);
        }
        else
        {
            m->name = StringCloneLen("Invalid", 7);
        }

        // The format of the description is "D:<description>\n"
        //
        nLen = GetLineTrunc(buffer, sizeof(buffer), fp);
        int  nMailAliasDesc;
        BOOL bMailAliasDesc;
        int  nVisualWidth;
        char *pMailAliasDesc = MakeCanonicalMailAliasDesc( buffer+2,
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
            m->desc = StringCloneLen("Invalid Desc", 12);
            m->desc_width = 12;
        }

        if (m->numrecep > 0)
        {
            for (j = 0; j < m->numrecep; j++)
            {
                m->list[j] = getref(fp);
            }
        }
        else
        {
            m->list[0] = 0;
        }
    }
}

void load_malias(FILE *fp)
{
    char buffer[200];

    getref(fp);
    if (  fscanf(fp, "*** Begin %s ***\n", buffer) == 1
       && !strcmp(buffer, "MALIAS"))
    {
        malias_read(fp);
    }
    else
    {
        Log.WriteString("ERROR: Couldn't find Begin MALIAS." ENDLINE);
        return;
    }
}

void load_mail(FILE *fp)
{
    char nbuf1[8];

    // Read the version number.
    //
    if (!fgets(nbuf1, sizeof(nbuf1), fp))
    {
        return;
    }
    if (strncmp(nbuf1, "+V5", 3) == 0)
    {
        load_mail_V5(fp);
    }
    else
    {
        return;
    }
    load_malias(fp);
}

static int get_folder_number(dbref player, char *name)
{
    // Look up a folder name and return the appropriate number.
    int aflags;
    dbref aowner;
    char *atrstr = atr_get(player, A_MAILFOLDERS, &aowner, &aflags);
    if (!*atrstr)
    {
        free_lbuf(atrstr);
        return -1;
    }

    char *str = alloc_lbuf("get_folder_num_str");
    char *pat = alloc_lbuf("get_folder_num_pat");
    char *bp = pat;
    strcpy(str, atrstr);
    _strupr(name);
    safe_tprintf_str(pat, &bp, ":%s:", name);
    char *res = strstr(str, pat);
    if (!res)
    {
        free_lbuf(str);
        free_lbuf(pat);
        free_lbuf(atrstr);
        return -1;
    }
    res += 2 + strlen(name);
    char *p = res;
    while (!Tiny_IsSpace[(unsigned char)*p])
    {
        p++;
    }
    p = '\0';
    free_lbuf(atrstr);
    free_lbuf(str);
    free_lbuf(pat);
    return Tiny_atol(res);
}

static char *get_folder_name(dbref player, int fld)
{
    // Get the name of the folder, or "nameless"
    char *pat = alloc_lbuf("get_folder_name");
    sprintf(pat, "%d:", fld);
    static char str[LBUF_SIZE];

    int flags;
    char *atrstr = atr_get(player, A_MAILFOLDERS, &player, &flags);
    if (!*atrstr)
    {
        strcpy(str, "unnamed");
        free_lbuf(pat);
        free_lbuf(atrstr);
        return str;
    }
    strcpy(str, atrstr);
    free_lbuf(atrstr);
    static char *old = (char *)string_match(str, pat);
    if (old)
    {
        char *r = old + strlen(pat);
        while (*r != ':')
        {
            r++;
        }
        *r = '\0';
        int len = strlen(pat);
        free_lbuf(pat);
        return old + len;
    }
    else
    {
        strcpy(str, "unnamed");
        free_lbuf(pat);
        return str;
    }
}

void add_folder_name(dbref player, int fld, char *name)
{

    // Muck with the player's MAILFOLDERS attrib to add a string of the form:
    // number:name:number to it, replacing any such string with a matching
    // number.
    //
    char *new0 = alloc_lbuf("add_folder_name.new");
    char *pat  = alloc_lbuf("add_folder_name.pat");
    char *str  = alloc_lbuf("add_folder_name.str");
    char *tbuf = alloc_lbuf("add_folder_name.tbuf");

    _strupr(name);
    sprintf(new0, "%d:%s:%d ", fld, name, fld);
    sprintf(pat, "%d:", fld);

    // get the attrib and the old string, if any
    char *old = NULL;
    int aflags;

    char *atrstr = atr_get(player, A_MAILFOLDERS, &player, &aflags);
    if (*atrstr)
    {
        strcpy(str, atrstr);
        old = (char *)string_match(str, pat);
    }

    char *res, *r;
    if (old && *old)
    {
        strcpy(tbuf, str);
        r = old;
        while (!Tiny_IsSpace[(unsigned char)*r])
        {
            r++;
        }
        *r = '\0';
        res = replace_string(old, new0, tbuf);
    }
    else
    {
        r = res = alloc_lbuf("mail_folders");
        if (*atrstr)
        {
            safe_str(str, res, &r);
        }
        safe_str(new0, res, &r);
        *r = '\0';
    }

    // put the attrib back
    //
    atr_add(player, A_MAILFOLDERS, res, player, AF_MDARK | AF_WIZARD | AF_NOPROG | AF_LOCK);
    free_lbuf(str);
    free_lbuf(pat);
    free_lbuf(new0);
    free_lbuf(tbuf);
    free_lbuf(atrstr);
    free_lbuf(res);
}

static int player_folder(dbref player)
{
    // Return the player's current folder number. If they don't have one, set
    // it to 0.
    //
    int flags;
    char *atrstr = atr_pget(player, A_MAILCURF, &player, &flags);
    if (!*atrstr)
    {
        free_lbuf(atrstr);
        set_player_folder(player, 0);
        return 0;
    }
    int number = Tiny_atol(atrstr);
    free_lbuf(atrstr);
    return number;
}

static int parse_folder(dbref player, char *folder_string)
{
    // Given a string, return a folder #, or -1 The string is just a number,
    // for now. Later, this will be where named folders are handled.
    //
    if (!folder_string || !*folder_string)
    {
        return -1;
    }
    if (Tiny_IsDigit[(unsigned char)*folder_string])
    {
        int fnum = Tiny_atol(folder_string);
        if ((fnum < 0) || (fnum > MAX_FOLDERS))
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

static BOOL mail_match(struct mail *mp, struct mail_selector ms, int num)
{
    // Does a piece of mail match the mail_selector?
    //
    if (ms.low && num < ms.low)
    {
        return FALSE;
    }
    if (ms.high && num > ms.high)
    {
        return FALSE;
    }
    if (ms.player && mp->from != ms.player)
    {
        return FALSE;
    }

    mail_flag mpflag = Read(mp) ? mp->read : (mp->read | M_MSUNREAD);
    if (!(ms.flags & M_ALL) && !(ms.flags & mpflag))
    {
        return FALSE;
    }

    if (ms.days == -1) 
    {
        return TRUE;
    }

    // Get the time now, subtract mp->time, and compare the results with
    // ms.days (in manner of ms.day_comp)
    //
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();

    const char *pMailTimeStr = mp->time;

    CLinearTimeAbsolute ltaMail;
    if (ltaMail.SetString(pMailTimeStr))
    {
        CLinearTimeDelta ltd(ltaMail, ltaNow);
        int iDiffDays = ltd.ReturnDays();
        if (sign(iDiffDays - ms.days) == ms.day_comp)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL parse_msglist(char *msglist, struct mail_selector *ms, dbref player)
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
        return TRUE;
    }

    char *p = msglist;
    while (Tiny_IsSpace[(unsigned char)*p])
    {
        p++;
    }

    if (*p == '\0')
    {
        return TRUE;
    }

    if (Tiny_IsDigit[(unsigned char)*p])
    {
        // Message or range.
        //
        char *q = strchr(p, '-');
        if (q)
        {
            // We have a subrange, split it up and test to see if it is valid.
            //
            q++;
            ms->low = Tiny_atol(p);
            if (ms->low <= 0)
            {
                notify(player, "MAIL: Invalid message range");
                return FALSE;
            }
            if (*q == '\0')
            {
                // Unbounded range.
                //
                ms->high = 0;
            }
            else
            {
                ms->high = Tiny_atol(q);
                if (ms->low > ms->high)
                {
                    notify(player, "MAIL: Invalid message range");
                    return FALSE;
                }
            }
        }
        else
        {
            // A single message.
            //
            ms->low = ms->high = Tiny_atol(p);
            if (ms->low <= 0)
            {
                notify(player, "MAIL: Invalid message number");
                return FALSE;
            }
        }
    }
    else
    {
        switch (Tiny_ToUpper[(unsigned char)*p])
        {
        case '-':

            // Range with no start.
            //
            p++;
            if (*p == '\0')
            {
                notify(player, "MAIL: Invalid message range");
                return FALSE;
            }
            ms->high = Tiny_atol(p);
            if (ms->high <= 0)
            {
                notify(player, "MAIL: Invalid message range");
                return FALSE;
            }
            break;

        case '~':

            // Exact # of days old.
            //
            p++;
            if (*p == '\0')
            {
                notify(player, "MAIL: Invalid age");
                return FALSE;
            }
            ms->day_comp = 0;
            ms->days = Tiny_atol(p);
            if (ms->days < 0)
            {
                notify(player, "MAIL: Invalid age");
                return FALSE;
            }
            break;

        case '<':

            // Less than # of days old.
            //
            p++;
            if (*p == '\0')
            {
                notify(player, "MAIL: Invalid age");
                return FALSE;
            }
            ms->day_comp = -1;
            ms->days = Tiny_atol(p);
            if (ms->days < 0)
            {
                notify(player, "MAIL: Invalid age");
                return FALSE;
            }
            break;

        case '>':

            // Greater than # of days old.
            //
            p++;
            if (*p == '\0')
            {
                notify(player, "MAIL: Invalid age");
                return FALSE;
            }
            ms->day_comp = 1;
            ms->days = Tiny_atol(p);
            if (ms->days < 0)
            {
                notify(player, "MAIL: Invalid age");
                return FALSE;
            }
            break;

        case '#':

            // From db#.
            //
            p++;
            if (*p == '\0')
            {
                notify(player, "MAIL: Invalid dbref #");
                return FALSE;
            }
            ms->player = Tiny_atol(p);
            if (!Good_obj(ms->player) || !(ms->player))
            {
                notify(player, "MAIL: Invalid dbref #");
                return FALSE;
            }
            break;

        case '*':

            // From player name.
            //
            p++;
            if (*p == '\0')
            {
                notify(player, "MAIL: Invalid player");
                return FALSE;
            }
            ms->player = lookup_player(player, p, TRUE);
            if (ms->player == NOTHING)
            {
                notify(player, "MAIL: Invalid player");
                return FALSE;
            }
            break;

#if 0
        case 'A':

            // All messages, all folders
            //
            ms->flags = M_ALL;
            break;
#endif

        case 'U':

            // Urgent, Unread
            //
            p++;
            if (*p == '\0')
            {
                notify(player, "MAIL: U is ambiguous (urgent or unread?)");
                return FALSE;
            }
            switch (Tiny_ToUpper[(unsigned char)*p])
            {
            case 'R':

                // Urgent
                //
                ms->flags = M_URGENT;
                break;

            case 'N':

                // Unread
                //
                ms->flags = M_MSUNREAD;
                break;

            default:

                // Bad
                //
                notify(player, "MAIL: Invalid message specification");
                return FALSE;
                break;
            }
            break;

        case 'R':

            // Read
            //
            ms->flags = M_ISREAD;
            break;

        case 'C':

            // Cleared.
            //
            ms->flags = M_CLEARED;
            break;

        case 'T':

            // Tagged.
            //
            ms->flags = M_TAG;
            break;

        case 'M':

            // Mass, me.
            //
            p++;
            if (*p == '\0')
            {
                notify(player, "MAIL: M is ambiguous (mass or me?)");
                return FALSE;
            }
            switch (Tiny_ToUpper[(unsigned char)*p])
            {
            case 'A':

                ms->flags = M_MASS;
                break;

            case 'E':

                ms->player = player;
                break;

            default:

                notify(player, "MAIL: Invalid message specification");
                return FALSE;
                break;
            }
            break;

        default:

            // Bad news.
            //
            notify(player, "MAIL: Invalid message specification");
            return FALSE;
            break;
        }
    }
    return TRUE;
}

void check_mail_expiration(void)
{
    // Negative values for expirations never expire.
    //
    if (0 > mudconf.mail_expiration)
    {
        return;
    }

    struct mail *mp, *nextp;
    dbref thing;
    int expire_secs = mudconf.mail_expiration * 86400;

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();

    CLinearTimeAbsolute ltaMail;
    MAIL_ITER_SAFE(mp, thing, nextp)
    {
        if (M_Safe(mp))
        {
            nextp = mp->next;
            continue;
        }

        const char *pMailTimeStr = mp->time;
        if (!ltaMail.SetString(pMailTimeStr))
        {
            nextp = mp->next;
            continue;
        }

        CLinearTimeDelta ltd(ltaMail, ltaNow);
        if (ltd.ReturnSeconds() <= expire_secs)
        {
            nextp = mp->next;
            continue;
        }

        // Delete this one.
        //
        // head and tail of the list are special.
        //
        dbref nTo = mp->to;
        if (mp->prev == NULL)
        {
            if (mp->next == NULL)
            {
                hashdeleteLEN(&nTo, sizeof(nTo), &mudstate.mail_htab);
            }
            else
            {
                hashreplLEN(&nTo, sizeof(nTo), (int *)(mp->next), &mudstate.mail_htab);
            }
        }
        else if (mp->next == NULL)
        {
            mp->prev->next = NULL;
        }

        // relink the list
        //
        if (mp->prev != NULL)
        {
            mp->prev->next = mp->next;
        }
        if (mp->next != NULL)
        {
            mp->next->prev = mp->prev;
        }

        nextp = mp->next;
        MessageReferenceDec(mp->number);
        MEMFREE(mp->subject);
        mp->subject = NULL;
        MEMFREE(mp->tolist);
        mp->tolist = NULL;
        MEMFREE(mp->time);
        mp->time = NULL;
        MEMFREE(mp);
        mp = NULL;
    }
}

static char *status_chars(struct mail *mp)
{
    // Return a short description of message flags.
    //
    static char res[10];

    char *p = res;
    *p++ = Read(mp) ? '-' : 'N';
    *p++ = M_Safe(mp) ? 'S' : '-';
    *p++ = Cleared(mp) ? 'C' : '-';
    *p++ = Urgent(mp) ? 'U' : '-';
    *p++ = Mass(mp) ? 'M' : '-';
    *p++ = Forward(mp) ? 'F' : '-';
    *p++ = Tagged(mp) ? '+' : '-';
    *p = '\0';
    return res;
}


#define NUM_MAILSTATUSTABLE 7
struct tag_mailstatusentry
{
    int nMask;
    char *pYes;
    int   nYes;
    char *pNo;
    int   nNo;
}
aMailStatusTable[NUM_MAILSTATUSTABLE] =
{
    { M_ISREAD,  "Read",    4, "Unread", 6 },
    { M_CLEARED, "Cleared", 7,  0,       0 },
    { M_URGENT,  "Urgent",  6,  0,       0 },
    { M_MASS,    "Mass",    4,  0,       0 },
    { M_FORWARD, "Fwd",     3,  0,       0 },
    { M_TAG,     "Tagged",  6,  0,       0 },
    { M_SAFE,    "Safe",    4,  0,       0 }
};

static char *status_string(struct mail *mp)
{
    // Return a longer description of message flags.
    //
    char *tbuf1 = alloc_lbuf("status_string");
    char *p = tbuf1;
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

void check_mail(dbref player, int folder, BOOL silent)
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
    notify(player,
           tprintf("MAIL: %d messages in folder %d [%s] (%d unread, %d cleared).\r\n",
               rc + uc, folder, get_folder_name(player, folder), uc, cc));
#else // MAIL_ALL_FOLDERS
    if (rc + uc > 0)
    {
        notify(player, tprintf("MAIL: %d messages in folder %d [%s] (%d unread, %d cleared).", rc + uc, folder, get_folder_name(player, folder), uc, cc));
    }
    else if (!silent)
    {
        notify(player, tprintf("\r\nMAIL: You have no mail.\r\n"));
    }
    if (gc > 0)
    {
        notify(player, tprintf("URGENT MAIL: You have %d urgent messages in folder %d [%s].", gc, folder, get_folder_name(player, folder)));
    }
#endif // MAIL_ALL_FOLDERS
}

#define GMA_NOTFOUND    1
#define GMA_FOUND       2
#define GMA_INVALIDFORM 3

struct malias *get_malias(dbref player, char *alias, int *pnResult)
{
    *pnResult = GMA_INVALIDFORM;
    if (!alias)
    {
        return NULL;
    }
    if (alias[0] == '#')
    {
        if (ExpMail(player))
        {
            int x = Tiny_atol(alias + 1);
            if (x < 0 || x >= ma_top)
            {
                *pnResult = GMA_NOTFOUND;
                return NULL;
            }
            *pnResult = GMA_FOUND;
            return malias[x];
        }
    }
    else if (alias[0] == '*')
    {
        int  nValidMailAlias;
        BOOL bValidMailAlias;
        char *pValidMailAlias = MakeCanonicalMailAlias
                                (   alias+1,
                                    &nValidMailAlias,
                                    &bValidMailAlias
                                );

        if (bValidMailAlias)
        {
            for (int i = 0; i < ma_top; i++)
            {
                struct malias *m = malias[i];
                if (  m->owner == player
                   || m->owner == GOD
                   || ExpMail(player))
                {
                    if (!strcmp(pValidMailAlias, m->name))
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
            notify(player, "MAIL: Mail aliases must be of the form *<name> or #<num>.");
        }
        else
        {
            notify(player, "MAIL: Mail aliases must be of the form *<name>.");
        }
    }
    return NULL;
}

void do_malias_send(dbref player, char *tolist, char *listto, char *subject, int number, mail_flag flags, BOOL silent)
{
    int nResult;
    struct malias *m = get_malias(player, tolist, &nResult);
    if (nResult == GMA_INVALIDFORM)
    {
        notify(player, tprintf("MAIL: I can't figure out from '%s' who you want to mail to.", tolist));
        return;
    }
    else if (nResult == GMA_NOTFOUND)
    {
        notify(player, tprintf("MAIL: Alias '%s' not found.", tolist));
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
            char *pMail = tprintf("Alias Error: Bad Player %d for %s", vic, tolist);
            int iMail = add_mail_message(player, pMail);
            if (iMail != NOTHING)
            {
                send_mail(GOD, GOD, listto, subject, iMail, 0, silent);
                MessageReferenceDec(iMail);
            }
        }
    }
}

void do_malias_create(dbref player, char *alias, char *tolist)
{
    struct malias **nm;
    int nResult;
    get_malias(player, alias, &nResult);

    if (nResult == GMA_INVALIDFORM)
    {
        notify(player, "MAIL: What alias do you want to create?.");
        return;
    }
    else if (nResult == GMA_FOUND)
    {
        notify(player, tprintf("MAIL: Mail Alias '%s' already exists.", alias));
        return;
    }

    int i = 0;
    if (!ma_size)
    {
        ma_size = MA_INC;
        malias = (struct malias **)MEMALLOC(sizeof(struct malias *) * ma_size);
        (void)ISOUTOFMEMORY(malias);
    }
    else if (ma_top >= ma_size)
    {
        ma_size += MA_INC;
        nm = (struct malias **)MEMALLOC(sizeof(struct malias *) * (ma_size));
        (void)ISOUTOFMEMORY(nm);

        for (i = 0; i < ma_top; i++)
        {
            nm[i] = malias[i];
        }
        MEMFREE(malias);
        malias = nm;
    }
    malias[ma_top] = (struct malias *)MEMALLOC(sizeof(struct malias));
    (void)ISOUTOFMEMORY(malias[ma_top]);


    // Parse the player list.
    //
    char *head = tolist;
    char *tail, spot;
    char *buff;
    dbref target;
    i = 0;
    while (head && *head && (i < (MAX_MALIAS_MEMBERSHIP - 1)))
    {
        while (*head == ' ')
            head++;
        tail = head;
        while (*tail && (*tail != ' '))
        {
            if (*tail == '"')
            {
                head++;
                tail++;
                while (  *tail 
                      && (*tail != '"'))
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
        if (!_stricmp(head, "me"))
        {
            target = player;
        }
        else if (*head == '#')
        {
            target = Tiny_atol(head + 1);
        }
        else
        {
            target = lookup_player(player, head, TRUE);
        }
        if (!Good_obj(target) || !isPlayer(target))
        {
            notify(player, "MAIL: No such player.");
        }
        else
        {
            buff = unparse_object(player, target, FALSE);
            notify(player,
            tprintf("MAIL: %s added to alias %s", buff, alias));
            malias[ma_top]->list[i] = target;
            i++;
            free_lbuf(buff);
        }

        // Get the next recip.
        //
        *tail = spot;
        head = tail;
        if (*head == '"')
            head++;
    }
    int  nValidMailAlias;
    BOOL bValidMailAlias;
    char *pValidMailAlias = MakeCanonicalMailAlias
                            (   alias+1,
                                &nValidMailAlias,
                                &bValidMailAlias
                            );

    if (!bValidMailAlias)
    {
        notify(player, "MAIL: Invalid mail alias.");
        return;
    }

    // The Mail Alias Description is a superset of the Mail Alias,
    // so, the following code is not necessary unless the specification
    // of the Mail Alias Description becomes more restrictive at some
    // future time.
    //
#if 0
    int  nValidMailAliasDesc;
    BOOL bValidMailAliasDesc;
    char *pValidMailAliasDesc = MakeCanonicalMailAliasDesc
                                (   alias+1,
                                    &nValidMailAliasDesc,
                                    &bValidMailAliasDesc
                                );

    if (!bValidMailAliasDesc)
    {
        notify(player, "MAIL: Invalid mail alias description.");
        break;
    }
#else
    char *pValidMailAliasDesc = pValidMailAlias;
    int nValidMailAliasDesc = nValidMailAlias;
#endif

    malias[ma_top]->list[i] = NOTHING;
    malias[ma_top]->name = StringCloneLen(pValidMailAlias, nValidMailAlias);
    malias[ma_top]->numrecep = i;
    malias[ma_top]->owner = player;
    malias[ma_top]->desc = StringCloneLen(pValidMailAliasDesc, nValidMailAliasDesc);
    malias[ma_top]->desc_width = nValidMailAliasDesc;
    ma_top++;

    notify(player, tprintf("MAIL: Alias set '%s' defined.", alias));
}

void do_malias_list(dbref player, char *alias)
{
    int nResult;
    struct malias *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        notify(player, tprintf("MAIL: Alias '%s' not found.", alias));
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    if (!ExpMail(player) && (player != m->owner) && !(God(m->owner)))
    {
        notify(player, "MAIL: Permission denied.");
        return;
    }
    char *buff = alloc_lbuf("do_malias_list");
    char *bp = buff;
     
    safe_tprintf_str(buff, &bp, "MAIL: Alias *%s: ", m->name);
    for (int i = m->numrecep - 1; i > -1; i--)
    {
        const char *p = Name(m->list[i]);
        if (strchr(p, ' '))
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

    notify(player, buff);
    free_lbuf(buff);
}

char *Spaces(unsigned int n)
{
    static char buffer[42] = "                                         ";
    static unsigned int nLast = 0;
    buffer[nLast] = ' ';
    if (n < sizeof(buffer)-1)
    {
        buffer[n] = '\0';
        nLast = n;
    }
    return buffer;
}

void do_malias_list_all(dbref player)
{
    int i = 0;
    int notified = 0;

    for (i = 0; i < ma_top; i++)
    {
        struct malias *m = malias[i];
        if (  m->owner == GOD
           || m->owner == player
           || God(player))
        {
            if (!notified)
            {
                notify(player, "Name         Description                              Owner");
                notified++;
            }
            char *p = tprintf( "%-12s %s%s %-15.15s",
                               m->name,
                               m->desc,
                               Spaces(40 - m->desc_width),
                               Name(m->owner));
            notify(player, p);
        }
    }
    notify(player, "*****  End of Mail Aliases *****");
}



void do_malias_switch(dbref player, char *a1, char *a2)
{
    if ((!a2 || !*a2) && !(!a1 || !*a1))
    {
        do_malias_list(player, a1);
    }
    else if ((!*a1 || !a1) && (!*a2 || !a2))
    {
        do_malias_list_all(player);
    }
    else
    {
        do_malias_create(player, a1, a2);
    }
}



void do_mail_cc(dbref player, char *arg)
{
    if (!(Flags2(player) & PLAYER_MAILS))
    {
        notify(player, "MAIL: No mail message in progress.");
        return;
    }
    if (!arg || !*arg)
    {
        notify(player, "MAIL: I do not know whom you want to mail.");
        return;
    }

    char *tolist = make_numlist(player, arg);
    if (!tolist) 
    {
        return;
    }
    char *fulllist = alloc_lbuf("do_mail_cc");
    char *bp = fulllist;

    safe_str(tolist, fulllist, &bp);
    const char *pPlayerMailTo = atr_get_raw(player, A_MAILTO);
    if (pPlayerMailTo)
    {
        safe_chr(' ', fulllist, &bp);
        safe_str(pPlayerMailTo, fulllist, &bp);
    }
    *bp = '\0';

    atr_add_raw(player, A_MAILTO, fulllist);
    char *names = make_namelist(fulllist);
    notify(player, tprintf("MAIL: You are sending mail to '%s'.", names));
    free_lbuf(names);
    free_lbuf(tolist);
    free_lbuf(fulllist);
}

static char *make_namelist(char *arg)
{
    char *p;
    char *oldarg = alloc_lbuf("make_namelist.oldarg");
    char *names = alloc_lbuf("make_namelist.names");
    char *bp = names;

    strcpy(oldarg, arg);

    TINY_STRTOK_STATE tts;
    Tiny_StrTokString(&tts, oldarg);
    Tiny_StrTokControl(&tts, " ");
    for (p = Tiny_StrTokParse(&tts); p; p = Tiny_StrTokParse(&tts))
    {
        dbref target = Tiny_atol(p);
        if (Good_obj(target) && isPlayer(target))
        {
            safe_str(Name(target), names, &bp);
            safe_str(", ", names, &bp);
        }
    }
    *(bp - 2) = '\0';
    free_lbuf(oldarg);
    return names;
}

static char *make_numlist(dbref player, char *arg)
{
    char *tail, spot;
    char buf[MBUF_SIZE];
    struct malias *m;
    struct mail *temp;
    dbref target;
    int num;

    ITL itl;
    char *numbuf = alloc_lbuf("make_numlist");
    char *numbp = numbuf;
    char *head = arg;
    ItemToList_Init(&itl, numbuf, &numbp);

    while (head && *head)
    {
        while (*head == ' ')
        {
            head++;
        }

        tail = head;
        while (*tail && (*tail != ' '))
        {
            if (*tail == '"')
            {
                head++;
                tail++;
                while (*tail && (*tail != '"'))
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
        *tail = 0;

        if (*head == '*')
        {
            int nResult;
            m = get_malias(player, head, &nResult);
            if (nResult == GMA_NOTFOUND)
            {
                notify(player, tprintf("MAIL: Alias '%s' does not exist.", head));
                ItemToList_Final(&itl);
                free_lbuf(numbuf);
                return NULL;
            }
            else if (nResult == GMA_INVALIDFORM)
            {
                notify(player, tprintf("MAIL: '%s' is a badly-formed alias.", head));
                ItemToList_Final(&itl);
                free_lbuf(numbuf);
                return NULL;
            }
            for (int i = 0; i < m->numrecep; i++)
            {
                ItemToList_AddInteger(&itl, m->list[i]);
            }
        }
        else
        {
            target = lookup_player(player, head, TRUE);
            if (Good_obj(target))
            {
                ItemToList_AddInteger(&itl, target);
            }
            else
            {
                notify(player, tprintf("MAIL: '%s' does not exist.", head));
                ItemToList_Final(&itl);
                free_lbuf(numbuf);
                return NULL;
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

    if (!*numbuf)
    {
        notify(player, "MAIL: No players specified.");
        ItemToList_Final(&itl);
        free_lbuf(numbuf);
        return NULL;
    }
    else
    {
        ItemToList_Final(&itl);
        return numbuf;
    }
}

void do_mail_quick(dbref player, char *arg1, char *arg2)
{
    if (!arg1 || !*arg1)
    {
        notify(player, "MAIL: I don't know who you want to mail.");
        return;
    }
    if (!arg2 || !*arg2)
    {
        notify(player, "MAIL: No message.");
        return;
    }
    if (Flags2(player) & PLAYER_MAILS)
    {
        notify(player, "MAIL: Mail message already in progress.");
        return;
    }
    char *buf = alloc_lbuf("do_mail_quick");
    char *bp = buf;

    strcpy(bp, arg1);
    parse_to(&bp, '/', 1);

    if (!bp)
    {
        notify(player, "MAIL: No subject.");
        free_lbuf(buf);
        return;
    }
    mail_to_list(player, make_numlist(player, buf), bp, arg2, 0, FALSE);
    free_lbuf(buf);
}

void mail_to_list(dbref player, char *list, char *subject, char *message, int flags, BOOL silent)
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

    char *tolist = alloc_lbuf("mail_to_list");
    strcpy(tolist, list);

    int number = add_mail_message(player, message);
    if (number != NOTHING)
    {
        char *tail, spot;
        dbref target;
        char *head = list;
        while (head && *head)
        {
            while (*head == ' ')
            {
                head++;
            }

            tail = head;
            while (*tail && (*tail != ' '))
            {
                if (*tail == '"')
                {
                    head++;
                    tail++;
                    while (*tail && (*tail != '"'))
                        tail++;
                }
                if (*tail)
                    tail++;
            }
            tail--;
            if (*tail != '"')
            {
                tail++;
            }
            spot = *tail;
            *tail = 0;

            if (*head == '*')
            {
                do_malias_send(player, head, tolist, subject, number, flags, silent);
            }
            else
            {
                target = Tiny_atol(head);
                if (Good_obj(target) && isPlayer(target))
                {
                    send_mail(player, target, tolist, subject, number, flags, silent);
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
        MessageReferenceDec(number);
    }
    free_lbuf(tolist);
    free_lbuf(list);
}

void do_expmail_stop(dbref player, int flags)
{
    if ((Flags2(player) & PLAYER_MAILS) != PLAYER_MAILS)
    {
        notify(player, "MAIL: No message started.");
        return;
    }

    dbref aowner;
    dbref aflags;
    char *tolist = atr_get(player, A_MAILTO, & aowner, &aflags);
    if (*tolist == '\0')
    {
        notify(player, "MAIL: No recipients.");
        free_lbuf(tolist);
    }
    else
    {
        char *mailmsg = atr_get(player, A_MAILMSG, &aowner, &aflags);
        if (*mailmsg == '\0')
        {
            notify(player, "MAIL: The body of this message is empty.  Use - to add to the message.");
        }
        else
        {
            char *mailsub   = atr_get(player, A_MAILSUB, &aowner, &aflags);
            char *mailflags = atr_get(player, A_MAILFLAGS, &aowner, &aflags);
            mail_to_list(player, tolist, mailsub, mailmsg, flags | Tiny_atol(mailflags), FALSE);
            free_lbuf(mailflags);
            free_lbuf(mailsub);

            Flags2(player) &= ~PLAYER_MAILS;
        }
        free_lbuf(mailmsg);
    }
}

void do_expmail_abort(dbref player)
{
    Flags2(player) &= ~PLAYER_MAILS;
    notify(player, "MAIL: Message aborted.");
}

void do_prepend(dbref executor, dbref caller, dbref enactor, int key, char *text)
{
    if (!mudconf.have_mailer)
    {
        return;
    }

    if (Flags2(executor) & PLAYER_MAILS)
    {
        dbref aowner;
        int aflags;

        char *oldmsg = atr_get(executor, A_MAILMSG, &aowner, &aflags);
        if (*oldmsg)
        {
            char *newmsg = alloc_lbuf("do_prepend");
            char *bp = newmsg;
            safe_str(text + 1, newmsg, &bp);
            safe_chr(' ', newmsg, &bp);
            safe_str(oldmsg, newmsg, &bp);
            *bp = '\0';
            atr_add_raw(executor, A_MAILMSG, newmsg);
            free_lbuf(newmsg);
        }
        else
        {
            atr_add_raw(executor, A_MAILMSG, text + 1);
        }

        free_lbuf(oldmsg);
        int nLen;
        const char *attr = atr_get_raw_LEN(executor, A_MAILMSG, &nLen);
        notify(executor, tprintf("%d/%d characters prepended.", nLen, LBUF_SIZE-1));
    }
    else
    {
        notify(executor, "MAIL: No message in progress.");
    }
}

void do_postpend(dbref executor, dbref caller, dbref enactor, int key, char *text)
{
    if (!mudconf.have_mailer)
    {
        return;
    }
    if ((*(text + 1) == '-') && !(*(text + 2)))
    {
        do_expmail_stop(executor, 0);
        return;
    }

    if (Flags2(executor) & PLAYER_MAILS)
    {
        dbref aowner;
        int aflags;

        char *oldmsg = atr_get(executor, A_MAILMSG, &aowner, &aflags);
        if (*oldmsg)
        {
            char *newmsg = alloc_lbuf("do_postpend");
            char *bp = newmsg;
            safe_str(oldmsg, newmsg, &bp);
            safe_chr(' ', newmsg, &bp);
            safe_str(text + 1, newmsg, &bp);
            *bp = '\0';
            atr_add_raw(executor, A_MAILMSG, newmsg);
            free_lbuf(newmsg);
        }
        else
        {
            atr_add_raw(executor, A_MAILMSG, text + 1);
        }

        free_lbuf(oldmsg);
        int nLen;
        const char *attr = atr_get_raw_LEN(executor, A_MAILMSG, &nLen);
        notify(executor, tprintf("%d/%d characters added.", nLen, LBUF_SIZE-1));
    }
    else
    {
        notify(executor, "MAIL: No message in progress.");
    }
}

static void do_edit_msg(dbref player, char *from, char *to)
{
    if (Flags2(player) & PLAYER_MAILS)
    {
        dbref aowner;
        int aflags;
        char *msg = atr_get(player, A_MAILMSG, &aowner, &aflags);
        char *result = replace_string(from, to, msg);
        atr_add(player, A_MAILMSG, result, aowner, aflags);
        notify(player, "Text edited.");
        free_lbuf(result);
        free_lbuf(msg);
    }
    else
    {
        notify(player, "MAIL: No message in progress.");
    }
}

static void do_mail_proof(dbref player)
{
    if (!(Flags2(player) & PLAYER_MAILS))
    {
        notify(player, "MAIL: No message in progress.");
        return;
    }

    char *mailmsg, *msg, *bp, *str;
    dbref aowner;
    int aflags;
    int iRealVisibleWidth;
    char szSubjectBuffer[MBUF_SIZE];

    char *mailto = atr_get(player, A_MAILTO, &aowner, &aflags);
    str = mailmsg = atr_get(player, A_MAILMSG, &aowner, &aflags);
    bp = msg = alloc_lbuf("do_mail_proof");
    TinyExec(msg, &bp, player, player, player, EV_EVAL | EV_FCHECK,
                &str, (char **)NULL, 0);
    *bp = '\0';
    free_lbuf(mailmsg);

    char *names = make_namelist(mailto);
    ANSI_TruncateToField(atr_get_raw(player, A_MAILSUB),
        sizeof(szSubjectBuffer), szSubjectBuffer, 35,
        &iRealVisibleWidth, ANSI_ENDGOAL_NORMAL);
    notify(player, DASH_LINE);
    notify(player, tprintf("From:  %-*s  Subject: %s\nTo: %s",
            PLAYER_NAME_LIMIT - 6, Name(player), szSubjectBuffer, names));
    notify(player, DASH_LINE);
    notify(player, msg);
    notify(player, DASH_LINE);
    free_lbuf(names);
    free_lbuf(mailto);
    free_lbuf(msg);
}

void do_malias_desc(dbref player, char *alias, char *desc)
{
    int nResult;
    struct malias *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        notify(player, tprintf("MAIL: Alias '%s' not found.", alias));
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    if (  m->owner != GOD
       || ExpMail(player))
    {
        int  nValidMailAliasDesc;
        BOOL bValidMailAliasDesc;
        int  nVisualWidth;
        char *pValidMailAliasDesc = MakeCanonicalMailAliasDesc
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
            notify(player, "MAIL: Description changed.");
        }
        else
        {
            notify(player, "MAIL: Description is not valid.");
        }
    }
    else
    {
        notify(player, "MAIL: Permission denied.");
    }
}

void do_malias_chown(dbref player, char *alias, char *owner)
{
    if (!ExpMail(player))
    {
        notify(player, "MAIL: You cannot do that!");
        return;
    }

    int nResult;
    struct malias *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        notify(player, tprintf("MAIL: Alias '%s' not found.", alias));
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    dbref no = lookup_player(player, owner, TRUE);
    if (no == NOTHING)
    {
        notify(player, "MAIL: I do not see that here.");
        return;
    }
    m->owner = no;
    notify(player, "MAIL: Owner changed for alias.");
}

void do_malias_add(dbref player, char *alias, char *person)
{
    int nResult;
    struct malias *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        notify(player, tprintf("MAIL: Alias '%s' not found.", alias));
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
            notify(player, "MAIL: Only players may be added.");
            return;
        }
    }

    if (thing == NOTHING)
    {
        thing = lookup_player(player, person, TRUE);
    }

    if (thing == NOTHING)
    {
        notify(player, "MAIL: I do not see that person here.");
        return;
    }

    if ((m->owner == GOD) && !ExpMail(player))
    {
        notify(player, "MAIL: Permission denied.");
        return;
    }
    int i;
    for (i = 0; i < m->numrecep; i++)
    {
        if (m->list[i] == thing)
        {
            notify(player, "MAIL: That person is already on the list.");
            return;
        }
    }

    if (i >= (MAX_MALIAS_MEMBERSHIP - 1))
    {
        notify(player, "MAIL: The list is full.");
        return;
    }

    m->list[m->numrecep] = thing;
    m->numrecep = m->numrecep + 1;
    notify(player, tprintf("MAIL: %s added to %s", Name(thing), m->name));
}

void do_malias_remove(dbref player, char *alias, char *person)
{
    int nResult;
    struct malias *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        notify(player, tprintf("MAIL: Alias '%s' not found.", alias));
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    if ((m->owner == GOD) && !ExpMail(player))
    {
        notify(player, "MAIL: Permission denied.");
        return;
    }

    dbref thing = NOTHING;
    if (*person == '#')
    {
        thing = parse_dbref(person + 1);
    }
    if (thing == NOTHING)
    {
        thing = lookup_player(player, person, TRUE);
    }
    if (thing == NOTHING)
    {
        notify(player, "MAIL: I do not see that person here.");
        return;
    }

    BOOL ok = FALSE;
    for (int i = 0; i < m->numrecep; i++)
    {
        if (ok)
        {
            m->list[i] = m->list[i + 1];
        }
        else if (m->list[i] == thing)
        {
            m->list[i] = m->list[i + 1];
            ok = TRUE;
        }
    }

    if (ok)
    {
        m->numrecep--;
        notify(player, tprintf("MAIL: %s removed from alias %s.",
                   Name(thing), alias));
    }
    else
    {
        notify(player, tprintf("MAIL: %s is not a member of alias %s.",
                   Name(thing), alias));
    }
}

void do_malias_rename(dbref player, char *alias, char *newname)
{
    int nResult;
    struct malias *m = get_malias(player, newname, &nResult);
    if (nResult == GMA_FOUND)
    {
        notify(player, "MAIL: That name already exists!");
        return;
    }
    if (nResult != GMA_NOTFOUND)
    {
        return;
    }
    m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        notify(player, "MAIL: I cannot find that alias!");
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    if (!ExpMail(player) && !(m->owner == player))
    {
        notify(player, "MAIL: Permission denied.");
        return;
    }

    int  nValidMailAlias;
    BOOL bValidMailAlias;
    char *pValidMailAlias = MakeCanonicalMailAlias
                            (   newname+1,
                                &nValidMailAlias,
                                &bValidMailAlias
                            );
    if (bValidMailAlias)
    {
        MEMFREE(m->name);
        m->name = StringCloneLen(pValidMailAlias, nValidMailAlias);
        notify(player, "MAIL: Mailing Alias renamed.");
    }
    else
    {
        notify(player, "MAIL: Alias is not valid.");
    }
}

void do_malias_delete(dbref player, char *alias)
{
    int nResult;
    struct malias *m = get_malias(player, alias, &nResult);
    if (nResult == GMA_NOTFOUND)
    {
        notify(player, tprintf("MAIL: Alias '%s' not found.", alias));
        return;
    }
    if (nResult != GMA_FOUND)
    {
        return;
    }
    BOOL done = FALSE;
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
                    done = TRUE;
                    notify(player, "MAIL: Alias Deleted.");
                    malias[i] = malias[i + 1];
                }
            }
        }
    }

    if (!done)
    {
        notify(player, tprintf("MAIL: Alias '%s' not found.", alias));
    }
    else
    {
        ma_top--;
    }
}

void do_malias_adminlist(dbref player)
{
    if (!ExpMail(player))
    {
        do_malias_list_all(player);
        return;
    }
    notify(player,
      "Num  Name         Description                              Owner");

    struct malias *m;
    int i;

    for (i = 0; i < ma_top; i++)
    {
        m = malias[i];
        notify(player, tprintf("%-4d %-12s %s%s %-15.15s",
                       i, m->name, m->desc, Spaces(40 - m->desc_width),
                       Name(m->owner)));
    }

    notify(player, "***** End of Mail Aliases *****");
}

void do_malias_status(dbref player)
{
    if (!ExpMail(player))
    {
        notify(player, "MAIL: Permission denied.");
    }
    else
    {
        notify(player, tprintf("MAIL: Number of mail aliases defined: %d", ma_top));
        notify(player, tprintf("MAIL: Allocated slots %d", ma_size));
    }
}

void malias_cleanup1 (struct malias *m, dbref target)
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

void malias_cleanup (dbref player)
{
    for (int i = 0; i < ma_top; i++)
    {
        malias_cleanup1(malias[i], player);
    }
}

void do_mail_retract1(dbref player, char *name, char *msglist)
{
    dbref target = lookup_player(player, name, TRUE);
    if (target == NOTHING)
    {
        notify(player, "MAIL: No such player.");
        return;
    }
    struct mail_selector ms;
    if (!parse_msglist(msglist, &ms, target))
    {
        return;
    }

    struct mail *mp, *nextp;
    int i = 0, j = 0;
    for (mp = (struct mail *)hashfindLEN(&target, sizeof(target), &mudstate.mail_htab); mp; mp = nextp)
    {
        if (mp->from == player)
        {
            i++;
            if (mail_match(mp, ms, i))
            {
                j++;
                if (Unread(mp))
                {
                    if (mp->prev == NULL)
                    {
                        if (mp->next == NULL)
                        {
                            hashdeleteLEN(&target, sizeof(target), &mudstate.mail_htab);
                        }
                        else
                        {
                            hashreplLEN(&target, sizeof(target), (int *)(mp->next), &mudstate.mail_htab);
                        }
                    }
                    else if (mp->next == NULL)
                        mp->prev->next = NULL;

                    if (mp->prev != NULL)
                    {
                        mp->prev->next = mp->next;
                    }
                    if (mp->next != NULL)
                    {
                        mp->next->prev = mp->prev;
                    }

                    nextp = mp->next;
                    MessageReferenceDec(mp->number);
                    MEMFREE(mp->subject);
                    mp->subject = NULL;
                    MEMFREE(mp->time);
                    mp->time = NULL;
                    MEMFREE(mp->tolist);
                    mp->tolist = NULL;
                    MEMFREE(mp);
                    mp = NULL;
                    notify(player, "MAIL: Mail retracted.");
                }
                else
                {
                    notify(player, "MAIL: That message has been read.");
                    nextp = mp->next;
                }
            }
            else
            {
                nextp = mp->next;
            }
        }
        else
        {
            nextp = mp->next;
        }
    }

    if (!j)
    {
        // Ran off the end of the list without finding anything.
        //
        notify(player, "MAIL: No matching messages.");
    }
}

void do_mail_retract(dbref player, char *name, char *msglist)
{
    if (*name == '*')
    {
        int pnResult;
        struct malias *m = get_malias(player, name, &pnResult);
        if (pnResult == GMA_NOTFOUND)
        {
            notify(player, tprintf("MAIL: Mail alias %s not found.", name));
            return;
        }
        if (pnResult == GMA_FOUND)
        {
            for (int i = 0; i < m->numrecep; i++)
            {
                do_mail_retract1(player, tprintf("#%d", m->list[i]), msglist);
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
    int   key,
    int   nargs,
    char *arg1,
    char *arg2
)
{
    if (!mudconf.have_mailer)
    {
        notify(executor, "Mailer is disabled.");
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
    int   key,
    int   nargs,
    char *arg1,
    char *arg2
)
{
    if (!mudconf.have_mailer)
    {
        notify(executor, "Mailer is disabled.");
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
        do_mail_list(executor, arg1, FALSE);
        break;
    case MAIL_READ:
        do_mail_read(executor, arg1);
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
        do_mail_reply(executor, arg1, FALSE, key);
        break;
    case MAIL_REPLYALL:
        do_mail_reply(executor, arg1, TRUE, key);
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
        do_mail_cc(executor, arg1);
        break;
    case MAIL_SAFE:
        do_mail_safe(executor, arg1);
        break;
    }
}
