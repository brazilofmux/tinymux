/*! \file conf.cpp
 * \brief Set up configuration information and static data.
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
#include "mathutil.h"

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

    mudconf.ip_address = nullptr;
    mudconf.ports.n = 1;
    mudconf.ports.pi = new int[1];
    if (nullptr != mudconf.ports.pi)
    {
        mudconf.ports.pi[0] = 2860;
    }
    else
    {
        ISOUTOFMEMORY(mudconf.ports.pi);
    }

#ifdef UNIX_SSL
    mudconf.sslPorts.n = 0;
    mudconf.sslPorts.pi = nullptr;
    mudconf.ssl_certificate_file[0] = '\0';
    mudconf.ssl_certificate_key[0] = '\0';
    mudconf.ssl_certificate_password[0] = '\0';
#endif

    mudconf.init_size = 1000;
    mudconf.guest_char = -1;
    mudconf.guest_nuker = GOD;
    mudconf.number_guests = 30;
    mudconf.min_guests = 1;
    mux_strncpy(mudconf.guest_prefix, T("Guest"), sizeof(mudconf.guest_prefix)-1);
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
    mux_strncpy(mudconf.fixed_home_msg, T("You are fixed in place and cannot move."), sizeof(mudconf.fixed_home_msg)-1);
    mux_strncpy(mudconf.fixed_tel_msg, T("You are fixed in place and cannot teleport."), sizeof(mudconf.fixed_tel_msg)-1);
    mux_strncpy(mudconf.public_channel, T("Public"), sizeof(mudconf.public_channel)-1);
    mux_strncpy(mudconf.public_channel_alias, T("pub"), sizeof(mudconf.public_channel_alias)-1);
    mux_strncpy(mudconf.guests_channel, T("Guests"), sizeof(mudconf.guests_channel)-1);
    mux_strncpy(mudconf.guests_channel_alias, T("g"), sizeof(mudconf.guests_channel_alias)-1);
    mux_strncpy(mudconf.pueblo_msg, T("</xch_mudtext><img xch_mode=html>"), sizeof(mudconf.pueblo_msg)-1);
#if defined(FIRANMUX)
    mux_strncpy(mudconf.immobile_msg, T("You have been set immobile."), sizeof(mudconf.immobile_msg)-1);
#endif // FIRANMUX
#if defined(INLINESQL)
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

    mudconf.art_rules = nullptr;
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
    mudconf.keepalive_interval = 60;
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

    // -- ??? Running SC on a non-SC DB may cause problems.
    //
    mudconf.space_compress = true;
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
    mux_strncpy(mudconf.mud_name, T("MUX"), sizeof(mudconf.mud_name)-1);
    mux_strncpy(mudconf.one_coin, T("penny"), sizeof(mudconf.one_coin)-1);
    mux_strncpy(mudconf.many_coins, T("pennies"), sizeof(mudconf.many_coins)-1);
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
    mudconf.references_per_hour = 500;
    mudconf.pcreate_per_hour = 100;
    mudconf.lbuf_size = LBUF_SIZE;
    mudconf.attr_name_charset = 0;
    mudconf.exit_name_charset = 0;
    mudconf.player_name_charset = 0;
    mudconf.room_name_charset = 0;
    mudconf.thing_name_charset = 0;
    mudconf.password_methods = CRYPT_DEFAULT;
    mudconf.default_charset = CHARSET_LATIN1;

    mudstate.events_flag = 0;
    mudstate.bReadingConfiguration = false;
    mudstate.bCanRestart = false;
    mudstate.panicking = false;
    mudstate.asserting = 0;
    mudstate.logging = 0;
    mudstate.epoch = 0;
    mudstate.generation = 0;
    mudstate.curr_executor = NOTHING;
    mudstate.curr_enactor = NOTHING;
    mudstate.shutdown_flag  = false;
    mudstate.attr_next = A_USER_START;
    mudstate.debug_cmd = T("< init >");
    mudstate.curr_cmd  = T("< none >");
    mux_strncpy(mudstate.doing_hdr, T("Doing"), sizeof(mudstate.doing_hdr)-1);
    mudstate.badname_head = nullptr;
    mudstate.attrperm_list = nullptr;
    mudstate.mstat_ixrss[0] = 0;
    mudstate.mstat_ixrss[1] = 0;
    mudstate.mstat_idrss[0] = 0;
    mudstate.mstat_idrss[1] = 0;
    mudstate.mstat_isrss[0] = 0;
    mudstate.mstat_isrss[1] = 0;
    mudstate.mstat_secs[0] = 0;
    mudstate.mstat_secs[1] = 0;
    mudstate.mstat_curr = 0;
    mudstate.iter_alist.data = nullptr;
    mudstate.iter_alist.len = 0;
    mudstate.iter_alist.next = nullptr;
    mudstate.mod_alist = nullptr;
    mudstate.mod_alist_len = 0;
    mudstate.mod_size = 0;
    mudstate.mod_al_id = NOTHING;
    mudstate.olist = nullptr;
    mudstate.min_size = 0;
    mudstate.db_top = 0;
    mudstate.db_size = 0;
    mudstate.mail_db_top = 0;
    mudstate.mail_db_size = 0;
    mudstate.freelist = NOTHING;
    mudstate.markbits = nullptr;
    mudstate.func_nest_lev = 0;
    mudstate.func_invk_ctr = 0;
    mudstate.wild_invk_ctr = 0;
    mudstate.ntfy_nest_lev = 0;
    mudstate.train_nest_lev = 0;
    mudstate.lock_nest_lev = 0;
    mudstate.zone_nest_num = 0;
    mudstate.pipe_nest_lev = 0;
    mudstate.inpipe = false;
    mudstate.pout = nullptr;
    mudstate.poutnew = nullptr;
    mudstate.poutbufc = nullptr;
    mudstate.poutobj = NOTHING;
    for (i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        mudstate.global_regs[i] = nullptr;
    }
#if defined(STUB_SLAVE)
    mudstate.pResultsSet = nullptr;
    mudstate.iRow = RS_TOP;
#endif // STUB_SLAVE
    mudstate.nObjEvalNest = 0;
    mudstate.in_loop = 0;
    mudstate.bStackLimitReached = false;
    mudstate.nStackNest = 0;
    mudstate.nHearNest  = 0;
    mudstate.aHelpDesc = nullptr;
    mudstate.mHelpDesc = 0;
    mudstate.nHelpDesc = 0;
#if defined(STUB_SLAVE)
    mudstate.pISlaveControl = nullptr;
#endif // STUB_SLAVE
    mudstate.pIQueryControl = nullptr;
}

// ---------------------------------------------------------------------------
// cf_log_notfound: Log a 'parameter not found' error.
//
void cf_log_notfound(dbref player, const UTF8 *cmd, const UTF8 *thingname, const UTF8 *thing)
{
    if (mudstate.bReadingConfiguration)
    {
        STARTLOG(LOG_STARTUP, "CNF", "NFND");
        Log.tinyprintf(T("%s: %s %s not found"), cmd, thingname, thing);
        ENDLOG;
    }
    else
    {
        notify(player, tprintf(T("%s %s not found"), thingname, thing));
    }
}

// ---------------------------------------------------------------------------
// cf_log_syntax: Log a syntax error.
//
void DCL_CDECL cf_log_syntax(dbref player, __in_z UTF8 *cmd, __in_z const UTF8 *fmt, ...)
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
            mux_sprintf(buff, LBUF_SIZE, T("%s: Nothing to set"), cmd);
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

    int *aPorts = nullptr;
    try
    {
        aPorts = new int[nExtra];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == aPorts)
    {
        cf_log_syntax(player, cmd, T("Out of memory."));
        return -1;
    }

    unsigned int nPorts = 0;
    UTF8 *p;
    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, str);
    mux_strtok_ctl(&tts, T(" \t\n\r"));
    while (  (p = mux_strtok_parse(&tts)) != nullptr
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
        int *q = nullptr;
        try
        {
            q = new int[nPorts];
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == q)
        {
            cf_log_syntax(player, cmd, T("Out of memory."));
            return -1;
        }

        if (pia->pi)
        {
            delete [] pia->pi;
            pia->pi = nullptr;
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
    {(UTF8 *)nullptr, 0,  0,  0}
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
            Log.tinyprintf(T("%s: String truncated"), cmd);
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
            Log.tinyprintf(T("%s: String truncated"), cmd);
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
    if (*ppc != nullptr)
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
        if (nullptr == cp)
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

    if (  nullptr != oldname
       && '\0' != oldname[0]
       && nullptr != newname
       && '\0' != newname[0])
    {
        size_t nCased;
        UTF8 *pCased = mux_strupr(oldname, nCased);
        void *cp = hashfindLEN(pCased, nCased, (CHashTable *) vp);
        if (nullptr == cp)
        {
            cf_log_notfound(player, cmd, T("Entry"), oldname);
            return -1;
        }

        size_t bCased = nCased;
        UTF8 Buffer[LBUF_SIZE];
        mux_strncpy(Buffer, pCased, sizeof(Buffer)-1);
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

    if (  nullptr == alias
       || '\0' == alias[0]
       || nullptr == orig
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

    if (  nullptr == alias
       || '\0' == alias[0]
       || nullptr == orig
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
    while (sp != nullptr)
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
    while (sp != nullptr)
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
    while (sp != nullptr)
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

    while (sp != nullptr)
    {
        // Canonical Flag Name.
        //
        int  nName;
        bool bValid;
        UTF8 *pName = MakeCanonicalFlagName(sp, &nName, &bValid);
        FLAGNAMEENT *fp = nullptr;
        if (bValid)
        {
            fp = (FLAGNAMEENT *)hashfindLEN(pName, nName, &mudstate.flags_htab);
        }
        if (fp != nullptr)
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

// ---------------------------------------------------------------------------
// cf_site: Update site information

static CF_HAND(cf_site)
{
    UNUSED_PARAMETER(pExtra);

    mux_subnet *pSubnet = parse_subnet(str, player, cmd);
    if (nullptr == pSubnet)
    {
        return -1;
    }

    mux_subnets *subnets = (mux_subnets *)vp;
    switch (nExtra)
    {
    case HC_PERMIT:
        if (!subnets->permit(pSubnet))
        {
            return -1;
        }
        break;

    case HC_REGISTER:
        if (!subnets->registered(pSubnet))
        {
            return -1;
        }
        break;

    case HC_FORBID:
        if (!subnets->forbid(pSubnet))
        {
            return -1;
        }
        break;

    case HC_NOSITEMON:
        if (!subnets->nositemon(pSubnet))
        {
            return -1;
        }
        break;

    case HC_SITEMON:
        if (!subnets->sitemon(pSubnet))
        {
            return -1;
        }
        break;

    case HC_NOGUEST:
        if (!subnets->noguest(pSubnet))
        {
            return -1;
        }
        break;

    case HC_GUEST:
        if (!subnets->guest(pSubnet))
        {
            return -1;
        }
        break;

    case HC_SUSPECT:
        if (!subnets->suspect(pSubnet))
        {
            return -1;
        }
        break;

    case HC_TRUST:
        if (!subnets->trust(pSubnet))
        {
            return -1;
        }
        break;

    case HC_RESET:
        if (!subnets->reset(pSubnet))
        {
            delete pSubnet;
            return -1;
        }
        break;

    default:
        delete pSubnet;
        return -1;
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
    if (pBase == nullptr)
    {
        cf_log_syntax(player, cmd, T("Missing path for helpfile %s"), pCmdName);
        return -1;
    }
    if (  pCmdName[0] == '_'
       && pCmdName[1] == '_')
    {
        cf_log_syntax(player, cmd,
            T("Helpfile %s would conflict with the use of @addcommand."),
            pCmdName);
        return -1;
    }
    if (SBUF_SIZE <= strlen((char *)pBase))
    {
        cf_log_syntax(player, cmd, T("Helpfile \xE2\x80\x98%s\xE2\x80\x99 filename too long"), pBase);
        return -1;
    }

    // Allocate an empty place in the table of help file hashes.
    //
    if (mudstate.aHelpDesc == nullptr)
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

        if (nullptr == mudstate.aHelpDesc)
        {
            cf_log_syntax(player, cmd, T("Out of memory."));
            return -1;
        }
    }
    else if (mudstate.mHelpDesc <= mudstate.nHelpDesc)
    {
        int newsize = mudstate.mHelpDesc + 4;
        HELP_DESC *q = nullptr;
        try
        {
            q = new HELP_DESC[newsize];
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == mudstate.aHelpDesc)
        {
            cf_log_syntax(player, cmd, T("Out of memory."));
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
    pDesc->ht = nullptr;
    pDesc->pBaseFilename = StringClone(pBase);
    pDesc->bEval = bEval;

    // Build up Command Entry.
    //
    CMDENT_ONE_ARG *cmdp = nullptr;
    try
    {
        cmdp = new CMDENT_ONE_ARG;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr != cmdp)
    {
        cmdp->callseq = CS_ONE_ARG;
        cmdp->cmdname = StringClone(pCmdName);
        cmdp->extra = mudstate.nHelpDesc;
        cmdp->handler = do_help;
        cmdp->flags = CEF_ALLOC;
        cmdp->perms = CA_PUBLIC;
        cmdp->switches = nullptr;

        // TODO: If a command is deleted with one or both of the two
        // hashdeleteLEN() calls below, what guarantee do we have that parts
        // of the command weren't dynamically allocated.  This might leak
        // memory.
        //
        const UTF8 *p = cmdp->cmdname;
        hashdeleteLEN(p, strlen((const char *)p), &mudstate.command_htab);
        hashaddLEN(p, strlen((const char *)p), cmdp, &mudstate.command_htab);

        p = tprintf(T("__%s"), cmdp->cmdname);
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
    {T("after"),      3, 0, CEF_HOOK_AFTER},
    {T("before"),     3, 0, CEF_HOOK_BEFORE},
    {T("fail"),       3, 0, CEF_HOOK_AFAIL},
    {T("ignore"),     3, 0, CEF_HOOK_IGNORE},
    {T("igswitch"),   3, 0, CEF_HOOK_IGSWITCH},
    {T("permit"),     3, 0, CEF_HOOK_PERMIT},
    {T("args"),       3, 0, CEF_HOOK_ARGS},
    {(UTF8 *)nullptr, 0, 0, 0}
};

static CF_HAND(cf_hook)
{
    UNUSED_PARAMETER(vp);
    UNUSED_PARAMETER(pExtra);
    UNUSED_PARAMETER(nExtra);
    UNUSED_PARAMETER(player);
    UNUSED_PARAMETER(cmd);

    UTF8 *hookcmd, *hookptr, playbuff[201];
    int hookflg;
    CMDENT *cmdp;

    int retval = -1;
    memset(playbuff, '\0', sizeof(playbuff));
    mux_strncpy(playbuff, str, sizeof(playbuff));
    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, playbuff);
    mux_strtok_ctl(&tts, T(" \t"));
    hookcmd = mux_strtok_parse(&tts);
    if (hookcmd != nullptr)
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

    int t = cmdp->flags;
    mux_strncpy(playbuff, str, sizeof(playbuff)-1);
    hookptr = mux_strtok_parse(&tts);
    while (hookptr != nullptr)
    {
       if (  hookptr[0] == '!'
          && hookptr[1] != '\0')
       {
          if (search_nametab(GOD, hook_names, hookptr+1, &hookflg))
          {
             retval = 0;
             t = t & ~hookflg;
          }
       }
       else
       {
          if (search_nametab(GOD, hook_names, hookptr, &hookflg))
          {
             retval = 0;
             t = t | hookflg;
          }
       }
       hookptr = mux_strtok_parse(&tts);
    }
    cmdp->flags = t;
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

    if (nullptr == modname)
    {
        cf_log_syntax(player, cmd, T("Module name is missing."));
        return -1;
    }

    if (nullptr == modtype)
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
        cf_log_syntax(player, cmd, T("load type is invalid."));
        return -1;
    }

#if defined(STUB_SLAVE)
    if (  nullptr == mudstate.pISlaveControl
       && eLocal == eType)
    {
        cf_log_syntax(player, cmd, T("No StubSlave management interface available."));
        return -1;
    }
#else // STUB_SLAVE
    if (eLocal == eType)
    {
        cf_log_syntax(player, cmd, T("StubSlave is not enabled."));
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
#if defined(WINDOWS_FILES)
        size_t n;
        mux_sprintf(buffer, LBUF_SIZE, T(".\\bin\\%s.dll"), str);
        UTF16 *filename = ConvertFromUTF8ToUTF16(buffer, &n);
#elif defined(UNIX_FILES)
        mux_sprintf(buffer, LBUF_SIZE, T("./bin/%s.so"), str);
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
    if (nullptr == fgets((char *)buf, LBUF_SIZE, fp))
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

        if (nullptr == fgets((char *)buf, LBUF_SIZE, fp))
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
    {T("access"),                    cf_access,      CA_GOD,    CA_DISABLED, nullptr,                         access_nametab,     0},
    {T("alias"),                     cf_cmd_alias,   CA_GOD,    CA_DISABLED, (int *)&mudstate.command_htab,   0,                  0},
    {T("article_rule"),              cf_art_rule,    CA_GOD,    CA_DISABLED, (int *)&mudconf.art_rules,       nullptr,            0},
    {T("attr_access"),               cf_attr_access, CA_GOD,    CA_DISABLED, (int *)&mudstate.attrperm_list,  attraccess_nametab, 0},
    {T("attr_alias"),                cf_alias,       CA_GOD,    CA_DISABLED, (int *)&mudstate.attr_name_htab, 0,                  0},
    {T("attr_cmd_access"),           cf_acmd_access, CA_GOD,    CA_DISABLED, nullptr,                         access_nametab,     0},
    {T("attr_name_charset"),         cf_modify_bits, CA_GOD,    CA_PUBLIC,   &mudconf.attr_name_charset,      allow_charset_nametab, 0},
    {T("autozone"),                  cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.autozone,        nullptr,            0},
    {T("bad_name"),                  cf_badname,     CA_GOD,    CA_DISABLED, nullptr,                         nullptr,            0},
    {T("badsite_file"),              cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.site_file,       nullptr, SIZEOF_PATHNAME},
    {T("cache_names"),               cf_bool,        CA_STATIC, CA_GOD,      (int *)&mudconf.cache_names,     nullptr,            0},
    {T("cache_pages"),               cf_int,         CA_STATIC, CA_WIZARD,   &mudconf.cache_pages,            nullptr,            0},
    {T("cache_tick_period"),         cf_seconds,     CA_GOD,    CA_WIZARD,   (int *)&mudconf.cache_tick_period, nullptr,          0},
    {T("check_interval"),            cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.check_interval,         nullptr,            0},
    {T("check_offset"),              cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.check_offset,           nullptr,            0},
    {T("clone_copies_cost"),         cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.clone_copy_cost, nullptr,            0},
    {T("command_quota_increment"),   cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.cmd_quota_incr,         nullptr,            0},
    {T("command_quota_max"),         cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.cmd_quota_max,          nullptr,            0},
    {T("compress_program"),          cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.compress,        nullptr, SIZEOF_PATHNAME},
    {T("compression"),               cf_bool,        CA_GOD,    CA_GOD,      (int *)&mudconf.compress_db,     nullptr,            0},
    {T("comsys_database"),           cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.comsys_db,       nullptr, SIZEOF_PATHNAME},
    {T("config_access"),             cf_cf_access,   CA_GOD,    CA_DISABLED, nullptr,                         access_nametab,     0},
    {T("conn_timeout"),              cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.conn_timeout,           nullptr,            0},
    {T("connect_file"),              cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.conn_file,       nullptr, SIZEOF_PATHNAME},
    {T("connect_reg_file"),          cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.creg_file,       nullptr, SIZEOF_PATHNAME},
    {T("lag_limit"),                 cf_seconds,     CA_GOD,    CA_WIZARD,   (int *)&mudconf.max_cmdsecs,     nullptr,            0},
    {T("crash_database"),            cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.crashdb,         nullptr, SIZEOF_PATHNAME},
    {T("crash_message"),             cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.crash_msg,        nullptr,    GBUF_SIZE},
    {T("create_max_cost"),           cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.createmax,              nullptr,            0},
    {T("create_min_cost"),           cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.createmin,              nullptr,            0},
    {T("dark_sleepers"),             cf_bool,        CA_GOD,    CA_WIZARD,   (int *)&mudconf.dark_sleepers,   nullptr,            0},
    {T("default_charset"),           cf_option,      CA_GOD,    CA_PUBLIC,   &mudconf.default_charset,        default_charset_nametab, 0},
    {T("default_home"),              cf_dbref,       CA_GOD,    CA_PUBLIC,   &mudconf.default_home,           nullptr,            0},
    {T("destroy_going_now"),         cf_bool,        CA_GOD,    CA_WIZARD,   (int *)&mudconf.destroy_going_now, nullptr,          0},
    {T("dig_cost"),                  cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.digcost,                nullptr,            0},
    {T("down_file"),                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.down_file,       nullptr, SIZEOF_PATHNAME},
    {T("down_motd_message"),         cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.downmotd_msg,     nullptr,    GBUF_SIZE},
    {T("dump_interval"),             cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.dump_interval,          nullptr,            0},
    {T("dump_message"),              cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.dump_msg,         nullptr,          256},
    {T("dump_offset"),               cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.dump_offset,            nullptr,            0},
    {T("earn_limit"),                cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.paylimit,               nullptr,            0},
    {T("eval_comtitle"),             cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.eval_comtitle,   nullptr,            0},
    {T("events_daily_hour"),         cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.events_daily_hour,      nullptr,            0},
    {T("examine_flags"),             cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.ex_flags,        nullptr,            0},
    {T("examine_public_attrs"),      cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.exam_public,     nullptr,            0},
    {T("exit_flags"),                cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.exit_flags,      nullptr,            0},
    {T("exit_name_charset"),         cf_modify_bits, CA_GOD,    CA_PUBLIC,   &mudconf.exit_name_charset,      allow_charset_nametab, 0},
    {T("exit_quota"),                cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.exit_quota,             nullptr,            0},
    {T("exit_parent"),               cf_dbref,       CA_GOD,    CA_PUBLIC,   &mudconf.exit_parent,            nullptr,            0},
    {T("fascist_teleport"),          cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.fascist_tport,   nullptr,            0},
    {T("find_money_chance"),         cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.payfind,                nullptr,            0},
    {T("fixed_home_message"),        cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.fixed_home_msg,   nullptr,          128},
    {T("fixed_tel_message"),         cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.fixed_tel_msg,    nullptr,          128},
    {T("flag_access"),               cf_flag_access, CA_GOD,    CA_DISABLED, nullptr,                         nullptr,            0},
    {T("flag_alias"),                cf_flagalias,   CA_GOD,    CA_DISABLED, nullptr,                         nullptr,            0},
    {T("flag_name"),                 cf_flag_name,   CA_GOD,    CA_DISABLED, nullptr,                         nullptr,            0},
    {T("float_precision"),           cf_int,         CA_STATIC, CA_PUBLIC,   &mudconf.float_precision,        nullptr,            0},
    {T("forbid_site"),               cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    nullptr,    HC_FORBID},
#if defined(HAVE_WORKING_FORK)
    {T("fork_dump"),                 cf_bool,        CA_GOD,    CA_WIZARD,   (int *)&mudconf.fork_dump,       nullptr,            0},
#endif // HAVE_WORKING_FORK
    {T("full_file"),                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.full_file,       nullptr, SIZEOF_PATHNAME},
    {T("full_motd_message"),         cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.fullmotd_msg,     nullptr,    GBUF_SIZE},
    {T("function_access"),           cf_func_access, CA_GOD,    CA_DISABLED, nullptr,                         access_nametab,     0},
    {T("function_alias"),            cf_alias,       CA_GOD,    CA_DISABLED, (int *)&mudstate.func_htab,      nullptr,            0},
    {T("function_invocation_limit"), cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.func_invk_lim,          nullptr,            0},
    {T("function_name"),             cf_name,        CA_GOD,    CA_DISABLED, (int *)&mudstate.func_htab,      nullptr,            0},
    {T("function_recursion_limit"),  cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.func_nest_lim,          nullptr,            0},
    {T("game_dir_file"),             cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.game_dir,        nullptr, SIZEOF_PATHNAME},
    {T("game_pag_file"),             cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.game_pag,        nullptr, SIZEOF_PATHNAME},
    {T("global_error_obj"),          cf_dbref,       CA_GOD,    CA_GOD,      &mudconf.global_error_obj,       nullptr,            0},
    {T("good_name"),                 cf_badname,     CA_GOD,    CA_DISABLED, nullptr,                         nullptr,            1},
    {T("guest_char_num"),            cf_dbref,       CA_STATIC, CA_WIZARD,   &mudconf.guest_char,             nullptr,            0},
    {T("guest_file"),                cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.guest_file,      nullptr, SIZEOF_PATHNAME},
    {T("guest_nuker"),               cf_dbref,       CA_GOD,    CA_WIZARD,   &mudconf.guest_nuker,            nullptr,            0},
    {T("guest_prefix"),              cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.guest_prefix,     nullptr,           32},
    {T("guest_site"),                cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    nullptr,     HC_GUEST},
    {T("guests_channel"),            cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.guests_channel,   nullptr,           32},
    {T("guests_channel_alias"),      cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.guests_channel_alias, nullptr,       32},
    {T("have_comsys"),               cf_bool,        CA_STATIC, CA_PUBLIC,   (int *)&mudconf.have_comsys,     nullptr,            0},
    {T("have_mailer"),               cf_bool,        CA_STATIC, CA_PUBLIC,   (int *)&mudconf.have_mailer,     nullptr,            0},
    {T("have_zones"),                cf_bool,        CA_STATIC, CA_PUBLIC,   (int *)&mudconf.have_zones,      nullptr,            0},
    {T("help_executor"),             cf_dbref,       CA_GOD,    CA_WIZARD,   &mudconf.help_executor,          nullptr,            0},
    {T("helpfile"),                  cf_helpfile,    CA_STATIC, CA_DISABLED, nullptr,                         nullptr,            0},
    {T("hook_cmd"),                  cf_hook,        CA_GOD,    CA_GOD,      nullptr,                         nullptr,            0},
    {T("hook_obj"),                  cf_dbref,       CA_GOD,    CA_GOD,      &mudconf.hook_obj,               nullptr,            0},
    {T("hostnames"),                 cf_bool,        CA_GOD,    CA_WIZARD,   (int *)&mudconf.use_hostname,    nullptr,            0},
    {T("idle_interval"),             cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.idle_interval,          nullptr,            0},
    {T("idle_timeout"),              cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.idle_timeout,           nullptr,            0},
    {T("idle_wiz_dark"),             cf_bool,        CA_GOD,    CA_WIZARD,   (int *)&mudconf.idle_wiz_dark,   nullptr,            0},
    {T("include"),                   cf_include,     CA_STATIC, CA_DISABLED, nullptr,                         nullptr,            0},
    {T("indent_desc"),               cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.indent_desc,     nullptr,            0},
    {T("initial_size"),              cf_int,         CA_STATIC, CA_WIZARD,   &mudconf.init_size,              nullptr,            0},
    {T("input_database"),            cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.indb,            nullptr, SIZEOF_PATHNAME},
    {T("ip_address"),                cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.ip_address,      nullptr,    LBUF_SIZE},
    {T("keepalive_interval"),        cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.keepalive_interval,     nullptr,            0},
    {T("kill_guarantee_cost"),       cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.killguarantee,          nullptr,            0},
    {T("kill_max_cost"),             cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.killmax,                nullptr,            0},
    {T("kill_min_cost"),             cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.killmin,                nullptr,            0},
    {T("lag_maximum"),               cf_seconds,     CA_GOD,    CA_WIZARD,   (int *)&mudconf.rpt_cmdsecs,     nullptr,            0},
    {T("lbuf_size"),                 cf_int,       CA_DISABLED, CA_PUBLIC,   (int *)&mudconf.lbuf_size,       nullptr,            0},
    {T("link_cost"),                 cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.linkcost,               nullptr,            0},
    {T("list_access"),               cf_ntab_access, CA_GOD,    CA_DISABLED, (int *)list_names,               access_nametab,     0},
    {T("lock_recursion_limit"),      cf_int,         CA_WIZARD, CA_PUBLIC,   &mudconf.lock_nest_lim,          nullptr,            0},
    {T("log"),                       cf_modify_bits, CA_GOD,    CA_DISABLED, &mudconf.log_options,            logoptions_nametab, 0},
    {T("log_options"),               cf_modify_bits, CA_GOD,    CA_DISABLED, &mudconf.log_info,               logdata_nametab,    0},
    {T("logout_cmd_access"),         cf_ntab_access, CA_GOD,    CA_DISABLED, (int *)logout_cmdtable,          access_nametab,     0},
    {T("logout_cmd_alias"),          cf_alias,       CA_GOD,    CA_DISABLED, (int *)&mudstate.logout_cmd_htab,nullptr,            0},
    {T("look_obey_terse"),           cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.terse_look,      nullptr,            0},
    {T("machine_command_cost"),      cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.machinecost,            nullptr,            0},
    {T("mail_database"),             cf_string_dyn,  CA_GOD,    CA_GOD,      (int *)&mudconf.mail_db,         nullptr, SIZEOF_PATHNAME},
    {T("mail_expiration"),           cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.mail_expiration,        nullptr,            0},
    {T("mail_per_hour"),             cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.mail_per_hour,          nullptr,            0},
    {T("master_room"),               cf_dbref,       CA_GOD,    CA_WIZARD,   &mudconf.master_room,            nullptr,            0},
    {T("match_own_commands"),        cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.match_mine,      nullptr,            0},
    {T("max_cache_size"),            cf_int,         CA_GOD,    CA_GOD,      (int *)&mudconf.max_cache_size,  nullptr,            0},
    {T("max_players"),               cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.max_players,            nullptr,            0},
    {T("min_guests"),                cf_int,         CA_STATIC, CA_GOD,      (int *)&mudconf.min_guests,      nullptr,            0},
    {T("money_name_plural"),         cf_string,      CA_GOD,    CA_PUBLIC,   (int *)mudconf.many_coins,       nullptr,           32},
    {T("money_name_singular"),       cf_string,      CA_GOD,    CA_PUBLIC,   (int *)mudconf.one_coin,         nullptr,           32},
    {T("motd_file"),                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.motd_file,       nullptr, SIZEOF_PATHNAME},
    {T("motd_message"),              cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.motd_msg,         nullptr,    GBUF_SIZE},
    {T("module"),                    cf_module,      CA_GOD,    CA_WIZARD,   (int *)nullptr,                  nullptr,            0},
    {T("mud_name"),                  cf_string,      CA_GOD,    CA_PUBLIC,   (int *)mudconf.mud_name,         nullptr,           32},
    {T("newuser_file"),              cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.crea_file,       nullptr, SIZEOF_PATHNAME},
    {T("noguest_site"),              cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    nullptr,   HC_NOGUEST},
    {T("nositemon_site"),            cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    nullptr, HC_NOSITEMON},
    {T("notify_recursion_limit"),    cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.ntfy_nest_lim,          nullptr,            0},
    {T("number_guests"),             cf_int,         CA_STATIC, CA_WIZARD,   &mudconf.number_guests,          nullptr,            0},
    {T("open_cost"),                 cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.opencost,               nullptr,            0},
    {T("output_database"),           cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.outdb,           nullptr, SIZEOF_PATHNAME},
    {T("output_limit"),              cf_int,         CA_GOD,    CA_WIZARD,   (int *)&mudconf.output_limit,    nullptr,            0},
    {T("page_cost"),                 cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.pagecost,               nullptr,            0},
    {T("paranoid_allocate"),         cf_bool,        CA_GOD,    CA_WIZARD,   (int *)&mudconf.paranoid_alloc,  nullptr,            0},
    {T("parent_recursion_limit"),    cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.parent_nest_lim,        nullptr,            0},
    {T("password_methods"),          cf_modify_bits, CA_GOD,    CA_PUBLIC,   &mudconf.password_methods,       method_nametab,     0},
    {T("paycheck"),                  cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.paycheck,               nullptr,            0},
    {T("pemit_any_object"),          cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.pemit_any,       nullptr,            0},
    {T("pemit_far_players"),         cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.pemit_players,   nullptr,            0},
    {T("permit_site"),               cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    nullptr,    HC_PERMIT},
    {T("player_flags"),              cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.player_flags,    nullptr,            0},
    {T("player_listen"),             cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.player_listen,   nullptr,            0},
    {T("player_match_own_commands"), cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.match_mine_pl,   nullptr,            0},
    {T("player_name_spaces"),        cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.name_spaces,     nullptr,            0},
    {T("player_name_charset"),       cf_modify_bits, CA_GOD,    CA_PUBLIC,   &mudconf.player_name_charset,    allow_charset_nametab, 0},
    {T("player_parent"),             cf_dbref,       CA_GOD,    CA_PUBLIC,   &mudconf.player_parent,          nullptr,            0},
    {T("player_queue_limit"),        cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.queuemax,               nullptr,            0},
    {T("player_quota"),              cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.player_quota,           nullptr,            0},
    {T("player_starting_home"),      cf_dbref,       CA_GOD,    CA_PUBLIC,   &mudconf.start_home,             nullptr,            0},
    {T("player_starting_room"),      cf_dbref,       CA_GOD,    CA_PUBLIC,   &mudconf.start_room,             nullptr,            0},
    {T("port"),                      cf_int_array,   CA_STATIC, CA_PUBLIC,   (int *)&mudconf.ports,           nullptr, MAX_LISTEN_PORTS},
#ifdef UNIX_SSL
    {T("port_ssl"),                  cf_int_array,   CA_STATIC, CA_PUBLIC,   (int *)&mudconf.sslPorts,        nullptr, MAX_LISTEN_PORTS},
#endif
    {T("postdump_message"),          cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.postdump_msg,     nullptr,          256},
    {T("power_alias"),               cf_poweralias,  CA_GOD,    CA_DISABLED, nullptr,                         nullptr,            0},
    {T("pcreate_per_hour"),          cf_int,         CA_STATIC, CA_PUBLIC,   (int *)&mudconf.pcreate_per_hour,nullptr,            0},
    {T("public_channel"),            cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.public_channel,   nullptr,           32},
    {T("public_channel_alias"),      cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.public_channel_alias, nullptr,       32},
    {T("public_flags"),              cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.pub_flags,       nullptr,            0},
    {T("pueblo_message"),            cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.pueblo_msg,       nullptr,    GBUF_SIZE},
    {T("queue_active_chunk"),        cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.active_q_chunk,         nullptr,            0},
    {T("queue_idle_chunk"),          cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.queue_chunk,            nullptr,            0},
    {T("quiet_look"),                cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.quiet_look,      nullptr,            0},
    {T("quiet_whisper"),             cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.quiet_whisper,   nullptr,            0},
    {T("quit_file"),                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.quit_file,       nullptr, SIZEOF_PATHNAME},
    {T("quotas"),                    cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.quotas,          nullptr,            0},
    {T("raw_helpfile"),              cf_raw_helpfile,CA_STATIC, CA_DISABLED, nullptr,                         nullptr,            0},
    {T("read_remote_desc"),          cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.read_rem_desc,   nullptr,            0},
    {T("read_remote_name"),          cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.read_rem_name,   nullptr,            0},
    {T("references_per_hour"),       cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.references_per_hour,    nullptr,            0},
    {T("register_create_file"),      cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.regf_file,       nullptr, SIZEOF_PATHNAME},
    {T("register_site"),             cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    nullptr,  HC_REGISTER},
    {T("reset_players"),             cf_bool,        CA_GOD,    CA_DISABLED, (int *)&mudconf.reset_players,   nullptr,            0},
    {T("reset_site"),                cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    nullptr,     HC_RESET},
    {T("restrict_home"),             cf_bool,        CA_GOD,    CA_DISABLED, (int *)&mudconf.restrict_home,   nullptr,            0},
    {T("retry_limit"),               cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.retry_limit,            nullptr,            0},
    {T("robot_cost"),                cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.robotcost,              nullptr,            0},
    {T("robot_flags"),               cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.robot_flags,     nullptr,            0},
    {T("robot_speech"),              cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.robot_speak,     nullptr,            0},
    {T("room_flags"),                cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.room_flags,      nullptr,            0},
    {T("room_name_charset"),         cf_modify_bits, CA_GOD,    CA_PUBLIC,   &mudconf.room_name_charset,      allow_charset_nametab, 0},
    {T("room_parent"),               cf_dbref,       CA_GOD,    CA_PUBLIC,   &mudconf.room_parent,            nullptr,            0},
    {T("room_quota"),                cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.room_quota,             nullptr,            0},
    {T("run_startup"),               cf_bool,        CA_STATIC, CA_WIZARD,   (int *)&mudconf.run_startup,     nullptr,            0},
    {T("sacrifice_adjust"),          cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.sacadjust,              nullptr,            0},
    {T("sacrifice_factor"),          cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.sacfactor,              nullptr,            0},
    {T("safe_wipe"),                 cf_bool,        CA_GOD,    CA_WIZARD,   (int *)&mudconf.safe_wipe,       nullptr,            0},
    {T("safer_passwords"),           cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.safer_passwords, nullptr,            0},
    {T("search_cost"),               cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.searchcost,             nullptr,            0},
    {T("see_owned_dark"),            cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.see_own_dark,    nullptr,            0},
    {T("signal_action"),             cf_option,      CA_STATIC, CA_GOD,      &mudconf.sig_action,             sigactions_nametab, 0},
    {T("site_chars"),                cf_int,         CA_GOD,    CA_WIZARD,   (int *)&mudconf.site_chars,      nullptr,            0},
    {T("sitemon_site"),              cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    nullptr,   HC_SITEMON},
    {T("space_compress"),            cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.space_compress,  nullptr,            0},
#ifdef UNIX_SSL
    {T("ssl_certificate_file"),      cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.ssl_certificate_file,nullptr,       128},
    {T("ssl_certificate_key"),       cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.ssl_certificate_key, nullptr,       128},
    {T("ssl_certificate_password"),  cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.ssl_certificate_password, nullptr,  128},
#endif
    {T("stack_limit"),               cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.stack_limit,            nullptr,            0},
    {T("starting_money"),            cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.paystart,               nullptr,            0},
    {T("starting_quota"),            cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.start_quota,            nullptr,            0},
    {T("status_file"),               cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.status_file,     nullptr, SIZEOF_PATHNAME},
    {T("stripped_flags"),            cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.stripped_flags,  nullptr,            0},
    {T("suspect_site"),              cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    nullptr,   HC_SUSPECT},
    {T("sweep_dark"),                cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.sweep_dark,      nullptr,            0},
    {T("switch_default_all"),        cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.switch_df_all,   nullptr,            0},
    {T("terse_shows_contents"),      cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.terse_contents,  nullptr,            0},
    {T("terse_shows_exits"),         cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.terse_exits,     nullptr,            0},
    {T("terse_shows_move_messages"), cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.terse_movemsg,   nullptr,            0},
    {T("thing_flags"),               cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.thing_flags,     nullptr,            0},
    {T("thing_name_charset"),        cf_modify_bits, CA_GOD,    CA_PUBLIC,   &mudconf.thing_name_charset,     allow_charset_nametab, 0},
    {T("thing_parent"),              cf_dbref,       CA_GOD,    CA_PUBLIC,   &mudconf.thing_parent,           nullptr,            0},
    {T("thing_quota"),               cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.thing_quota,            nullptr,            0},
    {T("timeslice"),                 cf_seconds,     CA_GOD,    CA_PUBLIC,   (int *)&mudconf.timeslice,       nullptr,            0},
    {T("toad_recipient"),            cf_dbref,       CA_GOD,    CA_WIZARD,   &mudconf.toad_recipient,         nullptr,            0},
    {T("trace_output_limit"),        cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.trace_limit,            nullptr,            0},
    {T("trace_topdown"),             cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.trace_topdown,   nullptr,            0},
    {T("trust_site"),                cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    nullptr,     HC_TRUST},
    {T("uncompress_program"),        cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.uncompress,      nullptr, SIZEOF_PATHNAME},
    {T("unowned_safe"),              cf_bool,        CA_GOD,    CA_PUBLIC,   (int *)&mudconf.safe_unowned,    nullptr,            0},
    {T("user_attr_access"),          cf_modify_bits, CA_GOD,    CA_DISABLED, &mudconf.vattr_flags,            attraccess_nametab, 0},
    {T("user_attr_per_hour"),        cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.vattr_per_hour,         nullptr,            0},
    {T("wait_cost"),                 cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.waitcost,               nullptr,            0},
    {T("wizard_motd_file"),          cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.wizmotd_file,    nullptr, SIZEOF_PATHNAME},
    {T("wizard_motd_message"),       cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.wizmotd_msg,      nullptr,    GBUF_SIZE},
    {T("zone_recursion_limit"),      cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.zone_nest_lim,          nullptr,            0},
#ifdef REALITY_LVLS
    {T("reality_level"),             cf_rlevel,      CA_STATIC, CA_GOD,      (int *)&mudconf,                 nullptr,            0},
    {T("def_room_rx"),               cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_room_rx,     nullptr,            0},
    {T("def_room_tx"),               cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_room_tx,     nullptr,            0},
    {T("def_player_rx"),             cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_player_rx,   nullptr,            0},
    {T("def_player_tx"),             cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_player_tx,   nullptr,            0},
    {T("def_exit_rx"),               cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_exit_rx,     nullptr,            0},
    {T("def_exit_tx"),               cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_exit_tx,     nullptr,            0},
    {T("def_thing_rx"),              cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_thing_rx,    nullptr,            0},
    {T("def_thing_tx"),              cf_int,         CA_WIZARD, CA_PUBLIC,   (int *)&mudconf.def_thing_tx,    nullptr,            0},
#endif // REALITY_LVLS

#ifdef FIRANMUX
    {T("immobile_message"),          cf_string,      CA_WIZARD, CA_PUBLIC,   (int *)mudconf.immobile_msg,     nullptr,          128},
#endif // FIRANMUX
#if defined(INLINESQL)
    {T("sql_server"),                cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.sql_server,       nullptr,          128},
    {T("sql_user"),                  cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.sql_user,         nullptr,          128},
    {T("sql_password"),              cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.sql_password,     nullptr,          128},
    {T("sql_database"),              cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.sql_database,     nullptr,          128},
#endif
    {T("mail_server"),               cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.mail_server,      nullptr,          128},
    {T("mail_ehlo"),                 cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.mail_ehlo,        nullptr,          128},
    {T("mail_sendaddr"),             cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.mail_sendaddr,    nullptr,          128},
    {T("mail_sendname"),             cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.mail_sendname,    nullptr,          128},
    {T("mail_subject"),              cf_string,      CA_STATIC, CA_DISABLED, (int *)mudconf.mail_subject,     nullptr,          128},
    {(UTF8 *) nullptr,                 nullptr,           0,         0,      nullptr,                         nullptr,            0}
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
                    log_text(T(" with args \xE2\x80\x98"));
                    log_text(buff);
                    log_text(T("\xE2\x80\x99.  Status: "));
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
        { nullptr,                   NOTHING }
    };

    for (int i = 0; nullptr != Table[i].ploc; i++)
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
    retval = cf_include(nullptr, mudconf.config_file, (void *)0, 0, 0, (UTF8 *)"init");
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
