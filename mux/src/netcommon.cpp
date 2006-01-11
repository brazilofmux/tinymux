// netcommon.cpp
//
// $Id: netcommon.cpp,v 1.74 2006-01-11 22:25:17 sdennis Exp $
//
// This file contains routines used by the networking code that do not
// depend on the implementation of the networking code.  The network-specific
// portions of the descriptor data structure are not used.
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <time.h>

#include "ansi.h"
#include "attrs.h"
#include "command.h"
#include "comsys.h"
#include "file_c.h"
#include "functions.h"
#include "mguests.h"
#include "powers.h"
#include "svdreport.h"
#ifdef REALITY_LVLS
#include "levels.h"
#endif /* REALITY_LVLS */


/* ---------------------------------------------------------------------------
 * make_portlist: Make a list of ports for PORTS().
 */

void make_portlist(dbref player, dbref target, char *buff, char **bufc)
{
    UNUSED_PARAMETER(player);

    ITL itl;
    ItemToList_Init(&itl, buff, bufc);

    DESC *d;
    DESC_ITER_CONN(d)
    {
        if (  d->player == target
           && !ItemToList_AddInteger64(&itl, d->descriptor))
        {
            break;
        }
    }
    ItemToList_Final(&itl);
}

// ---------------------------------------------------------------------------
// make_port_ulist: Make a list of connected user numbers for the LPORTS function.
// ---------------------------------------------------------------------------

void make_port_ulist(dbref player, char *buff, char **bufc)
{
    DESC *d;
    ITL itl;
    char *tmp = alloc_sbuf("make_port_ulist");
    ItemToList_Init(&itl, buff, bufc, '#');
    DESC_ITER_CONN(d)
    {
        if (  !See_Hidden(player)
           && Hidden(d->player))
        {
            continue;
        }

        // printf format: printf("%d:%d", d->player, d->descriptor);
        //
        char *p = tmp;
        p += mux_ltoa(d->player, p);
        *p++ = ':';
        p += mux_i64toa(d->descriptor, p);

        size_t n = p - tmp;
        if (!ItemToList_AddStringLEN(&itl, n, tmp))
        {
            break;
        }
    }
    ItemToList_Final(&itl);
    free_sbuf(tmp);
}

/* ---------------------------------------------------------------------------
 * update_quotas: Update timeslice quotas
 */

void update_quotas(CLinearTimeAbsolute& ltaLast, const CLinearTimeAbsolute& ltaCurrent)
{
    if (ltaCurrent < ltaLast)
    {
        ltaLast = ltaCurrent;
        return;
    }

    CLinearTimeDelta ltdDiff = ltaCurrent - ltaLast;
    if (ltdDiff < mudconf.timeslice)
    {
        return;
    }

    int nSlices = ltdDiff / mudconf.timeslice;
    int nExtraQuota = mudconf.cmd_quota_incr * nSlices;

    if (nExtraQuota > 0)
    {
        DESC *d;
        DESC_ITER_ALL(d)
        {
            d->quota += nExtraQuota;
            if (d->quota > mudconf.cmd_quota_max)
            {
                d->quota = mudconf.cmd_quota_max;
            }
        }
    }
    ltaLast += mudconf.timeslice * nSlices;
}

/* raw_notify_html() -- raw_notify() without the newline */
void raw_notify_html(dbref player, const char *msg)
{
    if (!msg || !*msg)
    {
        return;
    }

    if (  mudstate.inpipe
       && player == mudstate.poutobj)
    {
        safe_str(msg, mudstate.poutnew, &mudstate.poutbufc);
        return;
    }
    if (  !Connected(player)
       || !Html(player))
    {
        return;
    }

    DESC *d;
    DESC_ITER_PLAYER(player, d)
    {
        queue_string(d, msg);
    }
}

/* ---------------------------------------------------------------------------
 * raw_notify: write a message to a player
 */

void raw_notify(dbref player, const char *msg)
{
    DESC *d;

    if (!msg || !*msg)
    {
        return;
    }

    if (  mudstate.inpipe
       && player == mudstate.poutobj)
    {
        safe_str(msg, mudstate.poutnew, &mudstate.poutbufc);
        safe_str("\r\n", mudstate.poutnew, &mudstate.poutbufc);
        return;
    }

    if (!Connected(player))
    {
        return;
    }

    DESC_ITER_PLAYER(player, d)
    {
        queue_string(d, msg);
        queue_write_LEN(d, "\r\n", 2);
    }
}

void raw_notify_newline(dbref player)
{
    if (  mudstate.inpipe
       && player == mudstate.poutobj)
    {
        safe_str("\r\n", mudstate.poutnew, &mudstate.poutbufc);
        return;
    }
    if (!Connected(player))
    {
        return;
    }

    DESC *d;
    DESC_ITER_PLAYER(player, d)
    {
        queue_write_LEN(d, "\r\n", 2);
    }
}

/* ---------------------------------------------------------------------------
 * raw_broadcast: Send message to players who have indicated flags
 */

void DCL_CDECL raw_broadcast(int inflags, char *fmt, ...)
{
    if (!fmt || !*fmt)
    {
        return;
    }

    char buff[LBUF_SIZE];

    va_list ap;
    va_start(ap, fmt);
    mux_vsnprintf(buff, LBUF_SIZE, fmt, ap);
    va_end(ap);

    DESC *d;
    DESC_ITER_CONN(d)
    {
        if ((Flags(d->player) & inflags) == inflags)
        {
            queue_string(d, buff);
            queue_write_LEN(d, "\r\n", 2);
            process_output(d, false);
        }
    }
}

/* ---------------------------------------------------------------------------
 * clearstrings: clear out prefix and suffix strings
 */

void clearstrings(DESC *d)
{
    if (d->output_prefix)
    {
        free_lbuf(d->output_prefix);
        d->output_prefix = NULL;
    }
    if (d->output_suffix)
    {
        free_lbuf(d->output_suffix);
        d->output_suffix = NULL;
    }
}

static void add_to_output_queue(DESC *d, const char *b, size_t n)
{
    TBLOCK *tp;
    size_t left;

    // Allocate an output buffer if needed.
    //
    if (d->output_head == NULL)
    {
        tp = (TBLOCK *)MEMALLOC(OUTPUT_BLOCK_SIZE);
        ISOUTOFMEMORY(tp);
        tp->hdr.nxt = NULL;
        tp->hdr.start = tp->data;
        tp->hdr.end = tp->data;
        tp->hdr.nchars = 0;
        d->output_head = tp;
        d->output_tail = tp;
    }
    else
    {
        tp = d->output_tail;
    }

    // Now tp points to the last buffer in the chain.
    //
    do
    {
        // See if there is enough space in the buffer to hold the
        // string.  If so, copy it and update the pointers..
        //
        left = OUTPUT_BLOCK_SIZE - (tp->hdr.end - (char *)tp + 1);
        if (n <= left)
        {
            memcpy(tp->hdr.end, b, n);
            tp->hdr.end += n;
            tp->hdr.nchars += n;
            n = 0;
        }
        else
        {
            // It didn't fit.  Copy what will fit and then allocate
            // another buffer and retry.
            //
            if (left > 0)
            {
                memcpy(tp->hdr.end, b, left);
                tp->hdr.end += left;
                tp->hdr.nchars += left;
                b += left;
                n -= left;
            }
            tp = (TBLOCK *)MEMALLOC(OUTPUT_BLOCK_SIZE);
            ISOUTOFMEMORY(tp);
            tp->hdr.nxt = NULL;
            tp->hdr.start = tp->data;
            tp->hdr.end = tp->data;
            tp->hdr.nchars = 0;
            d->output_tail->hdr.nxt = tp;
            d->output_tail = tp;
        }
    } while (n > 0);
}

/* ---------------------------------------------------------------------------
 * queue_write: Add text to the output queue for the indicated descriptor.
 */

void queue_write_LEN(DESC *d, const char *b, size_t n)
{
    if (n <= 0)
    {
        return;
    }

    if (static_cast<size_t>(mudconf.output_limit) < d->output_size + n)
    {
        process_output(d, false);
    }

    if (static_cast<size_t>(mudconf.output_limit) < d->output_size + n)
    {
        TBLOCK *tp = d->output_head;
        if (tp == NULL)
        {
            STARTLOG(LOG_PROBLEMS, "QUE", "WRITE");
            log_text("Flushing when output_head is null!");
            ENDLOG;
        }
        else
        {
            STARTLOG(LOG_NET, "NET", "WRITE");
            char *buf = alloc_lbuf("queue_write.LOG");
            mux_sprintf(buf, LBUF_SIZE, "[%u/%s] Output buffer overflow, %d chars discarded by ", d->descriptor, d->addr, tp->hdr.nchars);
            log_text(buf);
            free_lbuf(buf);
            if (d->flags & DS_CONNECTED)
            {
                log_name(d->player);
            }
            ENDLOG;
            d->output_size -= tp->hdr.nchars;
            d->output_head = tp->hdr.nxt;
            d->output_lost += tp->hdr.nchars;
            if (d->output_head == NULL)
            {
                d->output_tail = NULL;
            }
            MEMFREE(tp);
            tp = NULL;
        }
    }

    add_to_output_queue(d, b, n);
    d->output_size += n;
    d->output_tot += n;

#ifdef WIN32
    if (  platform == VER_PLATFORM_WIN32_NT
       && !d->bWritePending
       && !d->bConnectionDropped)
    {
        d->bCallProcessOutputLater = true;
    }
#endif
}

void queue_write(DESC *d, const char *b)
{
    queue_write_LEN(d, b, strlen(b));
}

