/*! \file net.cpp
 * \brief Connection management and driver-side networking routines.
 *
 * This file contains the connection accessor layer, I/O queuing,
 * login processing, and other driver-side networking routines.
 * Engine-facing session semantics are in session.cpp.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "interface.h"
#include "sqlite_backend.h"
using namespace std;

NAMETAB default_charset_nametab[] =
{
    {T("ascii"),           5,       0,     CHARSET_ASCII},
    {T("oem"),             3,       0,     CHARSET_CP437},
    {T("cp437"),           5,       0,     CHARSET_CP437},
    {T("latin-1"),         7,       0,     CHARSET_LATIN1},
    {T("latin-2"),         7,       0,     CHARSET_LATIN2},
    {T("iso8859-1"),       9,       0,     CHARSET_LATIN1},
    {T("iso8859-2"),       9,       0,     CHARSET_LATIN2},
    { nullptr,     0,       0,     0}
};

/* ---------------------------------------------------------------------------
 * make_portlist: Make a list of ports for PORTS().
 */

struct portlist_context
{
    ITL *pitl;
};

static void portlist_callback(DESC *d, void *ctx)
{
    portlist_context *pc = static_cast<portlist_context *>(ctx);
    ItemToList_AddInteger64(pc->pitl, d->socket);
}

void make_portlist(dbref player, dbref target, UTF8 *buff, UTF8 **bufc)
{
    UNUSED_PARAMETER(player);

    ITL itl;
    ItemToList_Init(&itl, buff, bufc);

    portlist_context ctx = { &itl };
    for_each_player_desc(target, portlist_callback, &ctx);
    ItemToList_Final(&itl);
}







/* ---------------------------------------------------------------------------

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
    d->output_queue.emplace_back(reinterpret_cast<const char *>(b), n);
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
    // to queue_write_LEN(). If we could know somehow that process_output()
    // would be unproductive, then we wouldn't make even the following call.
    //
    if (static_cast<size_t>(mudconf.output_limit) < d->output_size + n)
    {
        process_output(d, false);
    }

    while (  static_cast<size_t>(mudconf.output_limit) < d->output_size + n
          && !d->output_queue.empty())
    {
        // Drop the oldest entry to make room.
        //
        const size_t nchars = d->output_queue.front().size();

        STARTLOG(LOG_NET, "NET", "WRITE");
        UTF8 *buf = alloc_lbuf("queue_write.LOG");
        mux_sprintf(buf, LBUF_SIZE, T("[%u/%s] Output buffer overflow, %zu chars discarded by "),
            d->socket, d->addr, nchars);
        log_text(buf);
        free_lbuf(buf);
        if (d->flags & DS_CONNECTED)
        {
            log_name(d->player);
        }
        ENDLOG;

        d->output_size -= nchars;
        d->output_lost += nchars;
        d->output_queue.pop_front();
    }

    // Append the request to the end of the output queue for later transmission.
    //
    add_to_output_queue(d, b, n);
    d->output_size += n;
    d->output_tot += n;

}

void queue_write(DESC *d, const UTF8 *b)
{
    queue_write_LEN(d, b, strlen(reinterpret_cast<const char *>(b)));
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
            const UTF8 *p = reinterpret_cast<const UTF8 *>(strchr(reinterpret_cast<const char *>(pString), NVT_IAC));
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
                    mux_strncpy(pBuffer, pString, sizeof(Buffer)-1);
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

void init_desc(DESC *d)
{
    new (&d->output_queue) std::deque<std::string>();
    new (&d->input_queue) std::deque<std::string>();
}

void destroy_desc(DESC *d)
{
    d->output_queue.~deque();
    d->input_queue.~deque();
}

void freeqs(DESC *d)
{
    d->output_queue.clear();
    d->output_size = 0;

    d->input_queue.clear();
    d->input_size = 0;

    if (d->raw_input_buf)
    {
        free_lbuf(d->raw_input_buf);
    }
    d->raw_input_buf = nullptr;

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
    d->charset_request_pending = false;
}

/* ---------------------------------------------------------------------------
 * desc_addhash: Add a net descriptor to its player hash list.
 */

