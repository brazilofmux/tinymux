// conf.cpp -- Set up configuration information and static data.
//
// $Id: conf.cpp,v 1.60 2002-01-24 07:57:33 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "interface.h"
#include "command.h"
#include "attrs.h"

// ---------------------------------------------------------------------------
// CONFPARM: Data used to find fields in CONFDATA.
//
typedef struct confparm
{
    char *pname;            // parm name
    int (*interpreter)(int *vp, char *str, void *pExtra, UINT32 nExtra,
                       dbref player, char *cmd); // routine to interp parameter
    int flags;              // control flags
    int rperms;             // read permissino flags.
    int *loc;               // where to store value
    void *pExtra;           // extra pointer for interpreter
    UINT32 nExtra;          // extra data for interpreter
} CONF;

// ---------------------------------------------------------------------------
// External symbols.
//
CONFDATA mudconf;
STATEDATA mudstate;

#ifndef STANDALONE
extern NAMETAB logdata_nametab[];
extern NAMETAB logoptions_nametab[];
extern NAMETAB access_nametab[];
extern NAMETAB attraccess_nametab[];
extern NAMETAB list_names[];
extern NAMETAB sigactions_nametab[];
extern CONF conftable[];

#endif // !STANDALONE

// ---------------------------------------------------------------------------
// cf_init: Initialize mudconf to default values.
//
void NDECL(cf_init)
{
#ifndef STANDALONE
    int i;

    mudconf.indb = StringClone("netmux.db");
    mudconf.outdb = StringClone("");
    mudconf.crashdb = StringClone("");
    mudconf.game_dir = StringClone("");
    mudconf.game_pag = StringClone("");
    mudconf.mail_db   = StringClone("mail.db");
    mudconf.comsys_db = StringClone("comsys.db");
    mudconf.compress_db = 0;
    mudconf.compress = StringClone("gzip");
    mudconf.uncompress = StringClone("gzip -d");
    mudconf.status_file = StringClone("shutdown.status");
    mudconf.max_cache_size = 1*1024*1024;

    mudconf.ports.n = 1;
    mudconf.ports.pi = (int *)MEMALLOC(sizeof(int));
    (void)ISOUTOFMEMORY(mudconf.ports.pi);
    mudconf.ports.pi[0] = 2860;

    mudconf.init_size = 1000;
    mudconf.guest_char = -1;
    mudconf.guest_nuker = 1;
    mudconf.number_guests = 30;
    strcpy(mudconf.guest_prefix, "Guest");
    mudconf.guest_file     = StringClone("text/guest.txt");
    mudconf.conn_file      = StringClone("text/connect.txt");
    mudconf.creg_file      = StringClone("text/register.txt");
    mudconf.regf_file      = StringClone("text/create_reg.txt");
    mudconf.motd_file      = StringClone("text/motd.txt");
    mudconf.wizmotd_file   = StringClone("text/wizmotd.txt");
    mudconf.quit_file      = StringClone("text/quit.txt");
    mudconf.down_file      = StringClone("text/down.txt");
    mudconf.full_file      = StringClone("text/full.txt");
    mudconf.site_file      = StringClone("text/badsite.txt");
    mudconf.crea_file      = StringClone("text/newuser.txt");
    mudconf.help_file      = StringClone("text/help.txt");
    mudconf.help_indx      = StringClone("text/help.indx");
    mudconf.news_file      = StringClone("text/news.txt");
    mudconf.news_indx      = StringClone("text/news.indx");
    mudconf.whelp_file     = StringClone("text/wizhelp.txt");
    mudconf.whelp_indx     = StringClone("text/wizhelp.indx");
    mudconf.plushelp_file  = StringClone("text/plushelp.txt");
    mudconf.plushelp_indx  = StringClone("text/plushelp.indx");
    mudconf.staffhelp_file = StringClone("text/staffhelp.txt");
    mudconf.staffhelp_indx = StringClone("text/staffhelp.indx");
    mudconf.wiznews_file   = StringClone("text/wiznews.txt");
    mudconf.wiznews_indx   = StringClone("text/wiznews.indx");
    mudconf.motd_msg[0] = '\0';
    mudconf.wizmotd_msg[0] = '\0';
    mudconf.downmotd_msg[0] = '\0';
    mudconf.fullmotd_msg[0] = '\0';
    mudconf.dump_msg[0] = '\0';
    mudconf.postdump_msg[0] = '\0';
    mudconf.fixed_home_msg[0] = '\0';
    mudconf.fixed_tel_msg[0] = '\0';
    strcpy(mudconf.public_channel, "Public");
    strcpy(mudconf.guests_channel, "Guests");
    strcpy(mudconf.pueblo_msg, "</xch_mudtext><img xch_mode=html>");
    mudconf.art_rules = NULL;
    mudconf.indent_desc = 0;
    mudconf.name_spaces = 1;
#ifndef WIN32
    mudconf.fork_dump = 1;
    mudstate.dumping = 0;
#endif
    mudconf.have_comsys = 1;
    mudconf.have_mailer = 1;
    mudconf.have_zones = 1;
    mudconf.paranoid_alloc = 0;
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
    mudconf.use_http = 0;
    mudconf.queuemax = 100;
    mudconf.queue_chunk = 10;
    mudconf.active_q_chunk = 10;
    mudconf.sacfactor = 5;
    mudconf.sacadjust = -1;
    mudconf.use_hostname = 1;
    mudconf.quotas = 0;
    mudconf.ex_flags = 1;
    mudconf.robot_speak = 1;
    mudconf.clone_copy_cost = 0;
    mudconf.pub_flags = 1;
    mudconf.quiet_look = 1;
    mudconf.exam_public = 1;
    mudconf.read_rem_desc = 0;
    mudconf.read_rem_name = 0;
    mudconf.sweep_dark = 0;
    mudconf.player_listen = 0;
    mudconf.quiet_whisper = 1;
    mudconf.dark_sleepers = 1;
    mudconf.see_own_dark = 1;
    mudconf.idle_wiz_dark = 0;
    mudconf.pemit_players = 0;
    mudconf.pemit_any = 0;
    mudconf.match_mine = 1;
    mudconf.match_mine_pl = 1;
    mudconf.switch_df_all = 1;
    mudconf.fascist_tport = 0;
    mudconf.terse_look = 1;
    mudconf.terse_contents = 1;
    mudconf.terse_exits = 1;
    mudconf.terse_movemsg = 1;
    mudconf.trace_topdown = 1;
    mudconf.trace_limit = 200;
    mudconf.safe_unowned = 0;
    mudconf.safer_passwords = 0;
    mudconf.autozone = TRUE;

    // -- ??? Running SC on a non-SC DB may cause problems.
    //
    mudconf.space_compress = 1;
    mudconf.allow_guest_from_registered_site = 1;
    mudconf.start_room = 0;
    mudconf.start_home = NOTHING;
    mudconf.default_home = NOTHING;
    mudconf.master_room = NOTHING;

    for (i = FLAG_WORD1; i <= FLAG_WORD3; i++)
    {
        mudconf.player_flags.word[i] = 0;
        mudconf.room_flags.word[i] = 0;
        mudconf.exit_flags.word[i]= 0;
        mudconf.thing_flags.word[i] = 0;
        mudconf.robot_flags.word[i] = 0;
    }
    mudconf.robot_flags.word[FLAG_WORD1] |= ROBOT;

    mudconf.vattr_flags = AF_ODARK;
    strcpy(mudconf.mud_name, "MUX");
    strcpy(mudconf.one_coin, "penny");
    strcpy(mudconf.many_coins, "pennies");
    mudconf.timeslice = 1000;
    mudconf.cmd_quota_max = 100;
    mudconf.cmd_quota_incr = 1;
    mudconf.max_cmdsecs = 120;
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
    mudconf.func_nest_lim = 50;
    mudconf.func_invk_lim = 2500;
    mudconf.ntfy_nest_lim = 20;
    mudconf.lock_nest_lim = 20;
    mudconf.parent_nest_lim = 10;
    mudconf.zone_nest_lim = 20;
    mudconf.stack_limit = 50;
    mudconf.cache_names = 1;
    mudconf.toad_recipient = -1;
    mudconf.eval_comtitle = 1;
    mudconf.run_startup = TRUE;
    mudconf.safe_wipe = FALSE;
    mudconf.destroy_going_now = FALSE;
    mudstate.events_flag = 0;
    mudstate.bReadingConfiguration = FALSE;
    mudstate.bCanRestart = FALSE;
    mudstate.panicking = 0;
    mudstate.logging = 0;
    mudstate.epoch = 0;
    mudstate.generation = 0;
    mudstate.curr_player = NOTHING;
    mudstate.curr_enactor = NOTHING;
    mudstate.shutdown_flag = 0;
    mudstate.attr_next = A_USER_START;
    mudstate.debug_cmd = "< init >";
    mudstate.curr_cmd  = "< none >";
    strcpy(mudstate.doing_hdr, "Doing");
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
    mudstate.ntfy_nest_lev = 0;
    mudstate.lock_nest_lev = 0;
    mudstate.zone_nest_num = 0;
    mudstate.inpipe = 0;
    mudstate.pout = NULL;
    mudstate.poutnew = NULL;
    mudstate.poutbufc = NULL;
    mudstate.poutobj = -1;
    for (i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        mudstate.global_regs[i] = NULL;
        mudstate.glob_reg_len[i] = 0;
    }
    mudstate.nObjEvalNest = 0;
#else // STANDALONE
    mudconf.paylimit = 10000;
    mudconf.digcost = 10;
    mudconf.opencost = 1;
    mudconf.robotcost = 1000;
    mudconf.createmin = 5;
    mudconf.createmax = 505;
    mudconf.sacfactor = 5;
    mudconf.sacadjust = -1;
    mudconf.room_quota = 1;
    mudconf.exit_quota = 1;
    mudconf.thing_quota = 1;
    mudconf.player_quota = 1;
    mudconf.quotas = 0;
    mudconf.start_room = 0;
    mudconf.start_home = NOTHING;
    mudconf.default_home = NOTHING;
    mudconf.vattr_flags = AF_ODARK;
    mudconf.log_options = 0xffffffff;
    mudconf.log_info = 0;
    mudconf.markdata[0] = 0x01;
    mudconf.markdata[1] = 0x02;
    mudconf.markdata[2] = 0x04;
    mudconf.markdata[3] = 0x08;
    mudconf.markdata[4] = 0x10;
    mudconf.markdata[5] = 0x20;
    mudconf.markdata[6] = 0x40;
    mudconf.markdata[7] = 0x80;
    mudconf.ntfy_nest_lim = 20;

    mudstate.logging = 0;
    mudstate.attr_next = A_USER_START;
    mudstate.iter_alist.data = NULL;
    mudstate.iter_alist.len = 0;
    mudstate.iter_alist.next = NULL;
    mudstate.mod_alist = NULL;
    mudstate.mod_alist_len = 0;
    mudstate.mod_size = 0;
    mudstate.mod_al_id = NOTHING;
    mudstate.min_size = 0;
    mudstate.db_top = 0;
    mudstate.db_size = 0;
    mudstate.freelist = NOTHING;
    mudstate.markbits = NULL;
#endif // STANDALONE
}

