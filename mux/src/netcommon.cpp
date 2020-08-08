/*! \file netcommon.cpp
 * \brief Network-independent networking routines.
 *
 * This file contains routines used by the networking code that do not
 * depend on the implementation of the networking code.  The network-specific
 * portions of the descriptor data structure are not used.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <time.h>

#include "attrs.h"
#include "command.h"
#include "comsys.h"
#include "file_c.h"
#include "functions.h"
#include "mathutil.h"
#include "mguests.h"
#include "powers.h"
#ifdef REALITY_LVLS
#include "levels.h"
#endif // REALITY_LVLS

NAMETAB default_charset_nametab[] =
{
    {T("ascii"),           5,       0,     CHARSET_ASCII},
    {T("oem"),             3,       0,     CHARSET_CP437},
    {T("cp437"),           5,       0,     CHARSET_CP437},
    {T("latin-1"),         7,       0,     CHARSET_LATIN1},
    {T("latin-2"),         7,       0,     CHARSET_LATIN2},
    {T("iso8859-1"),       9,       0,     CHARSET_LATIN1},
    {T("iso8859-2"),       9,       0,     CHARSET_LATIN2},
    {(UTF8 *) nullptr,     0,       0,     0}
};

/* ---------------------------------------------------------------------------
 * make_portlist: Make a list of ports for PORTS().
 */

void make_portlist(dbref player, dbref target, UTF8 *buff, UTF8 **bufc)
{
    UNUSED_PARAMETER(player);

    ITL itl;
    ItemToList_Init(&itl, buff, bufc);

    DESC *d;
    DESC_ITER_CONN(d)
    {
        if (  d->player == target
           && !ItemToList_AddInteger64(&itl, d->socket))
        {
            break;
        }
    }
    ItemToList_Final(&itl);
}

// ---------------------------------------------------------------------------
// make_port_ulist: Make a list of connected user numbers for the LPORTS function.
// ---------------------------------------------------------------------------

void make_port_ulist(dbref player, UTF8 *buff, UTF8 **bufc)
{
    DESC *d;
    ITL itl;
    UTF8 *tmp = alloc_sbuf("make_port_ulist");
    ItemToList_Init(&itl, buff, bufc, '#');
    DESC_ITER_CONN(d)
    {
        if (  !See_Hidden(player)
           && Hidden(d->player))
        {
            continue;
        }

        // printf format: printf("%d:%d", d->player, d->socket);
        //
        UTF8 *p = tmp;
        p += mux_ltoa(d->player, p);
        *p++ = ':';
        p += mux_i64toa(d->socket, p);

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
void raw_notify_html(dbref player, const mux_string &sMsg)
{
    if (0 == sMsg.length_byte())
    {
        return;
    }

    if (  mudstate.inpipe
       && player == mudstate.poutobj)
    {
        mudstate.poutbufc += sMsg.export_TextColor( mudstate.poutbufc, CursorMin,
                                CursorMax, mudstate.poutnew + LBUF_SIZE - mudstate.poutbufc);
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
        queue_string(d, sMsg);
    }
}

/* ---------------------------------------------------------------------------
 * raw_notify: write a message to a player
 */

void raw_notify(dbref player, const UTF8 *msg)
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
        safe_str(T("\r\n"), mudstate.poutnew, &mudstate.poutbufc);
        return;
    }

    if (!Connected(player))
    {
        return;
    }

    DESC_ITER_PLAYER(player, d)
    {
        queue_string(d, msg);
        queue_write_LEN(d, T("\r\n"), 2);
    }
}

void raw_notify(dbref player, const mux_string &sMsg)
{
    if (0 == sMsg.length_byte())
    {
        return;
    }

    if (  mudstate.inpipe
       && player == mudstate.poutobj)
    {
        mudstate.poutbufc += sMsg.export_TextColor( mudstate.poutbufc, CursorMin,
                                CursorMax, mudstate.poutnew + LBUF_SIZE - mudstate.poutbufc);
        safe_str(T("\r\n"), mudstate.poutnew, &mudstate.poutbufc);
        return;
    }

    if (!Connected(player))
    {
        return;
    }

    DESC *d;
    DESC_ITER_PLAYER(player, d)
    {
        queue_string(d, sMsg);
        queue_write_LEN(d, T("\r\n"), 2);
    }
}

void raw_notify_newline(dbref player)
{
    if (  mudstate.inpipe
       && player == mudstate.poutobj)
    {
        safe_str(T("\r\n"), mudstate.poutnew, &mudstate.poutbufc);
        return;
    }
    if (!Connected(player))
    {
        return;
    }

    DESC *d;
    DESC_ITER_PLAYER(player, d)
    {
        queue_write_LEN(d, T("\r\n"), 2);
    }
}

/* ---------------------------------------------------------------------------
 * raw_broadcast: Send message to players who have indicated flags
 */

void DCL_CDECL raw_broadcast(int inflags, __in_z const UTF8 *fmt, ...)
{
    if (!fmt || !*fmt)
    {
        return;
    }

    UTF8 buff[LBUF_SIZE];

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
            queue_write_LEN(d, T("\r\n"), 2);
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
        d->output_prefix = nullptr;
    }

    if (d->output_suffix)
    {
        free_lbuf(d->output_suffix);
        d->output_suffix = nullptr;
    }
}

/*! \brief Add text to the output queue of the indicated network descriptor
 *         without questions.
 *
 * This is private, lower-level helper function for adding a buffer to the
 * output queue. Unlike queue_write_LEN(), it does not attempt to control or
 * manage the output side of the network layer. It only changes the output
 * queue to include the requested buffer.  The only function that should
 * call this function is queue_write_LEN().
 *
 * \param d         Network descriptor state.
 * \param b         buffer to add to the output queue.
 * \param n         Number of bytes in buffer, b, to add to the output queue.
 * \return          None.
 */

static void add_to_output_queue(DESC *d, const UTF8 *b, size_t n)
{
    TBLOCK *tp;
    size_t left;

    // Allocate an output buffer if needed.
    //
    if (nullptr == d->output_head)
    {
        tp = (TBLOCK *)MEMALLOC(OUTPUT_BLOCK_SIZE);
        if (nullptr != tp)
        {
            tp->hdr.nxt = nullptr;
            tp->hdr.start = tp->data;
            tp->hdr.end = tp->data;
            tp->hdr.nchars = 0;
            tp->hdr.flags = 0;

            d->output_head = tp;
            d->output_tail = tp;
        }
        else
        {
            ISOUTOFMEMORY(tp);
            return;
        }
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
        // string.  If so, copy it and update the pointers.
        //
        // We cannot update a buffer marked TBLK_FLAG_LOCKED.  If fact, we
        // should not read or write to such a buffer in any fashion.
        //
        left = OUTPUT_BLOCK_SIZE - (tp->hdr.end - (UTF8 *)tp + 1);
        if (  n <= left
           && 0 == (tp->hdr.flags & TBLK_FLAG_LOCKED))
        {
            memcpy(tp->hdr.end, b, n);
            tp->hdr.end += n;
            tp->hdr.nchars += n;
            n = 0;
        }
        else
        {
            // The buffer we have will not fit into the existing block.  Copy
            // what will fit, allocate another buffer, and retry.
            //
            if (  0 < left
               && 0 == (tp->hdr.flags & TBLK_FLAG_LOCKED))
            {
                memcpy(tp->hdr.end, b, left);
                tp->hdr.end += left;
                tp->hdr.nchars += left;
                b += left;
                n -= left;
            }

            tp = (TBLOCK *)MEMALLOC(OUTPUT_BLOCK_SIZE);
            if (nullptr != tp)
            {
                tp->hdr.nxt = nullptr;
                tp->hdr.start = tp->data;
                tp->hdr.end = tp->data;
                tp->hdr.nchars = 0;
                tp->hdr.flags = 0;

                d->output_tail->hdr.nxt = tp;
                d->output_tail = tp;
            }
            else
            {
                ISOUTOFMEMORY(tp);
                return;
            }
        }
    } while (n > 0);
}

/*! \brief Add text to the output queue of the indicated network descriptor.
 *
 * This is the network output interface available to the rest of the server.
 * Above this point, we would typically find the Telnet negotiation, encoding,
 * and parsing layer.  Below this point, there exists only input and output
 * byte streams which may or may not use multi-threaded access to the network,
 * may or may not use SSL, and must be resilient to platform interface
 * concerns, abuse from the network, and the mis-match in flow rates between
 * inside and outside.
 *
 * Since the network layer is necessarily dealing intimately with the outside,
 * it necessarily has some hysteresis built into it so that on average, it's
 * attention is spent on useful things, and postponable things are postponed.
 *
 * \param d         Network descriptor state.
 * \param b         buffer to add to the output queue.
 * \param n         Number of bytes in buffer, b, to add to the output queue.
 * \return          None.
 */