static const char *encode_iac(const char *szString)
{
    static char Buffer[2*LBUF_SIZE];
    char *pBuffer = Buffer;

    const char *pString = szString;
    if (pString)
    {
        while (*pString)
        {
            const char *p = strchr(pString, NVT_IAC);
            if (!p)
            {
                // NVT_IAC does not appear in the buffer. This is by far the most-common case.
                //
                if (pString == szString)
                {
                    // Avoid copying to the static buffer, and just return the original buffer.
                    //
                    return szString;
                }
                else
                {
                    mux_strncpy(pBuffer, pString, 2*LBUF_SIZE-1);
                    return Buffer;
                }
            }
            else
            {
                // Copy up to and including the IAC.
                //
                size_t n = p - pString + 1;
                memcpy(pBuffer, pString, n);
                pBuffer += n;
                pString += n;

                // Add another IAC.
                //
                safe_copy_chr(NVT_IAC, Buffer, &pBuffer, sizeof(Buffer)-1);
            }
        }
    }
    *pBuffer = '\0';
    return Buffer;
}

void queue_string(DESC *d, const char *s)
{
    const char *p = s;

    if (d->flags & DS_CONNECTED)
    {
        if (  !Ansi(d->player)
           && strchr(s, ESC_CHAR))
        {
            p = strip_ansi(p);
        }
        else if (NoBleed(d->player))
        {
            p = normal_to_white(p);
        }

        if (NoAccents(d->player))
        {
            p = strip_accents(p);
        }
    }
    else
    {
        if (strchr(s, ESC_CHAR))
        {
            p = strip_ansi(p);
        }
        p = strip_accents(p);
    }
    p = encode_iac(p);
    queue_write(d, p);
}

void freeqs(DESC *d)
{
    TBLOCK *tb, *tnext;
    CBLK *cb, *cnext;

    tb = d->output_head;
    while (tb)
    {
        tnext = tb->hdr.nxt;
        MEMFREE(tb);
        tb = tnext;
    }
    d->output_head = NULL;
    d->output_tail = NULL;

    cb = d->input_head;
    while (cb)
    {
        cnext = (CBLK *) cb->hdr.nxt;
        free_lbuf(cb);
        cb = cnext;
    }

    d->input_head = NULL;
    d->input_tail = NULL;

    if (d->raw_input)
    {
        free_lbuf(d->raw_input);
    }
    d->raw_input = NULL;

    d->raw_input_at = NULL;
    d->nOption = 0;
    d->raw_input_state    = NVT_IS_NORMAL;
    d->nvt_sga_him_state  = OPTION_NO;
    d->nvt_sga_us_state   = OPTION_NO;
    d->nvt_eor_him_state  = OPTION_NO;
    d->nvt_eor_us_state   = OPTION_NO;
    d->nvt_naws_him_state = OPTION_NO;
    d->nvt_naws_us_state  = OPTION_NO;
    d->height = 24;
    d->width = 78;
}

/* ---------------------------------------------------------------------------
 * desc_addhash: Add a net descriptor to its player hash list.
 */

void desc_addhash(DESC *d)
{
    dbref player = d->player;
    DESC *hdesc = (DESC *)hashfindLEN(&player, sizeof(player), &mudstate.desc_htab);
    if (hdesc == NULL)
    {
        d->hashnext = NULL;
        hashaddLEN(&player, sizeof(player), d, &mudstate.desc_htab);
    }
    else
    {
        d->hashnext = hdesc;
        hashreplLEN(&player, sizeof(player), d, &mudstate.desc_htab);
    }
}

/* ---------------------------------------------------------------------------
 * desc_delhash: Remove a net descriptor from its player hash list.
 */

static void desc_delhash(DESC *d)
{
    dbref player = d->player;
    DESC *last = NULL;
    DESC *hdesc = (DESC *)hashfindLEN(&player, sizeof(player), &mudstate.desc_htab);
    while (hdesc != NULL)
    {
        if (d == hdesc)
        {
            if (last == NULL)
            {
                if (d->hashnext == NULL)
                {
                    hashdeleteLEN(&player, sizeof(player), &mudstate.desc_htab);
                }
                else
                {
                    hashreplLEN(&player, sizeof(player), d->hashnext, &mudstate.desc_htab);
                }
            }
            else
            {
                last->hashnext = d->hashnext;
            }
            break;
        }
        last = hdesc;
        hdesc = hdesc->hashnext;
    }
    d->hashnext = NULL;
}

void welcome_user(DESC *d)
{
    if (d->host_info & H_REGISTRATION)
    {
        fcache_dump(d, FC_CONN_REG);
    }
    else
    {
        fcache_dump(d, FC_CONN);
    }
}

void save_command(DESC *d, CBLK *command)
{
    command->hdr.nxt = NULL;
    if (d->input_tail == NULL)
    {
        d->input_head = command;

        // We have added our first command to an empty list. Go process it later.
        //
        scheduler.DeferImmediateTask(PRIORITY_SYSTEM, Task_ProcessCommand, d, 0);
    }
    else
    {
        d->input_tail->hdr.nxt = command;
    }
    d->input_tail = command;
}

static void set_userstring(char **userstring, const char *command)
{
    while (mux_isspace(*command))
    {
        command++;
    }

    if (!*command)
    {
        if (*userstring != NULL)
        {
            free_lbuf(*userstring);
            *userstring = NULL;
        }
    }
    else
    {
        if (*userstring == NULL)
        {
            *userstring = alloc_lbuf("set_userstring");
        }
        mux_strncpy(*userstring, command, LBUF_SIZE-1);
    }
}

static void parse_connect(const char *msg, char command[LBUF_SIZE], char user[LBUF_SIZE], char pass[LBUF_SIZE])
{
    size_t nmsg = strlen(msg);
    size_t i = 0;
    if (nmsg > MBUF_SIZE)
    {
        *command = '\0';
        *user = '\0';
        *pass = '\0';
        return;
    }
    while (  i < nmsg
          && mux_isspace(msg[i]))
    {
        i++;
    }
    char *p = command;
    while (  i < nmsg
          && !mux_isspace(msg[i]))
    {
        safe_copy_chr(msg[i], command, &p, LBUF_SIZE-1);
        i++;
    }
    *p = '\0';

    while (  i < nmsg
          && mux_isspace(msg[i]))
    {
        i++;
    }
    p = user;
    if (  mudconf.name_spaces
       && i < nmsg
       && msg[i] == '\"')
    {
        for (; i < nmsg && (msg[i] == '\"' || mux_isspace(msg[i])); i++)
        {
            // Nothing.
        }
        while (  i < nmsg
              && msg[i] != '\"')
        {
            while (  i < nmsg
                  && !mux_isspace(msg[i])
                  && msg[i] != '\"')
            {
                safe_copy_chr(msg[i], user, &p, LBUF_SIZE-1);
                i++;
            }

            if (  nmsg <= i
               || msg[i] == '\"')
            {
                break;
            }

            while (  i < nmsg
                  && mux_isspace(msg[i]))
            {
                i++;
            }

            if (  i < nmsg
               && msg[i] != '\"')
            {
                safe_copy_chr(' ', user, &p, LBUF_SIZE-1);
            }
        }
        while (  i < nmsg
              && msg[i] == '\"')
        {
             i++;
        }
    }
    else
    {
        while (  i < nmsg
              && !mux_isspace(msg[i]))
        {
            safe_copy_chr(msg[i], user, &p, LBUF_SIZE-1);
            i++;
        }
    }
    *p = '\0';
    while (  i < nmsg
          && mux_isspace(msg[i]))
    {
        i++;
    }
    p = pass;
    while (  i < nmsg
          && !mux_isspace(msg[i]))
    {
        safe_copy_chr(msg[i], pass, &p, LBUF_SIZE-1);
        i++;
    }
    *p = '\0';
}