#ifndef STANDALONE

// ---------------------------------------------------------------------------
// cf_log_notfound: Log a 'parameter not found' error.
//
void cf_log_notfound(dbref player, char *cmd, const char *thingname, char *thing)
{
    char buff[LBUF_SIZE * 2];

    if (mudstate.bReadingConfiguration)
    {
        STARTLOG(LOG_STARTUP, "CNF", "NFND");
        sprintf(buff, "%s: %s %s not found", cmd, thingname, thing);
        log_text(buff);
        ENDLOG;
    }
    else
    {
        sprintf(buff, "%s %s not found", thingname, thing);
        notify(player, buff);
    }
}

// ---------------------------------------------------------------------------
// cf_log_syntax: Log a syntax error.
//
void DCL_CDECL cf_log_syntax(dbref player, char *cmd, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    char *buf = alloc_lbuf("cf_log_syntax");
    Tiny_vsnprintf(buf, LBUF_SIZE, fmt, ap);
    if (mudstate.bReadingConfiguration)
    {
        STARTLOG(LOG_STARTUP, "CNF", "SYNTX")
        log_text(cmd);
        log_text(": ");
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
int cf_status_from_succfail(dbref player, char *cmd, int success, int failure)
{
    char *buff;

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
            sprintf(buff, "%s: Nothing to set", cmd);
            log_text(buff);
            free_lbuf(buff);
            ENDLOG
        }
        else
        {
            notify(player, "Nothing to set");
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// cf_int_array: Setup array of integers.
//
CF_HAND(cf_int_array)
{
    int *aPorts = (int *)MEMALLOC(nExtra*sizeof(int));
    (void)ISOUTOFMEMORY(aPorts);
    unsigned int nPorts = 0;

    char *p;
    TINY_STRTOK_STATE tts;
    Tiny_StrTokString(&tts, str);
    Tiny_StrTokControl(&tts, " \t\n\r");
    while ((p = Tiny_StrTokParse(&tts)) != NULL)
    {
        int unused;
        if (is_integer(p, &unused))
        {
            aPorts[nPorts++] = Tiny_atol(p);
            if (nPorts >= nExtra)
            {
                break;
            }
        }
    }

    IntArray *pia = (IntArray *)vp;
    if (nPorts)
    {
        if (pia->pi)
        {
            MEMFREE(pia->pi);
            pia->pi = NULL;
        }
        pia->pi = (int *)MEMALLOC(nPorts * sizeof(int));
        (void)ISOUTOFMEMORY(pia->pi);
        pia->n = nPorts;
        for (unsigned int i = 0; i < nPorts; i++)
        {
            pia->pi[i] = aPorts[i];
        }
    }
    MEMFREE(aPorts);
    return 0;
}

// ---------------------------------------------------------------------------
// cf_int: Set integer parameter.
//
CF_HAND(cf_int)
{
    // Copy the numeric value to the parameter.
    //
    *vp = Tiny_atol(str);
    return 0;
}

// ---------------------------------------------------------------------------
// cf_bool: Set boolean parameter.
//
NAMETAB bool_names[] =
{
    {"true",    1,  0,  1},
    {"false",   1,  0,  0},
    {"yes",     1,  0,  1},
    {"no",      1,  0,  0},
    {"1",       1,  0,  1},
    {"0",       1,  0,  0},
    {NULL,      0,  0,  0}
};

CF_HAND(cf_bool)
{
    *vp = (int) search_nametab(GOD, bool_names, str);
    if (*vp < 0)
        *vp = (long) 0;
    return 0;
}

// ---------------------------------------------------------------------------
// cf_option: Select one option from many choices.
//
CF_HAND(cf_option)
{
    int i;

    i = search_nametab(GOD, (NAMETAB *) pExtra, str);
    if (i < 0)
    {
        cf_log_notfound(player, cmd, "Value", str);
        return -1;
    }
    *vp = i;
    return 0;
}

// ---------------------------------------------------------------------------
// cf_string: Set string parameter.
//
CF_HAND(cf_string)
{
    char *pc = (char *)vp;

    // The following should never happen because extra is always a non-zero
    // constant in the config table.
    //
    if (nExtra <= 0) return 1;

    // Copy the string to the buffer if it is not too big.
    //
    int retval = 0;
    unsigned int nStr = strlen(str);
    if (nStr >= nExtra)
    {
        nStr = nExtra - 1;
        if (mudstate.bReadingConfiguration)
        {
            STARTLOG(LOG_STARTUP, "CNF", "NFND");
            char *buff = alloc_lbuf("cf_string.LOG");
            sprintf(buff, "%s: String truncated", cmd);
            log_text(buff);
            free_lbuf(buff);
            ENDLOG;
        }
        else
        {
            notify(player, "String truncated");
        }
        retval = 1;
    }
    memcpy(pc, str, nStr+1);
    pc[nStr] = '\0';

#ifdef WIN32
    if (pc == mudconf.mud_name)
    {
        // We are changing the name of the MUD. Let the logger know.
        //
        Log.ChangePrefix(mudconf.mud_name);
    }
#endif // WIN32

    return retval;
}

// ---------------------------------------------------------------------------
// cf_string_dyn: Set string parameter using dynamically allocated memory.
//
CF_HAND(cf_string_dyn)
{
    char **ppc = (char **)vp;

    // Allocate memory for buffer and copy string to it. If nExtra is non-zero,
    // then there is a size limitation as well.
    //
    int retval = 0;
    unsigned int nStr = strlen(str);
    if (nExtra && nStr >= nExtra)
    {
        nStr = nExtra - 1;
        if (mudstate.bReadingConfiguration)
        {
            STARTLOG(LOG_STARTUP, "CNF", "NFND");
            char *logbuff = alloc_lbuf("cf_string.LOG");
            sprintf(logbuff, "%s: String truncated", cmd);
            log_text(logbuff);
            free_lbuf(logbuff);
            ENDLOG;
        }
        else
        {
            notify(player, "String truncated");
        }
        retval = 1;
    }
    char *confbuff = StringCloneLen(str, nStr);

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
CF_HAND(cf_alias)
{
    TINY_STRTOK_STATE tts;
    Tiny_StrTokString(&tts, str);
    Tiny_StrTokControl(&tts, " \t=,");
    char *alias = Tiny_StrTokParse(&tts);
    char *orig = Tiny_StrTokParse(&tts);

    if (orig)
    {
        _strlwr(orig);
        int *cp = hashfindLEN(orig, strlen(orig), (CHashTable *) vp);
        if (cp == NULL)
        {
            _strupr(orig);
            cp = hashfindLEN(orig, strlen(orig), (CHashTable *) vp);
            if (cp == NULL)
            {
                cf_log_notfound(player, cmd, "Entry", orig);
                return -1;
            }
        }
        hashaddLEN(alias, strlen(alias), cp, (CHashTable *) vp);
        return 0;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// cf_flagalias: define a flag alias.
//
CF_HAND(cf_flagalias)
{
    TINY_STRTOK_STATE tts;
    Tiny_StrTokString(&tts, str);
    Tiny_StrTokControl(&tts, " \t=,");
    char *alias = Tiny_StrTokParse(&tts);
    char *orig = Tiny_StrTokParse(&tts);

    int success = 0;
    int  nName;
    BOOL bValid;
    int *cp;
    char *pName = MakeCanonicalFlagName(orig, &nName, &bValid);
    if (  bValid
       && (cp = hashfindLEN(pName, nName, &mudstate.flags_htab)))
    {
        char *pName = MakeCanonicalFlagName(alias, &nName, &bValid);
        if (bValid)
        {
            hashaddLEN(pName, nName, cp, &mudstate.flags_htab);
            success++;
        }
    }
    if (!success)
    {
        cf_log_notfound(player, cmd, "Flag", orig);
    }
    return ((success > 0) ? 0 : -1);
}

// ---------------------------------------------------------------------------
// cf_or_in_bits: OR in bits from namelist to a word.
//
CF_HAND(cf_or_in_bits)
{
    int f, success, failure;

    // Walk through the tokens.
    //
    success = failure = 0;
    TINY_STRTOK_STATE tts;
    Tiny_StrTokString(&tts, str);
    Tiny_StrTokControl(&tts, " \t");
    char *sp = Tiny_StrTokParse(&tts);
    while (sp != NULL)
    {
        // Set the appropriate bit.
        //
        f = search_nametab(GOD, (NAMETAB *) pExtra, sp);
        if (f > 0)
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
        sp = Tiny_StrTokParse(&tts);
    }
    return cf_status_from_succfail(player, cmd, success, failure);
}

// ---------------------------------------------------------------------------
// cf_modify_bits: set or clear bits in a flag word from a namelist.
//
CF_HAND(cf_modify_bits)
{
    int f, negate, success, failure;

    // Walk through the tokens.
    //
    success = failure = 0;
    TINY_STRTOK_STATE tts;
    Tiny_StrTokString(&tts, str);
    Tiny_StrTokControl(&tts, " \t");
    char *sp = Tiny_StrTokParse(&tts);
    while (sp != NULL)
    {
        // Check for negation.
        //
        negate = 0;
        if (*sp == '!')
        {
            negate = 1;
            sp++;
        }

        // Set or clear the appropriate bit.
        //
        f = search_nametab(GOD, (NAMETAB *)pExtra, sp);
        if (f > 0)
        {
            if (negate)
                *vp &= ~f;
            else
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
        sp = Tiny_StrTokParse(&tts);
    }
    return cf_status_from_succfail(player, cmd, success, failure);
}

// ---------------------------------------------------------------------------
// cf_set_bits: Clear flag word and then set specified bits from namelist.
//
CF_HAND(cf_set_bits)
{
    int f, success, failure;

    // Walk through the tokens
    //
    success = failure = 0;
    *vp = 0;

    TINY_STRTOK_STATE tts;
    Tiny_StrTokString(&tts, str);
    Tiny_StrTokControl(&tts, " \t");
    char *sp = Tiny_StrTokParse(&tts);
    while (sp != NULL)
    {
        // Set the appropriate bit.
        //
        f = search_nametab(GOD, (NAMETAB *)pExtra, sp);
        if (f > 0)
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
        sp = Tiny_StrTokParse(&tts);
    }
    return cf_status_from_succfail(player, cmd, success, failure);
}

// ---------------------------------------------------------------------------
// cf_set_flags: Clear flag word and then set from a flags htab.
//
CF_HAND(cf_set_flags)
{
    int success, failure;

    // Walk through the tokens.
    //
    success = failure = 0;
    TINY_STRTOK_STATE tts;
    Tiny_StrTokString(&tts, str);
    Tiny_StrTokControl(&tts, " \t");
    char *sp = Tiny_StrTokParse(&tts);
    FLAGSET *fset = (FLAGSET *) vp;

    while (sp != NULL)
    {
        // Canonical Flag Name.
        //
        int  nName;
        BOOL bValid;
        char *pName = MakeCanonicalFlagName(sp, &nName, &bValid);
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
            cf_log_notfound(player, cmd, "Entry", sp);
            failure++;
        }

        // Get the next token
        //
        sp = Tiny_StrTokParse(&tts);
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
CF_HAND(cf_badname)
{
    if (nExtra)
        badname_remove(str);
    else
        badname_add(str);
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

static BOOL DecodeN(int nType, size_t len, const char *p, unsigned long *pu32)
{
    static DECODEIPV4 DecodeIPv4Table[4] =
    {
        { 8,         255UL,  3,  3, 2 },
        { 16,      65535UL,  6,  5, 4 },
        { 24,   16777215UL,  8,  8, 6 },
        { 32, 4294967295UL, 11, 10, 8 }
    };

    *pu32 <<= DecodeIPv4Table[nType].nShift;
    if (len == 0)
    {
        return FALSE;
    }
    unsigned long ul = 0;
    unsigned long ul2;
    if (len >= 3 && p[0] == '0' && Tiny_ToLower[(unsigned char)p[1]] == 'x')
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
            return FALSE;
        }
        while (len)
        {
            unsigned char ch = Tiny_ToLower[(unsigned char)*p];
            ul2 = ul;
            ul <<= 4;
            if (ul < ul2)
            {
                // Overflow
                //
                return FALSE;
            }
            if ('0' <= ch && ch <= '9')
            {
                ul |= ch - '0';
            }
            else if ('a' <= ch && ch <= 'f')
            {
                ul |= ch - 'a';
            }
            else
            {
                return FALSE;
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
            return FALSE;
        }
        while (len)
        {
            unsigned char ch = *p;
            ul2 = ul;
            ul <<= 3;
            if (ul < ul2)
            {
                // Overflow
                //
                return FALSE;
            }
            if ('0' <= ch && ch <= '7')
            {
                ul |= ch - '0';
            }
            else
            {
                return FALSE;
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
            return FALSE;
        }
        while (len)
        {
            unsigned char ch = *p;
            ul2 = ul;
            ul *= 10;
            if (ul < ul2)
            {
                // Overflow
                //
                return FALSE;
            }
            ul2 = ul;
            if ('0' <= ch && ch <= '9')
            {
                ul += ch - '0';
            }
            else
            {
                return FALSE;
            }
            if (ul < ul2)
            {
                // Overflow
                //
                return FALSE;
            }
            p++;
            len--;
        }
    }
    if (ul > DecodeIPv4Table[nType].maxValue)
    {
        return FALSE;
    }
    *pu32 |= ul;
    return TRUE;
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
static BOOL MakeCanonicalIPv4(const char *str, unsigned long *pnIP)
{
    *pnIP = 0;
    if (!str)
    {
        return FALSE;
    }

    // Skip leading spaces.
    //
    const char *q = str;
    while (*q == ' ')
    {
        q++;
    }

    char *p = strchr(q, '.');
    int n = 0;
    while (p)
    {
        // Decode
        //
        n++;
        if (n > 3)
        {
            return FALSE;
        }
        if (!DecodeN(0, p-q, q, pnIP))
        {
            return FALSE;
        }
        q = p + 1;
        p = strchr(q, '.');
    }

    // Decode last element.
    //
    size_t len = strlen(q);
    if (!DecodeN(3-n, len, q, pnIP))
    {
        return FALSE;
    }
    *pnIP = htonl(*pnIP);
    return TRUE;
}

// Given a host-ordered mask, this function will determine whether it is a
// valid one. Valid masks consist of a N-bit sequence of '1' bits followed by
// a (32-N)-bit sequence of '0' bits, where N is 0 to 32.
//
BOOL isValidSubnetMask(unsigned long ulMask)
{
    unsigned long ulTest = 0xFFFFFFFFUL;
    for (int i = 0; i <= 32; i++)
    {
        if (ulMask == ulTest)
        {
            return TRUE;
        }
        ulTest <<= 1;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// cf_site: Update site information

CF_HAND(cf_site)
{
    struct in_addr addr_num, mask_num;
    unsigned long ulMask, ulNetBits;

    char *addr_txt;
    char *mask_txt = strchr(str, '/');
    if (!mask_txt)
    {
        // Standard IP range and netmask notation.
        //
        TINY_STRTOK_STATE tts;
        Tiny_StrTokString(&tts, str);
        Tiny_StrTokControl(&tts, " \t=,");
        addr_txt = Tiny_StrTokParse(&tts);
        mask_txt = NULL;
        if (addr_txt)
        {
            mask_txt = Tiny_StrTokParse(&tts);
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
        int mask_bits = Tiny_atol(mask_txt);
        if ((mask_bits > 32) || (mask_bits < 0))
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
                ulMask = 0xFFFFFFFFUL << (32 - mask_bits);
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
    unsigned long ulAddr = ntohl(addr_num.s_addr);

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

    SITE *head = (SITE *) * vp;

    // Parse the access entry and allocate space for it.
    //
    SITE *site = (SITE *)MEMALLOC(sizeof(SITE));
    (void)ISOUTOFMEMORY(site);

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
            *vp = (int) site;
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
        *vp = (int) site;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// cf_cf_access: Set access on config directives
//
CF_HAND(cf_cf_access)
{
    CONF *tp;
    char *ap;

    for (ap = str; *ap && !Tiny_IsSpace[(unsigned char)*ap]; ap++)
    {
        ; // Nothing
    }
    if (*ap)
    {
        *ap++ = '\0';
    }

    for (tp = conftable; tp->pname; tp++)
    {
        if (!strcmp(tp->pname, str))
        {
            // Cannot modify parameters set CA_STATIC.
            //
            if (tp->flags & CA_STATIC)
            {
                notify(player, NOPERM_MESSAGE);
                STARTLOG(LOG_CONFIGMODS, "CFG", "PERM");
                log_name(player);
                log_text(" tried to change access to static param: ");
                log_text(tp->pname);
                ENDLOG;
                return -1;
            }
            return cf_modify_bits(&tp->flags, ap, pExtra, nExtra, player, cmd);
        }
    }
    cf_log_notfound(player, cmd, "Config directive", str);
    return -1;
}

// ---------------------------------------------------------------------------
// cf_include: Read another config file.  Only valid during startup.
//
CF_HAND(cf_include)
{
    FILE *fp;
    char *cp, *ap, *zp, *buf;

    extern int FDECL(cf_set, (char *, char *, dbref));


    if (!mudstate.bReadingConfiguration)
        return -1;

    fp = fopen(str, "rb");
    if (fp == NULL)
    {
        cf_log_notfound(player, cmd, "Config file", str);
        return -1;
    }
    DebugTotalFiles++;
    buf = alloc_lbuf("cf_include");
    fgets(buf, LBUF_SIZE, fp);
    while (!feof(fp))
    {
        cp = buf;
        if (*cp == '#')
        {
            fgets(buf, LBUF_SIZE, fp);
            continue;
        }

        // Not a comment line. Strip off the NL and any characters
        // following it.  Then, split the line into the command and argument
        // portions (separated by a space).  Also, trim off the trailing
        // comment, if any (delimited by #).
        //
        for (cp = buf; *cp && *cp != '\n'; cp++) ;

        // Strip '\n'
        //
        *cp = '\0';

        // Strip spaces.
        //
        for (cp = buf; Tiny_IsSpace[(unsigned char)*cp]; cp++) ;

        // Skip over command.
        //
        for (ap = cp; *ap && !Tiny_IsSpace[(unsigned char)*ap]; ap++) ;

        // Trim command.
        //
        if (*ap)
            *ap++ = '\0';

        // Skip Spaces.
        //
        for (; Tiny_IsSpace[(unsigned char)*ap]; ap++) ;

        // Find comment.
        //
        for (zp = ap; *zp && (*zp != '#'); zp++) ;

        // Zap comment.
        //
        if (*zp)
            *zp = '\0';

        // Zap trailing spaces.
        //
        for (zp = zp - 1; zp >= ap && Tiny_IsSpace[(unsigned char)*zp]; zp--)
            *zp = '\0';

        cf_set(cp, ap, player);
        fgets(buf, LBUF_SIZE, fp);
    }
    free_lbuf(buf);
    if (fclose(fp) == 0)
    {
        DebugTotalFiles--;
    }
    return 0;
}

extern CF_HAND(cf_access);
extern CF_HAND(cf_cmd_alias);
extern CF_HAND(cf_acmd_access);
extern CF_HAND(cf_attr_access);
extern CF_HAND(cf_func_access);
extern CF_HAND(cf_flag_access);
extern CF_HAND(cf_art_rule);

// ---------------------------------------------------------------------------
// conftable: Table for parsing the configuration file.

CONF conftable[] =
{
    {"access",                    cf_access,      CA_GOD,    CA_DISABLED, NULL,                            access_nametab,     0},
    {"alias",                     cf_cmd_alias,   CA_GOD,    CA_DISABLED, (int *)&mudstate.command_htab,   0,                  0},
    {"article_rule",              cf_art_rule,    CA_GOD,    CA_DISABLED, (int *)&mudconf.art_rules,       NULL,               0},
    {"attr_access",               cf_attr_access, CA_GOD,    CA_DISABLED, NULL,                            attraccess_nametab, 0},
    {"attr_alias",                cf_alias,       CA_GOD,    CA_DISABLED, (int *)&mudstate.attr_name_htab, 0,                  0},
    {"attr_cmd_access",           cf_acmd_access, CA_GOD,    CA_DISABLED, NULL,                            access_nametab,     0},
    {"bad_name",                  cf_badname,     CA_GOD,    CA_DISABLED, NULL,                            NULL,               0},
    {"badsite_file",              cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.site_file,       NULL, SIZEOF_PATHNAME},
    {"cache_names",               cf_bool,        CA_STATIC, CA_GOD,      &mudconf.cache_names,            NULL,               0},
    {"check_interval",            cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.check_interval,         NULL,               0},
    {"check_offset",              cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.check_offset,           NULL,               0},
    {"clone_copies_cost",         cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.clone_copy_cost,        NULL,               0},
    {"comsys_database",           cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.comsys_db,       NULL, SIZEOF_PATHNAME},
    {"command_quota_increment",   cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.cmd_quota_incr,         NULL,               0},
    {"command_quota_max",         cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.cmd_quota_max,          NULL,               0},
    {"compress_program",          cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.compress,        NULL, SIZEOF_PATHNAME},
    {"compression",               cf_bool,        CA_GOD,    CA_GOD,      &mudconf.compress_db,            NULL,               0},
    {"config_access",             cf_cf_access,   CA_GOD,    CA_DISABLED, NULL,                            access_nametab,     0},
    {"conn_timeout",              cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.conn_timeout,           NULL,               0},
    {"connect_file",              cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.conn_file,       NULL, SIZEOF_PATHNAME},
    {"connect_reg_file",          cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.creg_file,       NULL, SIZEOF_PATHNAME},
    {"crash_database",            cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.crashdb,         NULL, SIZEOF_PATHNAME},
    {"create_max_cost",           cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.createmax,              NULL,               0},
    {"create_min_cost",           cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.createmin,              NULL,               0},
    {"dark_sleepers",             cf_bool,        CA_GOD,    CA_WIZARD,   &mudconf.dark_sleepers,          NULL,               0},
    {"default_home",              cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.default_home,           NULL,               0},
    {"destroy_going_now",         cf_bool,        CA_GOD,    CA_WIZARD,   &mudconf.destroy_going_now,      NULL,               0},
    {"dig_cost",                  cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.digcost,                NULL,               0},
    {"down_file",                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.down_file,       NULL, SIZEOF_PATHNAME},
    {"down_motd_message",         cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.downmotd_msg,     NULL,       GBUF_SIZE},
    {"dump_interval",             cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.dump_interval,          NULL,               0},
    {"dump_message",              cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.dump_msg,         NULL,             128},
    {"postdump_message",          cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.postdump_msg,     NULL,             128},
    {"pueblo_message",            cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.pueblo_msg,       NULL,       GBUF_SIZE},
    {"dump_offset",               cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.dump_offset,            NULL,               0},
    {"earn_limit",                cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.paylimit,               NULL,               0},
    {"examine_flags",             cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.ex_flags,               NULL,               0},
    {"examine_public_attrs",      cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.exam_public,            NULL,               0},
    {"exit_flags",                cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.exit_flags,      NULL,               0},
    {"exit_quota",                cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.exit_quota,             NULL,               0},
    {"eval_comtitle",             cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.eval_comtitle,          NULL,               0},
    {"events_daily_hour",         cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.events_daily_hour,      NULL,               0},
    {"fascist_teleport",          cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.fascist_tport,          NULL,               0},
    {"fixed_home_message",        cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.fixed_home_msg,   NULL,             128},
    {"fixed_tel_message",         cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.fixed_tel_msg,    NULL,             128},
    {"find_money_chance",         cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.payfind,                NULL,               0},
    {"flag_access",               cf_flag_access, CA_GOD,    CA_DISABLED, NULL,                            NULL,               0},
    {"flag_alias",                cf_flagalias,   CA_GOD,    CA_DISABLED, NULL,                            NULL,               0},
    {"forbid_site",               cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    NULL,     H_FORBIDDEN},
#ifndef WIN32
    {"fork_dump",                 cf_bool,        CA_GOD,    CA_WIZARD,   &mudconf.fork_dump,              NULL,               0},
#endif
    {"full_file",                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.full_file,       NULL, SIZEOF_PATHNAME},
    {"full_motd_message",         cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.fullmotd_msg,     NULL,       GBUF_SIZE},
    {"function_access",           cf_func_access, CA_GOD,    CA_DISABLED, NULL,                            access_nametab,     0},
    {"function_alias",            cf_alias,       CA_GOD,    CA_DISABLED, (int *)&mudstate.func_htab,      NULL,               0},
    {"function_invocation_limit", cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.func_invk_lim,          NULL,               0},
    {"function_recursion_limit",  cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.func_nest_lim,          NULL,               0},
    {"game_dir_file",             cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.game_dir,        NULL, SIZEOF_PATHNAME},
    {"game_pag_file",             cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.game_pag,        NULL, SIZEOF_PATHNAME},
    {"good_name",                 cf_badname,     CA_GOD,    CA_DISABLED, NULL,                            NULL,               1},
    {"guest_char_num",            cf_int,         CA_STATIC, CA_WIZARD,   &mudconf.guest_char,             NULL,               0},
    {"guest_nuker",               cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.guest_nuker,            NULL,               0},
    {"guest_prefix",              cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.guest_prefix,     NULL,              32},
    {"guest_file",                cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.guest_file,      NULL, SIZEOF_PATHNAME},
    {"number_guests",             cf_int,         CA_STATIC, CA_WIZARD,   &mudconf.number_guests,          NULL,               0},
    {"guests_channel",            cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.guests_channel,   NULL,              32},
    {"guest_site",                cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    NULL,         H_GUEST},
    {"have_comsys",               cf_bool,        CA_STATIC, CA_PUBLIC,   &mudconf.have_comsys,            NULL,               0},
    {"have_mailer",               cf_bool,        CA_STATIC, CA_PUBLIC,   &mudconf.have_mailer,            NULL,               0},
    {"have_zones",                cf_bool,        CA_STATIC, CA_PUBLIC,   &mudconf.have_zones,             NULL,               0},
    {"help_file",                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.help_file,       NULL, SIZEOF_PATHNAME},
    {"help_index",                cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.help_indx,       NULL, SIZEOF_PATHNAME},
    {"hostnames",                 cf_bool,        CA_GOD,    CA_WIZARD,   &mudconf.use_hostname,           NULL,               0},
    {"use_http",                  cf_bool,        CA_STATIC, CA_PUBLIC,   &mudconf.use_http,               NULL,               0},
    {"idle_wiz_dark",             cf_bool,        CA_GOD,    CA_WIZARD,   &mudconf.idle_wiz_dark,          NULL,               0},
    {"idle_interval",             cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.idle_interval,          NULL,               0},
    {"idle_timeout",              cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.idle_timeout,           NULL,               0},
    {"include",                   cf_include,     CA_STATIC, CA_DISABLED, NULL,                            NULL,               0},
    {"indent_desc",               cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.indent_desc,            NULL,               0},
    {"initial_size",              cf_int,         CA_STATIC, CA_WIZARD,   &mudconf.init_size,              NULL,               0},
    {"input_database",            cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.indb,            NULL, SIZEOF_PATHNAME},
    {"kill_guarantee_cost",       cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.killguarantee,          NULL,               0},
    {"kill_max_cost",             cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.killmax,                NULL,               0},
    {"kill_min_cost",             cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.killmin,                NULL,               0},
    {"lag_maximum",               cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.max_cmdsecs,            NULL,               0},
    {"link_cost",                 cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.linkcost,               NULL,               0},
    {"list_access",               cf_ntab_access, CA_GOD,    CA_DISABLED, (int *)list_names,               access_nametab,     0},
    {"lock_recursion_limit",      cf_int,         CA_WIZARD, CA_PUBLIC,   &mudconf.lock_nest_lim,          NULL,               0},
    {"log",                       cf_modify_bits, CA_GOD,    CA_DISABLED, &mudconf.log_options,            logoptions_nametab, 0},
    {"log_options",               cf_modify_bits, CA_GOD,    CA_DISABLED, &mudconf.log_info,               logdata_nametab,    0},
    {"logout_cmd_access",         cf_ntab_access, CA_GOD,    CA_DISABLED, (int *)logout_cmdtable,          access_nametab,     0},
    {"logout_cmd_alias",          cf_alias,       CA_GOD,    CA_DISABLED, (int *)&mudstate.logout_cmd_htab,NULL,               0},
    {"look_obey_terse",           cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.terse_look,             NULL,               0},
    {"machine_command_cost",      cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.machinecost,            NULL,               0},
    {"mail_database",             cf_string_dyn,  CA_GOD,    CA_GOD,      (int *)&mudconf.mail_db,         NULL, SIZEOF_PATHNAME},
    {"mail_expiration",           cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.mail_expiration,        NULL,               0},
    {"master_room",               cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.master_room,            NULL,               0},
    {"match_own_commands",        cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.match_mine,             NULL,               0},
    {"max_players",               cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.max_players,            NULL,               0},
    {"max_cache_size",            cf_int,         CA_GOD,    CA_GOD,      (int *)&mudconf.max_cache_size,  NULL,               0},
    {"money_name_plural",         cf_string,      CA_GOD,    CA_PUBLIC,   (int *)mudconf.many_coins,       NULL,              32},
    {"money_name_singular",       cf_string,      CA_GOD,    CA_PUBLIC,   (int *)mudconf.one_coin,         NULL,              32},
    {"motd_file",                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.motd_file,       NULL, SIZEOF_PATHNAME},
    {"motd_message",              cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.motd_msg,         NULL,       GBUF_SIZE},
    {"mud_name",                  cf_string,      CA_GOD,    CA_PUBLIC,   (int *)mudconf.mud_name,         NULL,              32},
    {"news_file",                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.news_file,       NULL, SIZEOF_PATHNAME},
    {"news_index",                cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.news_indx,       NULL, SIZEOF_PATHNAME},
    {"newuser_file",              cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.crea_file,       NULL, SIZEOF_PATHNAME},
    {"notify_recursion_limit",    cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.ntfy_nest_lim,          NULL,               0},
    {"open_cost",                 cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.opencost,               NULL,               0},
    {"output_database",           cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.outdb,           NULL, SIZEOF_PATHNAME},
    {"output_limit",              cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.output_limit,           NULL,               0},
    {"page_cost",                 cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.pagecost,               NULL,               0},
    {"paranoid_allocate",         cf_bool,        CA_GOD,    CA_WIZARD,   &mudconf.paranoid_alloc,         NULL,               0},
    {"parent_recursion_limit",    cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.parent_nest_lim,        NULL,               0},
    {"paycheck",                  cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.paycheck,               NULL,               0},
    {"pemit_far_players",         cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.pemit_players,          NULL,               0},
    {"pemit_any_object",          cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.pemit_any,              NULL,               0},
    {"permit_site",               cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    NULL,               0},
    {"player_flags",              cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.player_flags,    NULL,               0},
    {"player_listen",             cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.player_listen,          NULL,               0},
    {"player_match_own_commands", cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.match_mine_pl,          NULL,               0},
    {"player_name_spaces",        cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.name_spaces,            NULL,               0},
    {"player_queue_limit",        cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.queuemax,               NULL,               0},
    {"player_quota",              cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.player_quota,           NULL,               0},
    {"player_starting_home",      cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.start_home,             NULL,               0},
    {"player_starting_room",      cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.start_room,             NULL,               0},
    {"plushelp_file",             cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.plushelp_file,   NULL, SIZEOF_PATHNAME},
    {"plushelp_index",            cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.plushelp_indx,   NULL, SIZEOF_PATHNAME},
    {"staffhelp_file",            cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.staffhelp_file,  NULL, SIZEOF_PATHNAME},
    {"staffhelp_index",           cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.staffhelp_indx,  NULL, SIZEOF_PATHNAME},
    {"public_channel",            cf_string,      CA_STATIC, CA_PUBLIC,   (int *)mudconf.public_channel,   NULL,              32},
    {"wiznews_file",              cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.wiznews_file,    NULL, SIZEOF_PATHNAME},
    {"wiznews_index",             cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.wiznews_indx,    NULL, SIZEOF_PATHNAME},
    {"port",                      cf_int_array,   CA_STATIC, CA_PUBLIC,   (int *)&mudconf.ports,           NULL, MAX_LISTEN_PORTS},
    {"public_flags",              cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.pub_flags,              NULL,               0},
    {"queue_active_chunk",        cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.active_q_chunk,         NULL,               0},
    {"queue_idle_chunk",          cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.queue_chunk,            NULL,               0},
    {"quiet_look",                cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.quiet_look,             NULL,               0},
    {"quiet_whisper",             cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.quiet_whisper,          NULL,               0},
    {"quit_file",                 cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.quit_file,       NULL, SIZEOF_PATHNAME},
    {"quotas",                    cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.quotas,                 NULL,               0},
    {"read_remote_desc",          cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.read_rem_desc,          NULL,               0},
    {"read_remote_name",          cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.read_rem_name,          NULL,               0},
    {"register_create_file",      cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.regf_file,       NULL, SIZEOF_PATHNAME},
    {"register_site",             cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.access_list,    NULL,  H_REGISTRATION},
    {"allow_guest_from_registered_site", cf_bool, CA_GOD,    CA_WIZARD,   &mudconf.allow_guest_from_registered_site, NULL,     1},
    {"retry_limit",               cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.retry_limit,            NULL,               0},
    {"robot_cost",                cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.robotcost,              NULL,               0},
    {"robot_flags",               cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.robot_flags,     NULL,               0},
    {"robot_speech",              cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.robot_speak,            NULL,               0},
    {"room_flags",                cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.room_flags,      NULL,               0},
    {"room_quota",                cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.room_quota,             NULL,               0},
    {"run_startup",               cf_bool,        CA_STATIC, CA_WIZARD,   &mudconf.run_startup,            NULL,               0},
    {"sacrifice_adjust",          cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.sacadjust,              NULL,               0},
    {"sacrifice_factor",          cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.sacfactor,              NULL,               0},
    {"safe_wipe",                 cf_bool,        CA_GOD,    CA_WIZARD,   &mudconf.safe_wipe,              NULL,               0},
    {"safer_passwords",           cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.safer_passwords,        NULL,               0},
    {"search_cost",               cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.searchcost,             NULL,               0},
    {"see_owned_dark",            cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.see_own_dark,           NULL,               0},
    {"signal_action",             cf_option,      CA_STATIC, CA_GOD,      &mudconf.sig_action,             sigactions_nametab, 0},
    {"site_chars",                cf_int,         CA_GOD,    CA_WIZARD,   (int *)&mudconf.site_chars,      NULL,               0},
    {"space_compress",            cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.space_compress,         NULL,               0},
    {"stack_limit",               cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.stack_limit,            NULL,               0},
    {"starting_money",            cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.paystart,               NULL,               0},
    {"starting_quota",            cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.start_quota,            NULL,               0},
    {"status_file",               cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.status_file,     NULL, SIZEOF_PATHNAME},
    {"suspect_site",              cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.suspect_list,   NULL,       H_SUSPECT},
    {"sweep_dark",                cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.sweep_dark,             NULL,               0},
    {"switch_default_all",        cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.switch_df_all,          NULL,               0},
    {"terse_shows_contents",      cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.terse_contents,         NULL,               0},
    {"terse_shows_exits",         cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.terse_exits,            NULL,               0},
    {"terse_shows_move_messages", cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.terse_movemsg,          NULL,               0},
    {"thing_flags",               cf_set_flags,   CA_GOD,    CA_DISABLED, (int *)&mudconf.thing_flags,     NULL,               0},
    {"thing_quota",               cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.thing_quota,            NULL,               0},
    {"timeslice",                 cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.timeslice,              NULL,               0},
    {"toad_recipient",            cf_int,         CA_GOD,    CA_WIZARD,   &mudconf.toad_recipient,         NULL,               0},
    {"trace_output_limit",        cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.trace_limit,            NULL,               0},
    {"trace_topdown",             cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.trace_topdown,          NULL,               0},
    {"trust_site",                cf_site,        CA_GOD,    CA_DISABLED, (int *)&mudstate.suspect_list,   NULL,               0},
    {"uncompress_program",        cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.uncompress,      NULL, SIZEOF_PATHNAME},
    {"unowned_safe",              cf_bool,        CA_GOD,    CA_PUBLIC,   &mudconf.safe_unowned,           NULL,               0},
    {"user_attr_access",          cf_modify_bits, CA_GOD,    CA_DISABLED, &mudconf.vattr_flags,            attraccess_nametab, 0},
    {"wait_cost",                 cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.waitcost,               NULL,               0},
    {"wizard_help_file",          cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.whelp_file,      NULL, SIZEOF_PATHNAME},
    {"wizard_help_index",         cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.whelp_indx,      NULL, SIZEOF_PATHNAME},
    {"wizard_motd_file",          cf_string_dyn,  CA_STATIC, CA_GOD,      (int *)&mudconf.wizmotd_file,    NULL, SIZEOF_PATHNAME},
    {"wizard_motd_message",       cf_string,      CA_GOD,    CA_WIZARD,   (int *)mudconf.wizmotd_msg,      NULL,       GBUF_SIZE},
    {"zone_recursion_limit",      cf_int,         CA_GOD,    CA_PUBLIC,   &mudconf.zone_nest_lim,          NULL,               0},
    { NULL,                       NULL,           0,         0,           NULL,                            NULL,               0}
};

// ---------------------------------------------------------------------------
// cf_set: Set config parameter.
//
int cf_set(char *cp, char *ap, dbref player)
{
    CONF *tp;
    int i;
    char *buff = 0;

    // Search the config parameter table for the command. If we find
    // it, call the handler to parse the argument.
    //
    for (tp = conftable; tp->pname; tp++)
    {
        if (!strcmp(tp->pname, cp))
        {
            if (  !mudstate.bReadingConfiguration
               && !check_access(player, tp->flags))
            {
                notify(player, NOPERM_MESSAGE);
                return -1;
            }
            if (!mudstate.bReadingConfiguration)
            {
                buff = alloc_lbuf("cf_set");
                strcpy(buff, ap);
            }
            i = tp->interpreter(tp->loc, ap, tp->pExtra, tp->nExtra, player, cp);
            if (!mudstate.bReadingConfiguration)
            {
                STARTLOG(LOG_CONFIGMODS, "CFG", "UPDAT");
                log_name(player);
                log_text(" entered config directive: ");
                log_text(cp);
                log_text(" with args '");
                log_text(buff);
                log_text("'.  Status: ");
                switch (i)
                {
                case 0:
                    log_text("Success.");
                    break;

                case 1:
                    log_text("Partial success.");
                    break;

                case -1:
                    log_text("Failure.");
                    break;

                default:
                    log_text("Strange.");
                }
                ENDLOG;
                free_lbuf(buff);
            }
            return i;
        }
    }

    // Config directive not found.  Complain about it.
    //
    cf_log_notfound(player, "Set", "Config directive", cp);
    return -1;
}

// Validate important dbrefs.
//
void ValidateConfigurationDbrefs(void)
{
    static dbref *Table[] =
    {
        &mudconf.default_home,
        &mudconf.guest_char,
        &mudconf.guest_nuker,
        &mudconf.master_room,
        &mudconf.start_home,
        &mudconf.start_room,
        0
    };

    for (int i = 0; Table[i]; i++)
    {
        if (*Table[i] != NOTHING)
        {
            if (*Table[i] < 0 || mudstate.db_top <= *Table[i])
            {
                *Table[i] = NOTHING;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// do_admin: Command handler to set config params at runtime
//
void do_admin
(
    dbref player,
    dbref cause,
    int   extra,
    int   nargs,
    char *kw,
    char *value
)
{
    int i = cf_set(kw, value, player);
    if ((i >= 0) && !Quiet(player))
    {
        notify(player, "Set.");
    }
    ValidateConfigurationDbrefs();
}

// ---------------------------------------------------------------------------
// cf_read: Read in config parameters from named file
//
struct
{
    char **pFilename;
    char *pSuffix;
} DefaultSuffixes[]
=
{
    { &mudconf.outdb,    ".out" },
    { &mudconf.crashdb,  ".CRASH" },
    { &mudconf.game_dir, ".dir" },
    { &mudconf.game_pag, ".pag" },
    { 0, 0 }
};

int cf_read(void)
{
    int retval;

    mudstate.bReadingConfiguration = TRUE;
    retval = cf_include(NULL, mudconf.config_file, (void *)0, 0, 0, "init");
    mudstate.bReadingConfiguration = FALSE;

    // Fill in missing DB file names.
    //
    unsigned int nInDB = strlen(mudconf.indb);
    for (int i = 0; DefaultSuffixes[i].pFilename; i++)
    {
        char **p = DefaultSuffixes[i].pFilename;
        if (**p == '\0')
        {
            // The filename is an empty string so we should construct
            // a default filename.
            //
            char *pSuffix = DefaultSuffixes[i].pSuffix;
            int nSuffix = strlen(pSuffix);
            char *buff = (char *)MEMALLOC(nInDB + nSuffix + 1);
            (void)ISOUTOFMEMORY(buff);
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
    CONF *tp;
    char *buff;

    buff = alloc_mbuf("list_cf_access");
    for (tp = conftable; tp->pname; tp++)
    {
        if (God(player) || check_access(player, tp->flags))
        {
            sprintf(buff, "%s:", tp->pname);
            listset_nametab(player, access_nametab, tp->flags, buff, 1);
        }
    }
    free_mbuf(buff);
}

// ---------------------------------------------------------------------------
// cf_display: Given a config parameter by name, return its value in some
// sane fashion.
//

void cf_display(dbref player, char *param_name, char *buff, char **bufc)
{
    CONF *tp;

    for (tp = conftable; tp->pname; tp++)
    {
        if (!_stricmp(tp->pname, param_name))
        {
            if (check_access(player, tp->rperms))
            {
                if (  tp->interpreter == cf_int
                   || tp->interpreter == cf_bool)
                {
                    safe_ltoa(*(tp->loc), buff, bufc);
                    return;
                }
                else if (tp->interpreter == cf_string)
                {
                    safe_str((char *)tp->loc, buff, bufc);
                    return;
                }
                else if (tp->interpreter == cf_string_dyn)
                {
                    safe_str(*(char **)tp->loc, buff, bufc);
                    return;
                }
            }
            safe_noperm(buff, bufc);
            return;
        }
    }
    safe_nomatch(buff, bufc);
}

#endif // !STANDALONE