void queue_write_LEN(DESC *d, const UTF8 *b, size_t n)
{
    if (0 == n)
    {
        return;
    }

    // If the output queue has grown enough that it needs to be chopped, spend
    // some time attempting to push at least some of it out. It may be that
    // writes are already flowing out to the network, but we check anyway.
    //
    // TODO: The threshold for this should perhaps be lowered, but we should
    // not lower it to the point that process_output is called on every call
    // to queue_write_LEN(). Also, there may be an interaction with
    // TBLK_FLAG_LOCKED in that part of the queue cannot be thrown away.  We
    // should be carefull to avoid a state where process_output() is
    // inadvertantly called on every queue_write_LEN() call. Finally, if we
    // could know somehow that process_output() would be unproductive, then
    // we wouldn't make even the following call.
    //
    if (static_cast<size_t>(mudconf.output_limit) < d->output_size + n)
    {
        process_output(d, false);
    }

    if (static_cast<size_t>(mudconf.output_limit) < d->output_size + n)
    {
        // If possible, some of the output queue needs to be thrown away in
        // order to limit the output queue to the output_limit configuration
        // option.
        //
        TBLOCK *tp = d->output_head;
        if (nullptr == tp)
        {
            STARTLOG(LOG_PROBLEMS, "QUE", "WRITE");
            log_text(T("Flushing when output_head is null!"));
            ENDLOG;
        }
        else
        {
            // We cannot modify TBLK_FLAG_LOCKED buffers for three reasons:
            //
            //   1) It breaks SSL,
            //
            //   2) We use this feature on the Windows build to handle
            //      asyncronous I/O, and for Windows Async I/O, a program is
            //      not allowed to read or write to a buffer involved in an
            //      asyncronous I/O request, and
            //
            //   3) It is more proper when given an EWOULDBLOCK error to try
            //      the same exact write request later than to extend the
            //      request to something larger.
            //
#ifdef UNIX_SSL
            if (d->ssl_session)
            {
                tp->hdr.flags |= TBLK_FLAG_LOCKED;
            }
            else
#endif
            if (0 == (tp->hdr.flags & TBLK_FLAG_LOCKED))
            {
                STARTLOG(LOG_NET, "NET", "WRITE");
                UTF8 *buf = alloc_lbuf("queue_write.LOG");
                mux_sprintf(buf, LBUF_SIZE, T("[%u/%s] Output buffer overflow, %d chars discarded by "),
                    d->socket, d->addr, tp->hdr.nchars);
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
                if (d->output_head == nullptr)
                {
                    d->output_tail = nullptr;
                }
                MEMFREE(tp);
                tp = nullptr;
            }
        }
    }

    // Append the request to the end of the output queue for later transmission.
    //
    add_to_output_queue(d, b, n);
    d->output_size += n;
    d->output_tot += n;

#if defined(WINDOWS_NETWORKING)
    // As part of the heuristics for good performance, we may not call
    // process_output now, but if output is not flowing, we mark that output
    // should be kick-started the next time shovechars() is looking at this
    // descriptor.
    //
    if (  !d->bConnectionDropped
       && nullptr != d->output_head
       && 0 == (d->output_head->hdr.flags & TBLK_FLAG_LOCKED))
    {
        d->bCallProcessOutputLater = true;
    }
#endif // WINDOWS_NETWORKING
}

void queue_write(DESC *d, const UTF8 *b)
{
    queue_write_LEN(d, b, strlen((const char *)b));
}

static const UTF8 *encode_iac(const UTF8 *szString)
{
    static UTF8 Buffer[2*LBUF_SIZE];
    UTF8 *pBuffer = Buffer;

    const UTF8 *pString = szString;
    if (pString)
    {
        while (*pString)
        {
            const UTF8 *p = (const UTF8 *)strchr((char *)pString, NVT_IAC);
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
                    mux_strncpy((UTF8 *)pBuffer, (UTF8 *)pString, sizeof(Buffer)-1);
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
                safe_copy_chr_ascii(NVT_IAC, Buffer, &pBuffer, sizeof(Buffer)-1);
            }
        }
    }
    *pBuffer = '\0';
    return Buffer;
}

void queue_string(DESC *d, const UTF8 *s)
{
    const UTF8 *p;
    if (  (d->flags & DS_CONNECTED)
       && Ansi(d->player))
    {
        if (Html(d->player))
        {
            p = convert_to_html(s);
        }
        else
        {
            p = convert_color(s, NoBleed(d->player), Color256(d->player));
        }
    }
    else
    {
        p = strip_color(s);
    }

    const UTF8 *q;
    if (CHARSET_UTF8 == d->encoding)
    {
        q = p;
    }
    else
    {
        if (CHARSET_LATIN1 == d->encoding)
        {
            q = ConvertToLatin1(p);
        }
        else if (CHARSET_LATIN2 == d->encoding)
        {
            q = ConvertToLatin2(p);
        }
        else if (CHARSET_CP437 == d->encoding)
        {
            q = ConvertToCp437(p);
        }
        else // if (CHARSET_ASCII == d->encoding)
        {
            q = ConvertToAscii(p);
        }
    }

    q = encode_iac(q);
    queue_write(d, q);
}