static void announce_connect(dbref player, DESC *d)
{
    desc_addhash(d);

    DESC *dtemp;
    int count = 0;
    DESC_ITER_CONN(dtemp)
    {
        count++;
    }

    if (mudstate.record_players < count)
    {
        mudstate.record_players = count;
    }

    char *buf = alloc_lbuf("announce_connect");
    dbref aowner;
    int aflags;
    size_t nLen;
    atr_pget_str_LEN(buf, player, A_TIMEOUT, &aowner, &aflags, &nLen);
    if (nLen)
    {
        d->timeout = mux_atol(buf);
        if (d->timeout <= 0)
        {
            d->timeout = mudconf.idle_timeout;
        }
    }

    dbref loc = Location(player);
    s_Connected(player);

    if (d->flags & DS_PUEBLOCLIENT)
    {
        s_Html(player);
    }

    raw_notify( player, tprintf("\n%sMOTD:%s %s\n", ANSI_HILITE,
                ANSI_NORMAL, mudconf.motd_msg));

    if (Wizard(player))
    {
        raw_notify(player, tprintf("%sWIZMOTD:%s %s\n", ANSI_HILITE,
            ANSI_NORMAL, mudconf.wizmotd_msg));

        if (!(mudconf.control_flags & CF_LOGIN))
        {
            raw_notify(player, "*** Logins are disabled.");
        }
    }
    atr_get_str_LEN(buf, player, A_LPAGE, &aowner, &aflags, &nLen);
    if (nLen)
    {
        raw_notify(player, "Your PAGE LOCK is set.  You may be unable to receive some pages.");
    }
    int num = 0;
    DESC_ITER_PLAYER(player, dtemp)
    {
        num++;
    }

    // Reset vacation flag.
    //
    s_Flags(player, FLAG_WORD2, Flags2(player) & ~VACATION);

    char *pRoomAnnounceFmt;
    char *pMonitorAnnounceFmt;
    if (num < 2)
    {
        pRoomAnnounceFmt = "%s has connected.";
        if (mudconf.have_comsys)
        {
            do_comconnect(player);
        }
        if (  Hidden(player)
           && Can_Hide(player))
        {
            pMonitorAnnounceFmt = "GAME: %s has DARK-connected.";
        }
        else
        {
            pMonitorAnnounceFmt = "GAME: %s has connected.";
        }
        if (  Suspect(player)
           || (d->host_info & H_SUSPECT))
        {
            raw_broadcast(WIZARD, "[Suspect] %s has connected.", Moniker(player));
        }
    }
    else
    {
        pRoomAnnounceFmt = "%s has reconnected.";
        pMonitorAnnounceFmt = "GAME: %s has reconnected.";
        if (  Suspect(player)
           || (d->host_info & H_SUSPECT))
        {
            raw_broadcast(WIZARD, "[Suspect] %s has reconnected.", Moniker(player));
        }
    }
    mux_sprintf(buf, LBUF_SIZE, pRoomAnnounceFmt, Moniker(player));
    raw_broadcast(MONITOR, pMonitorAnnounceFmt, Moniker(player));

    int key = MSG_INV;
    if (  loc != NOTHING
       && !(  Hidden(player)
           && Can_Hide(player)))
    {
        key |= (MSG_NBR | MSG_NBR_EXITS | MSG_LOC | MSG_FWDLIST);
    }

    dbref temp = mudstate.curr_enactor;
    mudstate.curr_enactor = player;
#ifdef REALITY_LVLS
    if(loc == NOTHING)
        notify_check(player, player, buf, key);
    else
        notify_except_rlevel(loc, player, player, buf, 0);
#else
    notify_check(player, player, buf, key);
#endif /* REALITY_LVLS */
    atr_pget_str_LEN(buf, player, A_ACONNECT, &aowner, &aflags, &nLen);
    CLinearTimeAbsolute lta;
    dbref zone, obj;
    if (nLen)
    {
        wait_que(player, player, player, false, lta, NOTHING, 0, buf,
            (char **)NULL, 0, NULL);
    }
    if (mudconf.master_room != NOTHING)
    {
        atr_pget_str_LEN(buf, mudconf.master_room, A_ACONNECT, &aowner,
            &aflags, &nLen);
        if (nLen)
        {
            wait_que(mudconf.master_room, player, player, false, lta,
                NOTHING, 0, buf, (char **)NULL, 0, NULL);
        }
        DOLIST(obj, Contents(mudconf.master_room))
        {
            atr_pget_str_LEN(buf, obj, A_ACONNECT, &aowner, &aflags, &nLen);
            if (nLen)
            {
                wait_que(obj, player, player, false, lta, NOTHING, 0, buf,
                    (char **)NULL, 0, NULL);
            }
        }
    }

    // Do the zone of the player's location's possible aconnect.
    //
    if (  mudconf.have_zones
       && Good_obj(zone = Zone(loc)))
    {
        switch (Typeof(zone))
        {
        case TYPE_THING:

            atr_pget_str_LEN(buf, zone, A_ACONNECT, &aowner, &aflags, &nLen);
            if (nLen)
            {
                wait_que(zone, player, player, false, lta, NOTHING, 0, buf,
                    (char **)NULL, 0, NULL);
            }
            break;

        case TYPE_ROOM:

            // check every object in the room for a connect action.
            //
            DOLIST(obj, Contents(zone))
            {
                atr_pget_str_LEN(buf, obj, A_ACONNECT, &aowner, &aflags,
                    &nLen);
                if (nLen)
                {
                    wait_que(obj, player, player, false, lta, NOTHING, 0,
                        buf, (char **)NULL, 0, NULL);
                }
            }
            break;

        default:

            log_text(tprintf("Invalid zone #%d for %s(#%d) has bad type %d",
                zone, Name(player), player, Typeof(zone)));
        }
    }
    free_lbuf(buf);
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();
    char *time_str = ltaNow.ReturnDateString(7);

    record_login(player, true, time_str, d->addr, d->username,
        inet_ntoa((d->address).sin_addr));
    if (mudconf.have_mailer)
    {
        check_mail(player, 0, false);
    }
    look_in(player, Location(player), (LK_SHOWEXIT|LK_OBEYTERSE|LK_SHOWVRML));
    mudstate.curr_enactor = temp;
    if (Guest(player))
    {
        db[player].fs.word[FLAG_WORD1] &= ~DARK;
    }
}

void announce_disconnect(dbref player, DESC *d, const char *reason)
{
    int num = 0, key;
    DESC *dtemp;
    DESC_ITER_PLAYER(player, dtemp)
    {
        num++;
    }

    dbref temp = mudstate.curr_enactor;
    mudstate.curr_enactor = player;
    dbref loc = Location(player);

    if (num < 2)
    {
        if (  Suspect(player)
           || (d->host_info & H_SUSPECT))
        {
            raw_broadcast(WIZARD, "[Suspect] %s has disconnected.", Moniker(player));
        }
        char *buf = alloc_lbuf("announce_disconnect.only");

        mux_sprintf(buf, LBUF_SIZE, "%s has disconnected.", Moniker(player));
        key = MSG_INV;
        if (  loc != NOTHING
           && !(  Hidden(player)
               && Can_Hide(player)))
        {
            key |= (MSG_NBR | MSG_NBR_EXITS | MSG_LOC | MSG_FWDLIST);
        }
#ifdef REALITY_LVLS
        if(loc == NOTHING)
            notify_check(player, player, buf, key);
        else
            notify_except_rlevel(loc, player, player, buf, 0);
#else
        notify_check(player, player, buf, key);
#endif /* REALITY_LVLS */

        if (mudconf.have_mailer)
        {
            do_mail_purge(player);
        }

        raw_broadcast(MONITOR, "GAME: %s has disconnected. <%s>", Moniker(player), reason);

        c_Connected(player);

        if (mudconf.have_comsys)
        {
            do_comdisconnect(player);
        }

        dbref aowner, zone, obj;
        int aflags;
        size_t nLen;
        char *argv[1];
        argv[0] = (char *)reason;
        CLinearTimeAbsolute lta;
        atr_pget_str_LEN(buf, player, A_ADISCONNECT, &aowner, &aflags, &nLen);
        if (nLen)
        {
            wait_que(player, player, player, false, lta, NOTHING, 0, buf,
                argv, 1, NULL);
        }
        if (mudconf.master_room != NOTHING)
        {
            atr_pget_str_LEN(buf, mudconf.master_room, A_ADISCONNECT, &aowner,
                &aflags, &nLen);
            if (nLen)
            {
                wait_que(mudconf.master_room, player, player, false, lta,
                    NOTHING, 0, buf, (char **)NULL, 0, NULL);
            }
            DOLIST(obj, Contents(mudconf.master_room))
            {
                atr_pget_str_LEN(buf, obj, A_ADISCONNECT, &aowner, &aflags,
                    &nLen);
                if (nLen)
                {
                    wait_que(obj, player, player, false, lta, NOTHING, 0,
                        buf, (char **)NULL, 0, NULL);
                }
            }
        }

        // Do the zone of the player's location's possible adisconnect.
        //
        if (mudconf.have_zones && Good_obj(zone = Zone(loc)))
        {
            switch (Typeof(zone))
            {
            case TYPE_THING:

                atr_pget_str_LEN(buf, zone, A_ADISCONNECT, &aowner, &aflags,
                    &nLen);
                if (nLen)
                {
                    wait_que(zone, player, player, false, lta, NOTHING, 0,
                        buf, (char **)NULL, 0, NULL);
                }
                break;

            case TYPE_ROOM:

                // check every object in the room for a connect action.
                //
                DOLIST(obj, Contents(zone))
                {
                    atr_pget_str_LEN(buf, obj, A_ADISCONNECT, &aowner, &aflags,
                        &nLen);
                    if (nLen)
                    {
                        wait_que(obj, player, player, false, lta, NOTHING,
                            0, buf, (char **)NULL, 0, NULL);
                    }
                }
                break;

            default:
                log_text(tprintf("Invalid zone #%d for %s(#%d) has bad type %d",
                    zone, Name(player), player, Typeof(zone)));
            }
        }
        free_lbuf(buf);
        if (d->flags & DS_AUTODARK)
        {
            d->flags &= ~DS_AUTODARK;
            db[player].fs.word[FLAG_WORD1] &= ~DARK;
        }

        if (Guest(player))
        {
            db[player].fs.word[FLAG_WORD1] |= DARK;
            halt_que(NOTHING, player);
        }
    }
    else
    {
        if (  Suspect(player)
           || (d->host_info & H_SUSPECT))
        {
            raw_broadcast(WIZARD, "[Suspect] %s has partially disconnected.", Moniker(player));
        }
        char *mbuf = alloc_mbuf("announce_disconnect.partial");
        mux_sprintf(mbuf, MBUF_SIZE, "%s has partially disconnected.", Moniker(player));
        key = MSG_INV;
        if (  loc != NOTHING
           && !(  Hidden(player)
               && Can_Hide(player)))
        {
            key |= (MSG_NBR | MSG_NBR_EXITS | MSG_LOC | MSG_FWDLIST);
        }
#ifdef REALITY_LVLS
        if(loc == NOTHING)
            notify_check(player, player, mbuf, key);
        else
            notify_except_rlevel(loc, player, player, mbuf, 0);
#else
        notify_check(player, player, mbuf, key);
#endif /* REALITY_LVLS */
        raw_broadcast(MONITOR, "GAME: %s has partially disconnected.",
            Moniker(player));
        free_mbuf(mbuf);
    }

    mudstate.curr_enactor = temp;
    desc_delhash(d);

    local_disconnect(player, num);
}

int boot_off(dbref player, const char *message)
{
    DESC *d, *dnext;
    int count = 0;
    DESC_SAFEITER_PLAYER(player, d, dnext)
    {
        if (message && *message)
        {
            queue_string(d, message);
            queue_write_LEN(d, "\r\n", 2);
        }
        shutdownsock(d, R_BOOT);
        count++;
    }
    return count;
}

int boot_by_port(SOCKET port, bool bGod, const char *message)
{
    DESC *d, *dnext;
    int count = 0;
    DESC_SAFEITER_ALL(d, dnext)
    {
        if (  d->descriptor == port
           && (  bGod
              || !(d->flags & DS_CONNECTED)
              || !God(d->player)))
        {
            if (  message
               && *message)
            {
                queue_string(d, message);
                queue_write_LEN(d, "\r\n", 2);
            }
            shutdownsock(d, R_BOOT);
            count++;
        }
    }
    return count;
}

/* ---------------------------------------------------------------------------
 * desc_reload: Reload parts of net descriptor that are based on db info.
 */