void desc_addhash(DESC *d)
{
    dbref player = d->player;
    mudstate.dbref_to_descriptors_map.insert(make_pair(player, d));
}

/* ---------------------------------------------------------------------------
 * desc_delhash: Remove a net descriptor from its player hash list.
 */

static void desc_delhash(const DESC *d)
{
    const dbref player = d->player;
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(player);

    for (auto it = range.first; it != range.second; ++it) {
        if (it->second == d) {
            mudstate.dbref_to_descriptors_map.erase(it);
            return;
        }
    }
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

void save_command(DESC *d, const UTF8 *cmd, size_t len)
{
    bool was_empty = d->input_queue.empty();

    if (  CHARSET_UTF8 == d->encoding
       && !utf8_is_nfc(cmd, len))
    {
        // Normalize to NFC before queuing.  NFC never expands the string.
        //
        UTF8 nfc_buf[LBUF_SIZE];
        size_t nNfc;
        utf8_normalize_nfc(cmd, len, nfc_buf, sizeof(nfc_buf) - 1, &nNfc);
        nfc_buf[nNfc] = '\0';
        d->input_queue.emplace_back(reinterpret_cast<const char *>(nfc_buf), nNfc);
    }
    else
    {
        d->input_queue.emplace_back(reinterpret_cast<const char *>(cmd), len);
    }

    if (was_empty)
    {
        // We have added our first command to an empty queue. Go process it later.
        //
        scheduler.DeferImmediateTask(PRIORITY_SYSTEM, Task_ProcessCommand, d, 0);
    }
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
    size_t nmsg = strlen(reinterpret_cast<const char *>(msg));
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

void announce_connect(const dbref player, DESC *d)
{
    desc_addhash(d);

    // Preload built-in attributes for the player and their location
    // in a single bulk read each, avoiding individual cache misses.
    //
    cache_preload(player);
    const dbref ploc = Location(player);
    if (Good_obj(ploc))
    {
        cache_preload(ploc);
    }

    int count = get_total_connections();

    if (mudstate.record_players < count)
    {
        mudstate.record_players = count;
        if (!g_pSQLiteBackend->GetDB().PutMeta("record_players", mudstate.record_players))
        {
            Log.tinyprintf(T("announce_connect: failed to persist record_players=%d" ENDLINE),
                mudstate.record_players);
        }
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

    const dbref loc = Location(player);
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

    int num = count_player_descs(player);

    // Check for MUX_UNICODE forced on
    //
    if (Unicode(player))
    {
        set_player_encoding(player, CHARSET_UTF8);
    }

    if (Ascii(player))
    {
        set_player_encoding(player, CHARSET_ASCII);
    }

    // Reset vacation flag.
    //
    s_Flags(player, FLAG_WORD2, Flags2(player) & ~VACATION);
    if (Guest(player))
    {
        s_Flags(player, FLAG_WORD1, db[player].fs.word[FLAG_WORD1] & ~DARK);
    }

    const UTF8 *pRoomAnnounceFmt;
    const UTF8 *pMonitorAnnounceFmt;
    if (num < 2)
    {
        pRoomAnnounceFmt = T("%s has connected.");
        do_comconnect(player);
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
    const UTF8 *time_str = ltaNow.ReturnDateString(7);

    UTF8 host_address[MBUF_SIZE];
    d->address.ntop(host_address, sizeof(host_address));
    record_login(player, true, time_str, d->addr, d->username, host_address);
    check_mail(player, 0, false);
    look_in(player, Location(player), (LK_SHOWEXIT|LK_OBEYTERSE|LK_SHOWVRML));
    mudstate.curr_enactor = temp;
}

void announce_disconnect(const dbref player, DESC *d, const UTF8 *reason)
{
    int num = count_player_descs(player);
    int key;

    const dbref temp = mudstate.curr_enactor;
    mudstate.curr_enactor = player;
    const dbref loc = Location(player);

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

        do_mail_purge(player);

        raw_broadcast(MONITOR, T("GAME: %s has disconnected. <%s>"), Moniker(player), reason);

        c_Connected(player);

        do_comdisconnect(player);

        dbref aowner, zone, obj;
        int aflags;
        size_t nLen;
        CLinearTimeAbsolute lta;
        atr_pget_str_LEN(buf, player, A_ADISCONNECT, &aowner, &aflags, &nLen);
        if (nLen)
        {
            wait_que(player, player, player, AttrTrace(aflags, 0), false,
                lta, NOTHING, 0,
                buf,
                1, &reason,
                nullptr);
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
            s_Flags(player, FLAG_WORD1, db[player].fs.word[FLAG_WORD1] & ~DARK);
        }

        if (Guest(player))
        {
            s_Flags(player, FLAG_WORD1, db[player].fs.word[FLAG_WORD1] | DARK);
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

    mudstate.curr_enactor = temp;
    desc_delhash(d);

    local_disconnect(player, num);
    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->disconnect(player, num);
        p = p->pNext;
    }
}

int boot_off(const dbref player, const UTF8 *message)
{
    int count = 0;
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(player);
    for (auto it = range.first; it != range.second; )
    {
        DESC* d = it->second;
        ++it;
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
    int count = 0;
    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); )
    {
        DESC* d = *it;
        ++it;
        if (d->flags & DS_CONNECTED)
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
    }
    return count;
}

/* ---------------------------------------------------------------------------
 * desc_reload: Reload parts of net descriptor that are based on db info.
 */

void desc_reload(dbref player)
{
    dbref aowner;
    int aflags;

    const auto range = mudstate.dbref_to_descriptors_map.equal_range(player);
    for (auto it = range.first; it != range.second; ++it)
    {
        DESC* d = it->second;
        UTF8* buf = atr_pget(player, A_TIMEOUT, &aowner, &aflags);
        if (buf)
        {
            d->timeout = mux_atol(buf);
            if (d->timeout <= 0)
            {
                d->timeout = mudconf.idle_timeout;
            }
            free_lbuf(buf);
            buf = nullptr;
        }
    }
}

// ---------------------------------------------------------------------------
// fetch_session: Return number of sessions (or 0 if not logged in).
//

static DESC *find_least_idle(dbref target)
{
    CLinearTimeAbsolute ltaNewestLastTime;

    DESC *dLeastIdle = nullptr;
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    for (auto it = range.first; it != range.second; ++it)
    {
        DESC* d = it->second;
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
// find_oldest: Return descriptor with the oldest connected_at (or nullptr if
// not logged in).
//
void find_oldest(const dbref target, DESC *dOldest[2])
{
    dOldest[0] = nullptr;
    dOldest[1] = nullptr;

    bool bFound = false;
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    for (auto it = range.first; it != range.second; ++it)
    {
        DESC* d = it->second;
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

// ---------------------------------------------------------------------------
// find_desc_by_socket: Return connected descriptor matching a socket number,
// or nullptr if not found.  Used by softcode functions that accept a port
// number argument (height, width, doing, idle, etc.).
//
DESC *find_desc_by_socket(SOCKET s)
{
    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); ++it)
    {
        DESC* d = *it;
        if ((d->flags & DS_CONNECTED) && d->socket == s)
        {
            return d;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// find_desc_by_player: Return first connected descriptor for a player,
// or nullptr if not connected.
//
DESC *find_desc_by_player(dbref target)
{
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    if (range.first != range.second)
    {
        return range.first->second;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// get_total_connections: Return the total number of descriptor entries.
// Used by @list stats.
//
int get_total_connections(void)
{
    return static_cast<int>(mudstate.dbref_to_descriptors_map.size());
}

// ---------------------------------------------------------------------------
// for_each_connected_player: Call a function for each connected player dbref.
// Used by wall_broadcast, keepalive, and similar operations that need to
// iterate all connected sessions without touching DESC internals.
//
void for_each_connected_player(void (*callback)(dbref player, void *context), void *context)
{
    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); ++it)
    {
        DESC* d = *it;
        if (d->flags & DS_CONNECTED)
        {
            callback(d->player, context);
        }
    }
}

// ---------------------------------------------------------------------------
// set_player_encoding: Force a character encoding on all of a player's
// connections.  Called when the engine sets the UNICODE or ASCII flag.
//
void set_player_encoding(dbref target, int encoding)
{
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    for (auto it = range.first; it != range.second; ++it)
    {
        DESC* d = it->second;
        if (encoding != d->encoding)
        {
            d->encoding = encoding;
            if (CHARSET_UTF8 == encoding)
            {
                d->raw_codepoint_state = CL_PRINT_START_STATE;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// reset_player_encoding: Reset all of a player's connections to their
// negotiated encoding.  Called when the engine clears the UNICODE or ASCII
// flag.
//
void reset_player_encoding(dbref target)
{
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    for (auto it = range.first; it != range.second; ++it)
    {
        DESC* d = it->second;
        d->encoding = d->negotiated_encoding;
    }
}

// ---------------------------------------------------------------------------
// for_each_player_desc: Call a function for each descriptor belonging to a
// given player.  Used by fcache_send and similar per-player operations.
//
void for_each_player_desc(dbref target, void (*callback)(DESC *d, void *context), void *context)
{
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    for (auto it = range.first; it != range.second; ++it)
    {
        callback(it->second, context);
    }
}

// ---------------------------------------------------------------------------
// send_keepalive_nops: Send a Telnet NOP to all connected players that have
// the KeepAlive flag set.  Creates traffic to keep NAT routers happy.
//
void send_keepalive_nops(void)
{
    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); ++it)
    {
        DESC* d = *it;
        if (  (d->flags & DS_CONNECTED)
           && KeepAlive(d->player))
        {
            const UTF8 aNOP[2] = { NVT_IAC, NVT_NOP };
            queue_write_LEN(d, aNOP, sizeof(aNOP));
        }
    }
}

// ---------------------------------------------------------------------------
// player_has_program: Return true if any descriptor for the given player has
// non-null program_data (i.e., the player is in @program mode).
//
bool player_has_program(dbref target)
{
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    for (auto it = range.first; it != range.second; ++it)
    {
        if (nullptr != it->second->program_data)
        {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// detach_player_program: Remove program_data from all of a player's
// descriptors and return it.  The caller is responsible for freeing the
// returned program_data (and clearing its wait_regs).
//
program_data *detach_player_program(dbref target)
{
    program_data *program = nullptr;
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    for (auto it = range.first; it != range.second; ++it)
    {
        DESC* d = it->second;
        if (it == range.first)
        {
            program = d->program_data;
        }
        else
        {
            mux_assert(program == d->program_data);
        }
        d->program_data = nullptr;
    }
    return program;
}

// ---------------------------------------------------------------------------
// set_player_program: Set program_data on all of a player's descriptors.
//
void set_player_program(dbref target, program_data *program)
{
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    for (auto it = range.first; it != range.second; ++it)
    {
        it->second->program_data = program;
    }
}

// ---------------------------------------------------------------------------
// send_prog_prompt: Send the @program prompt ("> ") to all of a player's
// descriptors, using EOR or GA telnet sequences as appropriate.
//
void send_prog_prompt(dbref target)
{
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    for (auto it = range.first; it != range.second; ++it)
    {
        DESC* d = it->second;
        queue_string(d, tprintf(T("%s>%s "), COLOR_INTENSE, COLOR_RESET));

        if (OPTION_YES == us_state(d, TELNET_EOR))
        {
            // Use telnet protocol's EOR command to show prompt.
            //
            const UTF8 aEOR[2] = { NVT_IAC, NVT_EOR };
            queue_write_LEN(d, aEOR, sizeof(aEOR));
        }
        else if (OPTION_YES != us_state(d, TELNET_SGA))
        {
            // Use telnet protocol's GOAHEAD command to show prompt.
            //
            const UTF8 aGoAhead[2] = { NVT_IAC, NVT_GA };
            queue_write_LEN(d, aGoAhead, sizeof(aGoAhead));
        }
    }
}

// ---------------------------------------------------------------------------
// send_text_to_player: Queue encoded text to all of a player's connections.
//
void send_text_to_player(dbref target, const UTF8 *text)
{
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    for (auto it = range.first; it != range.second; ++it)
    {
        queue_string(it->second, text);
    }
}

void send_text_to_player(dbref target, const mux_string &text)
{
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    for (auto it = range.first; it != range.second; ++it)
    {
        queue_string(it->second, text);
    }
}

// ---------------------------------------------------------------------------
// send_raw_to_player: Queue raw bytes to all of a player's connections.
//
void send_raw_to_player(dbref target, const UTF8 *data, size_t len)
{
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    for (auto it = range.first; it != range.second; ++it)
    {
        queue_write_LEN(it->second, data, len);
    }
}

// ---------------------------------------------------------------------------
// count_player_descs: Return the number of descriptors for a given player.
//
int count_player_descs(dbref target)
{
    int count = 0;
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    for (auto it = range.first; it != range.second; ++it)
    {
        count++;
    }
    return count;
}

// ---------------------------------------------------------------------------
// sum_player_command_count: Return the sum of command_count across all of a
// player's descriptors, or -1 if not connected.
//
int sum_player_command_count(dbref target)
{
    int sum = 0;
    bool bFound = false;
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    for (auto it = range.first; it != range.second; ++it)
    {
        sum += it->second->command_count;
        bFound = true;
    }
    return bFound ? sum : -1;
}

// ---------------------------------------------------------------------------
// set_doing_all: Set the doing string on all of a player's descriptors.
//
void set_doing_all(dbref target, const UTF8 *doing, size_t len)
{
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(target);
    for (auto it = range.first; it != range.second; ++it)
    {
        memcpy(it->second->doing, doing, len + 1);
    }
}

// ---------------------------------------------------------------------------
// set_doing_least_idle: Set the doing string on the least-idle descriptor.
// Returns true if a descriptor was found.
//
bool set_doing_least_idle(dbref target, const UTF8 *doing, size_t len)
{
    DESC *d = find_least_idle(target);
    if (d)
    {
        memcpy(d->doing, doing, len + 1);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// update_all_desc_quotas: Add quota to all descriptors, clamped to max.
//
void update_all_desc_quotas(int nExtra, int nMax)
{
    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); ++it)
    {
        DESC* d = *it;
        d->quota += nExtra;
        if (d->quota > nMax)
        {
            d->quota = nMax;
        }
    }
}

// ---------------------------------------------------------------------------
// broadcast_and_flush: Send text to all connected players matching flags,
// and flush output immediately.
//
void broadcast_and_flush(int inflags, const UTF8 *text)
{
    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); )
    {
        DESC* d = *it;
        ++it;
        if (d->flags & DS_CONNECTED)
        {
            if ((Flags(d->player) & inflags) == inflags)
            {
                queue_string(d, text);
                queue_write_LEN(d, T("\r\n"), 2);
                process_output(d, false);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// for_each_connected_desc: Call a function for each connected descriptor.
// Unlike for_each_connected_player (which only passes dbref), this passes
// the descriptor itself, along with player dbref and socket.
//
void for_each_connected_desc(void (*callback)(dbref player, SOCKET sock, void *context), void *context)
{
    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); ++it)
    {
        DESC* d = *it;
        if (d->flags & DS_CONNECTED)
        {
            callback(d->player, d->socket, context);
        }
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
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();

    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); )
    {
        DESC* d = *it;
        ++it;
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
                    bool found = false;
                    auto range = mudstate.dbref_to_descriptors_map.equal_range(d->player);
                    for (auto it = range.first; it != range.second; ++it)
                    {
                        DESC* d1 = it->second;
                        if (d1 != d)
                        {
                            CLinearTimeDelta ltd = ltaNow - d1->last_time;
                            if (ltd.ReturnSeconds() <= mudconf.idle_timeout)
                            {
                                 found = true;
                                 break;
                            }
                        }
                    }
                    if (!found)
                    {
                        s_Flags(d->player, FLAG_WORD1, db[d->player].fs.word[FLAG_WORD1] | DARK);
                        auto range2 = mudstate.dbref_to_descriptors_map.equal_range(d->player);
                        for (auto it = range2.first; it != range2.second; ++it)
                        {
                            DESC* d1 = it->second;
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


#define MAX_TRIMMED_NAME_LENGTH 31
LBUF_OFFSET trimmed_name(const dbref player, UTF8 cbuff[MBUF_SIZE], const LBUF_OFFSET nMin, const LBUF_OFFSET nMax, const LBUF_OFFSET nPad)
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

    size_t nLen = strlen(reinterpret_cast<char *>(szName));
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
    UTF8 *fp, *sp, flist[4], slist[4];

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

    UTF8* buf = alloc_mbuf("dump_users");
    if (key == CMD_SESSION)
    {
        queue_write(e, T("                                    "));
        queue_write(e, T("     Characters Input----  Characters Output---\r\n"));
    }
    queue_write(e, T("Player Name             On For Idle "));
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
    int count = 0;

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();

    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); ++it)
    {
        DESC* d = *it;
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
                        const dbref room_it = where_room(d->player);
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
                const int host_info = mudstate.access_list.check(&d->address);
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
            size_t vwNameField = strlen(reinterpret_cast<char*>(NameField));
            if (d->flags & DS_CONNECTED)
            {
                vwNameField = trimmed_name(d->player, NameField, 18, MAX_TRIMMED_NAME_LENGTH, 1);
            }

            // The width size allocated to the 'On For' field.
            //
            const size_t nOnFor = 30 - vwNameField;

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
#if defined(REALITY_LVLS)
    T("REALITY_LVLS"),
#endif
#if defined(STUB_SLAVE)
    T("STUB_SLAVE"),
#endif
#if defined(WOD_REALMS)
    T("WOD_REALMS"),
#endif
    nullptr
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
    const UTF8 *temp = lta.ReturnDateString();
    queue_write(arg_desc, tprintf(T("Uptime: %s\r\n"), temp));

    int count = 0;
    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); ++it)
    {
        DESC* d = *it;
        if (d->flags & DS_CONNECTED)
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
    {T("HELP"),          4,  CA_PUBLIC,  CMD_HELP},
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
        mudstate.logout_cmd_htab.emplace(std::vector<UTF8>(cp->name, cp->name + strlen(reinterpret_cast<const char *>(cp->name))), cp);
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

const UTF8 *connect_fail = T("Either that player does not exist, or has a different password.\r\n");

static bool check_connect(DESC *d, UTF8 *msg)
{
    UTF8 *buff;
    dbref player, aowner;
    int aflags, nplayers;

    const UTF8 *cmdsave = mudstate.debug_cmd;
    mudstate.debug_cmd = T("< check_connect >");

    // Hide the password length from SESSION.
    //
    d->input_tot -= (strlen(reinterpret_cast<char*>(msg)) + 1);

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
    if (  strncmp(reinterpret_cast<char*>(command), "co", 2) == 0
       || strncmp(reinterpret_cast<char*>(command), "cd", 2) == 0)
    {
        bool isGuest = false;
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

                const UTF8* p = Guest.Create(d);
                if (!p)
                {
                    free_lbuf(command);
                    free_lbuf(user);
                    free_lbuf(password);
                    mudstate.debug_cmd = cmdsave;
                    return false;
                }
                mux_strncpy(user, p, LBUF_SIZE-1);
                mux_strncpy(password, reinterpret_cast<const UTF8*>(GUEST_PASSWORD), LBUF_SIZE-1);
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
            for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); ++it)
            {
                DESC* d2 = *it;
                if (d2->flags & DS_CONNECTED)
                {
                    nplayers++;
                }
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
            if (  strncmp(reinterpret_cast<char*>(command), "cd", 2) == 0
               && (  RealWizard(player)
                  || God(player)))
            {
                s_Flags(player, FLAG_WORD1, db[player].fs.word[FLAG_WORD1] | DARK);
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
            const auto range = mudstate.dbref_to_descriptors_map.equal_range(player);
            for (auto it = range.first; it != range.second; ++it)
            {
                DESC* d2 = it->second;
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

            int num_con = 0;
            const auto range2 = mudstate.dbref_to_descriptors_map.equal_range(player);
            for (auto it = range2.first; it != range2.second; ++it)
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
    else if (strncmp(reinterpret_cast<char*>(command), "cr", 2) == 0)
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
            for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); ++it)
            {
                DESC* d2 = *it;
                if (d2->flags & DS_CONNECTED)
                {
                    nplayers++;
                }
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
                AddToPlayerChannels(player);
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
        dump_users(d, arg, key);
        break;

    case CMD_PREFIX:

        set_userstring(&d->output_prefix, arg);
        break;

    case CMD_SUFFIX:

        set_userstring(&d->output_suffix, arg);
        break;

    case CMD_HELP:

        fcache_dump(d, FC_CONN_HELP);
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
        NamedRegsClear(mudstate.named_regs);

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
    auto it_logout = mudstate.logout_cmd_htab.find(std::vector<UTF8>(command, command + iArg));
    NAMETAB *cp = (it_logout != mudstate.logout_cmd_htab.end()) ? static_cast<NAMETAB*>(it_logout->second) : nullptr;
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
        const auto range = mudstate.dbref_to_descriptors_map.equal_range(executor);
        for (auto it = range.first; it != range.second; ++it)
        {
            DESC* d = it->second;
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
    DESC* dLatest = nullptr;
    const auto range = mudstate.dbref_to_descriptors_map.equal_range(executor);
    for (auto it = range.first; it != range.second; ++it)
    {
        DESC* d = it->second;
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

    logged_out1(executor, caller, enactor, 0, key, const_cast<UTF8 *>(T("")), nullptr, 0);
}

void Task_ProcessCommand(void *arg_voidptr, int arg_iInteger)
{
    UNUSED_PARAMETER(arg_iInteger);

    DESC *d = static_cast<DESC *>(arg_voidptr);
    if (d)
    {
        if (!d->input_queue.empty())
        {
            if (d->quota > 0)
            {
                d->quota--;
                std::string cmd = std::move(d->input_queue.front());
                d->input_queue.pop_front();

                if (!d->input_queue.empty())
                {
                    // There are still commands to process, so schedule another looksee.
                    //
                    scheduler.DeferImmediateTask(PRIORITY_SYSTEM, Task_ProcessCommand, d, 0);
                }

                d->input_size -= cmd.size();

                // IDLE command: keep connection alive without resetting idle timer (#590).
                //
                const char *pCmd = cmd.data();
                while (mux_isspace(static_cast<unsigned char>(*pCmd)))
                {
                    pCmd++;
                }
                if (mux_stricmp(reinterpret_cast<const UTF8 *>(pCmd),
                                reinterpret_cast<const UTF8 *>("IDLE")) == 0)
                {
                    if (!d->input_queue.empty())
                    {
                        scheduler.DeferImmediateTask(PRIORITY_SYSTEM, Task_ProcessCommand, d, 0);
                    }
                    return;
                }

                d->last_time.GetUTC();
                if (d->program_data != nullptr)
                {
                    handle_prog(d, reinterpret_cast<UTF8 *>(cmd.data()));
                }
                else
                {
                    do_command(d, reinterpret_cast<UTF8 *>(cmd.data()));
                }
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
        const SOCKET s = mux_atol(fargs[0]);
        DESC *d = find_desc_by_socket(s);
        if (d)
        {
            if (d->player == executor || Wizard_Who(executor))
            {
                safe_str(d->doing, buff, bufc);
            }
            else
            {
                safe_nothing(buff, bufc);
            }
        }
    }
    else
    {
        const dbref victim = lookup_player(executor, fargs[0], true);
        if (victim == NOTHING)
        {
            safe_str(T("#-1 PLAYER DOES NOT EXIST"), buff, bufc);
            return;
        }

        if (  Wizard_Who(executor)
           || !Hidden(victim))
        {
            DESC *d = find_desc_by_player(victim);
            if (d)
            {
                safe_str(d->doing, buff, bufc);
                return;
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

    const bool isPort = is_rational(fargs[0]);
    DESC* d = nullptr;
    if (isPort)
    {
        const SOCKET s = mux_atol(fargs[0]);
        d = find_desc_by_socket(s);
    }
    else
    {
        const dbref victim = lookup_player(executor, fargs[0], true);
        if (victim == NOTHING)
        {
            safe_str(T("#-1 PLAYER DOES NOT EXIST"), buff, bufc);
            return;
        }
        d = find_desc_by_player(victim);
    }
    if (d)
    {
        const UTF8 *hostname = ((d->username[0] != '\0') ?
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

    const bool isPort = is_rational(fargs[0]);
    DESC* d = nullptr;
    if (isPort)
    {
        const SOCKET s = mux_atol(fargs[0]);
        d = find_desc_by_socket(s);
    }
    else
    {
        const dbref victim = lookup_player(executor, fargs[0], true);
        if (victim == NOTHING)
        {
            safe_str(T("#-1 PLAYER DOES NOT EXIST"), buff, bufc);
            return;
        }
        d = find_desc_by_player(victim);
    }

    if (d)
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

    mux_subnet::SubnetComparison ct = (*msnRoot)->msn->compare_to(msn_arg->msn);
    switch (ct)
    {
    case mux_subnet::SubnetComparison::kLessThan:
        insert(&(*msnRoot)->pnRight, msn_arg);
        break;

    case mux_subnet::SubnetComparison::kEqual:
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

    case mux_subnet::SubnetComparison::kContains:
        insert(&(*msnRoot)->pnInside, msn_arg);
        break;

    case mux_subnet::SubnetComparison::kContainedBy:
        {
            msn_arg->pnInside = *msnRoot;
            msn_arg->pnLeft = (*msnRoot)->pnLeft;
            msn_arg->pnRight = (*msnRoot)->pnRight;
            (*msnRoot)->pnLeft = nullptr;
            (*msnRoot)->pnRight = nullptr;
            *msnRoot = msn_arg;
        }
        break;

    case mux_subnet::SubnetComparison::kGreaterThan:
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

    mux_subnet::SubnetComparison ct = msnRoot->msn->compare_to(msa);
    switch (ct)
    {
    case mux_subnet::SubnetComparison::kLessThan:
        search(msnRoot->pnRight, msa, pulInfo);
        break;

    case mux_subnet::SubnetComparison::kContains:
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

    case mux_subnet::SubnetComparison::kGreaterThan:
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
    mux_subnet::SubnetComparison ct = msnRoot->msn->compare_to(msn_arg);
    switch (ct)
    {
    case mux_subnet::SubnetComparison::kLessThan:
        msnRoot->pnRight = remove(msnRoot->pnRight, msn_arg);
        break;

    case mux_subnet::SubnetComparison::kEqual:
        {
            mux_subnet_node *x = msnRoot;
            delete msnRoot->pnInside;
            msnRoot->pnInside = nullptr;
            msnRoot = joinlr(msnRoot->pnLeft, msnRoot->pnRight);
            delete x;
        }
        break;

    case mux_subnet::SubnetComparison::kContains:
        msnRoot->pnInside = remove(msnRoot->pnInside, msn_arg);
        break;

    case mux_subnet::SubnetComparison::kContainedBy:
        delete msnRoot;
        msnRoot = nullptr;
        break;

    case mux_subnet::SubnetComparison::kGreaterThan:
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