void queue_string(DESC *d, const mux_string &s)
{
    const UTF8 *p = s.export_TextConverted((d->flags & DS_CONNECTED) && Ansi(d->player), NoBleed(d->player), Color256(d->player), Html(d->player));

    const UTF8 *q;
    if (CHARSET_UTF8 == d->encoding)
    {
        q = p;
    }
    else
    {
        if (CHARSET_LATIN1 == d->encoding)
        {
            q = ConvertToLatin1(p);
        }
        else if (CHARSET_LATIN2 == d->encoding)
        {
            q = ConvertToLatin2(p);
        }
        else if (CHARSET_CP437 == d->encoding)
        {
            q = ConvertToCp437(p);
        }
        else // if (CHARSET_ASCII == d->encoding)
        {
            q = ConvertToAscii(p);
        }
    }

    q = encode_iac(q);
    queue_write(d, q);
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
    d->output_head = nullptr;
    d->output_tail = nullptr;

    cb = d->input_head;
    while (cb)
    {
        cnext = (CBLK *) cb->hdr.nxt;
        free_lbuf(cb);
        cb = cnext;
    }

    d->input_head = nullptr;
    d->input_tail = nullptr;

    if (d->raw_input)
    {
        free_lbuf(d->raw_input);
    }
    d->raw_input = nullptr;

    d->raw_input_at = nullptr;
    d->nOption = 0;
    d->raw_input_state    = NVT_IS_NORMAL;
    for (int i = 0; i < 256; i++)
    {
        d->nvt_him_state[i] = OPTION_NO;
    }
    for (int i = 0; i < 256; i++)
    {
        d->nvt_us_state[i] = OPTION_NO;
    }
    if (d->ttype)
    {
        MEMFREE(d->ttype);
        d->ttype = nullptr;
    }
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
    if (hdesc == nullptr)
    {
        d->hashnext = nullptr;
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
    DESC *last = nullptr;
    DESC *hdesc = (DESC *)hashfindLEN(&player, sizeof(player), &mudstate.desc_htab);
    while (hdesc != nullptr)
    {
        if (d == hdesc)
        {
            if (last == nullptr)
            {
                if (d->hashnext == nullptr)
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
    d->hashnext = nullptr;
}

void welcome_user(DESC *d)
{
    if (mudstate.access_list.isRegistered(&d->address))
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
    command->hdr.nxt = nullptr;
    if (d->input_tail == nullptr)
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

static void set_userstring(UTF8 **userstring, const UTF8 *command)
{
    while (mux_isspace(*command))
    {
        command++;
    }

    if (!*command)
    {
        if (*userstring != nullptr)
        {
            free_lbuf(*userstring);
            *userstring = nullptr;
        }
    }
    else
    {
        if (*userstring == nullptr)
        {
            *userstring = alloc_lbuf("set_userstring");
        }
        mux_strncpy(*userstring, command, LBUF_SIZE-1);
    }
}

static void parse_connect(const UTF8 *msg, UTF8 command[LBUF_SIZE], UTF8 user[LBUF_SIZE], UTF8 pass[LBUF_SIZE])
{
    size_t nmsg = strlen((char *)msg);
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
    UTF8 *p = command;
    while (  i < nmsg
          && !mux_isspace(msg[i]))
    {
        safe_copy_chr_ascii(msg[i], command, &p, LBUF_SIZE-1);
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
                safe_copy_chr_ascii(msg[i], user, &p, LBUF_SIZE-1);
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
                safe_copy_chr_ascii(' ', user, &p, LBUF_SIZE-1);
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
            safe_copy_chr_ascii(msg[i], user, &p, LBUF_SIZE-1);
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
        safe_copy_chr_ascii(msg[i], pass, &p, LBUF_SIZE-1);
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

    UTF8 *buf = alloc_lbuf("announce_connect");
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

    if ('\0' != mudconf.motd_msg[0])
    {
        raw_notify( player, tprintf(T("\n%sMOTD:%s %s\n"), COLOR_INTENSE,
                    COLOR_RESET, mudconf.motd_msg));
    }

    if (Wizard(player))
    {
        if ('\0' != mudconf.wizmotd_msg[0])
        {
            raw_notify(player, tprintf(T("%sWIZMOTD:%s %s\n"), COLOR_INTENSE,
                        COLOR_RESET, mudconf.wizmotd_msg));
        }

        if (!(mudconf.control_flags & CF_LOGIN))
        {
            raw_notify(player, T("*** Logins are disabled."));
        }
    }
    atr_get_str_LEN(buf, player, A_LPAGE, &aowner, &aflags, &nLen);
    if (nLen)
    {
        raw_notify(player, T("Your PAGE LOCK is set.  You may be unable to receive some pages."));
    }
    int num = 0;
    DESC_ITER_PLAYER(player, dtemp)
    {
        num++;
    }

    // Check for MUX_UNICODE forced on
    //
    if (Unicode(player))
    {
        DESC_ITER_PLAYER(player, dtemp)
        {
            if (CHARSET_UTF8 != dtemp->encoding)
            {
                // Since we are changing to the UTF-8 character set, the
                // printable state machine needs to be initialized.
                //
                dtemp->encoding = CHARSET_UTF8;
                dtemp->raw_codepoint_state = CL_PRINT_START_STATE;
            }
        }
    }

    if (Ascii(player))
    {
        DESC_ITER_PLAYER(player, dtemp)
        {
            dtemp->encoding = CHARSET_ASCII;
        }
    }


    // Reset vacation flag.
    //
    s_Flags(player, FLAG_WORD2, Flags2(player) & ~VACATION);
    if (Guest(player))
    {
        db[player].fs.word[FLAG_WORD1] &= ~DARK;
    }

    const UTF8 *pRoomAnnounceFmt;
    const UTF8 *pMonitorAnnounceFmt;
    if (num < 2)
    {
        pRoomAnnounceFmt = T("%s has connected.");
        if (mudconf.have_comsys)
        {
            do_comconnect(player);
        }
        if (  Hidden(player)
           && Can_Hide(player))
        {
            pMonitorAnnounceFmt = T("GAME: %s has DARK-connected.");
        }
        else
        {
            pMonitorAnnounceFmt = T("GAME: %s has connected.");
        }
        if (  Suspect(player)
           || mudstate.access_list.isSuspect(&d->address))
        {
            raw_broadcast(WIZARD, T("[Suspect] %s has connected."), Moniker(player));
        }
    }
    else
    {
        pRoomAnnounceFmt = T("%s has reconnected.");
        pMonitorAnnounceFmt = T("GAME: %s has reconnected.");
        if (  Suspect(player)
           || mudstate.access_list.isSuspect(&d->address))
        {
            raw_broadcast(WIZARD, T("[Suspect] %s has reconnected."), Moniker(player));
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
    if (NOTHING == loc)
    {
        notify_check(player, player, buf, key);
    }
    else
    {
        notify_except_rlevel(loc, player, player, buf, 0);
    }
#else
    notify_check(player, player, buf, key);
#endif // REALITY_LVLS
    atr_pget_str_LEN(buf, player, A_ACONNECT, &aowner, &aflags, &nLen);
    CLinearTimeAbsolute lta;
    dbref zone, obj;
    if (nLen)
    {
        wait_que(player, player, player, AttrTrace(aflags, 0), false, lta,
            NOTHING, 0,
            buf,
            0, nullptr,
            nullptr);
    }
    if (mudconf.master_room != NOTHING)
    {
        atr_pget_str_LEN(buf, mudconf.master_room, A_ACONNECT, &aowner,
            &aflags, &nLen);
        if (nLen)
        {
            wait_que(mudconf.master_room, player, player, AttrTrace(aflags, 0),
                false, lta, NOTHING, 0,
                buf,
                0, nullptr,
                nullptr);
        }
        DOLIST(obj, Contents(mudconf.master_room))
        {
            atr_pget_str_LEN(buf, obj, A_ACONNECT, &aowner, &aflags, &nLen);
            if (nLen)
            {
                wait_que(obj, player, player, AttrTrace(aflags, 0), false, lta,
                    NOTHING, 0,
                    buf,
                    0, nullptr,
                    nullptr);
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
                wait_que(zone, player, player, AttrTrace(aflags, 0), false,
                    lta, NOTHING, 0,
                    buf,
                    0, nullptr,
                    nullptr);
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
                    wait_que(obj, player, player, AttrTrace(aflags, 0), false,
                        lta, NOTHING, 0,
                        buf,
                        0, nullptr,
                        nullptr);
                }
            }
            break;

        default:

            log_printf(T("Invalid zone #%d for %s(#%d) has bad type %d"),
                zone, PureName(player), player, Typeof(zone));
        }
    }
    free_lbuf(buf);
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();
    UTF8 *time_str = ltaNow.ReturnDateString(7);

    UTF8 host_address[MBUF_SIZE];
    d->address.ntop(host_address, sizeof(host_address));
    record_login(player, true, time_str, d->addr, d->username, host_address);
    if (mudconf.have_mailer)
    {
        check_mail(player, 0, false);
    }
    look_in(player, Location(player), (LK_SHOWEXIT|LK_OBEYTERSE|LK_SHOWVRML));
    mudstate.curr_enactor = temp;
}

void announce_disconnect(dbref player, DESC *d, const UTF8 *reason)
{
    int num = 0, key;
    DESC *dtemp;
    DESC_ITER_PLAYER(player, dtemp)
    {
        num++;
    }

#ifdef FIRANMUX
    // Modified so that %# would be the dbref of the object which @booted you,
    //  if such is the case.
#else
    dbref temp = mudstate.curr_enactor;
    mudstate.curr_enactor = player;
#endif
    dbref loc = Location(player);

    if (num < 2)
    {
        if (  Suspect(player)
           || mudstate.access_list.isSuspect(&d->address))
        {
            raw_broadcast(WIZARD, T("[Suspect] %s has disconnected."), Moniker(player));
        }
        UTF8 *buf = alloc_lbuf("announce_disconnect.only");

        mux_sprintf(buf, LBUF_SIZE, T("%s has disconnected."), Moniker(player));
        key = MSG_INV;
        if (  loc != NOTHING
           && !(  Hidden(player)
               && Can_Hide(player)))
        {
            key |= (MSG_NBR | MSG_NBR_EXITS | MSG_LOC | MSG_FWDLIST);
        }
#ifdef REALITY_LVLS
        if (NOTHING == loc)
        {
            notify_check(player, player, buf, key);
        }
        else
        {
            notify_except_rlevel(loc, player, player, buf, 0);
        }
#else
        notify_check(player, player, buf, key);
#endif // REALITY_LVLS

        if (mudconf.have_mailer)
        {
            do_mail_purge(player);
        }

        raw_broadcast(MONITOR, T("GAME: %s has disconnected. <%s>"), Moniker(player), reason);

        c_Connected(player);

        if (mudconf.have_comsys)
        {
            do_comdisconnect(player);
        }

        dbref aowner, zone, obj;
        int aflags;
        size_t nLen;
        CLinearTimeAbsolute lta;
        atr_pget_str_LEN(buf, player, A_ADISCONNECT, &aowner, &aflags, &nLen);
        if (nLen)
        {
#if defined(FIRANMUX)
            wait_que(player, player, mudstate.curr_enactor,
                AttrTrace(aflags, 0), false, lta, NOTHING, 0,
                buf,
                1, &reason,
                nullptr);
#else
            wait_que(player, player, player, AttrTrace(aflags, 0), false,
                lta, NOTHING, 0,
                buf,
                1, &reason,
                nullptr);
#endif // FIRANMUX
        }
        if (mudconf.master_room != NOTHING)
        {
            atr_pget_str_LEN(buf, mudconf.master_room, A_ADISCONNECT, &aowner,
                &aflags, &nLen);
            if (nLen)
            {
                wait_que(mudconf.master_room, player, player,
                    AttrTrace(aflags, 0), false, lta, NOTHING, 0,
                    buf,
                    0, nullptr,
                    nullptr);
            }
            DOLIST(obj, Contents(mudconf.master_room))
            {
                atr_pget_str_LEN(buf, obj, A_ADISCONNECT, &aowner, &aflags,
                    &nLen);
                if (nLen)
                {
                    wait_que(obj, player, player, AttrTrace(aflags, 0), false,
                        lta, NOTHING, 0,
                        buf,
                        0, nullptr,
                        nullptr);
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
                    wait_que(zone, player, player, AttrTrace(aflags, 0),
                        false, lta, NOTHING, 0,
                        buf,
                        0, nullptr,
                        nullptr);
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
                        wait_que(obj, player, player, AttrTrace(aflags, 0),
                            false, lta, NOTHING, 0,
                            buf,
                            0, nullptr,
                            nullptr);
                    }
                }
                break;

            default:
                log_printf(T("Invalid zone #%d for %s(#%d) has bad type %d"),
                    zone, PureName(player), player, Typeof(zone));
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
           || mudstate.access_list.isSuspect(&d->address))
        {
            raw_broadcast(WIZARD, T("[Suspect] %s has partially disconnected."), Moniker(player));
        }
        UTF8 *mbuf = alloc_mbuf("announce_disconnect.partial");
        mux_sprintf(mbuf, MBUF_SIZE, T("%s has partially disconnected."), Moniker(player));
        key = MSG_INV;
        if (  loc != NOTHING
           && !(  Hidden(player)
               && Can_Hide(player)))
        {
            key |= (MSG_NBR | MSG_NBR_EXITS | MSG_LOC | MSG_FWDLIST);
        }
#ifdef REALITY_LVLS
        if (NOTHING == loc)
        {
            notify_check(player, player, mbuf, key);
        }
        else
        {
            notify_except_rlevel(loc, player, player, mbuf, 0);
        }
#else
        notify_check(player, player, mbuf, key);
#endif // REALITY_LVLS
        raw_broadcast(MONITOR, T("GAME: %s has partially disconnected."),
            Moniker(player));
        free_mbuf(mbuf);
    }

#if !defined(FIRANMUX)
    mudstate.curr_enactor = temp;
#endif // FIRANMUX
    desc_delhash(d);

    local_disconnect(player, num);
    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->disconnect(player, num);
        p = p->pNext;
    }
}

int boot_off(dbref player, const UTF8 *message)
{
    DESC *d, *dnext;
    int count = 0;
    DESC_SAFEITER_PLAYER(player, d, dnext)
    {
        if (message && *message)
        {
            queue_string(d, message);
            queue_write_LEN(d, T("\r\n"), 2);
        }
        shutdownsock(d, R_BOOT);
        count++;
    }
    return count;
}

int boot_by_port(SOCKET port, bool bGod, const UTF8 *message)
{
    DESC *d, *dnext;
    int count = 0;
    DESC_SAFEITER_ALL(d, dnext)
    {
        if (  d->socket == port
           && (  bGod
              || !(d->flags & DS_CONNECTED)
              || !God(d->player)))
        {
            if (  message
               && *message)
            {
                queue_string(d, message);
                queue_write_LEN(d, T("\r\n"), 2);
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
    UTF8 *buf;
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
    DESC *dLeastIdle = nullptr;
    DESC_ITER_PLAYER(target, d)
    {
        if (  nullptr == dLeastIdle
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
    if (nullptr != d)
    {
        return d->height;
    }
    return 24;
}

int fetch_width(dbref target)
{
    DESC *d = find_least_idle(target);
    if (nullptr != d)
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
    if (nullptr != d)
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
// find_oldest: Return descriptor with the oldeset connected_at (or nullptr if
// not logged in).
//
void find_oldest(dbref target, DESC *dOldest[2])
{
    dOldest[0] = nullptr;
    dOldest[1] = nullptr;

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
                queue_write(d, T("*** Inactivity Timeout ***\r\n"));
                shutdownsock(d, R_TIMEOUT);
            }
        }
        else if (0 < mudconf.conn_timeout)
        {
            CLinearTimeDelta ltdIdle = ltaNow - d->connected_at;
            if (ltdIdle.ReturnSeconds() > mudconf.conn_timeout)
            {
                queue_write(d, T("*** Login Timeout ***\r\n"));
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
                if (H_Daily(thing))
                {
                    did_it(Owner(thing), thing, 0, nullptr, 0, nullptr, A_DAILY, 0,
                        nullptr, 0);
                    break;
                }
            }
        }
    }

}

#define MAX_TRIMMED_NAME_LENGTH 16
LBUF_OFFSET trimmed_name(dbref player, UTF8 cbuff[MBUF_SIZE], LBUF_OFFSET nMin, LBUF_OFFSET nMax, LBUF_OFFSET nPad)
{
    mux_field nName = StripTabsAndTruncate(
                                             Moniker(player),
                                             cbuff,
                                             MBUF_SIZE-1,
                                             nMax
                                           );
    nName = PadField( cbuff,
                      MBUF_SIZE-1,
                      nName.m_column <= nMin ? nMin + nPad : nName.m_column + nPad,
                      nName);
    return nName.m_column;
}

static UTF8 *trimmed_site(UTF8 *szName)
{
    static UTF8 buff[MBUF_SIZE];

    size_t nLen = strlen((char *)szName);
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

static void dump_users(DESC *e, const UTF8 *match, int key)
{
    DESC *d;
    int count;
    UTF8 *buf, *fp, *sp, flist[4], slist[4];
    dbref room_it;

    if (match)
    {
        while (mux_isspace(*match))
        {
            match++;
        }

        if (!*match)
        {
            match = nullptr;
        }
    }

    if (  (e->flags & (DS_PUEBLOCLIENT|DS_CONNECTED))
       && Html(e->player))
    {
        queue_write(e, T("<pre>"));
    }

    buf = alloc_mbuf("dump_users");
    if (key == CMD_SESSION)
    {
        queue_write(e, T("                               "));
        queue_write(e, T("     Characters Input----  Characters Output---\r\n"));
    }
    queue_write(e, T("Player Name        On For Idle "));
    if (key == CMD_SESSION)
    {
        queue_write(e, T("Port Pend  Lost     Total  Pend  Lost     Total\r\n"));
    }
    else if (  (e->flags & DS_CONNECTED)
            && Wizard_Who(e->player)
            && key == CMD_WHO)
    {
        queue_write(e, T("  Room    Cmds   Host\r\n"));
    }
    else
    {
        if (  Wizard_Who(e->player)
           || See_Hidden(e->player))
        {
            queue_write(e, T("  "));
        }
        else
        {
            queue_write(e, T(" "));
        }
        queue_string(e, mudstate.doing_hdr);
        queue_write_LEN(e, T("\r\n"), 2);
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
                        safe_copy_chr_ascii('d', flist, &fp, sizeof(flist)-1);
                    }
                    else
                    {
                        safe_copy_chr_ascii('D', flist, &fp, sizeof(flist)-1);
                    }
                }
                if (d->flags & DS_CONNECTED)
                {
                    if (Hideout(d->player))
                    {
                        safe_copy_chr_ascii('U', flist, &fp, sizeof(flist)-1);
                    }
                    else
                    {
                        room_it = where_room(d->player);
                        if (Good_obj(room_it))
                        {
                            if (Hideout(room_it))
                            {
                                safe_copy_chr_ascii('u', flist, &fp, sizeof(flist)-1);
                            }
                        }
                        else
                        {
                            safe_copy_chr_ascii('u', flist, &fp, sizeof(flist)-1);
                        }
                    }

                    if (Suspect(d->player))
                    {
                        safe_copy_chr_ascii('+', flist, &fp, sizeof(flist)-1);
                    }
                }
                int host_info = mudstate.access_list.check(&d->address);
                if (host_info & HI_FORBID)
                {
                    safe_copy_chr_ascii('F', slist, &sp, sizeof(slist)-1);
                }
                if (host_info & HI_REGISTER)
                {
                    safe_copy_chr_ascii('R', slist, &sp, sizeof(slist)-1);
                }
                if (host_info & HI_SUSPECT)
                {
                    safe_copy_chr_ascii('+', slist, &sp, sizeof(slist)-1);
                }
                if (host_info & HI_NOGUEST)
                {
                    safe_copy_chr_ascii('G', slist, &sp, sizeof(slist)-1);
                }
            }
            else if (  (e->flags & DS_CONNECTED)
                    && (d->flags & DS_CONNECTED)
                    && See_Hidden(e->player)
                    && Hidden(d->player))
            {
                if (d->flags & DS_AUTODARK)
                {
                    safe_copy_chr_ascii('d', flist, &fp, sizeof(flist)-1);
                }
                else
                {
                    safe_copy_chr_ascii('D', flist, &fp, sizeof(flist)-1);
                }
            }
            *fp = '\0';
            *sp = '\0';

            CLinearTimeDelta ltdConnected = ltaNow - d->connected_at;
            CLinearTimeDelta ltdLastTime  = ltaNow - d->last_time;

            static UTF8 NameField[MBUF_SIZE];
            mux_strncpy(NameField, T("<Unconnected>"), sizeof(NameField)-1);
            size_t vwNameField = strlen((char *)NameField);
            if (d->flags & DS_CONNECTED)
            {
                vwNameField = trimmed_name(d->player, NameField, 13, MAX_TRIMMED_NAME_LENGTH, 1);
            }

            // The width size allocated to the 'On For' field.
            //
            size_t nOnFor = 25 - vwNameField;

            const UTF8 *pTimeStamp1 = time_format_1(ltdConnected.ReturnSeconds(), nOnFor);
            const UTF8 *pTimeStamp2 = time_format_2(ltdLastTime.ReturnSeconds());

            if (  (e->flags & DS_CONNECTED)
               && Wizard_Who(e->player)
               && key == CMD_WHO)
            {
                mux_sprintf(buf, MBUF_SIZE, T("%s%s %4s%-3s#%-6d%5d%3s%s\r\n"),
                    NameField,
                    pTimeStamp1,
                    pTimeStamp2,
                    flist,
                    ((d->flags & DS_CONNECTED) ? Location(d->player) : -1),
                    d->command_count,
                    slist,
                    trimmed_site(((d->username[0] != '\0') ? tprintf(T("%s@%s"), d->username, d->addr) : d->addr)));
            }
            else if (key == CMD_SESSION)
            {
                mux_sprintf(buf, MBUF_SIZE, T("%s%s %4s%5u%5d%6d%10d%6d%6d%10d\r\n"),
                    NameField,
                    pTimeStamp1,
                    pTimeStamp2,
                    d->socket,
                    d->input_size, d->input_lost,
                    d->input_tot,
                    d->output_size, d->output_lost,
                    d->output_tot);
            }
            else if (  Wizard_Who(e->player)
                    || See_Hidden(e->player))
            {
                mux_sprintf(buf, MBUF_SIZE, T("%s%s %4s%-3s%s\r\n"),
                    NameField,
                    pTimeStamp1,
                    pTimeStamp2,
                    flist,
                    d->doing);
            }
            else
            {
                mux_sprintf(buf, MBUF_SIZE, T("%s%s %4s  %s\r\n"),
                    NameField,
                    pTimeStamp1,
                    pTimeStamp2,
                    d->doing);
            }
            queue_string(e, buf);
        }
    }

    // Sometimes I like the ternary operator.
    //
    mux_sprintf(buf, MBUF_SIZE, T("%d Player%slogged in, %d record, %s maximum.\r\n"), count,
        (count == 1) ? T(" ") : T("s "), mudstate.record_players,
        (mudconf.max_players == -1) ? T("no") : mux_ltoa_t(mudconf.max_players));
    queue_write(e, buf);

    if (  (e->flags & (DS_PUEBLOCLIENT|DS_CONNECTED))
       && Html(e->player))
    {
        queue_write(e, T("</pre>"));
    }
    free_mbuf(buf);
}

static const UTF8 *DumpInfoTable[] =
{
#if defined(DEPRECATED)
    T("DEPRECATED"),
#endif
#if defined(FIRANMUX)
    T("FIRANMUX"),
#endif
#if defined(MEMORY_BASED)
    T("MEMORY_BASED"),
#endif
#if defined(REALITY_LVLS)
    T("REALITY_LVLS"),
#endif
#if defined(STUB_SLAVE)
    T("STUB_SLAVE"),
#endif
#if defined(WOD_REALMS)
    T("WOD_REALMS"),
#endif
    (UTF8 *)nullptr
};

static void dump_info(DESC *arg_desc)
{
    size_t nDumpInfoTable = 0;
    while (  nDumpInfoTable < sizeof(DumpInfoTable)/sizeof(DumpInfoTable[0])
          && nullptr != DumpInfoTable[nDumpInfoTable])
    {
        nDumpInfoTable++;
    }

    const UTF8 **LocalDumpInfoTable = local_get_info_table();
    size_t nLocalDumpInfoTable = 0;
    while (nullptr != LocalDumpInfoTable[nLocalDumpInfoTable])
    {
        nLocalDumpInfoTable++;
    }

    if (  0 == nDumpInfoTable
       && 0 == nLocalDumpInfoTable)
    {
        queue_write(arg_desc, T("### Begin INFO 1\r\n"));
    }
    else
    {
        queue_write(arg_desc, T("### Begin INFO 1.1\r\n"));
    }

    queue_string(arg_desc, tprintf(T("Name: %s\r\n"), mudconf.mud_name));

    CLinearTimeAbsolute lta = mudstate.start_time;
    lta.UTC2Local();
    UTF8 *temp = lta.ReturnDateString();
    queue_write(arg_desc, tprintf(T("Uptime: %s\r\n"), temp));

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
    queue_write(arg_desc, tprintf(T("Connected: %d\r\n"), count));
    queue_write(arg_desc, tprintf(T("Size: %d\r\n"), mudstate.db_top));
    queue_write(arg_desc, tprintf(T("Version: %s\r\n"), mudstate.short_ver));

    if (  0 != nDumpInfoTable
       || 0 != nLocalDumpInfoTable)
    {
        UTF8 *buf = alloc_lbuf("dump_info");
        UTF8 *bp  = buf;

        size_t i;
        bool bFirst = true;
        safe_str(T("Patches: "), buf, &bp);
        for (i = 0; i < nDumpInfoTable; i++)
        {
            if (!bFirst)
            {
                safe_chr(' ', buf, &bp);
            }
            else
            {
                bFirst = false;
            }
            safe_str(DumpInfoTable[i], buf, &bp);
        }

        for (i = 0; i < nLocalDumpInfoTable; i++)
        {
            if (!bFirst)
            {
                safe_chr(' ', buf, &bp);
            }
            else
            {
                bFirst = false;
            }
            safe_str(LocalDumpInfoTable[i], buf, &bp);
        }
        *bp = '\0';
        queue_write(arg_desc, buf);
        free_lbuf(buf);
        queue_write(arg_desc, T("\r\n"));
    }
    queue_write(arg_desc, T("### End INFO\r\n"));
}

UTF8 *MakeCanonicalDoing(UTF8 *pDoing, size_t *pnValidDoing, bool *pbValidDoing)
{
    *pnValidDoing = 0;
    *pbValidDoing = false;

    if (!pDoing)
    {
        return nullptr;
    }

    static UTF8 szFittedDoing[SIZEOF_DOING_STRING+1];
    mux_field nDoing = StripTabsAndTruncate( pDoing, szFittedDoing,
                                              SIZEOF_DOING_STRING, WIDTHOF_DOING_STRING);

    *pnValidDoing = nDoing.m_byte;
    *pbValidDoing = true;
    return szFittedDoing;
}

// ---------------------------------------------------------------------------
// do_doing: Set the doing string that appears in the WHO report.
// Idea from R'nice@TinyTIM.
//
void do_doing(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *arg, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Make sure there can be no embedded newlines from %r
    //
    static UTF8 *Empty = (UTF8 *)"";
    UTF8 *szValidDoing = Empty;
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
                notify(executor, T("Set."));
            }
        }
        else
        {
            notify(executor, T("Not connected."));
        }
    }
    else if (key == DOING_UNIQUE)
    {
        DESC *d;
        DESC *dMax = nullptr;
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
                notify(executor, T("Set."));
            }
        }
        else
        {
            notify(executor, T("Not connected."));
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
            mux_strncpy(mudstate.doing_hdr, T("Doing"), sizeof(mudstate.doing_hdr)-1);
        }
        else
        {
            memcpy(mudstate.doing_hdr, szValidDoing, nValidDoing+1);
        }

        if (  !bQuiet
           && !Quiet(executor))
        {
            notify(executor, T("Set."));
        }
    }
    else // if (key == DOING_POLL)
    {
        notify(executor, tprintf(T("Poll: %s"), mudstate.doing_hdr));
    }
}

NAMETAB logout_cmdtable[] =
{
    {T("DOING"),         5,  CA_PUBLIC,  CMD_DOING},
    {T("LOGOUT"),        6,  CA_PUBLIC,  CMD_LOGOUT},
    {T("OUTPUTPREFIX"), 12,  CA_PUBLIC,  CMD_PREFIX|CMD_NOxFIX},
    {T("OUTPUTSUFFIX"), 12,  CA_PUBLIC,  CMD_SUFFIX|CMD_NOxFIX},
    {T("QUIT"),          4,  CA_PUBLIC,  CMD_QUIT},
    {T("SESSION"),       7,  CA_PUBLIC,  CMD_SESSION},
    {T("WHO"),           3,  CA_PUBLIC,  CMD_WHO},
    {T("PUEBLOCLIENT"), 12,  CA_PUBLIC,  CMD_PUEBLOCLIENT},
    {T("INFO"),          4,  CA_PUBLIC,  CMD_INFO},
    {nullptr,            0,          0,         0}
};

void init_logout_cmdtab(void)
{
    NAMETAB *cp;

    // Make the htab bigger than the number of entries so that we find things
    // on the first check.  Remember that the admin can add aliases.
    //
    for (cp = logout_cmdtable; cp->flag; cp++)
    {
        hashaddLEN(cp->name, strlen((char *)cp->name), cp, &mudstate.logout_cmd_htab);
    }
}

static void failconn(const UTF8 *logcode, const UTF8 *logtype, const UTF8 *logreason,
                     DESC *d, int disconnect_reason,
                     dbref player, int filecache, UTF8 *motd_msg, UTF8 *command,
                     UTF8 *user, UTF8 *password, const UTF8 *cmdsave)
{
    STARTLOG(LOG_LOGIN | LOG_SECURITY, logcode, "RJCT");
    UTF8 *buff = alloc_mbuf("failconn.LOG");
    mux_sprintf(buff, MBUF_SIZE, T("[%u/%s] %s rejected to "), d->socket, d->addr, logtype);
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
    log_text(T(" ("));
    log_text(logreason);
    log_text(T(")"));
    ENDLOG;
    fcache_dump(d, filecache);
    if (*motd_msg)
    {
        queue_string(d, motd_msg);
        queue_write_LEN(d, T("\r\n"), 2);
    }
    free_lbuf(command);
    free_lbuf(user);
    free_lbuf(password);
    shutdownsock(d, disconnect_reason);
    mudstate.debug_cmd = cmdsave;
    return;
}

static const UTF8 *connect_fail = T("Either that player does not exist, or has a different password.\r\n");

static bool check_connect(DESC *d, UTF8 *msg)
{
    UTF8 *buff;
    dbref player, aowner;
    int aflags, nplayers;
    DESC *d2;
    const UTF8 *p;
    bool isGuest = false;

    const UTF8 *cmdsave = mudstate.debug_cmd;
    mudstate.debug_cmd = T("< check_connect >");

    // Hide the password length from SESSION.
    //
    d->input_tot -= (strlen((char *)msg) + 1);

    // Crack the command apart.
    //
    UTF8 *command = alloc_lbuf("check_conn.cmd");
    UTF8 *user = alloc_lbuf("check_conn.user");
    UTF8 *password = alloc_lbuf("check_conn.pass");
    parse_connect(msg, command, user, password);

    int host_info = mudstate.access_list.check(&d->address);

    // At this point, command, user, and password are all less than
    // MBUF_SIZE.
    //
    if (  strncmp((char *)command, "co", 2) == 0
       || strncmp((char *)command, "cd", 2) == 0)
    {
        if (string_prefix(user, mudconf.guest_prefix))
        {
            if (host_info & HI_NOGUEST)
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
                failconn(T("CONN"), T("Connect"), T("Guest Site Forbidden"), d,
                    R_GAMEDOWN, NOTHING, FC_CONN_REG, mudconf.downmotd_msg,
                    command, user, password, cmdsave);
                return false;
            }

            if (mudconf.control_flags & CF_LOGIN)
            {
                if (  mudconf.number_guests <= 0
                   || !Good_obj(mudconf.guest_char)
                   || !(mudconf.control_flags & CF_GUEST))
                {
                    queue_write(d, T("Guest logins are disabled.\r\n"));
                    free_lbuf(command);
                    free_lbuf(user);
                    free_lbuf(password);
                    mudstate.debug_cmd = cmdsave;
                    return false;
                }

                if ((p = Guest.Create(d)) == nullptr)
                {
                    free_lbuf(command);
                    free_lbuf(user);
                    free_lbuf(password);
                    mudstate.debug_cmd = cmdsave;
                    return false;
                }
                mux_strncpy(user, p, LBUF_SIZE-1);
                mux_strncpy(password, (UTF8 *)GUEST_PASSWORD, LBUF_SIZE-1);
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

        UTF8 host_address[MBUF_SIZE];
        d->address.ntop(host_address, sizeof(host_address));
        player = connect_player(user, password, d->addr, d->username, host_address);
        if (  player == NOTHING
           || (!isGuest && Guest.CheckGuest(player)))
        {
            // Not a player, or wrong password.
            //
            queue_write(d, connect_fail);
            STARTLOG(LOG_LOGIN | LOG_SECURITY, "CON", "BAD");
            buff = alloc_lbuf("check_conn.LOG.bad");
            mux_sprintf(buff, LBUF_SIZE, T("[%u/%s] Failed connect to \xE2\x80\x98%s\xE2\x80\x99"), d->socket, d->addr, user);
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
                || RealWizRoy(player)
                || God(player))
        {
            if (  strncmp((char *)command, "cd", 2) == 0
               && (  RealWizard(player)
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
               && (host_info & HI_NOGUEST))
            {
                failconn(T("CON"), T("Connect"), T("Guest Site Forbidden"), d,
                    R_GAMEDOWN, player, FC_CONN_SITE,
                    mudconf.downmotd_msg, command, user, password,
                    cmdsave);
                return false;
            }

            // Logins are enabled, or wiz or god.
            //
            STARTLOG(LOG_LOGIN, "CON", "LOGIN");
            buff = alloc_mbuf("check_conn.LOG.login");
            mux_sprintf(buff, MBUF_SIZE, T("[%u/%s] Connected to "), d->socket, d->addr);
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
                if (  nullptr != d2->program_data
                   && nullptr == d->program_data)
                {
                    d->program_data = d2->program_data;
                }
                else if (nullptr != d2->program_data)
                {
                    // Enforce that all program_data pointers for this player
                    // are the same.
                    //
                    mux_assert(d->program_data == d2->program_data);
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
                buff = atr_get("check_connect.2375", player, A_LAST, &aowner, &aflags);
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

            ServerEventsSinkNode *pNode = g_pServerEventsSinkListHead;
            while (nullptr != pNode)
            {
                pNode->pSink->connect(player, 0, num_con);
                pNode = pNode->pNext;
            }

            // If stuck in an @prog, show the prompt.
            //
            if (nullptr != d->program_data)
            {
                queue_write_LEN(d, T(">\377\371"), 3);
            }

        }
        else if (!(mudconf.control_flags & CF_LOGIN))
        {
            failconn(T("CON"), T("Connect"), T("Logins Disabled"), d, R_GAMEDOWN, player, FC_CONN_DOWN,
                mudconf.downmotd_msg, command, user, password, cmdsave);
            return false;
        }
        else
        {
            failconn(T("CON"), T("Connect"), T("Game Full"), d, R_GAMEFULL, player, FC_CONN_FULL,
                mudconf.fullmotd_msg, command, user, password, cmdsave);
            return false;
        }
    }
    else if (strncmp((char *)command, "cr", 2) == 0)
    {
        // Enforce game down.
        //
        if (!(mudconf.control_flags & CF_LOGIN))
        {
            failconn(T("CRE"), T("Create"), T("Logins Disabled"), d, R_GAMEDOWN, NOTHING, FC_CONN_DOWN,
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
            failconn(T("CRE"), T("Create"), T("Game Full"), d,
                R_GAMEFULL, NOTHING, FC_CONN_FULL,
                mudconf.fullmotd_msg, command, user, password,
                cmdsave);
            return false;
        }
        if (host_info & HI_REGISTER)
        {
            fcache_dump(d, FC_CREA_REG);
        }
        else
        {
            const UTF8 *pmsg;
            player = create_player(user, password, NOTHING, false, &pmsg);
            if (player == NOTHING)
            {
                queue_write(d, pmsg);
                queue_write(d, T("\r\n"));
                STARTLOG(LOG_SECURITY | LOG_PCREATES, "CON", "BAD");
                buff = alloc_lbuf("check_conn.LOG.badcrea");
                mux_sprintf(buff, LBUF_SIZE, T("[%u/%s] Create of \xE2\x80\x98%s\xE2\x80\x99 failed"), d->socket, d->addr, user);
                log_text(buff);
                free_lbuf(buff);
                ENDLOG;
            }
            else
            {
                AddToPublicChannel(player);
                STARTLOG(LOG_LOGIN | LOG_PCREATES, "CON", "CREA");
                buff = alloc_mbuf("check_conn.LOG.create");
                mux_sprintf(buff, MBUF_SIZE, T("[%u/%s] Created "), d->socket, d->addr);
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
                ServerEventsSinkNode *pNode = g_pServerEventsSinkListHead;
                while (nullptr != pNode)
                {
                    pNode->pSink->connect(player, 1, 0);
                    pNode = pNode->pNext;
                }
            }
        }
    }
    else
    {
        welcome_user(d);
        STARTLOG(LOG_LOGIN | LOG_SECURITY, "CON", "BAD");
        buff = alloc_mbuf("check_conn.LOG.bad");
        msg[150] = '\0';
        mux_sprintf(buff, MBUF_SIZE, T("[%u/%s] Failed connect: \xE2\x80\x98%s\xE2\x80\x99"), d->socket, d->addr, msg);
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

static void do_logged_out_internal(DESC *d, int key, const UTF8 *arg)
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

#if defined(FIRANMUX)
        if ((d->flags & DS_CONNECTED) == 0)
        {
            queue_string(d, T("This command is disabled on login."));
            queue_write_LEN(d, T("\r\n"), 2);
        }
        else
#endif // FIRANMUX
        {
            dump_users(d, arg, key);
        }
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
        queue_write_LEN(d, T("\r\n"), 2);
        break;

    default:

        {
            UTF8 buf[LBUF_SIZE * 2];
            STARTLOG(LOG_BUGS, "BUG", "PARSE");
            mux_sprintf(buf, sizeof(buf), T("Logged-out command with no handler: \xE2\x80\x98%s\xE2\x80\x99"), mudstate.debug_cmd);
            log_text(buf);
            ENDLOG;
        }
    }
}

void do_command(DESC *d, UTF8 *command)
{
    const UTF8 *cmdsave = mudstate.debug_cmd;
    mudstate.debug_cmd = T("< do_command >");

    if (d->flags & DS_CONNECTED)
    {
        // Normal logged-in command processing.
        //
        d->command_count++;
        if (d->output_prefix)
        {
            queue_string(d, d->output_prefix);
            queue_write_LEN(d, T("\r\n"), 2);
        }
        mudstate.curr_executor = d->player;
        mudstate.curr_enactor = d->player;
        for (int i = 0; i < MAX_GLOBAL_REGS; i++)
        {
            if (mudstate.global_regs[i])
            {
                RegRelease(mudstate.global_regs[i]);
                mudstate.global_regs[i] = nullptr;
            }
        }

#if defined(STUB_SLAVE)
        mudstate.iRow = RS_TOP;
        if (nullptr != mudstate.pResultsSet)
        {
            mudstate.pResultsSet->Release();
            mudstate.pResultsSet = nullptr;
        }
#endif // STUB_SLAVE

        CLinearTimeAbsolute ltaBegin;
        ltaBegin.GetUTC();
        alarm_clock.set(mudconf.max_cmdsecs);

        UTF8 *log_cmdbuf = process_command(d->player, d->player, d->player,
            0, true, command, nullptr, 0);

        CLinearTimeAbsolute ltaEnd;
        ltaEnd.GetUTC();
        if (alarm_clock.alarmed)
        {
            notify(d->player, T("GAME: Expensive activity abbreviated."));
            halt_que(d->player, NOTHING);
            s_Flags(d->player, FLAG_WORD1, Flags(d->player) | HALT);
        }
        alarm_clock.clear();

        CLinearTimeDelta ltd = ltaEnd - ltaBegin;
        if (ltd > mudconf.rpt_cmdsecs)
        {
            STARTLOG(LOG_PROBLEMS, "CMD", "CPU");
            log_name_and_loc(d->player);
            UTF8 *logbuf = alloc_lbuf("do_command.LOG.cpu");
            mux_sprintf(logbuf, LBUF_SIZE, T(" queued command taking %s secs: "),
                ltd.ReturnSecondsString(4));
            log_text(logbuf);
            free_lbuf(logbuf);
            log_text(log_cmdbuf);
            ENDLOG;
        }

        mudstate.curr_cmd = T("");
        if (d->output_suffix)
        {
            queue_string(d, d->output_suffix);
            queue_write_LEN(d, T("\r\n"), 2);
        }
        mudstate.debug_cmd = cmdsave;
        return;
    }

    // Login screen (logged-out) command processing.
    //

    // Split off the command from the arguments.
    //
    size_t iArg = 0;
    UTF8* cmd_argument = command;
    while (  '\0' != command[iArg]
          && !mux_isspace(command[iArg]))
    {
        iArg++;
        cmd_argument++;
    }

    // Look up the command in the logged-out command table.
    //
    NAMETAB *cp = (NAMETAB *)hashfindLEN(command, iArg, &mudstate.logout_cmd_htab);
    if (cp == nullptr)
    {
        // Not in the logged-out command table, so maybe a connect attempt.
        //
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
            queue_write_LEN(d, T("\r\n"), 2);
        }
    }
    if (cp->perm != CA_PUBLIC)
    {
        queue_write(d, T("Permission denied.\r\n"));
    }
    else
    {
        mudstate.debug_cmd = cp->name;
        do_logged_out_internal(d, cp->flag & CMD_MASK, cmd_argument);
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
            queue_write_LEN(d, T("\r\n"), 2);
        }
    }
    mudstate.debug_cmd = cmdsave;
}

void logged_out1(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *arg, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

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
    DESC *dLatest = nullptr;
    DESC_ITER_PLAYER(executor, d)
    {
        if (  dLatest == nullptr
           || dLatest->last_time < d->last_time)
        {
            dLatest = d;
        }
    }
    if (dLatest != nullptr)
    {
        do_logged_out_internal(dLatest, key, arg);
    }
}

void logged_out0(dbref executor, dbref caller, dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(eval);

    logged_out1(executor, caller, enactor, 0, key, (UTF8 *)"", nullptr, 0);
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
                    d->input_tail = nullptr;
                }
                d->input_size -= strlen((char *)t->cmd);
                d->last_time.GetUTC();
                if (d->program_data != nullptr)
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
 * list_siteinfo: List information about specially-marked sites.
 */

void list_siteinfo(dbref player)
{
    mudstate.access_list.listinfo(player);
}

/* ---------------------------------------------------------------------------
 * make_ulist: Make a list of connected user numbers for the LWHO function.
 */

void make_ulist(dbref player, UTF8 *buff, UTF8 **bufc, bool bPorts)
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

dbref find_connected_name(dbref player, UTF8 *name)
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
    UNUSED_PARAMETER(eval);
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
            if (d->socket == s)
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
            safe_str(T("#-1 PLAYER DOES NOT EXIST"), buff, bufc);
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
        safe_str(T("#-1 NOT A CONNECTED PLAYER"), buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_host: Return hostname of player or port descriptor.
// ---------------------------------------------------------------------------
FUNCTION(fun_host)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
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
            if (d->socket == s)
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
            safe_str(T("#-1 PLAYER DOES NOT EXIST"), buff, bufc);
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
        UTF8 *hostname = ((d->username[0] != '\0') ?
            tprintf(T("%s@%s"), d->username, d->addr) : d->addr);
        safe_str(hostname, buff, bufc);
        return;
    }
    if (isPort)
    {
        safe_str(T("#-1 NOT AN ACTIVE PORT"), buff, bufc);
    }
    else
    {
        safe_str(T("#-1 NOT A CONNECTED PLAYER"), buff, bufc);
    }
}

FUNCTION(fun_poll)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_str(mudconf.motd_msg, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_siteinfo: Return special site flags of player or port descriptor.
//               Same output as wizard-accessible WHO.
// ---------------------------------------------------------------------------
FUNCTION(fun_siteinfo)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
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
            if (d->socket == s)
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
            safe_str(T("#-1 PLAYER DOES NOT EXIST"), buff, bufc);
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
        int host_info = mudstate.access_list.check(&d->address);
        if (host_info & HI_FORBID)
        {
            safe_chr('F', buff, bufc);
        }
        if (host_info & HI_REGISTER)
        {
            safe_chr('R', buff, bufc);
        }
        if (host_info & HI_SUSPECT)
        {
            safe_chr('+', buff, bufc);
        }
        if (host_info & HI_NOGUEST)
        {
            safe_chr('G', buff, bufc);
        }
        return;
    }

    if (isPort)
    {
        safe_str(T("#-1 NOT AN ACTIVE PORT"), buff, bufc);
    }
    else
    {
        safe_str(T("#-1 NOT A CONNECTED PLAYER"), buff, bufc);
    }

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

static void ParseConnectionInfoString(UTF8 *pConnInfo, UTF8 *pFields[5])
{
    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, pConnInfo);
    mux_strtok_ctl(&tts, T(" "));
    for (int i = 0; i < 5; i++)
    {
        pFields[i] = mux_strtok_parse(&tts);
    }
}

void fetch_ConnectionInfoFields(dbref target, long anFields[4])
{
    dbref aowner;
    int   aflags;
    UTF8 *pConnInfo = atr_get("fetch_ConnectionInfoFields.3263", target, A_CONNINFO, &aowner, &aflags);
    UTF8 *aFields[5];
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
    UTF8 *pConnInfo = alloc_lbuf("put_CIF");
    UTF8 *p = pConnInfo;
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
    UTF8 *pConnInfo = atr_get("fetch_ConnectionInfoField.3305", target, A_CONNINFO, &aowner, &aflags);
    UTF8 *aFields[5];
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
    UTF8 *pConnInfo = atr_get("fetch_logouttime.3325", target, A_CONNINFO, &aowner, &aflags);
    UTF8 *aFields[5];
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

mux_subnets::mux_subnets()
{
    msnRoot = nullptr;
}

mux_subnets::~mux_subnets()
{
    delete msnRoot;
}

mux_subnet_node::mux_subnet_node(mux_subnet *msn_arg, unsigned long ulControl_arg)
{
    msn = msn_arg;
    pnLeft = nullptr;
    pnInside = nullptr;
    pnRight = nullptr;
    ulControl = ulControl_arg;
}

mux_subnet_node::~mux_subnet_node()
{
    delete msn;
    delete pnLeft;
    delete pnInside;
    delete pnRight;
}

void mux_subnets::insert(mux_subnet_node **msnRoot, mux_subnet_node *msn_arg)
{
    if (nullptr == *msnRoot)
    {
        *msnRoot = msn_arg;
        return;
    }

    mux_subnet::Comparison ct = (*msnRoot)->msn->compare_to(msn_arg->msn);
    switch (ct)
    {
    case mux_subnet::kLessThan:
        insert(&(*msnRoot)->pnRight, msn_arg);
        break;

    case mux_subnet::kEqual:
        if (0 != ((HC_PERMIT|HC_REGISTER|HC_FORBID) & msn_arg->ulControl))
        {
            (*msnRoot)->ulControl &= ~(HC_PERMIT|HC_REGISTER|HC_FORBID);
            (*msnRoot)->ulControl |= (msn_arg->ulControl) & (HC_PERMIT|HC_REGISTER|HC_FORBID);
        }

        if (0 != ((HC_NOSITEMON|HC_SITEMON) & msn_arg->ulControl))
        {
            (*msnRoot)->ulControl &= ~(HC_NOSITEMON|HC_SITEMON);
            (*msnRoot)->ulControl |= (msn_arg->ulControl) & (HC_NOSITEMON|HC_SITEMON);
        }

        if (0 != ((HC_NOGUEST|HC_GUEST) & msn_arg->ulControl))
        {
            (*msnRoot)->ulControl &= ~(HC_NOGUEST|HC_GUEST);
            (*msnRoot)->ulControl |= (msn_arg->ulControl) & (HC_NOGUEST|HC_GUEST);
        }

        if (0 != ((HC_SUSPECT|HC_TRUST) & msn_arg->ulControl))
        {
            (*msnRoot)->ulControl &= ~(HC_SUSPECT|HC_TRUST);
            (*msnRoot)->ulControl |= (msn_arg->ulControl) & (HC_SUSPECT|HC_TRUST);
        }

        delete msn_arg;
        break;

    case mux_subnet::kContains:
        insert(&(*msnRoot)->pnInside, msn_arg);
        break;

    case mux_subnet::kContainedBy:
        {
            msn_arg->pnInside = *msnRoot;
            msn_arg->pnLeft = (*msnRoot)->pnLeft;
            msn_arg->pnRight = (*msnRoot)->pnRight;
            (*msnRoot)->pnLeft = nullptr;
            (*msnRoot)->pnRight = nullptr;
            *msnRoot = msn_arg;
        }
        break;

    case mux_subnet::kGreaterThan:
        insert(&(*msnRoot)->pnLeft, msn_arg);
        break;
    }
}

void mux_subnets::search(mux_subnet_node *msnRoot, MUX_SOCKADDR *msa, unsigned long *pulInfo)
{
    if (nullptr == msnRoot)
    {
        return;
    }

    mux_subnet::Comparison ct = msnRoot->msn->compare_to(msa);
    switch (ct)
    {
    case mux_subnet::kLessThan:
        search(msnRoot->pnRight, msa, pulInfo);
        break;

    case mux_subnet::kContains:
        if (HC_PERMIT & msnRoot->ulControl)
        {
            *pulInfo &= ~(HI_REGISTER|HI_FORBID);
        }
        else if (HC_REGISTER & msnRoot->ulControl)
        {
            *pulInfo |= HI_REGISTER;
        }
        else if (HC_FORBID & msnRoot->ulControl)
        {
            *pulInfo |= HI_FORBID;
        }

        if (HC_NOSITEMON & msnRoot->ulControl)
        {
            *pulInfo |= HI_NOSITEMON;
        }
        else if (HC_SITEMON & msnRoot->ulControl)
        {
            *pulInfo &= ~(HI_NOSITEMON);
        }

        if (HC_NOGUEST & msnRoot->ulControl)
        {
            *pulInfo |= HI_NOGUEST;
        }
        else if (HC_GUEST & msnRoot->ulControl)
        {
            *pulInfo &= ~(HI_NOGUEST);
        }

        if (HC_SUSPECT & msnRoot->ulControl)
        {
            *pulInfo |= HI_SUSPECT;
        }
        else if (HC_TRUST & msnRoot->ulControl)
        {
            *pulInfo &= ~(HI_SUSPECT);
        }

        search(msnRoot->pnInside, msa, pulInfo);
        break;

    case mux_subnet::kGreaterThan:
        search(msnRoot->pnLeft, msa, pulInfo);
        break;

    default:
        break;
    }
}

mux_subnet_node *mux_subnets::rotr(mux_subnet_node *msnRoot)
{
    mux_subnet_node *x = msnRoot->pnLeft;
    msnRoot->pnLeft = x->pnRight;
    x->pnRight = msnRoot;
    return x;
}

mux_subnet_node *mux_subnets::rollallr(mux_subnet_node *msnRoot)
{
    if (nullptr != msnRoot->pnLeft)
    {
        msnRoot->pnLeft = rollallr(msnRoot->pnLeft);
        msnRoot = rotr(msnRoot);
    }
    return msnRoot;
}

mux_subnet_node *mux_subnets::joinlr(mux_subnet_node *a, mux_subnet_node *b)
{
    if (nullptr == b)
    {
        return a;
    }
    b = rollallr(b);
    b->pnLeft = a;
    return b;
}

mux_subnet_node *mux_subnets::remove(mux_subnet_node *msnRoot, mux_subnet *msn_arg)
{
    if (nullptr == msnRoot)
    {
        return nullptr;
    }
    mux_subnet::Comparison ct = msnRoot->msn->compare_to(msn_arg);
    switch (ct)
    {
    case mux_subnet::kLessThan:
        msnRoot->pnRight = remove(msnRoot->pnRight, msn_arg);
        break;

    case mux_subnet::kEqual:
        {
            mux_subnet_node *x = msnRoot;
            delete msnRoot->pnInside;
            msnRoot->pnInside = nullptr;
            msnRoot = joinlr(msnRoot->pnLeft, msnRoot->pnRight);
            delete x;
        }
        break;

    case mux_subnet::kContains:
        msnRoot->pnInside = remove(msnRoot->pnInside, msn_arg);
        break;

    case mux_subnet::kContainedBy:
        delete msnRoot;
        msnRoot = nullptr;
        break;

    case mux_subnet::kGreaterThan:
        msnRoot->pnLeft = remove(msnRoot->pnLeft, msn_arg);
        break;
    }
    return msnRoot;
}

bool mux_subnets::permit(mux_subnet *msn_arg)
{
    mux_subnet_node *msn = new mux_subnet_node(msn_arg, HC_PERMIT);
    insert(&msnRoot, msn);
    return true;
}

bool mux_subnets::registered(mux_subnet *msn_arg)
{
    mux_subnet_node *msn = new mux_subnet_node(msn_arg, HC_REGISTER);
    insert(&msnRoot, msn);
    return true;
}

bool mux_subnets::forbid(mux_subnet *msn_arg)
{
    mux_subnet_node *msn = new mux_subnet_node(msn_arg, HC_FORBID);
    insert(&msnRoot, msn);
    return true;
}

bool mux_subnets::nositemon(mux_subnet *msn_arg)
{
    mux_subnet_node *msn = new mux_subnet_node(msn_arg, HC_NOSITEMON);
    insert(&msnRoot, msn);
    return true;
}

bool mux_subnets::sitemon(mux_subnet *msn_arg)
{
    mux_subnet_node *msn = new mux_subnet_node(msn_arg, HC_SITEMON);
    insert(&msnRoot, msn);
    return true;
}

bool mux_subnets::noguest(mux_subnet *msn_arg)
{
    mux_subnet_node *msn = new mux_subnet_node(msn_arg, HC_NOGUEST);
    insert(&msnRoot, msn);
    return true;
}

bool mux_subnets::guest(mux_subnet *msn_arg)
{
    mux_subnet_node *msn = new mux_subnet_node(msn_arg, HC_GUEST);
    insert(&msnRoot, msn);
    return true;
}

bool mux_subnets::suspect(mux_subnet *msn_arg)
{
    mux_subnet_node *msn = new mux_subnet_node(msn_arg, HC_SUSPECT);
    insert(&msnRoot, msn);
    return true;
}

bool mux_subnets::trust(mux_subnet *msn_arg)
{
    mux_subnet_node *msn = new mux_subnet_node(msn_arg, HC_TRUST);
    insert(&msnRoot, msn);
    return true;
}

bool mux_subnets::reset(mux_subnet *msn_arg)
{
    mux_subnet_node *msn = remove(msnRoot, msn_arg);
    return true;
}

static struct access_keyword
{
    unsigned long  m;
    const UTF8    *s;
} access_keywords[] =
{
    { HC_PERMIT,     T("Permit")    },
    { HC_REGISTER,   T("Register")  },
    { HC_FORBID,     T("Forbid")    },
    { HC_NOSITEMON,  T("NoSiteMon") },
    { HC_SITEMON,    T("SiteMon")   },
    { HC_NOGUEST,    T("NoGuest")   },
    { HC_GUEST,      T("Guest")     },
    { HC_SUSPECT,    T("Suspect")   },
    { HC_TRUST,      T("Trust")     },
};

void mux_subnets::listinfo(dbref player, UTF8 *sLine, UTF8 *sAddress, UTF8 *sControl, mux_subnet_node *p)
{
    if (nullptr == p)
    {
        return;
    }
    listinfo(player, sLine, sAddress, sControl, p->pnLeft);

    int nLeadingBits;
    p->msn->listinfo(sLine, &nLeadingBits);

    bool fFirst = true;
    UTF8* bufc = sControl;
    for (size_t i = 0; i < sizeof(access_keywords)/sizeof(access_keywords[0]); i++)
    {
        if (p->ulControl & access_keywords[i].m)
        {
            if (fFirst)
            {
                fFirst = false;
            }
            else
            {
                safe_chr(' ', sControl, &bufc);
            }

            safe_str(access_keywords[i].s, sControl, &bufc);
        }
    }
    *bufc = '\0';

    mux_sprintf(sAddress, LBUF_SIZE, T("%s/%d"), sLine, nLeadingBits);
    mux_sprintf(sLine, LBUF_SIZE, T("%-50s %s"), sAddress, sControl);
    notify(player, sLine);

    listinfo(player, sLine, sAddress, sControl, p->pnInside);
    listinfo(player, sLine, sAddress, sControl, p->pnRight);
}

void mux_subnets::listinfo(dbref player)
{
    notify(player, T("----- Site Access -----"));
    notify(player, T("Address                                            Status"));

    UTF8 *sAddress = alloc_lbuf("list_sites.addr");
    UTF8 *sControl = alloc_lbuf("list_sites.control");
    UTF8 *sLine = alloc_lbuf("list_sites.line");

    listinfo(player, sLine, sAddress, sControl, msnRoot);

    free_lbuf(sLine);
    free_lbuf(sControl);
    free_lbuf(sAddress);
}

int mux_subnets::check(MUX_SOCKADDR *msa)
{
    unsigned long ulInfo = HI_PERMIT;
    search(msnRoot, msa, &ulInfo);
    return ulInfo;
}

bool mux_subnets::isRegistered(MUX_SOCKADDR *msa)
{
    unsigned long ulInfo = HI_PERMIT;
    search(msnRoot, msa, &ulInfo);
    return 0 != (ulInfo & HI_REGISTER);
}

bool mux_subnets::isForbid(MUX_SOCKADDR *msa)
{
    unsigned long ulInfo = HI_PERMIT;
    search(msnRoot, msa, &ulInfo);
    return 0 != (ulInfo & HI_FORBID);
}

bool mux_subnets::isSuspect(MUX_SOCKADDR *msa)
{
    unsigned long ulInfo = HI_PERMIT;
    search(msnRoot, msa, &ulInfo);
    return 0 != (ulInfo & HI_SUSPECT);
}