void desc_reload(dbref player)
{
    DESC *d;
    char *buf;
    dbref aowner;
    FLAG aflags;

    DESC_ITER_PLAYER(player, d)
    {
        buf = atr_pget(player, A_TIMEOUT, &aowner, &aflags);
        if (buf)
        {
            d->timeout = mux_atol(buf);
            if (d->timeout <= 0)
            {
                d->timeout = mudconf.idle_timeout;
            }
        }
        free_lbuf(buf);
    }
}

// ---------------------------------------------------------------------------
// fetch_session: Return number of sessions (or 0 if not logged in).
//
int fetch_session(dbref target)
{
    DESC *d;
    int nCount = 0;
    DESC_ITER_PLAYER(target, d)
    {
        nCount++;
    }
    return nCount;
}

static DESC *find_least_idle(dbref target)
{
    CLinearTimeAbsolute ltaNewestLastTime;

    DESC *d;
    DESC *dLeastIdle = NULL;
    DESC_ITER_PLAYER(target, d)
    {
        if (  NULL == dLeastIdle
           || ltaNewestLastTime < d->last_time)
        {
            dLeastIdle = d;
            ltaNewestLastTime = d->last_time;
        }
    }
    return dLeastIdle;
}

int fetch_height(dbref target)
{
    DESC *d = find_least_idle(target);
    if (NULL != d)
    {
        return d->height;
    }
    return 24;
}

int fetch_width(dbref target)
{
    DESC *d = find_least_idle(target);
    if (NULL != d)
    {
        return d->width;
    }
    return 78;
}

// ---------------------------------------------------------------------------
// fetch_idle: Return smallest idle time for a player (or -1 if not logged in).
//
int fetch_idle(dbref target)
{
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();

    DESC *d = find_least_idle(target);
    if (NULL != d)
    {
        CLinearTimeDelta ltdResult;
        ltdResult = ltaNow - d->last_time;
        return ltdResult.ReturnSeconds();
    }
    else
    {
        return -1;
    }
}

// ---------------------------------------------------------------------------
// find_oldest: Return descriptor with the oldeset connected_at (or NULL if
// not logged in).
//
void find_oldest(dbref target, DESC *dOldest[2])
{
    dOldest[0] = NULL;
    dOldest[1] = NULL;

    DESC *d;
    bool bFound = false;
    DESC_ITER_PLAYER(target, d)
    {
        if (  !bFound
           || d->connected_at < dOldest[0]->connected_at)
        {
            bFound = true;
            dOldest[1] = dOldest[0];
            dOldest[0] = d;
        }
    }
}

// ---------------------------------------------------------------------------
// fetch_connect: Return largest connect time for a player (or -1 if not
// logged in).
//
int fetch_connect(dbref target)
{
    DESC *dOldest[2];
    find_oldest(target, dOldest);
    if (dOldest[0])
    {
        CLinearTimeAbsolute ltaNow;
        CLinearTimeDelta ltdOldest;

        ltaNow.GetUTC();
        ltdOldest = ltaNow - dOldest[0]->connected_at;
        return ltdOldest.ReturnSeconds();
    }
    else
    {
        return -1;
    }
}

// A NOTE about AUTODARK: It only works for wizard players. Wizard players
// are automatically set DARK if they are not already set DARK and they have
// no session which is unidle.
//
// The AUTODARK state is cleared when at least one session becomes unidle.
// The AUTODARK state is also cleared when the last idle session is
// disconnected from the server (session shutdown or @shutdown).
//
void check_idle(void)
{
    DESC *d, *dnext;

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();

    DESC_SAFEITER_ALL(d, dnext)
    {
        if (  (d->flags & DS_CONNECTED)
           && KeepAlive(d->player))
        {
            // Send a Telnet NOP code - creates traffic to keep NAT routers
            // happy.  Hopefully this only runs once a minute.
            //
            const char aNOP[2] = { NVT_IAC, NVT_NOP };
            queue_write_LEN(d, aNOP, sizeof(aNOP));
        }
        if (d->flags & DS_AUTODARK)
        {
            continue;
        }
        if (d->flags & DS_CONNECTED)
        {
            if (mudconf.idle_timeout <= 0)
            {
                // Idle timeout checking on connected players is effectively disabled.
                // PennMUSH uses idle_timeout == 0. Rhost uses idel_timeout == -1.
                // We will be disabled for either setting.
                //
                continue;
            }

            CLinearTimeDelta ltdIdle = ltaNow - d->last_time;
            if (Can_Idle(d->player))
            {
                if (  mudconf.idle_wiz_dark
                   && (Flags(d->player) & (WIZARD|DARK)) == WIZARD
                   && ltdIdle.ReturnSeconds() > mudconf.idle_timeout)
                {
                    // Make sure this Wizard player does not have some other
                    // active session.
                    //
                    DESC *d1;
                    bool bFound = false;
                    DESC_ITER_PLAYER(d->player, d1)
                    {
                        if (d1 != d)
                        {
                            CLinearTimeDelta ltd = ltaNow - d1->last_time;
                            if (ltd.ReturnSeconds() <= mudconf.idle_timeout)
                            {
                                 bFound = true;
                                 break;
                            }
                        }
                    }
                    if (!bFound)
                    {
                        db[d->player].fs.word[FLAG_WORD1] |= DARK;
                        DESC_ITER_PLAYER(d->player, d1)
                        {
                            d1->flags |= DS_AUTODARK;
                        }
                    }
                }
            }
            else if (ltdIdle.ReturnSeconds() > d->timeout)
            {
                queue_write(d, "*** Inactivity Timeout ***\r\n");
                shutdownsock(d, R_TIMEOUT);
            }
        }
        else if (0 < mudconf.conn_timeout)
        {
            CLinearTimeDelta ltdIdle = ltaNow - d->connected_at;
            if (ltdIdle.ReturnSeconds() > mudconf.conn_timeout)
            {
                queue_write(d, "*** Login Timeout ***\r\n");
                shutdownsock(d, R_TIMEOUT);
            }
        }
    }
}

void check_events(void)
{
    dbref thing, parent;
    int lev;

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();

    FIELDEDTIME ft;
    if (!ltaNow.ReturnFields(&ft))
    {
        return;
    }

    // Resetting every midnight.
    //
    static int iLastHourChecked = 25;
    if (  iLastHourChecked == 23
       && ft.iHour < iLastHourChecked)
    {
        mudstate.events_flag &= ~ET_DAILY;
    }
    iLastHourChecked = ft.iHour;

    if (  ft.iHour == mudconf.events_daily_hour
       && !(mudstate.events_flag & ET_DAILY))
    {
        mudstate.events_flag |= ET_DAILY;
        DO_WHOLE_DB(thing)
        {
            if (Going(thing))
            {
                continue;
            }

            ITER_PARENTS(thing, parent, lev)
            {
                if (Flags2(thing) & HAS_DAILY)
                {
                    did_it(Owner(thing), thing, 0, NULL, 0, NULL, A_DAILY, (char **)NULL, 0);
                    break;
                }
            }
        }
    }

}

#define MAX_TRIMMED_NAME_LENGTH 16
static const char *trimmed_name(dbref player, size_t *pvw)
{
    static char cbuff[MBUF_SIZE];

    ANSI_TruncateToField(
        Moniker(player),
        sizeof(cbuff),
        cbuff,
        MAX_TRIMMED_NAME_LENGTH,
        pvw,
        ANSI_ENDGOAL_NORMAL
    );
    return cbuff;
}

static char *trimmed_site(char *szName)
{
    static char buff[MBUF_SIZE];

    size_t nLen = strlen(szName);
    if (  mudconf.site_chars <= 0
       || nLen <= mudconf.site_chars)
    {
        return szName;
    }
    nLen = mudconf.site_chars;
    if (nLen > sizeof(buff)-1)
    {
        nLen = sizeof(buff)-1;
    }
    memcpy(buff, szName, nLen);
    buff[nLen] = '\0';
    return buff;
}

