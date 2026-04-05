/*! \file session.cpp
 * \brief Engine-facing session and notification routines.
 *
 * Functions in this file implement game-level session semantics
 * (notification, connection events, softcode helpers) using the
 * connection accessor layer defined in net.cpp.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

// ---------------------------------------------------------------------------
// make_port_ulist: Make a list of connected user numbers for the LPORTS function.
// ---------------------------------------------------------------------------

struct port_ulist_context
{
    dbref viewer;
    ITL *pitl;
    UTF8 *tmp;
};

static void port_ulist_callback(dbref player, SOCKET sock, void *ctx)
{
    port_ulist_context *puc = static_cast<port_ulist_context *>(ctx);
    if (  !See_Hidden(puc->viewer)
       && Hidden(player))
    {
        return;
    }

    UTF8* p = puc->tmp;
    p += mux_ltoa(player, p);
    *p++ = ':';
    p += mux_i64toa(sock, p);

    const size_t n = p - puc->tmp;
    ItemToList_AddStringLEN(puc->pitl, n, puc->tmp);
}

void make_port_ulist(dbref player, UTF8 *buff, UTF8 **bufc)
{
    ITL itl;
    UTF8 *tmp = alloc_sbuf("make_port_ulist");
    ItemToList_Init(&itl, buff, bufc, '#');

    port_ulist_context ctx = { player, &itl, tmp };
    for_each_connected_desc(port_ulist_callback, &ctx);

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

    const CLinearTimeDelta ltdDiff = ltaCurrent - ltaLast;
    if (ltdDiff < mudconf.timeslice)
    {
        return;
    }

    const int nSlices = ltdDiff / mudconf.timeslice;
    const int nExtraQuota = mudconf.cmd_quota_incr * nSlices;

    if (nExtraQuota > 0)
    {
        update_all_desc_quotas(nExtraQuota, mudconf.cmd_quota_max);
    }
    ltaLast += mudconf.timeslice * nSlices;
}

/* raw_notify_html() -- raw_notify() without the newline */
void raw_notify_html(dbref player, const UTF8 *msg)
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

    send_text_to_player(player, msg);
}

/* ---------------------------------------------------------------------------
 * raw_notify: write a message to a player
 */

void raw_notify(dbref player, const UTF8 *msg)
{
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

    send_text_to_player(player, msg);
    send_raw_to_player(player, T("\r\n"), 2);
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

    send_raw_to_player(player, T("\r\n"), 2);
}

/* ---------------------------------------------------------------------------
 * raw_broadcast: Send message to players who have indicated flags
 */

void DCL_CDECL raw_broadcast(int inflags, const UTF8 *fmt, ...)
{
    if (!fmt || !*fmt)
    {
        return;
    }

    LBuf buff = LBuf_Src("raw_notify_printf");

    va_list ap;
    va_start(ap, fmt);
    mux_vsnprintf(buff, LBUF_SIZE, fmt, ap);
    va_end(ap);

    broadcast_and_flush(inflags, buff);
}

int fetch_session(dbref target)
{
    return count_player_descs(target);
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

UTF8 *MakeCanonicalDoing(UTF8 *pDoing, size_t *pnValidDoing, bool *pbValidDoing)
{
    *pnValidDoing = 0;
    *pbValidDoing = false;

    if (!pDoing)
    {
        return nullptr;
    }

    thread_local UTF8 szFittedDoing[SIZEOF_DOING_STRING+1];
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
    static UTF8 Empty[] = {'\0'};
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

    const bool bQuiet = ((key & DOING_QUIET) == DOING_QUIET);
    key &= DOING_MASK;
    if (key == DOING_MESSAGE)
    {
        int count = count_player_descs(executor);
        if (count > 0)
        {
            set_doing_all(executor, szValidDoing, nValidDoing);
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
        if (set_doing_least_idle(executor, szValidDoing, nValidDoing))
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

void list_siteinfo(dbref player)
{
    list_siteinfo_via_driver(player);
}

/* ---------------------------------------------------------------------------
 * make_ulist: Make a list of connected user numbers for the LWHO function.
 */

struct ulist_context
{
    dbref viewer;
    ITL *pitl;
};

static void ulist_callback(dbref player, void *ctx)
{
    ulist_context *uc = static_cast<ulist_context *>(ctx);
    if (  !See_Hidden(uc->viewer)
       && Hidden(player))
    {
        return;
    }
    ItemToList_AddInteger(uc->pitl, player);
}

void make_ulist(dbref player, UTF8 *buff, UTF8 **bufc, bool bPorts)
{
    if (bPorts)
    {
        make_port_ulist(player, buff, bufc);
    }
    else
    {
        ITL pContext;
        ItemToList_Init(&pContext, buff, bufc, '#');
        ulist_context ctx = { player, &pContext };
        for_each_connected_player(ulist_callback, &ctx);
        ItemToList_Final(&pContext);
    }
}

/* ---------------------------------------------------------------------------
 * find_connected_name: Resolve a playername from the list of connected
 * players using prefix matching.  We only return a match if the prefix
 * was unique.
 */

struct connected_name_context
{
    dbref viewer;
    const UTF8 *name;
    dbref found;
    bool ambiguous;
};

static void connected_name_callback(dbref player, void *ctx)
{
    connected_name_context *cnc = static_cast<connected_name_context *>(ctx);
    if (cnc->ambiguous)
    {
        return;
    }
    if (  Good_obj(cnc->viewer)
       && !See_Hidden(cnc->viewer)
       && Hidden(player))
    {
        return;
    }
    if (!string_prefix(Name(player), cnc->name))
    {
        return;
    }
    if (  cnc->found != NOTHING
       && cnc->found != player)
    {
        cnc->ambiguous = true;
        return;
    }
    cnc->found = player;
}

dbref find_connected_name(const dbref player, const UTF8 *name)
{
    connected_name_context ctx = { player, name, NOTHING, false };
    for_each_connected_player(connected_name_callback, &ctx);
    return ctx.ambiguous ? NOTHING : ctx.found;
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


// fetch_cmds - Retrieve Player's number of commands entered.
//
int fetch_cmds(const dbref target)
{
    return sum_player_command_count(target);
}

static void ParseConnectionInfoString(UTF8 *pConnInfo, UTF8 *pFields[5])
{
    string_token st(pConnInfo, T(" "));
    for (int i = 0; i < 5; i++)
    {
        pFields[i] = st.parse();
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

