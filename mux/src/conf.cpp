/*! \file conf.cpp
 * \brief Set up configuration information and static data.
 *
 * $Id$
 *
 * This parses the configuration files and controls configuration options used
 * to control the server and its behavior.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "command.h"
#include "interface.h"

// ---------------------------------------------------------------------------
// CONFPARM: Data used to find fields in CONFDATA.
//
typedef struct confparm
{
    const UTF8 *pname;            // parm name
    int (*interpreter)(int *vp, UTF8 *str, void *pExtra, UINT32 nExtra,
                       dbref player, UTF8 *cmd); // routine to interp parameter
    int flags;              // control flags
    int rperms;             // read permission flags.
    int *loc;               // where to store value
    void *pExtra;           // extra pointer for interpreter
    UINT32 nExtra;          // extra data for interpreter
} CONFPARM;

// ---------------------------------------------------------------------------
// External symbols.
//
CONFDATA mudconf;
STATEDATA mudstate;
int cf_set(UTF8 *, UTF8 *, dbref);
CF_HAND(cf_cf_access);

// ---------------------------------------------------------------------------
// cf_init: Initialize mudconf to default values.
//
void cf_init(void)
{
    int i;

    mudconf.indb = StringClone(T("netmux.db"));
    mudconf.outdb = StringClone(T(""));
    mudconf.crashdb = StringClone(T(""));
    mudconf.game_dir = StringClone(T(""));
    mudconf.game_pag = StringClone(T(""));
    mudconf.mail_db   = StringClone(T("mail.db"));
    mudconf.comsys_db = StringClone(T("comsys.db"));

    mudconf.compress_db = false;
    mudconf.compress = StringClone(T("gzip"));
    mudconf.uncompress = StringClone(T("gzip -d"));
    mudconf.status_file = StringClone(T("shutdown.status"));
    mudconf.max_cache_size = 1*1024*1024;

    mudconf.ports.n = 1;
    mudconf.ports.pi = (int *)MEMALLOC(sizeof(int));
    if (mudconf.ports.pi)
    {
        mudconf.ports.pi[0] = 2860;
    }
    else
    {
        ISOUTOFMEMORY(mudconf.ports.pi);
    }

#ifdef SSL_ENABLED
    mudconf.sslPorts.n = 0;
    mudconf.sslPorts.pi = NULL;
    mudconf.ssl_certificate_file[0] = '\0';
    mudconf.ssl_certificate_key[0] = '\0';
    mudconf.ssl_certificate_password[0] = '\0';
#endif

    mudconf.init_size = 1000;
    mudconf.guest_char = -1;
    mudconf.guest_nuker = GOD;
    mudconf.number_guests = 30;
    mudconf.min_guests = 1;
    mux_strncpy(mudconf.guest_prefix, T("Guest"), 31);
    mudconf.guest_file     = StringClone(T("text/guest.txt"));
    mudconf.conn_file      = StringClone(T("text/connect.txt"));
    mudconf.creg_file      = StringClone(T("text/register.txt"));
    mudconf.regf_file      = StringClone(T("text/create_reg.txt"));
    mudconf.motd_file      = StringClone(T("text/motd.txt"));
    mudconf.wizmotd_file   = StringClone(T("text/wizmotd.txt"));
    mudconf.quit_file      = StringClone(T("text/quit.txt"));
    mudconf.down_file      = StringClone(T("text/down.txt"));
    mudconf.full_file      = StringClone(T("text/full.txt"));
    mudconf.site_file      = StringClone(T("text/badsite.txt"));
    mudconf.crea_file      = StringClone(T("text/newuser.txt"));
    mudconf.crash_msg[0] = '\0';
    mudconf.motd_msg[0] = '\0';
    mudconf.wizmotd_msg[0] = '\0';
    mudconf.downmotd_msg[0] = '\0';
    mudconf.fullmotd_msg[0] = '\0';
    mudconf.dump_msg[0] = '\0';
    mudconf.postdump_msg[0] = '\0';
    mudconf.fixed_home_msg[0] = '\0';
    mudconf.fixed_tel_msg[0] = '\0';
    mux_strncpy(mudconf.public_channel, T("Public"), 31);
    mux_strncpy(mudconf.public_channel_alias, T("pub"), 31);
    mux_strncpy(mudconf.guests_channel, T("Guests"), 31);
    mux_strncpy(mudconf.guests_channel_alias, T("g"), 31);
    mux_strncpy(mudconf.pueblo_msg, T("</xch_mudtext><img xch_mode=html>"), GBUF_SIZE-1);
#if defined(FIRANMUX)
    mux_strncpy(mudconf.immobile_msg, T("You have been set immobile."), sizeof(mudconf.immobile_msg)-1);
#endif // FIRANMUX
#if defined(INLINESQL) || defined(HAVE_DLOPEN) || defined(WIN32)
    mudconf.sql_server[0]   = '\0';
    mudconf.sql_user[0]     = '\0';
    mudconf.sql_password[0] = '\0';
    mudconf.sql_database[0] = '\0';
#endif // INLINESQL

    mudconf.mail_server[0]  = '\0';
    mudconf.mail_ehlo[0]    = '\0';
    mudconf.mail_sendaddr[0]= '\0';
    mudconf.mail_sendname[0]= '\0';
    mudconf.mail_subject[0] = '\0';

    mudconf.art_rules = NULL;
    mudconf.indent_desc = false;
    mudconf.name_spaces = true;
#if defined(HAVE_WORKING_FORK)
    mudconf.fork_dump = true;
    mudstate.dumping  = false;
    mudstate.dumper   = 0;
    mudstate.dumped   = 0;
    mudstate.write_protect = false;
#endif // HAVE_WORKING_FORK
    mudconf.restrict_home = false;
    mudconf.have_comsys = true;
    mudconf.have_mailer = true;
    mudconf.have_zones = true;
    mudconf.paranoid_alloc = false;
    mudconf.sig_action = SA_DFLT;
    mudconf.max_players = -1;
    mudconf.dump_interval = 3600;
    mudconf.check_interval = 600;
    mudconf.events_daily_hour = 7;
    mudconf.dump_offset = 0;
    mudconf.check_offset = 300;
    mudconf.idle_timeout = 3600;
    mudconf.conn_timeout = 120;
    mudconf.idle_interval = 60;
    mudconf.retry_limit = 3;
    mudconf.output_limit = 16384;
    mudconf.paycheck = 0;
    mudconf.paystart = 0;
    mudconf.paylimit = 10000;
#ifdef REALITY_LVLS
    mudconf.no_levels = 0;
    mudconf.def_room_rx = 1;
    mudconf.def_room_tx = ~(RLEVEL)0;
    mudconf.def_player_rx = 1;
    mudconf.def_player_tx = 1;
    mudconf.def_exit_rx = 1;
    mudconf.def_exit_tx = 1;
    mudconf.def_thing_rx = 1;
    mudconf.def_thing_tx = 1;
#endif // REALITY_LVLS
    mudconf.start_quota = 20;
    mudconf.site_chars = 25;
    mudconf.payfind = 0;
    mudconf.digcost = 10;
    mudconf.linkcost = 1;
    mudconf.opencost = 1;
    mudconf.createmin = 10;
    mudconf.createmax = 505;
    mudconf.killmin = 10;
    mudconf.killmax = 100;
    mudconf.killguarantee = 100;
    mudconf.robotcost = 1000;
    mudconf.pagecost = 10;
    mudconf.searchcost = 100;
    mudconf.waitcost = 10;
    mudconf.machinecost = 64;
    mudconf.exit_quota = 1;
    mudconf.player_quota = 1;
    mudconf.room_quota = 1;
    mudconf.thing_quota = 1;
    mudconf.mail_expiration = 14;
    mudconf.queuemax = 100;
    mudconf.queue_chunk = 10;
    mudconf.active_q_chunk  = 10;
    mudconf.sacfactor       = 5;
    mudconf.sacadjust       = -1;
    mudconf.trace_limit     = 200;
    mudconf.float_precision = -1;

    mudconf.autozone        = true;
    mudconf.use_hostname    = true;
    mudconf.clone_copy_cost = false;
    mudconf.dark_sleepers   = true;
    mudconf.ex_flags        = true;
    mudconf.exam_public     = true;
    mudconf.fascist_tport   = false;
    mudconf.idle_wiz_dark   = false;
    mudconf.match_mine      = true;
    mudconf.match_mine_pl   = true;
    mudconf.pemit_players   = false;
    mudconf.pemit_any       = false;
    mudconf.player_listen   = false;
    mudconf.pub_flags       = true;
    mudconf.quiet_look      = true;
    mudconf.quiet_whisper   = true;
    mudconf.quotas          = false;
    mudconf.read_rem_desc   = false;
    mudconf.read_rem_name   = false;
    mudconf.reset_players   = false;
    mudconf.robot_speak     = true;
    mudconf.safe_unowned    = false;
    mudconf.safer_passwords = false;
    mudconf.see_own_dark    = true;
    mudconf.sweep_dark      = false;
    mudconf.switch_df_all   = true;
    mudconf.terse_contents  = true;
    mudconf.terse_exits     = true;
    mudconf.terse_look      = true;
    mudconf.terse_movemsg   = true;
    mudconf.trace_topdown   = true;
    mudconf.use_http        = false;

    // -- ??? Running SC on a non-SC DB may cause problems.
    //
    mudconf.space_compress = true;
    mudconf.allow_guest_from_registered_site = true;
    mudconf.start_room = 0;
    mudconf.start_home = NOTHING;
    mudconf.default_home = NOTHING;
    mudconf.master_room = NOTHING;

    mudconf.exit_parent = NOTHING;
    mudconf.player_parent = NOTHING;
    mudconf.room_parent = NOTHING;
    mudconf.thing_parent = NOTHING;

    for (i = FLAG_WORD1; i <= FLAG_WORD3; i++)
    {
        mudconf.player_flags.word[i] = 0;
        mudconf.room_flags.word[i] = 0;
        mudconf.exit_flags.word[i] = 0;
        mudconf.thing_flags.word[i] = 0;
        mudconf.robot_flags.word[i] = 0;
        mudconf.stripped_flags.word[i] = 0;
    }
    mudconf.robot_flags.word[FLAG_WORD1] |= ROBOT;
    mudconf.stripped_flags.word[FLAG_WORD1] = IMMORTAL | INHERIT | ROYALTY | WIZARD;
    mudconf.stripped_flags.word[FLAG_WORD2] = BLIND | CONNECTED | GAGGED
         | HEAD_FLAG | SLAVE | STAFF | SUSPECT | UNINSPECTED;

    mudconf.vattr_flags = AF_ODARK;
    mux_strncpy(mudconf.mud_name, T("MUX"), 31);
    mux_strncpy(mudconf.one_coin, T("penny"), 31);
    mux_strncpy(mudconf.many_coins, T("pennies"), 31);
    mudconf.timeslice.SetSeconds(1);
    mudconf.cmd_quota_max = 100;
    mudconf.cmd_quota_incr = 1;
    mudconf.rpt_cmdsecs.SetSeconds(120);
    mudconf.max_cmdsecs.SetSeconds(60);
    mudconf.cache_tick_period.SetSeconds(30);
    mudconf.control_flags = 0xffffffff; // Everything for now...
    mudconf.log_options = LOG_ALWAYS | LOG_BUGS | LOG_SECURITY |
        LOG_NET | LOG_LOGIN | LOG_DBSAVES | LOG_CONFIGMODS |
        LOG_SHOUTS | LOG_STARTUP | LOG_WIZARD | LOG_SUSPECTCMDS |
        LOG_PROBLEMS | LOG_PCREATES | LOG_TIMEUSE;
    mudconf.log_info = LOGOPT_TIMESTAMP | LOGOPT_LOC;
    mudconf.markdata[0] = 0x01;
    mudconf.markdata[1] = 0x02;
    mudconf.markdata[2] = 0x04;
    mudconf.markdata[3] = 0x08;
    mudconf.markdata[4] = 0x10;
    mudconf.markdata[5] = 0x20;
    mudconf.markdata[6] = 0x40;
    mudconf.markdata[7] = 0x80;
    mudconf.func_nest_lim = 500;
    mudconf.func_invk_lim = 25000;
    mudconf.wild_invk_lim = 100000;
    mudconf.ntfy_nest_lim = 20;
    mudconf.lock_nest_lim = 20;
    mudconf.parent_nest_lim = 10;
    mudconf.zone_nest_lim = 20;
    mudconf.stack_limit = 50;
    mudconf.cache_names = true;
    mudconf.toad_recipient = -1;
    mudconf.eval_comtitle = true;
    mudconf.run_startup = true;
    mudconf.safe_wipe = false;
    mudconf.destroy_going_now = false;
    mudconf.nStackLimit = 10000;
    mudconf.hook_obj = NOTHING;
    mudconf.help_executor = NOTHING;
    mudconf.global_error_obj = NOTHING;
    mudconf.cache_pages = 40;
    mudconf.mail_per_hour = 50;
    mudconf.vattr_per_hour = 5000;
    mudconf.pcreate_per_hour = 100;
    mudconf.lbuf_size = LBUF_SIZE;
    mudconf.attr_name_charset = 0;
    mudconf.exit_name_charset = 0;
    mudconf.player_name_charset = 0;
    mudconf.room_name_charset = 0;
    mudconf.thing_name_charset = 0;

    mudstate.events_flag = 0;
    mudstate.bReadingConfiguration = false;
    mudstate.bCanRestart = false;
    mudstate.panicking = false;
    mudstate.logging = 0;
    mudstate.epoch = 0;
    mudstate.generation = 0;
    mudstate.curr_executor = NOTHING;
    mudstate.curr_enactor = NOTHING;
    mudstate.shutdown_flag  = false;
    mudstate.attr_next = A_USER_START;
    mudstate.debug_cmd = T("< init >");
    mudstate.curr_cmd  = T("< none >");
    mux_strncpy(mudstate.doing_hdr, T("Doing"), SIZEOF_DOING_STRING-1);
    mudstate.access_list = NULL;
    mudstate.suspect_list = NULL;
    mudstate.badname_head = NULL;
    mudstate.mstat_ixrss[0] = 0;
    mudstate.mstat_ixrss[1] = 0;
    mudstate.mstat_idrss[0] = 0;
    mudstate.mstat_idrss[1] = 0;
    mudstate.mstat_isrss[0] = 0;
    mudstate.mstat_isrss[1] = 0;
    mudstate.mstat_secs[0] = 0;
    mudstate.mstat_secs[1] = 0;
    mudstate.mstat_curr = 0;
    mudstate.iter_alist.data = NULL;
    mudstate.iter_alist.len = 0;
    mudstate.iter_alist.next = NULL;
    mudstate.mod_alist = NULL;
    mudstate.mod_alist_len = 0;
    mudstate.mod_size = 0;
    mudstate.mod_al_id = NOTHING;
    mudstate.olist = NULL;
    mudstate.min_size = 0;
    mudstate.db_top = 0;
    mudstate.db_size = 0;
    mudstate.mail_db_top = 0;
    mudstate.mail_db_size = 0;
    mudstate.freelist = NOTHING;
    mudstate.markbits = NULL;
    mudstate.func_nest_lev = 0;
    mudstate.func_invk_ctr = 0;
    mudstate.wild_invk_ctr = 0;
    mudstate.ntfy_nest_lev = 0;
    mudstate.train_nest_lev = 0;
    mudstate.lock_nest_lev = 0;
    mudstate.zone_nest_num = 0;
    mudstate.pipe_nest_lev = 0;
    mudstate.inpipe = false;
    mudstate.pout = NULL;
    mudstate.poutnew = NULL;
    mudstate.poutbufc = NULL;
    mudstate.poutobj = NOTHING;
    for (i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        mudstate.global_regs[i] = NULL;
    }
#if defined(STUB_SLAVE)
    mudstate.pResultsSet = NULL;
    mudstate.iRow = RS_TOP;
#endif // STUB_SLAVE
    mudstate.nObjEvalNest = 0;
    mudstate.in_loop = 0;
    mudstate.bStackLimitReached = false;
    mudstate.nStackNest = 0;
    mudstate.nHearNest  = 0;
    mudstate.aHelpDesc = NULL;
    mudstate.mHelpDesc = 0;
    mudstate.nHelpDesc = 0;
#if defined(STUB_SLAVE)
    mudstate.pISlaveControl = NULL;
#endif // STUB_SLAVE
    mudstate.pIQueryControl = NULL;
}

// ---------------------------------------------------------------------------
// cf_log_notfound: Log a 'parameter not found' error.
//
void cf_log_notfound(dbref player, const UTF8 *cmd, const UTF8 *thingname, const UTF8 *thing)
{
    if (mudstate.bReadingConfiguration)
    {
        STARTLOG(LOG_STARTUP, "CNF", "NFND");
        Log.tinyprintf("%s: %s %s not found", cmd, thingname, thing);
        ENDLOG;
    }
    else
    {
        notify(player, tprintf("%s %s not found", thingname, thing));
    }
}

// ---------------------------------------------------------------------------
// cf_log_syntax: Log a syntax error.
//
void DCL_CDECL cf_log_syntax(dbref player, UTF8 *cmd, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    UTF8 *buf = alloc_lbuf("cf_log_syntax");
    mux_vsnprintf(buf, LBUF_SIZE, fmt, ap);
    if (mudstate.bReadingConfiguration)
    {
        STARTLOG(LOG_STARTUP, "CNF", "SYNTX")
        log_text(cmd);
        log_text(T(": "));
        log_text(buf);
        ENDLOG;
    }
    else
    {
        notify(player, buf);
    }
    free_lbuf(buf);
    va_end(ap);
}

// ---------------------------------------------------------------------------
// cf_status_from_succfail: Return command status from succ and fail info
//
static int cf_status_from_succfail(dbref player, UTF8 *cmd, int success, int failure)
{
    UTF8 *buff;

    // If any successes, return SUCCESS(0) if no failures or
    // PARTIAL_SUCCESS(1) if any failures.
    //
    if (success > 0)
        return ((failure == 0) ? 0 : 1);

    // No successes.  If no failures indicate nothing done. Always return
    // FAILURE(-1)
    //
    if (failure == 0)
    {
        if (mudstate.bReadingConfiguration)
        {
            STARTLOG(LOG_STARTUP, "CNF", "NDATA")
            buff = alloc_lbuf("cf_status_from_succfail.LOG");
            mux_sprintf(buff, LBUF_SIZE, "%s: Nothing to set", cmd);
            log_text(buff);
            free_lbuf(buff);
            ENDLOG
        }
        else
        {
            notify(player, T("Nothing to set"));
        }
    }
    return -1;
}

//---------------------------------------------------------------------------
// cf_rlevel
//

#ifdef REALITY_LVLS

CF_HAND(cf_rlevel)
{
    CONFDATA *mc = (CONFDATA *)vp;
    int i;

    if (mc->no_levels >= 32)
    {
        return 1;
    }
    for (i=0; *str && !mux_isspace[*str]; ++str)
    {
        if (i < 8)
        {
            mc->reality_level[mc->no_levels].name[i++] = *str;
        }
    }
    mc->reality_level[mc->no_levels].name[i] = '\0';
    mc->reality_level[mc->no_levels].value = 1;
    strcpy((char *)mc->reality_level[mc->no_levels].attr, "DESC");
    for (; *str && mux_isspace[*str]; ++str)
    {
        ; // Nothing.
    }
    for (i=0; *str && mux_isdigit[*str]; ++str)
    {
        i = i * 10 + (*str - '0');
    }
    if (i)
    {
        mc->reality_level[mc->no_levels].value = (RLEVEL) i;
    }
    for (; *str && mux_isspace[*str]; ++str)
    {
        ; // Nothing.
    }
    if (*str)
    {
        strncpy((char *)mc->reality_level[mc->no_levels].attr, (char *)str, 32);
    }
    mc->no_levels++;
    return 0;
}
#endif // REALITY_LVLS

// ---------------------------------------------------------------------------
// cf_int_array: Setup array of integers.
//
static CF_HAND(cf_int_array)
{
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(cmd);

    int *aPorts = NULL;
    try
    {
        aPorts = new int[nExtra];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL == aPorts)
    {
        cf_log_syntax(player, cmd, "Out of memory.");
        return -1;
    }

    unsigned int nPorts = 0;
    UTF8 *p;
    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, str);
    mux_strtok_ctl(&tts, T(" \t\n\r"));
    while (  (p = mux_strtok_parse(&tts)) != NULL
          && nPorts < nExtra)
    {
        int unused;
        if (is_integer(p, &unused))
        {
            aPorts[nPorts++] = mux_atol(p);
        }
    }

    IntArray *pia = (IntArray *)vp;
    if (nPorts)
    {
        int *q = NULL;
        try
        {
            q = new int[nPorts];
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == q)
        {
            cf_log_syntax(player, cmd, "Out of memory.");
            return -1;
        }

        if (pia->pi)
        {
            delete [] pia->pi;
            pia->pi = NULL;
        }

        pia->pi = q;
        pia->n = nPorts;
        for (unsigned int i = 0; i < nPorts; i++)
        {
            pia->pi[i] = aPorts[i];
        }
    }
    delete [] aPorts;
    return 0;
}

// ---------------------------------------------------------------------------
// cf_int: Set integer parameter.
//
static CF_HAND(cf_int)
{
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(cmd);

    // Copy the numeric value to the parameter.
    //
    *vp = mux_atol(str);
    return 0;
}

// ---------------------------------------------------------------------------
// cf_dbref: Set dbref parameter....looking for an ignoring the leading '#'.
//
static CF_HAND(cf_dbref)
{
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(cmd);

    UTF8 *p = str;
    while (mux_isspace(*p))
    {
        p++;
    }
    if (*p == '#')
    {
        p++;
    }

    // Copy the numeric value to the parameter.
    //
    *vp = mux_atol(p);
    return 0;
}

// ---------------------------------------------------------------------------
// cf_seconds: Set CLinearTimeDelta in units of seconds.
//
static CF_HAND(cf_seconds)
{
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(cmd);

    CLinearTimeDelta *pltd = (CLinearTimeDelta *)vp;
    pltd->SetSecondsString(str);
    return 0;
}

// ---------------------------------------------------------------------------
// cf_bool: Set boolean parameter.
//
static NAMETAB bool_names[] =
{
    {T("true"),    1,  0,  true},
    {T("false"),   1,  0,  false},
    {T("yes"),     1,  0,  true},
    {T("no"),      1,  0,  false},
    {T("1"),       1,  0,  true},
    {T("0"),       1,  0,  false},
    {(UTF8 *)NULL, 0,  0,  0}
};

static CF_HAND(cf_bool)
{
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);

    int i;
    if (!search_nametab(GOD, bool_names, str, &i))
    {
        cf_log_notfound(player, cmd, T("Value"), str);
        return -1;
    }
    bool *pb = (bool *)vp;
    *pb = isTRUE(i);
    return 0;
}

// ---------------------------------------------------------------------------
// cf_option: Select one option from many choices.
//
static CF_HAND(cf_option)
{
    UNUSED_PARAMETER(nExtra);

    int i;
    if (!search_nametab(GOD, (NAMETAB *)pExtra, str, &i))
    {
        cf_log_notfound(player, cmd, T("Value"), str);
        return -1;
    }
    *vp = i;
    return 0;
}

// ---------------------------------------------------------------------------
// cf_string: Set string parameter.
//
static CF_HAND(cf_string)
{
    UNUSED_PARAMETER(pExtra);

    UTF8 *pc = (UTF8 *)vp;

    // The following should never happen because extra is always a non-zero
    // constant in the config table.
    //
    if (nExtra <= 0)
    {
        return 1;
    }

    // Copy the string to the buffer if it is not too big.
    //
    int retval = 0;
    size_t nStr = strlen((char *)str);
    if (nStr >= nExtra)
    {
        nStr = nExtra - 1;
        if (mudstate.bReadingConfiguration)
        {
            STARTLOG(LOG_STARTUP, "CNF", "NFND");
            Log.tinyprintf("%s: String truncated", cmd);
            ENDLOG;
        }
        else
        {
            notify(player, T("String truncated"));
        }
        retval = 1;
    }
    memcpy(pc, str, nStr+1);
    pc[nStr] = '\0';

    if (pc == mudconf.mud_name)
    {
        // We are changing the name of the MUD. Form a prefix from the
        // mudname and let the logger know.
        //
        UTF8 *buff = alloc_sbuf("cf_string.prefix");
        UTF8 *p = buff;
        UTF8 *q = strip_color(mudconf.mud_name);
        size_t nLen = 0;
        while (  *q
              && nLen < SBUF_SIZE)
        {
            if (mux_isalnum(*q))
            {
                *p++ = *q;
                nLen++;
            }
            q++;
        }
        *p = '\0';
        Log.SetPrefix(buff);
        free_sbuf(buff);
    }
    return retval;
}

// ---------------------------------------------------------------------------
// cf_string_dyn: Set string parameter using dynamically allocated memory.
//
static CF_HAND(cf_string_dyn)
{
    UNUSED_PARAMETER(pExtra);

    UTF8 **ppc = (UTF8 **)vp;

    // Allocate memory for buffer and copy string to it. If nExtra is non-zero,
    // then there is a size limitation as well.
    //
    int retval = 0;
    size_t nStr = strlen((char *)str);
    if (nExtra && nStr >= nExtra)
    {
        nStr = nExtra - 1;
        if (mudstate.bReadingConfiguration)
        {
            STARTLOG(LOG_STARTUP, "CNF", "NFND");
            Log.tinyprintf("%s: String truncated", cmd);
            ENDLOG;
        }
        else
        {
            notify(player, T("String truncated"));
        }
        retval = 1;
    }
    UTF8 *confbuff = StringCloneLen(str, nStr);

    // Free previous memory for buffer.
    //
    if (*ppc != NULL)
    {
        MEMFREE(*ppc);
    }
    *ppc = confbuff;

    return retval;
}

// ---------------------------------------------------------------------------
// cf_alias: define a generic hash table alias.
//
static CF_HAND(cf_alias)
{
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);

    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, str);
    mux_strtok_ctl(&tts, T(" \t=,"));
    UTF8 *alias = mux_strtok_parse(&tts);
    UTF8 *orig = mux_strtok_parse(&tts);

    if (orig)
    {
        size_t nCased;
        UTF8 *pCased = mux_strupr(orig, nCased);
        void *cp = hashfindLEN(pCased, nCased, (CHashTable *) vp);
        if (NULL == cp)
        {
            cf_log_notfound(player, cmd, T("Entry"), orig);
            return -1;
        }

        pCased = mux_strupr(alias, nCased);
        if (!hashfindLEN(pCased, nCased, (CHashTable *) vp))
        {
            hashaddLEN(pCased, nCased, cp, (CHashTable *) vp);
        }
        return 0;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// cf_name: rename a generic hash table entry
//
static CF_HAND(cf_name)
{
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);

    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, str);
    mux_strtok_ctl(&tts, T(" \t=,"));
    UTF8 *oldname = mux_strtok_parse(&tts);
    UTF8 *newname = mux_strtok_parse(&tts);

    if (  NULL != oldname
       && '\0' != oldname[0]
       && NULL != newname
       && '\0' != newname[0])
    {
        size_t nCased;
        UTF8 *pCased = mux_strupr(oldname, nCased);
        void *cp = hashfindLEN(pCased, nCased, (CHashTable *) vp);
        if (NULL == cp)
        {
            cf_log_notfound(player, cmd, T("Entry"), oldname);
            return -1;
        }

        size_t bCased = nCased;
        UTF8 Buffer[LBUF_SIZE];
        mux_strncpy(Buffer, pCased, LBUF_SIZE-1);
        pCased = mux_strupr(newname, nCased);
        if (!hashfindLEN(pCased, nCased, (CHashTable *) vp))
        {
            hashaddLEN(pCased, nCased, cp, (CHashTable *) vp);
            hashdeleteLEN(Buffer, bCased, (CHashTable *) vp);
            return 0;
        }
    }
    return -1;
}


// ---------------------------------------------------------------------------
// cf_flagalias: define a flag alias.
//
static CF_HAND(cf_flagalias)
{
    UNUSED_PARAMETER(vp);
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);

    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, str);
    mux_strtok_ctl(&tts, T(" \t=,"));
    UTF8 *alias = mux_strtok_parse(&tts);
    UTF8 *orig = mux_strtok_parse(&tts);

    if (  NULL == alias
       || '\0' == alias[0]
       || NULL == orig
       || '\0' == orig[0])
    {
        return -1;
    }

    bool success = false;
    int  nName;
    bool bValid;
    void *cp;
    UTF8 *pName = MakeCanonicalFlagName(orig, &nName, &bValid);
    if (bValid)
    {
        cp = hashfindLEN(pName, nName, &mudstate.flags_htab);
        if (cp)
        {
            pName = MakeCanonicalFlagName(alias, &nName, &bValid);
            if (bValid)
            {
                if (!hashfindLEN(pName, nName, &mudstate.flags_htab))
                {
                    hashaddLEN(pName, nName, cp, &mudstate.flags_htab);
                    success = true;
               }
            }
        }
    }
    if (!success)
    {
        cf_log_notfound(player, cmd, T("Flag"), orig);
    }
    return (success ? 0 : -1);
}

// ---------------------------------------------------------------------------
// cf_poweralias: define a power alias.
//
static CF_HAND(cf_poweralias)
{
    UNUSED_PARAMETER(vp);
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);

    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, str);
    mux_strtok_ctl(&tts, T(" \t=,"));
    UTF8 *alias = mux_strtok_parse(&tts);
    UTF8 *orig = mux_strtok_parse(&tts);

    if (  NULL == alias
       || '\0' == alias[0]
       || NULL == orig
       || '\0' == orig[0])
    {
        return -1;
    }

    bool success = false;
    int  nName;
    bool bValid;
    void *cp;
    UTF8 *pName = MakeCanonicalFlagName(orig, &nName, &bValid);
    if (bValid)
    {
        cp = hashfindLEN(pName, nName, &mudstate.powers_htab);
        if (cp)
        {
            pName = MakeCanonicalFlagName(alias, &nName, &bValid);
            if (bValid)
            {
                hashaddLEN(pName, nName, cp, &mudstate.powers_htab);
                success = true;
            }
        }
    }
    if (!success)
    {
        cf_log_notfound(player, cmd, T("Power"), orig);
    }
    return (success ? 0 : -1);
}

#if 0
// ---------------------------------------------------------------------------
// cf_or_in_bits: OR in bits from namelist to a word.
//
static CF_HAND(cf_or_in_bits)
{
    UNUSED_PARAMETER(nExtra);

    int f, success, failure;

    // Walk through the tokens.
    //
    success = failure = 0;
    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, str);
    mux_strtok_ctl(&tts, " \t");
    UTF8 *sp = mux_strtok_parse(&tts);
    while (sp != NULL)
    {
        // Set the appropriate bit.
        //
        if (search_nametab(GOD, (NAMETAB *)pExtra, sp, &f))
        {
            *vp |= f;
            success++;
        }
        else
        {
            cf_log_notfound(player, cmd, "Entry", sp);
            failure++;
        }

        // Get the next token.
        //
        sp = mux_strtok_parse(&tts);
    }
    return cf_status_from_succfail(player, cmd, success, failure);
}
#endif

// ---------------------------------------------------------------------------
// cf_modify_bits: set or clear bits in a flag word from a namelist.
//
CF_HAND(cf_modify_bits)
{
    UNUSED_PARAMETER(nExtra);

    int f, success, failure;
    bool negate;

    // Walk through the tokens.
    //
    success = failure = 0;
    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, str);
    mux_strtok_ctl(&tts, T(" \t"));
    UTF8 *sp = mux_strtok_parse(&tts);
    while (sp != NULL)
    {
        // Check for negation.
        //
        negate = false;
        if (*sp == '!')
        {
            negate = true;
            sp++;
        }

        // Set or clear the appropriate bit.
        //
        if (search_nametab(GOD, (NAMETAB *)pExtra, sp, &f))
        {
            if (negate)
                *vp &= ~f;
            else
                *vp |= f;
            success++;
        }
        else
        {
            cf_log_notfound(player, cmd, T("Entry"), sp);
            failure++;
        }

        // Get the next token.
        //
        sp = mux_strtok_parse(&tts);
    }
    return cf_status_from_succfail(player, cmd, success, failure);
}

#if 0
// ---------------------------------------------------------------------------
// cf_set_bits: Clear flag word and then set specified bits from namelist.
//
static CF_HAND(cf_set_bits)
{
    UNUSED_PARAMETER(nExtra);

    int f, success, failure;

    // Walk through the tokens
    //
    success = failure = 0;
    *vp = 0;

    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, str);
    mux_strtok_ctl(&tts, " \t");
    UTF8 *sp = mux_strtok_parse(&tts);
    while (sp != NULL)
    {
        // Set the appropriate bit.
        //
        if (search_nametab(GOD, (NAMETAB *)pExtra, sp, &f))
        {
            *vp |= f;
            success++;
        }
        else
        {
            cf_log_notfound(player, cmd, "Entry", sp);
            failure++;
        }

        // Get the next token.
        //
        sp = mux_strtok_parse(&tts);
    }
    return cf_status_from_succfail(player, cmd, success, failure);
}
#endif

// ---------------------------------------------------------------------------
// cf_set_flags: Clear flag word and then set from a flags htab.
//
static CF_HAND(cf_set_flags)
{
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);

    int success, failure;

    // Walk through the tokens.
    //
    success = failure = 0;
    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, str);
    mux_strtok_ctl(&tts, T(" \t"));
    UTF8 *sp = mux_strtok_parse(&tts);
    FLAGSET *fset = (FLAGSET *) vp;

    while (sp != NULL)
    {
        // Canonical Flag Name.
        //
        int  nName;
        bool bValid;
        UTF8 *pName = MakeCanonicalFlagName(sp, &nName, &bValid);
        FLAGNAMEENT *fp = NULL;
        if (bValid)
        {
            fp = (FLAGNAMEENT *)hashfindLEN(pName, nName, &mudstate.flags_htab);
        }
        if (fp != NULL)
        {
            // Set the appropriate bit.
            //
            if (success == 0)
            {
                for (int i = FLAG_WORD1; i <= FLAG_WORD3; i++)
                {
                    (*fset).word[i] = 0;
                }
            }
            FLAGBITENT *fbe = fp->fbe;
            if (fp->bPositive)
            {
                (*fset).word[fbe->flagflag] |= fbe->flagvalue;
            }
            else
            {
                (*fset).word[fbe->flagflag] &= ~(fbe->flagvalue);
            }
            success++;
        }
        else
        {
            cf_log_notfound(player, cmd, T("Entry"), sp);
            failure++;
        }

        // Get the next token
        //
        sp = mux_strtok_parse(&tts);
    }
    if ((success == 0) && (failure == 0))
    {
        for (int i = FLAG_WORD1; i <= FLAG_WORD3; i++)
        {
            (*fset).word[i] = 0;
        }
        return 0;
    }
    if (success > 0)
    {
        return ((failure == 0) ? 0 : 1);
    }
    return -1;
}

// ---------------------------------------------------------------------------
// cf_badname: Disallow use of player name/alias.
//
static CF_HAND(cf_badname)
{
    UNUSED_PARAMETER(vp);
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(cmd);

    if (nExtra)
    {
        badname_remove(str);
    }
    else
    {
        badname_add(str);
    }
    return 0;
}

typedef struct
{
    int    nShift;
    UINT32 maxValue;
    size_t maxOctLen;
    size_t maxDecLen;
    size_t maxHexLen;
} DECODEIPV4;

static bool DecodeN(int nType, size_t len, const UTF8 *p, in_addr_t *pu32)
{
    static DECODEIPV4 DecodeIPv4Table[4] =
    {
        { 8,         255UL,  3,  3, 2 },
        { 16,      65535UL,  6,  5, 4 },
        { 24,   16777215UL,  8,  8, 6 },
        { 32, 4294967295UL, 11, 10, 8 }
    };

    *pu32  = (*pu32 << DecodeIPv4Table[nType].nShift) & 0xFFFFFFFFUL;
    if (len == 0)
    {
        return false;
    }
    in_addr_t ul = 0;
    in_addr_t ul2;
    if (  len >= 3
       && p[0] == '0'
       && (  'x' == p[1]
          || 'X' == p[1]))
    {
        // Hexadecimal Path
        //
        // Skip the leading zeros.
        //
        p += 2;
        len -= 2;
        while (*p == '0' && len)
        {
            p++;
            len--;
        }
        if (len > DecodeIPv4Table[nType].maxHexLen)
        {
            return false;
        }
        while (len)
        {
            UTF8 ch = *p;
            ul2 = ul;
            ul  = (ul << 4) & 0xFFFFFFFFUL;
            if (ul < ul2)
            {
                // Overflow
                //
                return false;
            }
            if ('0' <= ch && ch <= '9')
            {
                ul |= ch - '0';
            }
            else if ('A' <= ch && ch <= 'F')
            {
                ul |= ch - 'A';
            }
            else if ('a' <= ch && ch <= 'f')
            {
                ul |= ch - 'a';
            }
            else
            {
                return false;
            }
            p++;
            len--;
        }
    }
    else if (len >= 1 && p[0] == '0')
    {
        // Octal Path
        //
        // Skip the leading zeros.
        //
        p++;
        len--;
        while (*p == '0' && len)
        {
            p++;
            len--;
        }
        if (len > DecodeIPv4Table[nType].maxOctLen)
        {
            return false;
        }
        while (len)
        {
            UTF8 ch = *p;
            ul2 = ul;
            ul  = (ul << 3) & 0xFFFFFFFFUL;
            if (ul < ul2)
            {
                // Overflow
                //
                return false;
            }
            if ('0' <= ch && ch <= '7')
            {
                ul |= ch - '0';
            }
            else
            {
                return false;
            }
            p++;
            len--;
        }
    }
    else
    {
        // Decimal Path
        //
        if (len > DecodeIPv4Table[nType].maxDecLen)
        {
            return false;
        }
        while (len)
        {
            UTF8 ch = *p;
            ul2 = ul;
            ul  = (ul * 10) & 0xFFFFFFFFUL;
            if (ul < ul2)
            {
                // Overflow
                //
                return false;
            }
            ul2 = ul;
            if ('0' <= ch && ch <= '9')
            {
                ul += ch - '0';
            }
            else
            {
                return false;
            }
            if (ul < ul2)
            {
                // Overflow
                //
                return false;
            }
            p++;
            len--;
        }
    }
    if (ul > DecodeIPv4Table[nType].maxValue)
    {
        return false;
    }
    *pu32 |= ul;
    return true;
}

// ---------------------------------------------------------------------------
// MakeCanonicalIPv4: inet_addr() does not do reasonable checking for sane
// syntax on all platforms. On certain operating systems, if passed less than
// four octets, it will cause a segmentation violation. Furthermore, there is
// confusion between return values for valid input "255.255.255.255" and
// return values for invalid input (INADDR_NONE as -1). To overcome these
// problems, it appears necessary to re-implement inet_addr() with a different
// interface.
//
// n8.n8.n8.n8  Class A format. 0 <= n8 <= 255.
//
// Supported Berkeley IP formats:
//
//    n8.n8.n16  Class B 128.net.host format. 0 <= n16 <= 65535.
//    n8.n24     Class A net.host format. 0 <= n24 <= 16777215.
//    n32        Single 32-bit number. 0 <= n32 <= 4294967295.
//
// Each element may be expressed in decimal, octal or hexadecimal. '0' is the
// octal prefix. '0x' or '0X' is the hexadecimal prefix. Otherwise the number
// is taken as decimal.
//
//    08  Octal
//    0x8 Hexadecimal
//    0X8 Hexadecimal
//    8   Decimal
//
static bool MakeCanonicalIPv4(const UTF8 *str, in_addr_t *pnIP)
{
    *pnIP = 0;
    if (!str)
    {
        return false;
    }

    // Skip leading spaces.
    //
    const UTF8 *q = str;
    while (*q == ' ')
    {
        q++;
    }

    const UTF8 *p = (UTF8 *)strchr((char *)q, '.');
    int n = 0;
    while (p)
    {
        // Decode
        //
        n++;
        if (n > 3)
        {
            return false;
        }
        if (!DecodeN(0, p-q, q, pnIP))
        {
            return false;
        }
        q = p + 1;
        p = (UTF8 *)strchr((char *)q, '.');
    }

    // Decode last element.
    //
    size_t len = strlen((char *)q);
    if (!DecodeN(3-n, len, q, pnIP))
    {
        return false;
    }
    *pnIP = htonl(*pnIP);
    return true;
}

// Given a host-ordered mask, this function will determine whether it is a
// valid one. Valid masks consist of a N-bit sequence of '1' bits followed by
// a (32-N)-bit sequence of '0' bits, where N is 0 to 32.
//
static bool isValidSubnetMask(in_addr_t ulMask)
{
    in_addr_t ulTest = 0xFFFFFFFFUL;
    for (int i = 0; i <= 32; i++)
    {
        if (ulMask == ulTest)
        {
            return true;
        }
        ulTest = (ulTest << 1) & 0xFFFFFFFFUL;
    }
    return false;
}

// ---------------------------------------------------------------------------
// cf_site: Update site information

static CF_HAND(cf_site)
{
    UNUSED_PARAMETER(pExtra);

    SITE **ppv = (SITE **)vp;
    struct in_addr addr_num, mask_num;
    in_addr_t ulMask, ulNetBits;

    UTF8 *addr_txt;
    UTF8 *mask_txt = (UTF8 *)strchr((char *)str, '/');
    if (!mask_txt)
    {
        // Standard IP range and netmask notation.
        //
        MUX_STRTOK_STATE tts;
        mux_strtok_src(&tts, str);
        mux_strtok_ctl(&tts, T(" \t=,"));
        addr_txt = mux_strtok_parse(&tts);
        mask_txt = NULL;
        if (addr_txt)
        {
            mask_txt = mux_strtok_parse(&tts);
        }
        if (!addr_txt || !*addr_txt || !mask_txt || !*mask_txt)
        {
            cf_log_syntax(player, cmd, "Missing host address or mask.", "");
            return -1;
        }
        if (  !MakeCanonicalIPv4(mask_txt, &ulNetBits)
           || !isValidSubnetMask(ulMask = ntohl(ulNetBits)))
        {
            cf_log_syntax(player, cmd, "Malformed mask address: %s", mask_txt);
            return -1;
        }
        mask_num.s_addr = ulNetBits;
    }
    else
    {
        // RFC 1517, 1518, 1519, 1520: CIDR IP prefix notation
        //
        addr_txt = str;
        *mask_txt++ = '\0';
        if (!is_integer(mask_txt, NULL))
        {
            cf_log_syntax(player, cmd, "Mask field (%s) in CIDR IP prefix is not numeric.", mask_txt);
            return -1;
        }
        int mask_bits = mux_atol(mask_txt);
        if (  mask_bits < 0
           || 32 < mask_bits)
        {
            cf_log_syntax(player, cmd, "Mask bits (%d) in CIDR IP prefix out of range.", mask_bits);
            return -1;
        }
        else
        {
            // << [0,31] works. << 32 is problematic on some systems.
            //
            ulMask = 0;
            if (mask_bits > 0)
            {
                ulMask = (0xFFFFFFFFUL << (32 - mask_bits)) & 0xFFFFFFFFUL;
            }
            mask_num.s_addr = htonl(ulMask);
        }
    }
    if (!MakeCanonicalIPv4(addr_txt, &ulNetBits))
    {
        cf_log_syntax(player, cmd, "Malformed host address: %s", addr_txt);
        return -1;
    }
    addr_num.s_addr = ulNetBits;
    in_addr_t ulAddr = ntohl(addr_num.s_addr);

    if (ulAddr & ~ulMask)
    {
        // The given subnet address contains 'one' bits which are outside
        // the given subnet mask. If we don't clear these bits, they will
        // interfere with the subnet tests in site_check. The subnet spec
        // would be defunct and useless.
        //
        cf_log_syntax(player, cmd, "Non-zero host address bits outside the subnet mask (fixed): %s %s", addr_txt, mask_txt);
        ulAddr &= ulMask;
        addr_num.s_addr = htonl(ulAddr);
    }

    SITE *head = *ppv;

    // Parse the access entry and allocate space for it.
    //
    SITE *site = NULL;
    try
    {
        site = new SITE;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL == site)
    {
        cf_log_syntax(player, cmd, "Out of memory.");
        return -1;
    }

    // Initialize the site entry.
    //
    site->address.s_addr = addr_num.s_addr;
    site->mask.s_addr = mask_num.s_addr;
    site->flag = nExtra;
    site->next = NULL;

    // Link in the entry. Link it at the start if not initializing, at the
    // end if initializing. This is so that entries in the config file are
    // processed as you would think they would be, while entries made while
    // running are processed first.
    //
    if (mudstate.bReadingConfiguration)
    {
        if (head == NULL)
        {
            *ppv = site;
        }
        else
        {
            SITE *last;
            for (last = head; last->next; last = last->next)
            {
                // Nothing
            }
            last->next = site;
        }
    }
    else
    {
        site->next = head;
        *ppv = site;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// cf_helpfile, cf_raw_helpfile: Add help files and their corresponding
// command.
//
static int add_helpfile(dbref player, UTF8 *cmd, UTF8 *str, bool bEval)
{
    // Parse the two arguments.
    //
    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, str);
    mux_strtok_ctl(&tts, T(" \t\n\r"));

    UTF8 *pCmdName = mux_strtok_parse(&tts);
    UTF8 *pBase = mux_strtok_parse(&tts);
    if (pBase == NULL)
    {
        cf_log_syntax(player, cmd, "Missing path for helpfile %s", pCmdName);
        return -1;
    }
    if (  pCmdName[0] == '_'
       && pCmdName[1] == '_')
    {
        cf_log_syntax(player, cmd,
            "Helpfile %s would conflict with the use of @addcommand.",
            pCmdName);
        return -1;
    }
    if (SBUF_SIZE <= strlen((char *)pBase))
    {
        cf_log_syntax(player, cmd, "Helpfile '%s' filename too long", pBase);
        return -1;
    }

    // Allocate an empty place in the table of help file hashes.
    //
    if (mudstate.aHelpDesc == NULL)
    {
        mudstate.mHelpDesc = 4;
        mudstate.nHelpDesc = 0;
        try
        {
            mudstate.aHelpDesc = new HELP_DESC[mudstate.mHelpDesc];
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == mudstate.aHelpDesc)
        {
            cf_log_syntax(player, cmd, "Out of memory.");
            return -1;
        }
    }
    else if (mudstate.mHelpDesc <= mudstate.nHelpDesc)
    {
        int newsize = mudstate.mHelpDesc + 4;
        HELP_DESC *q = NULL;
        try
        {
            q = new HELP_DESC[newsize];
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == mudstate.aHelpDesc)
        {
            cf_log_syntax(player, cmd, "Out of memory.");
            return -1;
        }

        memset(q, 0, sizeof(HELP_DESC)*newsize);
        memcpy(q, mudstate.aHelpDesc, sizeof(HELP_DESC)*mudstate.mHelpDesc);
        delete [] mudstate.aHelpDesc;
        mudstate.aHelpDesc = q;
        mudstate.mHelpDesc = newsize;
    }

    // Build HELP_DESC
    //
    HELP_DESC *pDesc = mudstate.aHelpDesc + mudstate.nHelpDesc;
    pDesc->CommandName = StringClone(pCmdName);
    pDesc->ht = NULL;
    pDesc->pBaseFilename = StringClone(pBase);
    pDesc->bEval = bEval;

    // Build up Command Entry.
    //
    CMDENT_ONE_ARG *cmdp = NULL;
    try
    {
        cmdp = new CMDENT_ONE_ARG;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL != cmdp)
    {
        cmdp->callseq = CS_ONE_ARG;
        cmdp->cmdname = StringClone(pCmdName);
        cmdp->extra = mudstate.nHelpDesc;
        cmdp->handler = do_help;
        cmdp->hookmask = 0;
        cmdp->perms = CA_PUBLIC;
        cmdp->switches = NULL;

        // TODO: If a command is deleted with one or both of the two
        // hashdeleteLEN() calls below, what guarantee do we have that parts
        // of the command weren't dynamically allocated.  This might leak
        // memory.
        //
        const UTF8 *p = cmdp->cmdname;
        hashdeleteLEN(p, strlen((const char *)p), &mudstate.command_htab);
        hashaddLEN(p, strlen((const char *)p), cmdp, &mudstate.command_htab);

        p = tprintf("__%s", cmdp->cmdname);
        hashdeleteLEN(p, strlen((const char *)p), &mudstate.command_htab);
        hashaddLEN(p, strlen((const char *)p), cmdp, &mudstate.command_htab);
    }
    else
    {
        ISOUTOFMEMORY(cmdp);
    }

    mudstate.nHelpDesc++;

    return 0;
}

static CF_HAND(cf_helpfile)
{
    UNUSED_PARAMETER(vp);
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);

    return add_helpfile(player, cmd, str, true);
}

static CF_HAND(cf_raw_helpfile)
{
    UNUSED_PARAMETER(vp);
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);

    return add_helpfile(player, cmd, str, false);
}

// @hook: run softcode before or after running a hardcode command, or softcode access.
// Original idea from TinyMUSH 3, code from RhostMUSH.
// Used with express permission of RhostMUSH developers.
// Bludgeoned into MUX by Jake Nelson 7/2002.
//
static NAMETAB hook_names[] =
{
    {T("after"),      3, 0, HOOK_AFTER},
    {T("before"),     3, 0, HOOK_BEFORE},
    {T("fail"),       3, 0, HOOK_AFAIL},
    {T("ignore"),     3, 0, HOOK_IGNORE},
    {T("igswitch"),   3, 0, HOOK_IGSWITCH},
    {T("permit"),     3, 0, HOOK_PERMIT},
    {T("args"),       3, 0, HOOK_ARGS},
    {(UTF8 *)NULL,    0, 0, 0}
};

static CF_HAND(cf_hook)
{
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(cmd);

    UTF8 *hookcmd, *hookptr, playbuff[201];
    int hookflg;
    CMDENT *cmdp;

    int retval = -1;
    memset(playbuff, '\0', sizeof(playbuff));
    mux_strncpy(playbuff, str, 200);
    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, playbuff);
    mux_strtok_ctl(&tts, T(" \t"));
    hookcmd = mux_strtok_parse(&tts);
    if (hookcmd != NULL)
    {
       cmdp = (CMDENT *)hashfindLEN(hookcmd, strlen((char *)hookcmd), &mudstate.command_htab);
    }
    else
    {
       return retval;
    }
    if (!cmdp)
    {
       return retval;
    }

    *vp = cmdp->hookmask;
    mux_strncpy(playbuff, str, 200);
    hookptr = mux_strtok_parse(&tts);
    while (hookptr != NULL)
    {
       if (  hookptr[0] == '!'
          && hookptr[1] != '\0')
       {
          if (search_nametab(GOD, hook_names, hookptr+1, &hookflg))
          {
             retval = 0;
             *vp = *vp & ~hookflg;
          }
       }
       else
       {
          if (search_nametab(GOD, hook_names, hookptr, &hookflg))
          {
             retval = 0;
             *vp = *vp | hookflg;
          }
       }
       hookptr = mux_strtok_parse(&tts);
    }
    cmdp->hookmask = *vp;
    return retval;
}

static CF_HAND(cf_module)
{
    UNUSED_PARAMETER(vp);
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);

    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, str);
    mux_strtok_ctl(&tts, T(" \t"));
    UTF8 *modname = mux_strtok_parse(&tts);
    UTF8 *modtype = mux_strtok_parse(&tts);

    enum MODTYPE
    {
        eInProc = 0,
        eLocal
    } eType = eInProc;

    if (NULL == modname)
    {
        cf_log_syntax(player, cmd, "Module name is missing.");
        return -1;
    }

    if (NULL == modtype)
    {
        eType = eInProc;
    }
    else if (strcmp((char *)modtype, "inproc") == 0)
    {
        eType = eInProc;
    }
    else if (  strcmp((char *)modtype, "local") == 0
            || strcmp((char *)modtype, "slave") == 0)
    {
        eType = eLocal;
    }
    else
    {
        cf_log_syntax(player, cmd, "load type is invalid.");
        return -1;
    }

#if defined(STUB_SLAVE)
    if (  NULL == mudstate.pISlaveControl
       && eLocal == eType)
    {
        cf_log_syntax(player, cmd, "No StubSlave management interface available.");
        return -1;
    }
#else // STUB_SLAVE
    if (eLocal == eType)
    {
        cf_log_syntax(player, cmd, "StubSlave is not enabled.");
        return -1;
    }
#endif // STUB_SLAVE

    MUX_RESULT mr = MUX_S_OK;
    if ('!' == str[0])
    {
        if (eInProc == eType)
        {
            mr = mux_RemoveModule(str+1);
        }
#if defined(STUB_SLAVE)
        else
        {
            mr = mudstate.pISlaveControl->RemoveModule(str+1);
        }
#endif // STUB_SLAVE
    }
    else
    {
        UTF8 *buffer = alloc_lbuf("cf_module");
#ifdef WIN32
        size_t n;
        mux_sprintf(buffer, LBUF_SIZE, ".\\bin\\%s.dll", str);
        UTF16 *filename = ConvertFromUTF8ToUTF16(buffer, &n);
#else
        mux_sprintf(buffer, LBUF_SIZE, "./bin/%s.so", str);
        UTF8 *filename = buffer;
#endif
        if (eInProc == eType)
        {
            mr = mux_AddModule(str, filename);
        }
#if defined(STUB_SLAVE)
        else
        {
            mr = mudstate.pISlaveControl->AddModule(str, filename);
        }
#endif // STUB_SLAVE
        free_lbuf(buffer);
    }

    if (MUX_FAILED(mr))
    {
        cf_log_notfound(player, cmd, T("Module"), str);
        return -1;
    }
    else
    {
        return 0;
    }
}

// ---------------------------------------------------------------------------
// cf_include: Read another config file.  Only valid during startup.
//
static CF_HAND(cf_include)
{
    UNUSED_PARAMETER(vp);
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);

    if (!mudstate.bReadingConfiguration)
    {
        return -1;
    }

    FILE *fp;
    if (!mux_fopen(&fp, str, T("rb")))
    {
        cf_log_notfound(player, cmd, T("Config file"), str);
        return -1;
    }
    DebugTotalFiles++;

    UTF8 *buf = alloc_lbuf("cf_include");
    if (NULL == fgets((char *)buf, LBUF_SIZE, fp))
    {
        return 0;
    }
    while (!feof(fp))
    {
        UTF8 *zp = buf;

        // Remove comments.  Anything after the '#' is a comment except if it
        // matches:  whitespace + '#' + digit.
        //
        while (*zp != '\0')
        {
            if (  *zp == '#'
               && (  zp <= buf
                  || !mux_isspace(zp[-1])
                  || !mux_isdigit(zp[1])))
            {
                // Found a comment.
                //
                *zp = '\0';
            }
            else
            {
                zp++;
            }
        }

        // Trim trailing spaces.
        //
        while (  buf < zp
              && mux_isspace(zp[-1]))
        {
            *(--zp) = '\0';
        }

        // Process line.
        //
        UTF8 *cp = buf;

        // Trim leading spaces.
        //
        while (mux_isspace(*cp))
        {
            cp++;
        }

        // Skip over command.
        //
        UTF8 *ap;
        for (ap = cp; *ap && !mux_isspace(*ap); ap++)
        {
            ; // Nothing.
        }

        // Terminate command.
        //
        if (*ap)
        {
            *ap++ = '\0';
        }

        // Skip spaces between command and argument.
        //
        while (mux_isspace(*ap))
        {
            ap++;
        }

        if (*cp)
        {
            cf_set(cp, ap, player);
        }

        if (NULL == fgets((char *)buf, LBUF_SIZE, fp))
        {
            break;
        }
    }
    free_lbuf(buf);
    if (fclose(fp) == 0)
    {
        DebugTotalFiles--;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// conftable: Table for parsing the configuration file.

static CONFPARM conftable[] =
{
    {T("access"),                    cf_access,      CA_GOD,    CA_DISABLED, NULL,                            access_nametab,     0},
    {T("alias"),                     cf_cmd_alias,   CA_GOD,    CA_DISABLED, (int *)&mudstate.command_htab,   0,                  0},
    {T("allow_guest_from_registered_site"), cf_bool, CA_GOD,    CA_WIZARD,   (int *)&mudconf.allow_guest_from_registered_site, NULL,     1},
    {T("article_rule"),              cf_art_rule,    CA_GOD,    CA_DISABLED, (int *)&mudconf.art_rules,       NULL,               0},
    {T("attr_access"),               cf_attr_access, CA_GOD,    CA_DISABLED, NULL,                            attraccess_nametab, 0},
    {T("attr_alias"),                cf_alias,       CA_GOD,    CA_DISABLED, (int *)&mudstate.attr_name_htab, 0,                  0},
    {T("attr_cmd_access"),           cf_acmd_access, CA_GOD,    CA_DISABLED, NULL,                            access_nametab,     0},
    {T("attr_name_charset"),         cf_modify_bits, CA_GOD,    CA_PUBLIC,   &mudconf.attr_name_charset,      charset_nametab,    0},
    {T("autozone"),                  cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.autozone,        NULL,               0},
    {T("bad_name"),                  cf_badname,     CA_GOD,    CA_DISABLED, NULL,                            NULL,               0},
    {T("badsite_file"),              cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.site_file,       NULL, SIZEOF_PATHNAME},
    {T("cache_names"),               cf_bool,        CA_STATIC, CA_GOD,      (int *)&mudconf.cache_names,     NULL,               0},
    {T("cache_pages"),               cf_int,         CA_STATIC, CA_WIZARD,   &mudconf.cache_pages,            NULL,               0},
    {T("cache_tick_period"),         cf_seconds,     CA_GOD,    CA_WIZARD,   (int *)&mudconf.cache_tick_period, NULL,             0},
    {T("check_interval"),            cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.check_interval,         NULL,               0},
    {T("check_offset"),              cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.check_offset,           NULL,               0},
    {T("clone_copies_cost"),         cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.clone_copy_cost, NULL,               0},
    {T("command_quota_increment"),   cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.cmd_quota_incr,         NULL,               0},
    {T("command_quota_max"),         cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.cmd_quota_max,          NULL,               0},
    {T("compress_program"),          cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.compress,        NULL, SIZEOF_PATHNAME},
    {T("compression"),               cf_bool,        CA_GOD,    CA_GOD,      (int *)&mudconf.compress_db,     NULL,               0},
    {T("comsys_database"),           cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.comsys_db,       NULL, SIZEOF_PATHNAME},
    {T("config_access"),             cf_cf_access,   CA_GOD,    CA_DISABLED, NULL,                            access_nametab,     0},
    {T("conn_timeout"),              cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.conn_timeout,           NULL,               0},
    {T("connect_file"),              cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.conn_file,       NULL, SIZEOF_PATHNAME},
    {T("connect_reg_file"),          cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.creg_file,       NULL, SIZEOF_PATHNAME},
    {T("lag_limit"),                 cf_seconds,     CA_GOD,    CA_WIZARD,   (int *)&mudconf.max_cmdsecs,     NULL,               0},
    {T("crash_database"),            cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.crashdb,         NULL, SIZEOF_PATHNAME},
    {T("crash_message"),             cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.crash_msg,        NULL,       GBUF_SIZE},
    {T("create_max_cost"),           cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.createmax,              NULL,               0},
    {T("create_min_cost"),           cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.createmin,              NULL,               0},
    {T("dark_sleepers"),             cf_bool,        CA_GOD,    CA_WIZARD,   (int *)&mudconf.dark_sleepers,   NULL,               0},
    {T("default_home"),              cf_dbref,       CA_GOD,    CA_PUBLIC,   &mudconf.default_home,           NULL,               0},
    {T("destroy_going_now"),         cf_bool,        CA_GOD,    CA_WIZARD,   (int *)&mudconf.destroy_going_now, NULL,               0},
    {T("dig_cost"),                  cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.digcost,                NULL,               0},
    {T("down_file"),                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.down_file,       NULL, SIZEOF_PATHNAME},
    {T("down_motd_message"),         cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.downmotd_msg,     NULL,       GBUF_SIZE},
    {T("dump_interval"),             cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.dump_interval,          NULL,               0},
    {T("dump_message"),              cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.dump_msg,         NULL,             256},
    {T("dump_offset"),               cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.dump_offset,            NULL,               0},
    {T("earn_limit"),                cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.paylimit,               NULL,               0},
    {T("eval_comtitle"),             cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.eval_comtitle,   NULL,               0},
    {T("events_daily_hour"),         cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.events_daily_hour,      NULL,               0},
    {T("examine_flags"),             cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.ex_flags,        NULL,               0},
    {T("examine_public_attrs"),      cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.exam_public,     NULL,               0},
    {T("exit_flags"),                cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.exit_flags,      NULL,               0},
    {T("exit_name_charset"),         cf_modify_bits, CA_GOD,    CA_PUBLIC,   &mudconf.exit_name_charset,      charset_nametab,    0},
    {T("exit_quota"),                cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.exit_quota,             NULL,               0},
    {T("exit_parent"),               cf_dbref,       CA_GOD,    CA_PUBLIC,   &mudconf.exit_parent,            NULL,               0},
    {T("fascist_teleport"),          cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.fascist_tport,   NULL,               0},
    {T("find_money_chance"),         cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.payfind,                NULL,               0},
    {T("fixed_home_message"),        cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.fixed_home_msg,   NULL,             128},
    {T("fixed_tel_message"),         cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.fixed_tel_msg,    NULL,             128},
    {T("flag_access"),               cf_flag_access, CA_GOD,    CA_DISABLED, NULL,                            NULL,               0},
    {T("flag_alias"),                cf_flagalias,   CA_GOD,    CA_DISABLED, NULL,                            NULL,               0},
    {T("flag_name"),                 cf_flag_name,   CA_GOD,    CA_DISABLED, NULL,                            NULL,               0},
    {T("float_precision"),           cf_int,         CA_STATIC, CA_PUBLIC,   &mudconf.float_precision,        NULL,               0},
    {T("forbid_site"),               cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    NULL,     H_FORBIDDEN},
#if defined(HAVE_WORKING_FORK)
    {T("fork_dump"),                 cf_bool,        CA_GOD,    CA_WIZARD,   (int *)&mudconf.fork_dump,       NULL,               0},
#endif // HAVE_WORKING_FORK
    {T("full_file"),                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.full_file,       NULL, SIZEOF_PATHNAME},
    {T("full_motd_message"),         cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.fullmotd_msg,     NULL,       GBUF_SIZE},
    {T("function_access"),           cf_func_access, CA_GOD,    CA_DISABLED, NULL,                            access_nametab,     0},
    {T("function_alias"),            cf_alias,       CA_GOD,    CA_DISABLED, (int *)&mudstate.func_htab,      NULL,               0},
    {T("function_invocation_limit"), cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.func_invk_lim,          NULL,               0},
    {T("function_name"),             cf_name,        CA_GOD,    CA_DISABLED, (int *)&mudstate.func_htab,      NULL,               0},
    {T("function_recursion_limit"),  cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.func_nest_lim,          NULL,               0},
    {T("game_dir_file"),             cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.game_dir,        NULL, SIZEOF_PATHNAME},
    {T("game_pag_file"),             cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.game_pag,        NULL, SIZEOF_PATHNAME},
    {T("global_error_obj"),          cf_dbref,       CA_GOD,    CA_GOD,      &mudconf.global_error_obj,       NULL,               0},
    {T("good_name"),                 cf_badname,     CA_GOD,    CA_DISABLED, NULL,                            NULL,               1},
    {T("guest_char_num"),            cf_dbref,       CA_STATIC, CA_WIZARD,   &mudconf.guest_char,             NULL,               0},
    {T("guest_file"),                cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.guest_file,      NULL, SIZEOF_PATHNAME},
    {T("guest_nuker"),               cf_dbref,       CA_GOD,    CA_WIZARD,   &mudconf.guest_nuker,            NULL,               0},
    {T("guest_prefix"),              cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.guest_prefix,     NULL,              32},
    {T("guest_site"),                cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    NULL,         H_GUEST},
    {T("guests_channel"),            cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.guests_channel,   NULL,              32},
    {T("guests_channel_alias"),      cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.guests_channel_alias, NULL,          32},
    {T("have_comsys"),               cf_bool,        CA_STATIC, CA_PUBLIC,   (int *)&mudconf.have_comsys,     NULL,               0},
    {T("have_mailer"),               cf_bool,        CA_STATIC, CA_PUBLIC,   (int *)&mudconf.have_mailer,     NULL,               0},
    {T("have_zones"),                cf_bool,        CA_STATIC, CA_PUBLIC,   (int *)&mudconf.have_zones,      NULL,               0},
    {T("help_executor"),             cf_dbref,       CA_GOD,    CA_WIZARD,   &mudconf.help_executor,          NULL,               0},
    {T("helpfile"),                  cf_helpfile,    CA_STATIC, CA_DISABLED, NULL,                            NULL,               0},
    {T("hook_cmd"),                  cf_hook,        CA_GOD,    CA_GOD,      &mudconf.hook_cmd,               NULL,               0},
    {T("hook_obj"),                  cf_dbref,       CA_GOD,    CA_GOD,      &mudconf.hook_obj,               NULL,               0},
    {T("hostnames"),                 cf_bool,        CA_GOD,    CA_WIZARD,   (int *)&mudconf.use_hostname,    NULL,               0},
    {T("idle_interval"),             cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.idle_interval,          NULL,               0},
    {T("idle_timeout"),              cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.idle_timeout,           NULL,               0},
    {T("idle_wiz_dark"),             cf_bool,        CA_GOD,    CA_WIZARD,   (int *)&mudconf.idle_wiz_dark,   NULL,               0},
    {T("include"),                   cf_include,     CA_STATIC, CA_DISABLED, NULL,                            NULL,               0},
    {T("indent_desc"),               cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.indent_desc,     NULL,               0},
    {T("initial_size"),              cf_int,         CA_STATIC, CA_WIZARD,   &mudconf.init_size,              NULL,               0},
    {T("input_database"),            cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.indb,            NULL, SIZEOF_PATHNAME},
    {T("kill_guarantee_cost"),       cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.killguarantee,          NULL,               0},
    {T("kill_max_cost"),             cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.killmax,                NULL,               0},
    {T("kill_min_cost"),             cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.killmin,                NULL,               0},
    {T("lag_maximum"),               cf_seconds,     CA_GOD,    CA_WIZARD,   (int *)&mudconf.rpt_cmdsecs,     NULL,               0},
    {T("lbuf_size"),                 cf_int,       CA_DISABLED, CA_PUBLIC,   (int *)&mudconf.lbuf_size,       NULL,               0},
    {T("link_cost"),                 cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.linkcost,               NULL,               0},
    {T("list_access"),               cf_ntab_access, CA_GOD,    CA_DISABLED, (int *)list_names,               access_nametab,     0},
    {T("lock_recursion_limit"),      cf_int,         CA_WIZARD, CA_PUBLIC,   &mudconf.lock_nest_lim,          NULL,               0},
    {T("log"),                       cf_modify_bits, CA_GOD,    CA_DISABLED, &mudconf.log_options,            logoptions_nametab, 0},
    {T("log_options"),               cf_modify_bits, CA_GOD,    CA_DISABLED, &mudconf.log_info,               logdata_nametab,    0},
    {T("logout_cmd_access"),         cf_ntab_access, CA_GOD,    CA_DISABLED, (int *)logout_cmdtable,          access_nametab,     0},
    {T("logout_cmd_alias"),          cf_alias,       CA_GOD,    CA_DISABLED, (int *)&mudstate.logout_cmd_htab,NULL,               0},
    {T("look_obey_terse"),           cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.terse_look,      NULL,               0},
    {T("machine_command_cost"),      cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.machinecost,            NULL,               0},
    {T("mail_database"),             cf_string_dyn,  CA_GOD,    CA_GOD,      (int *)&mudconf.mail_db,         NULL, SIZEOF_PATHNAME},
    {T("mail_expiration"),           cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.mail_expiration,        NULL,               0},
    {T("mail_per_hour"),             cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.mail_per_hour,          NULL,               0},
    {T("master_room"),               cf_dbref,       CA_GOD,    CA_WIZARD,   &mudconf.master_room,            NULL,               0},
    {T("match_own_commands"),        cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.match_mine,      NULL,               0},
    {T("max_cache_size"),            cf_int,         CA_GOD,    CA_GOD,      (int *)&mudconf.max_cache_size,  NULL,               0},
    {T("max_players"),               cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.max_players,            NULL,               0},
    {T("min_guests"),                cf_int,         CA_STATIC, CA_GOD,      (int *)&mudconf.min_guests,      NULL,               0},
    {T("money_name_plural"),         cf_string,      CA_GOD,    CA_PUBLIC,   (int *)mudconf.many_coins,       NULL,              32},
    {T("money_name_singular"),       cf_string,      CA_GOD,    CA_PUBLIC,   (int *)mudconf.one_coin,         NULL,              32},
    {T("motd_file"),                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.motd_file,       NULL, SIZEOF_PATHNAME},
    {T("motd_message"),              cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.motd_msg,         NULL,       GBUF_SIZE},
    {T("module"),                    cf_module,      CA_GOD,    CA_WIZARD,   (int *)NULL,                     NULL,               0},
    {T("mud_name"),                  cf_string,      CA_GOD,    CA_PUBLIC,   (int *)mudconf.mud_name,         NULL,              32},
    {T("newuser_file"),              cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.crea_file,       NULL, SIZEOF_PATHNAME},
    {T("nositemon_site"),            cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    NULL,     H_NOSITEMON},
    {T("notify_recursion_limit"),    cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.ntfy_nest_lim,          NULL,               0},
    {T("number_guests"),             cf_int,         CA_STATIC, CA_WIZARD,   &mudconf.number_guests,          NULL,               0},
    {T("open_cost"),                 cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.opencost,               NULL,               0},
    {T("output_database"),           cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.outdb,           NULL, SIZEOF_PATHNAME},
    {T("output_limit"),              cf_int,         CA_GOD,    CA_WIZARD,   (int *)&mudconf.output_limit,    NULL,               0},
    {T("page_cost"),                 cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.pagecost,               NULL,               0},
    {T("paranoid_allocate"),         cf_bool,        CA_GOD,    CA_WIZARD,   (int *)&mudconf.paranoid_alloc,  NULL,               0},
    {T("parent_recursion_limit"),    cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.parent_nest_lim,        NULL,               0},
    {T("paycheck"),                  cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.paycheck,               NULL,               0},
    {T("pemit_any_object"),          cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.pemit_any,       NULL,               0},
    {T("pemit_far_players"),         cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.pemit_players,   NULL,               0},
    {T("permit_site"),               cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    NULL,               0},
    {T("player_flags"),              cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.player_flags,    NULL,               0},
    {T("player_listen"),             cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.player_listen,   NULL,               0},
    {T("player_match_own_commands"), cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.match_mine_pl,   NULL,               0},
    {T("player_name_spaces"),        cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.name_spaces,     NULL,               0},
    {T("player_name_charset"),       cf_modify_bits, CA_GOD,    CA_PUBLIC,   &mudconf.player_name_charset,    charset_nametab,    0},
    {T("player_parent"),             cf_dbref,       CA_GOD,    CA_PUBLIC,   &mudconf.player_parent,          NULL,               0},
    {T("player_queue_limit"),        cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.queuemax,               NULL,               0},
    {T("player_quota"),              cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.player_quota,           NULL,               0},
    {T("player_starting_home"),      cf_dbref,       CA_GOD,    CA_PUBLIC,   &mudconf.start_home,             NULL,               0},
    {T("player_starting_room"),      cf_dbref,       CA_GOD,    CA_PUBLIC,   &mudconf.start_room,             NULL,               0},
    {T("port"),                      cf_int_array,   CA_STATIC, CA_PUBLIC,   (int *)&mudconf.ports,           NULL, MAX_LISTEN_PORTS},
#ifdef SSL_ENABLED
    {T("port_ssl"),                  cf_int_array,   CA_STATIC, CA_PUBLIC,   (int *)&mudconf.sslPorts,        NULL, MAX_LISTEN_PORTS},
#endif
    {T("postdump_message"),          cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.postdump_msg,     NULL,             256},
    {T("power_alias"),               cf_poweralias,  CA_GOD,    CA_DISABLED, NULL,                            NULL,               0},
    {T("pcreate_per_hour"),          cf_int,         CA_STATIC, CA_PUBLIC,   (int *)&mudconf.pcreate_per_hour,NULL,               0},
    {T("public_channel"),            cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.public_channel,   NULL,              32},
    {T("public_channel_alias"),      cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.public_channel_alias, NULL,          32},
    {T("public_flags"),              cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.pub_flags,       NULL,               0},
    {T("pueblo_message"),            cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.pueblo_msg,       NULL,       GBUF_SIZE},
    {T("queue_active_chunk"),        cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.active_q_chunk,         NULL,               0},
    {T("queue_idle_chunk"),          cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.queue_chunk,            NULL,               0},
    {T("quiet_look"),                cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.quiet_look,      NULL,               0},
    {T("quiet_whisper"),             cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.quiet_whisper,   NULL,               0},
    {T("quit_file"),                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.quit_file,       NULL, SIZEOF_PATHNAME},
    {T("quotas"),                    cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.quotas,          NULL,               0},
    {T("raw_helpfile"),              cf_raw_helpfile,CA_STATIC, CA_DISABLED, NULL,                            NULL,               0},
    {T("read_remote_desc"),          cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.read_rem_desc,   NULL,               0},
    {T("read_remote_name"),          cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.read_rem_name,   NULL,               0},
    {T("register_create_file"),      cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.regf_file,       NULL, SIZEOF_PATHNAME},
    {T("register_site"),             cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    NULL,  H_REGISTRATION},
    {T("reset_players"),             cf_bool,        CA_GOD,    CA_DISABLED, (int *)&mudconf.reset_players,   NULL,               0},
    {T("restrict_home"),             cf_bool,        CA_GOD,    CA_DISABLED, (int *)&mudconf.restrict_home,   NULL,               0},
    {T("retry_limit"),               cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.retry_limit,            NULL,               0},
    {T("robot_cost"),                cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.robotcost,              NULL,               0},
    {T("robot_flags"),               cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.robot_flags,     NULL,               0},
    {T("robot_speech"),              cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.robot_speak,     NULL,               0},
    {T("room_flags"),                cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.room_flags,      NULL,               0},
    {T("room_name_charset"),         cf_modify_bits, CA_GOD,    CA_PUBLIC,   &mudconf.room_name_charset,      charset_nametab,    0},
    {T("room_parent"),               cf_dbref,       CA_GOD,    CA_PUBLIC,   &mudconf.room_parent,            NULL,               0},
    {T("room_quota"),                cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.room_quota,             NULL,               0},
    {T("run_startup"),               cf_bool,        CA_STATIC, CA_WIZARD,   (int *)&mudconf.run_startup,     NULL,               0},
    {T("sacrifice_adjust"),          cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.sacadjust,              NULL,               0},
    {T("sacrifice_factor"),          cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.sacfactor,              NULL,               0},
    {T("safe_wipe"),                 cf_bool,        CA_GOD,    CA_WIZARD,   (int *)&mudconf.safe_wipe,       NULL,               0},
    {T("safer_passwords"),           cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.safer_passwords, NULL,               0},
    {T("search_cost"),               cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.searchcost,             NULL,               0},
    {T("see_owned_dark"),            cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.see_own_dark,    NULL,               0},
    {T("signal_action"),             cf_option,      CA_STATIC, CA_GOD,      &mudconf.sig_action,             sigactions_nametab, 0},
    {T("site_chars"),                cf_int,         CA_GOD,    CA_WIZARD,   (int *)&mudconf.site_chars,      NULL,               0},
    {T("space_compress"),            cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.space_compress,  NULL,               0},
#ifdef SSL_ENABLED
    {T("ssl_certificate_file"),      cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.ssl_certificate_file,NULL,          128},
    {T("ssl_certificate_key"),       cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.ssl_certificate_key, NULL,          128},
    {T("ssl_certificate_password"),  cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.ssl_certificate_password, NULL,     128},
#endif
    {T("stack_limit"),               cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.stack_limit,            NULL,               0},
    {T("starting_money"),            cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.paystart,               NULL,               0},
    {T("starting_quota"),            cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.start_quota,            NULL,               0},
    {T("status_file"),               cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.status_file,     NULL, SIZEOF_PATHNAME},
    {T("stripped_flags"),            cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.stripped_flags,  NULL,               0},
    {T("suspect_site"),              cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.suspect_list,   NULL,       H_SUSPECT},
    {T("sweep_dark"),                cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.sweep_dark,      NULL,               0},
    {T("switch_default_all"),        cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.switch_df_all,   NULL,               0},
    {T("terse_shows_contents"),      cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.terse_contents,  NULL,               0},
    {T("terse_shows_exits"),         cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.terse_exits,     NULL,               0},
    {T("terse_shows_move_messages"), cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.terse_movemsg,   NULL,               0},
    {T("thing_flags"),               cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.thing_flags,     NULL,               0},
    {T("thing_name_charset"),        cf_modify_bits, CA_GOD,    CA_PUBLIC,   &mudconf.thing_name_charset,     charset_nametab,    0},
    {T("thing_parent"),              cf_dbref,       CA_GOD,    CA_PUBLIC,   &mudconf.thing_parent,           NULL,               0},
    {T("thing_quota"),               cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.thing_quota,            NULL,               0},
    {T("timeslice"),                 cf_seconds,     CA_GOD,    CA_PUBLIC,   (int *)&mudconf.timeslice,       NULL,               0},
    {T("toad_recipient"),            cf_dbref,       CA_GOD,    CA_WIZARD,   &mudconf.toad_recipient,         NULL,               0},
    {T("trace_output_limit"),        cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.trace_limit,            NULL,               0},
    {T("trace_topdown"),             cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.trace_topdown,   NULL,               0},
    {T("trust_site"),                cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.suspect_list,   NULL,               0},
    {T("uncompress_program"),        cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.uncompress,      NULL, SIZEOF_PATHNAME},
    {T("unowned_safe"),              cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.safe_unowned,    NULL,               0},
    {T("use_http"),                  cf_bool,        CA_STATIC, CA_PUBLIC,   (int *)&mudconf.use_http,        NULL,               0},
    {T("user_attr_access"),          cf_modify_bits, CA_GOD,    CA_DISABLED, &mudconf.vattr_flags,            attraccess_nametab, 0},
    {T("user_attr_per_hour"),        cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.vattr_per_hour,         NULL,               0},
    {T("wait_cost"),                 cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.waitcost,               NULL,               0},
    {T("wizard_motd_file"),          cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.wizmotd_file,    NULL, SIZEOF_PATHNAME},
    {T("wizard_motd_message"),       cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.wizmotd_msg,      NULL,       GBUF_SIZE},
    {T("zone_recursion_limit"),      cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.zone_nest_lim,          NULL,               0},
#ifdef REALITY_LVLS
    {T("reality_level"),             cf_rlevel,      CA_STATIC, CA_GOD,      (int *)&mudconf,                 NULL,               0},
    {T("def_room_rx"),               cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_room_rx,     NULL,               0},
    {T("def_room_tx"),               cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_room_tx,     NULL,               0},
    {T("def_player_rx"),             cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_player_rx,   NULL,               0},
    {T("def_player_tx"),             cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_player_tx,   NULL,               0},
    {T("def_exit_rx"),               cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_exit_rx,     NULL,               0},
    {T("def_exit_tx"),               cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_exit_tx,     NULL,               0},
    {T("def_thing_rx"),              cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_thing_rx,    NULL,               0},
    {T("def_thing_tx"),              cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_thing_tx,    NULL,               0},
#endif // REALITY_LVLS

#ifdef FIRANMUX
    {T("immobile_message"),          cf_string,      CA_WIZARD, CA_PUBLIC,   (int *)mudconf.immobile_msg,     NULL,             128},
#endif // FIRANMUX
#if defined(INLINESQL) || defined(HAVE_DLOPEN) || defined(WIN32)
    {T("sql_server"),                cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.sql_server,       NULL,             128},
    {T("sql_user"),                  cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.sql_user,         NULL,             128},
    {T("sql_password"),              cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.sql_password,     NULL,             128},
    {T("sql_database"),              cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.sql_database,     NULL,             128},
#endif
    {T("mail_server"),               cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.mail_server,      NULL,             128},
    {T("mail_ehlo"),                 cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.mail_ehlo,        NULL,             128},
    {T("mail_sendaddr"),             cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.mail_sendaddr,    NULL,             128},
    {T("mail_sendname"),             cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.mail_sendname,    NULL,             128},
    {T("mail_subject"),              cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.mail_subject,     NULL,             128},
    {(UTF8 *) NULL,                       NULL,           0,         0,           NULL,                       NULL,               0}
};

// ---------------------------------------------------------------------------
// cf_cf_access: Set access on config directives
//
CF_HAND(cf_cf_access)
{
    UNUSED_PARAMETER(vp);

    CONFPARM *tp;
    UTF8 *ap;

    for (ap = str; *ap && !mux_isspace(*ap); ap++)
    {
        ; // Nothing
    }
    if (*ap)
    {
        *ap++ = '\0';
    }

    for (tp = conftable; tp->pname; tp++)
    {
        if (!strcmp((char *)tp->pname, (char *)str))
        {
            // Cannot modify parameters set CA_STATIC.
            //
            if (  tp->flags & CA_STATIC
               && !mudstate.bReadingConfiguration)
            {
                notify(player, NOPERM_MESSAGE);
                STARTLOG(LOG_CONFIGMODS, "CFG", "PERM");
                log_name(player);
                log_text(T(" tried to change access to static param: "));
                log_text(tp->pname);
                ENDLOG;
                return -1;
            }
            return cf_modify_bits(&tp->flags, ap, pExtra, nExtra, player, cmd);
        }
    }
    cf_log_notfound(player, cmd, T("Config directive"), str);
    return -1;
}

// ---------------------------------------------------------------------------
// cf_set: Set config parameter.
//
int cf_set(UTF8 *cp, UTF8 *ap, dbref player)
{
    CONFPARM *tp;
    UTF8 *buff = 0;

    // Search the config parameter table for the command. If we find
    // it, call the handler to parse the argument.
    //
    for (tp = conftable; tp->pname; tp++)
    {
        if (!strcmp((char *)tp->pname, (char *)cp))
        {
            int i = -1;
            if (  (tp->flags & CA_DISABLED) == 0
               && (  mudstate.bReadingConfiguration
                  || check_access(player, tp->flags)))
            {
                if (!mudstate.bReadingConfiguration)
                {
                    buff = alloc_lbuf("cf_set");
                    mux_strncpy(buff, ap, LBUF_SIZE-1);
                }

                i = tp->interpreter(tp->loc, ap, tp->pExtra, tp->nExtra, player, cp);

                if (!mudstate.bReadingConfiguration)
                {
                    STARTLOG(LOG_CONFIGMODS, "CFG", "UPDAT");
                    log_name(player);
                    log_text(T(" entered config directive: "));
                    log_text(cp);
                    log_text(T(" with args '"));
                    log_text(buff);
                    log_text(T("'.  Status: "));
                    switch (i)
                    {
                    case 0:
                        log_text(T("Success."));
                        break;

                    case 1:
                        log_text(T("Partial success."));
                        break;

                    case -1:
                        log_text(T("Failure."));
                        break;

                    default:
                        log_text(T("Strange."));
                    }
                    ENDLOG;
                    free_lbuf(buff);
                }
            }
            else  if (!mudstate.bReadingConfiguration)
            {
                notify(player, NOPERM_MESSAGE);
            }
            return i;
        }
    }

    // Config directive not found.  Complain about it.
    //
    cf_log_notfound(player, T("Set"), T("Config directive"), cp);
    return -1;
}

// Validate important dbrefs.
//
void ValidateConfigurationDbrefs(void)
{
    static struct
    {
        dbref *ploc;
        dbref dflt;
    } Table[] =
    {
        { &mudconf.default_home,     NOTHING },
        { &mudconf.exit_parent,      NOTHING },
        { &mudconf.global_error_obj, NOTHING },
        { &mudconf.guest_char,       NOTHING },
        { &mudconf.guest_nuker,      GOD     },
        { &mudconf.help_executor,    NOTHING },
        { &mudconf.hook_obj,         NOTHING },
        { &mudconf.master_room,      NOTHING },
        { &mudconf.player_parent,    NOTHING },
        { &mudconf.start_home,       NOTHING },
        { &mudconf.start_room,       0       },
        { &mudconf.room_parent,      NOTHING },
        { &mudconf.thing_parent,     NOTHING },
        { &mudconf.toad_recipient,   GOD     },
        { NULL,                      NOTHING }
    };

    for (int i = 0; NULL != Table[i].ploc; i++)
    {
        if (*Table[i].ploc != Table[i].dflt)
        {
            if (!Good_obj(*Table[i].ploc))
            {
                *Table[i].ploc = Table[i].dflt;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// do_admin: Command handler to set config params at runtime
//
void do_admin
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *kw,
    UTF8 *value,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int i = cf_set(kw, value, executor);
    if ((i >= 0) && !Quiet(executor))
    {
        notify(executor, T("Set."));
    }
    ValidateConfigurationDbrefs();
}

// ---------------------------------------------------------------------------
// cf_read: Read in config parameters from named file
//
static struct
{
    UTF8 **pFilename;
    const UTF8 *pSuffix;
} DefaultSuffixes[]
=
{
    { &mudconf.outdb,    T(".out") },
    { &mudconf.crashdb,  T(".CRASH") },
    { &mudconf.game_dir, T(".dir") },
    { &mudconf.game_pag, T(".pag") },
    { 0, 0 }
};

int cf_read(void)
{
    int retval;

    mudstate.bReadingConfiguration = true;
    retval = cf_include(NULL, mudconf.config_file, (void *)0, 0, 0, (UTF8 *)"init");
    mudstate.bReadingConfiguration = false;

    // Fill in missing DB file names.
    //
    size_t nInDB = strlen((char *)mudconf.indb);
    for (int i = 0; DefaultSuffixes[i].pFilename; i++)
    {
        UTF8 **p = DefaultSuffixes[i].pFilename;
        if (**p == '\0')
        {
            // The filename is an empty string so we should construct
            // a default filename.
            //
            const UTF8 *pSuffix = DefaultSuffixes[i].pSuffix;
            size_t nSuffix = strlen((const char *)pSuffix);
            UTF8 *buff = (UTF8 *)MEMALLOC(nInDB + nSuffix + 1);
            ISOUTOFMEMORY(buff);
            memcpy(buff, mudconf.indb, nInDB);
            memcpy(buff + nInDB, pSuffix, nSuffix+1);
            MEMFREE(*p);
            *p = buff;
        }
    }
    return retval;
}

// ---------------------------------------------------------------------------
// list_cf_access: List access to config directives.
//
void list_cf_access(dbref player)
{
    for (CONFPARM *tp = conftable; tp->pname; tp++)
    {
        if (God(player) || check_access(player, tp->flags))
        {
            listset_nametab(player, access_nametab, tp->flags, tp->pname, true);
        }
    }
}

// ---------------------------------------------------------------------------
// cf_display: Given a config parameter by name, return its value in some
// sane fashion.
//
void cf_display(dbref player, UTF8 *param_name, UTF8 *buff, UTF8 **bufc)
{
    CONFPARM *tp;

    for (tp = conftable; tp->pname; tp++)
    {
        if (!mux_stricmp(tp->pname, param_name))
        {
            if (check_access(player, tp->rperms))
            {
                if (tp->interpreter == cf_int)
                {
                    safe_ltoa(*(tp->loc), buff, bufc);
                    return;
                }
                else if (tp->interpreter == cf_dbref)
                {
                    safe_chr('#', buff, bufc);
                    safe_ltoa(*(tp->loc), buff, bufc);
                    return;
                }
                else if (tp->interpreter == cf_bool)
                {
                    bool *pb = (bool *)tp->loc;
                    safe_bool(*pb, buff, bufc);
                    return;
                }
                else if (tp->interpreter == cf_string)
                {
                    safe_str((UTF8 *)tp->loc, buff, bufc);
                    return;
                }
                else if (tp->interpreter == cf_string_dyn)
                {
                    safe_str(*(UTF8 **)tp->loc, buff, bufc);
                    return;
                }
                else if (tp->interpreter == cf_int_array)
                {
                    IntArray *pia = (IntArray *)(tp->loc);
                    ITL itl;
                    ItemToList_Init(&itl, buff, bufc);
                    for (int i = 0; i < pia->n; i++)
                    {
                        if (!ItemToList_AddInteger(&itl, pia->pi[i]))
                        {
                            break;
                        }
                    }
                    ItemToList_Final(&itl);
                    return;
                }
                else if (tp->interpreter == cf_seconds)
                {
                    CLinearTimeDelta *pltd = (CLinearTimeDelta *)(tp->loc);
                    safe_str(pltd->ReturnSecondsString(7), buff, bufc);
                    return;
                }
            }
            safe_noperm(buff, bufc);
            return;
        }
    }
    safe_nomatch(buff, bufc);
}

// ---------------------------------------------------------------------------
// cf_list: List all config options the player can read.
//
void cf_list(dbref player, UTF8 *buff, UTF8 **bufc)
{
    CONFPARM *tp;
    ITL itl;
    ItemToList_Init(&itl, buff, bufc);

    for (tp = conftable; tp->pname; tp++)
    {
        if (check_access(player, tp->rperms))
        {
            if (!ItemToList_AddString(&itl, tp->pname))
            {
                break;
            }
        }
    }
    ItemToList_Final(&itl);
    return;
}