static void dump_users(DESC *e, char *match, int key)
{
    DESC *d;
    int count;
    char *buf, *fp, *sp, flist[4], slist[4];
    dbref room_it;

    if (match)
    {
        while (mux_isspace(*match))
        {
            match++;
        }

        if (!*match)
        {
            match = NULL;
        }
    }

    if (  (e->flags & (DS_PUEBLOCLIENT|DS_CONNECTED))
       && Html(e->player))
    {
        queue_write(e, "<pre>");
    }

    buf = alloc_mbuf("dump_users");
    if (key == CMD_SESSION)
    {
        queue_write(e, "                               ");
        queue_write(e, "     Characters Input----  Characters Output---\r\n");
    }
    queue_write(e, "Player Name        On For Idle ");
    if (key == CMD_SESSION)
    {
        queue_write(e, "Port Pend  Lost     Total  Pend  Lost     Total\r\n");
    }
    else if (  (e->flags & DS_CONNECTED)
            && Wizard_Who(e->player)
            && key == CMD_WHO)
    {
        queue_write(e, "  Room    Cmds   Host\r\n");
    }
    else
    {
        if (  Wizard_Who(e->player)
           || See_Hidden(e->player))
        {
            queue_write(e, "  ");
        }
        else
        {
            queue_write(e, " ");
        }
        queue_string(e, mudstate.doing_hdr);
        queue_write_LEN(e, "\r\n", 2);
    }
    count = 0;

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();

    DESC_ITER_ALL(d)
    {
        if (!(  (  (e->flags & DS_CONNECTED)
                && SiteMon(e->player))
             || (d->flags & DS_CONNECTED)))
        {
            continue;
        }
        if (  !(d->flags & DS_CONNECTED)
           || !Hidden(d->player)
           || (  (e->flags & DS_CONNECTED)
              && (  Wizard_Who(e->player)
                 || See_Hidden(e->player))))
        {
            count++;
            if (  match
               && (  !(d->flags & DS_CONNECTED)
                  || string_prefix(Name(d->player), match) == 0))
            {
                continue;
            }
            if (  key == CMD_SESSION
               && (  !(e->flags & DS_CONNECTED)
                  || !Wizard_Who(e->player))
               && (  !(e->flags & DS_CONNECTED)
                  || !(d->flags & DS_CONNECTED)
                  || d->player != e->player))
            {
                continue;
            }

            // Get choice flags for wizards.
            //
            fp = flist;
            sp = slist;
            if (  (e->flags & DS_CONNECTED)
               && Wizard_Who(e->player))
            {
                if (  (d->flags & DS_CONNECTED)
                   && Hidden(d->player))
                {
                    if (d->flags & DS_AUTODARK)
                    {
                        safe_copy_chr('d', flist, &fp, sizeof(flist)-1);
                    }
                    else
                    {
                        safe_copy_chr('D', flist, &fp, sizeof(flist)-1);
                    }
                }
                if (d->flags & DS_CONNECTED)
                {
                    if (Hideout(d->player))
                    {
                        safe_copy_chr('U', flist, &fp, sizeof(flist)-1);
                    }
                    else
                    {
                        room_it = where_room(d->player);
                        if (Good_obj(room_it))
                        {
                            if (Hideout(room_it))
                            {
                                safe_copy_chr('u', flist, &fp, sizeof(flist)-1);
                            }
                        }
                        else
                        {
                            safe_copy_chr('u', flist, &fp, sizeof(flist)-1);
                        }
                    }

                    if (Suspect(d->player))
                    {
                        safe_copy_chr('+', flist, &fp, sizeof(flist)-1);
                    }
                }
                if (d->host_info & H_FORBIDDEN)
                {
                    safe_copy_chr('F', slist, &sp, sizeof(slist)-1);
                }
                if (d->host_info & H_REGISTRATION)
                {
                    safe_copy_chr('R', slist, &sp, sizeof(slist)-1);
                }
                if (d->host_info & H_SUSPECT)
                {
                    safe_copy_chr('+', slist, &sp, sizeof(slist)-1);
                }
                if (d->host_info & H_GUEST)
                {
                    safe_copy_chr('G', slist, &sp, sizeof(slist)-1);
                }
            }
            else if (  (e->flags & DS_CONNECTED)
                    && (d->flags & DS_CONNECTED)
                    && See_Hidden(e->player)
                    && Hidden(d->player))
            {
                if (d->flags & DS_AUTODARK)
                {
                    safe_copy_chr('d', flist, &fp, sizeof(flist)-1);
                }
                else
                {
                    safe_copy_chr('D', flist, &fp, sizeof(flist)-1);
                }
            }
            *fp = '\0';
            *sp = '\0';

            CLinearTimeDelta ltdConnected = ltaNow - d->connected_at;
            CLinearTimeDelta ltdLastTime  = ltaNow - d->last_time;

            const char *pNameField = "<Unconnected>";
            size_t vwNameField = strlen(pNameField);
            if (d->flags & DS_CONNECTED)
            {
                pNameField = trimmed_name(d->player, &vwNameField);
            }

            // How many spaces between the name field and the 'On For' field.
            //
            size_t nFill;
            if (13 <= vwNameField)
            {
                nFill = 1;
            }
            else
            {
                nFill = 14-vwNameField;
            }
            char aFill[15];
            memset(aFill, ' ', nFill);
            aFill[nFill] = '\0';

            // The width size allocated to the 'On For' field.
            //
            size_t nOnFor = 25 - nFill - vwNameField;

            const char *pTimeStamp1 = time_format_1(ltdConnected.ReturnSeconds(), nOnFor);
            const char *pTimeStamp2 = time_format_2(ltdLastTime.ReturnSeconds());

            if (  (e->flags & DS_CONNECTED)
               && Wizard_Who(e->player)
               && key == CMD_WHO)
            {
                mux_sprintf(buf, MBUF_SIZE, "%s%s%s %4s%-3s#%-6d%5d%3s%s\r\n",
                    pNameField, aFill,
                    pTimeStamp1,
                    pTimeStamp2,
                    flist,
                    ((d->flags & DS_CONNECTED) ? Location(d->player) : -1),
                    d->command_count,
                    slist,
                    trimmed_site(((d->username[0] != '\0') ? tprintf("%s@%s", d->username, d->addr) : d->addr)));
            }
            else if (key == CMD_SESSION)
            {
                mux_sprintf(buf, MBUF_SIZE, "%s%s%s %4s%5u%5d%6d%10d%6d%6d%10d\r\n",
                    pNameField, aFill,
                    pTimeStamp1,
                    pTimeStamp2,
                    d->descriptor,
                    d->input_size, d->input_lost,
                    d->input_tot,
                    d->output_size, d->output_lost,
                    d->output_tot);
            }
            else if (  Wizard_Who(e->player)
                    || See_Hidden(e->player))
            {
                mux_sprintf(buf, MBUF_SIZE, "%s%s%s %4s%-3s%s\r\n",
                    pNameField, aFill,
                    pTimeStamp1,
                    pTimeStamp2,
                    flist,
                    d->doing);
            }
            else
            {
                mux_sprintf(buf, MBUF_SIZE, "%s%s%s %4s  %s\r\n",
                    pNameField, aFill,
                    pTimeStamp1,
                    pTimeStamp2,
                    d->doing);
            }
            queue_string(e, buf);
        }
    }

    // Sometimes I like the ternary operator.
    //
    mux_sprintf(buf, MBUF_SIZE, "%d Player%slogged in, %d record, %s maximum.\r\n", count,
        (count == 1) ? " " : "s ", mudstate.record_players,
        (mudconf.max_players == -1) ? "no" : mux_ltoa_t(mudconf.max_players));
    queue_write(e, buf);

    if (  (e->flags & (DS_PUEBLOCLIENT|DS_CONNECTED))
       && Html(e->player))
    {
        queue_write(e, "</pre>");
    }
    free_mbuf(buf);
}

#ifdef WOD_REALMS
#define INFO_VERSION "1.1"
#else // WOD_REALMS
#define INFO_VERSION "1"
#endif // WOD_REALMS

static void dump_info(DESC *arg_desc)
{
    queue_write(arg_desc, "### Begin INFO " INFO_VERSION "\r\n");

    queue_string(arg_desc, tprintf("Name: %s\r\n", mudconf.mud_name));

    char *temp = mudstate.start_time.ReturnDateString();
    queue_write(arg_desc, tprintf("Uptime: %s\r\n", temp));

    DESC *d;
    int count = 0;
    DESC_ITER_CONN(d)
    {
        if (!Good_obj(d->player))
        {
            continue;
        }
        if (  !Hidden(d->player)
           || (  (arg_desc->flags & DS_CONNECTED)
              && See_Hidden(arg_desc->player)))
        {
            count++;
        }
    }
    queue_write(arg_desc, tprintf("Connected: %d\r\n", count));
    queue_write(arg_desc, tprintf("Size: %d\r\n", mudstate.db_top));
    queue_write(arg_desc, tprintf("Version: %s\r\n", mudstate.short_ver));
#ifdef WOD_REALMS
    queue_write(arg_desc, tprintf("Patches: WOD_REALMS\r\n"));
#endif // WOD_REALMS
    queue_write(arg_desc, "### End INFO\r\n");
}

static char *MakeCanonicalDoing(char *pDoing, size_t *pnValidDoing, bool *pbValidDoing)
{
    *pnValidDoing = 0;
    *pbValidDoing = false;

    if (!pDoing)
    {
        return NULL;
    }

    // First, remove all '\r\n\t' from the string.
    //
    char *Buffer = RemoveSetOfCharacters(pDoing, "\r\n\t");

    // Optimize/terminate any ANSI in the string.
    //
    size_t nVisualWidth;
    static char szFittedDoing[SIZEOF_DOING_STRING];
    *pnValidDoing = ANSI_TruncateToField
                    ( Buffer,
                      SIZEOF_DOING_STRING,
                      szFittedDoing,
                      WIDTHOF_DOING_STRING,
                      &nVisualWidth,
                      ANSI_ENDGOAL_NORMAL
                    );
    *pbValidDoing = true;
    return szFittedDoing;
}

// ---------------------------------------------------------------------------
// do_doing: Set the doing string that appears in the WHO report.
// Idea from R'nice@TinyTIM.
//
void do_doing(dbref executor, dbref caller, dbref enactor, int key, char *arg)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);

    // Make sure there can be no embedded newlines from %r
    //
    static char *Empty = "";
    char *szValidDoing = Empty;
    bool bValidDoing;
    size_t nValidDoing = 0;
    if (arg)
    {
        szValidDoing = MakeCanonicalDoing(arg, &nValidDoing, &bValidDoing);
        if (!bValidDoing)
        {
            szValidDoing = Empty;
            nValidDoing = 0;
        }
    }

    bool bQuiet = ((key & DOING_QUIET) == DOING_QUIET);
    key &= DOING_MASK;
    if (key == DOING_MESSAGE)
    {
        DESC *d;
        bool bFound = false;
        DESC_ITER_PLAYER(executor, d)
        {
            memcpy(d->doing, szValidDoing, nValidDoing+1);
            bFound = true;
        }
        if (bFound)
        {
            if (  !bQuiet
               && !Quiet(executor))
            {
                notify(executor, "Set.");
            }
        }
        else
        {
            notify(executor, "Not connected.");
        }
    }
    else if (key == DOING_UNIQUE)
    {
        DESC *d;
        DESC *dMax = NULL;
        CLinearTimeAbsolute ltaMax;
        DESC_ITER_PLAYER(executor, d)
        {
            if (  !dMax
               && ltaMax < d->last_time)
            {
                ltaMax = d->last_time;
                dMax = d;
            }
        }
        if (dMax)
        {
            memcpy(dMax->doing, szValidDoing, nValidDoing+1);
            if (  !bQuiet
               && !Quiet(executor))
            {
                notify(executor, "Set.");
            }
        }
        else
        {
            notify(executor, "Not connected.");
        }
    }
    else if (key == DOING_HEADER)
    {
        if (!Can_Poll(executor))
        {
            notify(executor, NOPERM_MESSAGE);
            return;
        }
        if (nValidDoing == 0)
        {
            mux_strncpy(mudstate.doing_hdr, "Doing", SIZEOF_DOING_STRING-1);
        }
        else
        {
            memcpy(mudstate.doing_hdr, szValidDoing, nValidDoing+1);
        }
        if (  !bQuiet
           && !Quiet(executor))
        {
            notify(executor, "Set.");
        }
    }
    else // if (key == DOING_POLL)
    {
        notify(executor, tprintf("Poll: %s", mudstate.doing_hdr));
    }
}

NAMETAB logout_cmdtable[] =
{
    {(char *)"DOING",         5,  CA_PUBLIC,  CMD_DOING},
    {(char *)"LOGOUT",        6,  CA_PUBLIC,  CMD_LOGOUT},
    {(char *)"OUTPUTPREFIX", 12,  CA_PUBLIC,  CMD_PREFIX|CMD_NOxFIX},
    {(char *)"OUTPUTSUFFIX", 12,  CA_PUBLIC,  CMD_SUFFIX|CMD_NOxFIX},
    {(char *)"QUIT",          4,  CA_PUBLIC,  CMD_QUIT},
    {(char *)"SESSION",       7,  CA_PUBLIC,  CMD_SESSION},
    {(char *)"WHO",           3,  CA_PUBLIC,  CMD_WHO},
    {(char *)"PUEBLOCLIENT", 12,  CA_PUBLIC,  CMD_PUEBLOCLIENT},
    {(char *)"INFO",          4,  CA_PUBLIC,  CMD_INFO},
    {NULL,                    0,          0,         0}
};

void init_logout_cmdtab(void)
{
    NAMETAB *cp;

    // Make the htab bigger than the number of entries so that we find things
    // on the first check.  Remember that the admin can add aliases.
    //
    for (cp = logout_cmdtable; cp->flag; cp++)
    {
        hashaddLEN(cp->name, strlen(cp->name), cp, &mudstate.logout_cmd_htab);
    }
}

static void failconn(const char *logcode, const char *logtype, const char *logreason,
                     DESC *d, int disconnect_reason,
                     dbref player, int filecache, char *motd_msg, char *command,
                     char *user, char *password, char *cmdsave)
{
    STARTLOG(LOG_LOGIN | LOG_SECURITY, logcode, "RJCT");
    char *buff = alloc_mbuf("failconn.LOG");
    mux_sprintf(buff, MBUF_SIZE, "[%u/%s] %s rejected to ", d->descriptor, d->addr, logtype);
    log_text(buff);
    free_mbuf(buff);
    if (player != NOTHING)
    {
        log_name(player);
    }
    else
    {
        log_text(user);
    }
    log_text(" (");
    log_text(logreason);
    log_text(")");
    ENDLOG;
    fcache_dump(d, filecache);
    if (*motd_msg)
    {
        queue_string(d, motd_msg);
        queue_write_LEN(d, "\r\n", 2);
    }
    free_lbuf(command);
    free_lbuf(user);
    free_lbuf(password);
    shutdownsock(d, disconnect_reason);
    mudstate.debug_cmd = cmdsave;
    return;
}

static const char *connect_fail = "Either that player does not exist, or has a different password.\r\n";

static bool check_connect(DESC *d, char *msg)
{
    char *buff;
    dbref player, aowner;
    int aflags, nplayers;
    DESC *d2;
    const char *p;
    bool isGuest = false;

    char *cmdsave = mudstate.debug_cmd;
    mudstate.debug_cmd = (char *)"< check_connect >";

    // Hide the password length from SESSION.
    //
    d->input_tot -= (strlen(msg) + 1);

    // Crack the command apart.
    //
    char *command = alloc_lbuf("check_conn.cmd");
    char *user = alloc_lbuf("check_conn.user");
    char *password = alloc_lbuf("check_conn.pass");
    parse_connect(msg, command, user, password);

    // At this point, command, user, and password are all less than
    // MBUF_SIZE.
    //
    if (  strncmp(command, "co", 2) == 0
       || strncmp(command, "cd", 2) == 0)
    {
        if (string_prefix(user, mudconf.guest_prefix))
        {
            if (  (d->host_info & H_GUEST)
               || (   !mudconf.allow_guest_from_registered_site
                  && (d->host_info & H_REGISTRATION)))
            {
                // Someone from an IP with guest restrictions is
                // trying to use a guest account. Give them the blurb
                // most likely to have instructions about requesting a
                // character by other means and then fail this
                // connection.
                //
                // The guest 'power' is handled separately further
                // down.
                //
                failconn("CONN", "Connect", "Guest Site Forbidden", d,
                    R_GAMEDOWN, NOTHING, FC_CONN_REG, mudconf.downmotd_msg,
                    command, user, password, cmdsave);
                return false;
            }
            if (  mudconf.guest_char != NOTHING
               && (mudconf.control_flags & CF_LOGIN))
            {
                if (!(mudconf.control_flags & CF_GUEST))
                {
                    queue_write(d, "Guest logins are disabled.\r\n");
                    free_lbuf(command);
                    free_lbuf(user);
                    free_lbuf(password);
                    return false;
                }

                if ((p = Guest.Create(d)) == NULL)
                {
                    queue_write(d, "All guests are tied up, please try again later.\r\n");
                    free_lbuf(command);
                    free_lbuf(user);
                    free_lbuf(password);
                    return false;
                }
                mux_strncpy(user, p, LBUF_SIZE-1);
                mux_strncpy(password, GUEST_PASSWORD, LBUF_SIZE-1);
                isGuest = true;
            }
        }

        // See if this connection would exceed the max #players.
        //
        if (mudconf.max_players < 0)
        {
            nplayers = mudconf.max_players - 1;
        }
        else
        {
            nplayers = 0;
            DESC_ITER_CONN(d2)
            {
                nplayers++;
            }
        }

        player = connect_player(user, password, d->addr, d->username, inet_ntoa((d->address).sin_addr));
        if (  player == NOTHING
           || (!isGuest && Guest.CheckGuest(player)))
        {
            // Not a player, or wrong password.
            //
            queue_write(d, connect_fail);
            STARTLOG(LOG_LOGIN | LOG_SECURITY, "CON", "BAD");
            buff = alloc_lbuf("check_conn.LOG.bad");
            mux_sprintf(buff, LBUF_SIZE, "[%u/%s] Failed connect to '%s'", d->descriptor, d->addr, user);
            log_text(buff);
            free_lbuf(buff);
            ENDLOG;
            if (--(d->retries_left) <= 0)
            {
                free_lbuf(command);
                free_lbuf(user);
                free_lbuf(password);
                shutdownsock(d, R_BADLOGIN);
                mudstate.debug_cmd = cmdsave;
                return false;
            }
        }
        else if (  (  (mudconf.control_flags & CF_LOGIN)
                   && (nplayers < mudconf.max_players))
                || WizRoy(player)
                || God(player))
        {
            if (  strncmp(command, "cd", 2) == 0
               && (  Wizard(player)
                  || God(player)))
            {
                db[player].fs.word[FLAG_WORD1] |= DARK;
            }

            // Make sure we don't have a guest from an unwanted host.
            // The majority of these are handled above.
            //
            // The following code handles the case where a staffer
            // (#1-only by default) has specifically given the guest 'power'
            // to an existing player.
            //
            // In this case, the player -already- has an account complete
            // with password. We still fail the connection to -this- player
            // but if the site isn't register_sited, this player can simply
            // auto-create another player. So, the procedure is not much
            // different from @newpassword'ing them. Oh well. We are just
            // following orders. ;)
            //
            if (  Guest(player)
               && (  (d->host_info & H_GUEST)
                  || (   !mudconf.allow_guest_from_registered_site
                     && (d->host_info & H_REGISTRATION))))
            {
                failconn("CON", "Connect", "Guest Site Forbidden", d,
                    R_GAMEDOWN, player, FC_CONN_SITE,
                    mudconf.downmotd_msg, command, user, password,
                    cmdsave);
                return false;
            }

            // Logins are enabled, or wiz or god.
            //
            STARTLOG(LOG_LOGIN, "CON", "LOGIN");
            buff = alloc_mbuf("check_conn.LOG.login");
            mux_sprintf(buff, MBUF_SIZE, "[%u/%s] Connected to ", d->descriptor, d->addr);
            log_text(buff);
            log_name_and_loc(player);
            free_mbuf(buff);
            ENDLOG;
            d->flags |= DS_CONNECTED;
            d->connected_at.GetUTC();
            d->player = player;

            // Check to see if the player is currently running an
            // @program. If so, drop the new descriptor into it.
            //
            DESC_ITER_PLAYER(player, d2)
            {
                if (d2->program_data != NULL)
                {
                    d->program_data = d2->program_data;
                    break;
                }
            }

            // Give the player the MOTD file and the settable MOTD
            // message(s). Use raw notifies so the player doesn't try
            // to match on the text.
            //
            if (Guest(player))
            {
                fcache_dump(d, FC_CONN_GUEST);
            }
            else
            {
                buff = atr_get(player, A_LAST, &aowner, &aflags);
                if (*buff == '\0')
                    fcache_dump(d, FC_CREA_NEW);
                else
                    fcache_dump(d, FC_MOTD);
                if (Wizard(player))
                    fcache_dump(d, FC_WIZMOTD);
                free_lbuf(buff);
            }
            announce_connect(player, d);

            DESC* dtemp;
            int num_con = 0;
            DESC_ITER_PLAYER(player, dtemp)
            {
                num_con++;
            }
            local_connect(player, 0, num_con);

            // If stuck in an @prog, show the prompt.
            //
            if (d->program_data != NULL)
            {
                queue_write_LEN(d, ">\377\371", 3);
            }

        }
        else if (!(mudconf.control_flags & CF_LOGIN))
        {
            failconn("CON", "Connect", "Logins Disabled", d, R_GAMEDOWN, player, FC_CONN_DOWN,
                mudconf.downmotd_msg, command, user, password, cmdsave);
            return false;
        }
        else
        {
            failconn("CON", "Connect", "Game Full", d, R_GAMEFULL, player, FC_CONN_FULL,
                mudconf.fullmotd_msg, command, user, password, cmdsave);
            return false;
        }
    }
    else if (strncmp(command, "cr", 2) == 0)
    {
        // Enforce game down.
        //
        if (!(mudconf.control_flags & CF_LOGIN))
        {
            failconn("CRE", "Create", "Logins Disabled", d, R_GAMEDOWN, NOTHING, FC_CONN_DOWN,
                mudconf.downmotd_msg, command, user, password, cmdsave);
            return false;
        }

        // Enforce max #players.
        //
        if (mudconf.max_players < 0)
        {
            nplayers = mudconf.max_players;
        }
        else
        {
            nplayers = 0;
            DESC_ITER_CONN(d2)
            {
                nplayers++;
            }
        }
        if (nplayers > mudconf.max_players)
        {
            // Too many players on, reject the attempt.
            //
            failconn("CRE", "Create", "Game Full", d,
                R_GAMEFULL, NOTHING, FC_CONN_FULL,
                mudconf.fullmotd_msg, command, user, password,
                cmdsave);
            return false;
        }
        if (d->host_info & H_REGISTRATION)
        {
            fcache_dump(d, FC_CREA_REG);
        }
        else
        {
            const char *pmsg;
            player = create_player(user, password, NOTHING, false, &pmsg);
            if (player == NOTHING)
            {
                queue_write(d, pmsg);
                queue_write(d, "\r\n");
                STARTLOG(LOG_SECURITY | LOG_PCREATES, "CON", "BAD");
                buff = alloc_lbuf("check_conn.LOG.badcrea");
                mux_sprintf(buff, LBUF_SIZE, "[%u/%s] Create of '%s' failed", d->descriptor, d->addr, user);
                log_text(buff);
                free_lbuf(buff);
                ENDLOG;
            }
            else
            {
                AddToPublicChannel(player);
                STARTLOG(LOG_LOGIN | LOG_PCREATES, "CON", "CREA");
                buff = alloc_mbuf("check_conn.LOG.create");
                mux_sprintf(buff, MBUF_SIZE, "[%u/%s] Created ", d->descriptor, d->addr);
                log_text(buff);
                log_name(player);
                free_mbuf(buff);
                ENDLOG;
                move_object(player, mudconf.start_room);
                d->flags |= DS_CONNECTED;
                d->connected_at.GetUTC();
                d->player = player;
                fcache_dump(d, FC_CREA_NEW);
                announce_connect(player, d);

                // Since it is on the create call, assume connection count
                // is 0 and indicate the connect is a new character.
                //
                local_connect(player, 1, 0);
            }
        }
    }
    else
    {
        welcome_user(d);
        STARTLOG(LOG_LOGIN | LOG_SECURITY, "CON", "BAD");
        buff = alloc_mbuf("check_conn.LOG.bad");
        msg[150] = '\0';
        mux_sprintf(buff, MBUF_SIZE, "[%u/%s] Failed connect: '%s'", d->descriptor, d->addr, msg);
        log_text(buff);
        free_mbuf(buff);
        ENDLOG;
    }
    free_lbuf(command);
    free_lbuf(user);
    free_lbuf(password);

    mudstate.debug_cmd = cmdsave;
    return true;
}

static void do_logged_out_internal(DESC *d, int key, char *arg)
{
    switch (key)
    {
    case CMD_QUIT:

        shutdownsock(d, R_QUIT);
        break;

    case CMD_LOGOUT:

        shutdownsock(d, R_LOGOUT);
        break;

    case CMD_WHO:
    case CMD_DOING:
    case CMD_SESSION:

        dump_users(d, arg, key);
        break;

    case CMD_PREFIX:

        set_userstring(&d->output_prefix, arg);
        break;

    case CMD_SUFFIX:

        set_userstring(&d->output_suffix, arg);
        break;

    case CMD_INFO:

        dump_info(d);
        break;

    case CMD_PUEBLOCLIENT:

        // Set the descriptor's flag.
        //
        d->flags |= DS_PUEBLOCLIENT;

        queue_string(d, mudconf.pueblo_msg);
        queue_write_LEN(d, "\r\n", 2);
        break;

    default:

        {
            char buf[LBUF_SIZE * 2];
            STARTLOG(LOG_BUGS, "BUG", "PARSE");
            mux_sprintf(buf, sizeof(buf), "Logged-out command with no handler: '%s'", mudstate.debug_cmd);
            log_text(buf);
            ENDLOG;
        }
    }
}

void do_command(DESC *d, char *command)
{
    char *cmdsave = mudstate.debug_cmd;
    mudstate.debug_cmd = (char *)"< do_command >";

    if (d->flags & DS_CONNECTED)
    {
        // Normal logged-in command processing.
        //
        d->command_count++;
        if (d->output_prefix)
        {
            queue_string(d, d->output_prefix);
            queue_write_LEN(d, "\r\n", 2);
        }
        mudstate.curr_executor = d->player;
        mudstate.curr_enactor = d->player;
        for (int i = 0; i < MAX_GLOBAL_REGS; i++)
        {
            mudstate.global_regs[i][0] = '\0';
            mudstate.glob_reg_len[i] = 0;
        }

        CLinearTimeAbsolute ltaBegin;
        ltaBegin.GetUTC();
        MuxAlarm.Set(mudconf.max_cmdsecs);

        char *log_cmdbuf = process_command(d->player, d->player, d->player,
            true, command, (char **)NULL, 0);

        CLinearTimeAbsolute ltaEnd;
        ltaEnd.GetUTC();
        if (MuxAlarm.bAlarmed)
        {
            notify(d->player, "GAME: Expensive activity abbreviated.");
            halt_que(d->player, NOTHING);
            s_Flags(d->player, FLAG_WORD1, Flags(d->player) | HALT);
        }
        MuxAlarm.Clear();

        CLinearTimeDelta ltd = ltaEnd - ltaBegin;
        if (ltd > mudconf.rpt_cmdsecs)
        {
            STARTLOG(LOG_PROBLEMS, "CMD", "CPU");
            log_name_and_loc(d->player);
            char *logbuf = alloc_lbuf("do_command.LOG.cpu");
            mux_sprintf(logbuf, LBUF_SIZE, " queued command taking %s secs: ",
                ltd.ReturnSecondsString(4));
            log_text(logbuf);
            free_lbuf(logbuf);
            log_text(log_cmdbuf);
            ENDLOG;
        }

        mudstate.curr_cmd = (char *) "";
        if (d->output_suffix)
        {
            queue_string(d, d->output_suffix);
            queue_write_LEN(d, "\r\n", 2);
        }
        mudstate.debug_cmd = cmdsave;
        return;
    }

    // Login screen (logged-out) command processing.
    //

    // Split off the command from the arguments.
    //
    char *arg = command;
    while (*arg && !mux_isspace(*arg))
    {
        arg++;
    }

    if (*arg)
    {
        *arg++ = '\0';
    }

    // Look up the command in the logged-out command table.
    //
    NAMETAB *cp = (NAMETAB *)hashfindLEN(command, strlen(command), &mudstate.logout_cmd_htab);
    if (cp == NULL)
    {
        // Not in the logged-out command table, so maybe a connect attempt.
        //
        if (*arg)
        {
            // Restore nullified space
            //
            *--arg = ' ';
        }
        mudstate.curr_executor = NOTHING;
        mudstate.curr_enactor = NOTHING;
        mudstate.debug_cmd = cmdsave;
        check_connect(d, command);
        return;
    }

    // The command was in the logged-out command table. Perform
    // prefix and suffix processing, and invoke the command
    // handler.
    //
    d->command_count++;
    if (!(cp->flag & CMD_NOxFIX))
    {
        if (d->output_prefix)
        {
            queue_string(d, d->output_prefix);
            queue_write_LEN(d, "\r\n", 2);
        }
    }
    if (cp->perm != CA_PUBLIC)
    {
        queue_write(d, "Permission denied.\r\n");
    }
    else
    {
        mudstate.debug_cmd = cp->name;
        do_logged_out_internal(d, cp->flag & CMD_MASK, arg);
    }
    // QUIT or LOGOUT will close the connection and cause the
    // descriptor to be freed!
    //
    if (  ((cp->flag & CMD_MASK) != CMD_QUIT)
       && ((cp->flag & CMD_MASK) != CMD_LOGOUT)
       && !(cp->flag & CMD_NOxFIX))
    {
        if (d->output_suffix)
        {
            queue_string(d, d->output_suffix);
            queue_write_LEN(d, "\r\n", 2);
        }
    }
    mudstate.debug_cmd = cmdsave;
}

void logged_out1(dbref executor, dbref caller, dbref enactor, int key, char *arg)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);

    // PUEBLOCLIENT affects all the player's connections.
    //
    if (key == CMD_PUEBLOCLIENT)
    {
        DESC *d;
        DESC_ITER_PLAYER(executor, d)
        {
            do_logged_out_internal(d, key, arg);
        }
        // Set the player's flag.
        //
        s_Html(executor);
        return;
    }

    // Other logged-out commands affect only the player's most recently
    // used connection.
    //
    DESC *d;
    DESC *dLatest = NULL;
    DESC_ITER_PLAYER(executor, d)
    {
        if (  dLatest == NULL
           || dLatest->last_time < d->last_time)
        {
            dLatest = d;
        }
    }
    if (dLatest != NULL)
    {
        do_logged_out_internal(dLatest, key, arg);
    }
}

void logged_out0(dbref executor, dbref caller, dbref enactor, int key)
{
    logged_out1(executor, caller, enactor, key, "");
}

void Task_ProcessCommand(void *arg_voidptr, int arg_iInteger)
{
    UNUSED_PARAMETER(arg_iInteger);

    DESC *d = (DESC *)arg_voidptr;
    if (d)
    {
        CBLK *t = d->input_head;
        if (t)
        {
            if (d->quota > 0)
            {
                d->quota--;
                d->input_head = (CBLK *) t->hdr.nxt;
                if (d->input_head)
                {
                    // There are still commands to process, so schedule another looksee.
                    //
                    scheduler.DeferImmediateTask(PRIORITY_SYSTEM, Task_ProcessCommand, d, 0);
                }
                else
                {
                    d->input_tail = NULL;
                }
                d->input_size -= strlen(t->cmd);
                d->last_time.GetUTC();
                if (d->program_data != NULL)
                {
                    handle_prog(d, t->cmd);
                }
                else
                {
                    do_command(d, t->cmd);
                }
                free_lbuf(t);
            }
            else
            {
                // Don't bother looking for more quota until at least this much time has past.
                //
                CLinearTimeAbsolute lsaWhen;
                lsaWhen.GetUTC();

                scheduler.DeferTask(lsaWhen + mudconf.timeslice, PRIORITY_SYSTEM, Task_ProcessCommand, d, 0);
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 * site_check: Check for site flags in a site list.
 */

int site_check(struct in_addr host, SITE *site_list)
{
    SITE *this0;

    for (this0 = site_list; this0; this0 = this0->next)
    {
        if ((host.s_addr & this0->mask.s_addr) == this0->address.s_addr)
        {
            return this0->flag;
        }
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * list_sites: Display information in a site list
 */

#define S_SUSPECT   1
#define S_ACCESS    2

static const char *stat_string(int strtype, int flag)
{
    const char *str;

    switch (strtype)
    {
    case S_SUSPECT:
        if (flag)
        {
            str = "Suspected";
        }
        else
        {
            str = "Trusted";
        }
        break;

    case S_ACCESS:
        switch (flag)
        {
        case H_FORBIDDEN:
            str = "Forbidden";
            break;

        case H_REGISTRATION:
            str = "Registration";
            break;

        case H_GUEST:
            str = "NoGuest";
            break;

        case H_NOSITEMON:
            str = "NoSiteMon";
            break;

        case 0:
            str = "Unrestricted";
            break;

        default:
            str = "Strange";
            break;
        }
        break;

    default:
        str = "Strange";
        break;
    }
    return str;
}

static void list_sites(dbref player, SITE *site_list, const char *header_txt, int stat_type)
{
    char *buff, *buff1, *str;
    SITE *this0;

    buff = alloc_mbuf("list_sites.buff");
    buff1 = alloc_sbuf("list_sites.addr");
    mux_sprintf(buff, MBUF_SIZE, "----- %s -----", header_txt);
    notify(player, buff);
    notify(player, "Address              Mask                 Status");
    for (this0 = site_list; this0; this0 = this0->next)
    {
        str = (char *)stat_string(stat_type, this0->flag);
        mux_strncpy(buff1, inet_ntoa(this0->mask), SBUF_SIZE-1);
        mux_sprintf(buff, MBUF_SIZE, "%-20s %-20s %s", inet_ntoa(this0->address), buff1,
            str);
        notify(player, buff);
    }
    free_mbuf(buff);
    free_sbuf(buff1);
}

/* ---------------------------------------------------------------------------
 * list_siteinfo: List information about specially-marked sites.
 */

void list_siteinfo(dbref player)
{
    list_sites(player, mudstate.access_list, "Site Access", S_ACCESS);
    list_sites(player, mudstate.suspect_list, "Suspected Sites", S_SUSPECT);
}

/* ---------------------------------------------------------------------------
 * make_ulist: Make a list of connected user numbers for the LWHO function.
 */

void make_ulist(dbref player, char *buff, char **bufc, bool bPorts)
{
    DESC *d;
    if (bPorts)
    {
        make_port_ulist(player, buff, bufc);
    }
    else
    {
        ITL pContext;
        ItemToList_Init(&pContext, buff, bufc, '#');
        DESC_ITER_CONN(d)
        {
            if (  !See_Hidden(player)
               && Hidden(d->player))
            {
                continue;
            }
            if (!ItemToList_AddInteger(&pContext, d->player))
            {
                break;
            }
        }
        ItemToList_Final(&pContext);
    }
}

/* ---------------------------------------------------------------------------
 * find_connected_name: Resolve a playername from the list of connected
 * players using prefix matching.  We only return a match if the prefix
 * was unique.
 */

dbref find_connected_name(dbref player, char *name)
{
    DESC *d;
    dbref found = NOTHING;
    DESC_ITER_CONN(d)
    {
        if (  Good_obj(player)
           && !See_Hidden(player)
           && Hidden(d->player))
        {
            continue;
        }
        if (!string_prefix(Name(d->player), name))
        {
            continue;
        }
        if (  found != NOTHING
           && found != d->player)
        {
            return NOTHING;
        }
        found = d->player;
    }
    return found;
}

FUNCTION(fun_doing)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (is_rational(fargs[0]))
    {
        SOCKET s = mux_atol(fargs[0]);
        bool bFound = false;
        DESC *d;
        DESC_ITER_CONN(d)
        {
            if (d->descriptor == s)
            {
                bFound = true;
                break;
            }
        }
        if (  bFound
           && (  d->player == executor
              || Wizard_Who(executor)))
        {
            safe_str(d->doing, buff, bufc);
        }
        else
        {
            safe_nothing(buff, bufc);
        }
    }
    else
    {
        dbref victim = lookup_player(executor, fargs[0], true);
        if (victim == NOTHING)
        {
            safe_str("#-1 PLAYER DOES NOT EXIST", buff, bufc);
            return;
        }

        if (  Wizard_Who(executor)
           || !Hidden(victim))
        {
            DESC *d;
            DESC_ITER_CONN(d)
            {
                if (d->player == victim)
                {
                    safe_str(d->doing, buff, bufc);
                    return;
                }
            }
        }
        safe_str("#-1 NOT A CONNECTED PLAYER", buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_host: Return hostname of player or port descriptor.
// ---------------------------------------------------------------------------
FUNCTION(fun_host)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!Wizard_Who(executor))
    {
        safe_noperm(buff, bufc);
        return;
    }

    bool isPort = is_rational(fargs[0]);
    bool bFound = false;
    DESC *d;
    if (isPort)
    {
        SOCKET s = mux_atol(fargs[0]);
        DESC_ITER_CONN(d)
        {
            if (d->descriptor == s)
            {
                bFound = true;
                break;
            }
        }
    }
    else
    {
        dbref victim = lookup_player(executor, fargs[0], true);
        if (victim == NOTHING)
        {
            safe_str("#-1 PLAYER DOES NOT EXIST", buff, bufc);
            return;
        }
        DESC_ITER_CONN(d)
        {
            if (d->player == victim)
            {
                bFound = true;
                break;
            }
        }
    }
    if (bFound)
    {
        char *hostname = ((d->username[0] != '\0') ?
            tprintf("%s@%s", d->username, d->addr) : d->addr);
        safe_str(hostname, buff, bufc);
        return;
    }
    if (isPort)
    {
        safe_str("#-1 NOT AN ACTIVE PORT", buff, bufc);
    }
    else
    {
        safe_str("#-1 NOT A CONNECTED PLAYER", buff, bufc);
    }
}

FUNCTION(fun_poll)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_str(mudstate.doing_hdr, buff, bufc);
}

FUNCTION(fun_motd)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_str(mudconf.motd_msg, buff, bufc);
}

// fetch_cmds - Retrieve Player's number of commands entered.
//
int fetch_cmds(dbref target)
{
    int sum = 0;
    bool bFound = false;

    DESC *d;
    DESC_ITER_PLAYER(target, d)
    {
        sum += d->command_count;
        bFound = true;
    }

    if (bFound)
    {
        return sum;
    }
    else
    {
        return -1;
    }
}

static void ParseConnectionInfoString(char *pConnInfo, char *pFields[5])
{
    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, pConnInfo);
    mux_strtok_ctl(&tts, " ");
    for (int i = 0; i < 5; i++)
    {
        pFields[i] = mux_strtok_parse(&tts);
    }
}

void fetch_ConnectionInfoFields(dbref target, long anFields[4])
{
    dbref aowner;
    int   aflags;
    char *pConnInfo = atr_get(target, A_CONNINFO, &aowner, &aflags);
    char *aFields[5];
    ParseConnectionInfoString(pConnInfo, aFields);

    for (int i = 0; i < 4; i++)
    {
        long result;
        if (  !aFields[i]
           || (result = mux_atol(aFields[i])) < 0)
        {
            result = 0;
        }
        anFields[i] = result;
    }
    free_lbuf(pConnInfo);
}

void put_ConnectionInfoFields
(
    dbref target,
    long anFields[4],
    CLinearTimeAbsolute &ltaLogout
)
{
    char *pConnInfo = alloc_lbuf("put_CIF");
    char *p = pConnInfo;
    for (int i = 0; i < 4; i++)
    {
        p += mux_ltoa(anFields[i], p);
        *p++ = ' ';
    }
    p += mux_i64toa(ltaLogout.ReturnSeconds(), p);
    *p++ = 0;

    atr_add_raw_LEN(target, A_CONNINFO, pConnInfo, p - pConnInfo);
    free_lbuf(pConnInfo);
}

long fetch_ConnectionInfoField(dbref target, int iField)
{
    dbref aowner;
    int   aflags;
    char *pConnInfo = atr_get(target, A_CONNINFO, &aowner, &aflags);
    char *aFields[5];
    ParseConnectionInfoString(pConnInfo, aFields);

    long result;
    if (  !aFields[iField]
       || (result = mux_atol(aFields[iField])) < 0)
    {
        result = 0;
    }
    free_lbuf(pConnInfo);
    return result;
}

#define CIF_LOGOUTTIME     4

CLinearTimeAbsolute fetch_logouttime(dbref target)
{
    dbref aowner;
    int   aflags;
    char *pConnInfo = atr_get(target, A_CONNINFO, &aowner, &aflags);
    char *aFields[5];
    ParseConnectionInfoString(pConnInfo, aFields);

    CLinearTimeAbsolute lta;
    if (aFields[CIF_LOGOUTTIME])
    {
        lta.SetSecondsString(aFields[CIF_LOGOUTTIME]);
    }
    else
    {
        lta.SetSeconds(0);
    }
    free_lbuf(pConnInfo);
    return lta;
}
