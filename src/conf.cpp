/*
 * conf.cpp: set up configuration information and static data 
 */
/*
 * $Id: conf.cpp,v 1.11 2000-05-24 08:36:45 sdennis Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "mudconf.h"
#include "db.h"
#include "interface.h"
#include "command.h"
#include "htab.h"
#include "alloc.h"
#include "attrs.h"
#include "flags.h"
#include "powers.h"
#include "match.h"

/* Some systems are lame, and inet_addr() claims to return -1 on failure,
 * despite the fact that it returns an unsigned long. (It's not really a -1,
 * obviously.) Better-behaved systems use INADDR_NONE.
 */
#ifndef INADDR_NONE
#define INADDR_NONE -1
#endif

/*
 * ---------------------------------------------------------------------------
 * * CONFPARM: Data used to find fields in CONFDATA.
 */

typedef struct confparm
{
    char *pname;            // parm name 
    int (*interpreter)(int *vp, char *str, unsigned int extra, dbref player, char *cmd);  // routine to interp parameter 
    int flags;              // control flags 
    int *loc;               // where to store value 
    long extra;             // extra data for interpreter 
} CONF;

/*
 * ---------------------------------------------------------------------------
 * * External symbols.
 */

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

#endif

#ifndef STANDALONE

/*
 * ---------------------------------------------------------------------------
 * * StringClone: allocate memory and copy string
 */

char *StringClone(const char *str)
{
    unsigned int nStr = strlen(str);
    char *buff = (char *)MEMALLOC(nStr+1, __FILE__, __LINE__);
    if (buff)
    {
        memcpy(buff, str, nStr+1);
    }
    else if (mudstate.initializing)
    {
        Log.WriteString("ABORT! conf.cpp, failed to allocate memory in StringClone().\n");
        Log.Flush();
        abort();
    }
    return buff;
}

#endif

/*
 * ---------------------------------------------------------------------------
 * * cf_init: Initialize mudconf to default values.
 */

void NDECL(cf_init)
{
#ifndef STANDALONE
    int i;

    mudconf.indb = StringClone("tinymush.db");
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
    mudconf.port = 2860;
    mudconf.conc_port = 2861;
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
    mudconf.indent_desc = 0;
    mudconf.name_spaces = 1;
#if !defined(VMS) && !defined(WIN32)
    mudconf.fork_dump = 1;
    mudconf.fork_vfork = 0;
    mudstate.dumping = 0;
#endif // WIN32
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

    // -- ??? Running SC on a non-SC DB may cause problems.
    //
    mudconf.space_compress = 1;
    mudconf.allow_guest_from_registered_site = 1;
    mudconf.start_room = 0;
    mudconf.start_home = NOTHING;
    mudconf.default_home = NOTHING;
    mudconf.master_room = NOTHING;
    mudconf.player_flags.word1 = 0;
    mudconf.player_flags.word2 = 0;
    mudconf.player_flags.word3 = 0;
    mudconf.room_flags.word1 = 0;
    mudconf.room_flags.word2 = 0;
    mudconf.room_flags.word3 = 0;
    mudconf.exit_flags.word1 = 0;
    mudconf.exit_flags.word2 = 0;
    mudconf.exit_flags.word3 = 0;
    mudconf.thing_flags.word1 = 0;
    mudconf.thing_flags.word2 = 0;
    mudconf.thing_flags.word3 = 0;
    mudconf.robot_flags.word1 = ROBOT;
    mudconf.robot_flags.word2 = 0;
    mudconf.robot_flags.word3 = 0;
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
        LOG_SHOUTS | LOG_STARTUP | LOG_WIZARD |
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
    mudstate.events_flag = 0;
    mudstate.initializing = 0;
    mudstate.panicking = 0;
    mudstate.logging = 0;
    mudstate.epoch = 0;
    mudstate.generation = 0;
    mudstate.curr_player = NOTHING;
    mudstate.curr_enactor = NOTHING;
    mudstate.shutdown_flag = 0;
    mudstate.attr_next = A_USER_START;
    mudstate.debug_cmd = (char *)"< init >";
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
    mudstate.mail_freelist = 0;
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
#else
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

/*
 * ---------------------------------------------------------------------------
 * * cf_log_notfound: Log a 'parameter not found' error.
 */

void cf_log_notfound(dbref player, char *cmd, const char *thingname, char *thing)
{
    char *buff;

    if (mudstate.initializing) 
    {
        STARTLOG(LOG_STARTUP, "CNF", "NFND")
        buff = alloc_lbuf("cf_log_notfound.LOG");
        sprintf(buff, "%s: %s %s not found", cmd, thingname, thing);
        log_text(buff);
        free_lbuf(buff);
        ENDLOG
    }
    else
    {
        buff = alloc_lbuf("cf_log_notfound");
        sprintf(buff, "%s %s not found", thingname, thing);
        notify(player, buff);
        free_lbuf(buff);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * cf_log_syntax: Log a syntax error.
 */
void DCL_CDECL cf_log_syntax(dbref player, char *cmd, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    char *buf = alloc_lbuf("cf_log_syntax");
    Tiny_vsnprintf(buf, LBUF_SIZE, fmt, ap);
    if (mudstate.initializing)
    {
        STARTLOG(LOG_STARTUP, "CNF", "SYNTX")
        log_text(cmd);
        log_text((char *)": ");
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

/*
 * ---------------------------------------------------------------------------
 * * cf_status_from_succfail: Return command status from succ and fail info
 */

int cf_status_from_succfail(dbref player, char *cmd, int success, int failure)
{
    char *buff;

    /*
     * If any successes, return SUCCESS(0) if no failures or * * * * *
     * PARTIAL_SUCCESS(1) if any failures. 
     */

    if (success > 0)
        return ((failure == 0) ? 0 : 1);

    /*
     * No successes.  If no failures indicate nothing done. Always return 
     * 
     * *  * *  * *  * *  * * FAILURE(-1) 
     */

    if (failure == 0)
    {
        if (mudstate.initializing)
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

/*
 * ---------------------------------------------------------------------------
 * * cf_int: Set integer parameter.
 */

CF_HAND(cf_int)
{
    // Copy the numeric value to the parameter.
    //
    *vp = Tiny_atol(str);
    return 0;
}

/* ---------------------------------------------------------------------------
 * cf_bool: Set boolean parameter.
 */

NAMETAB bool_names[] =
{
    {(char *)"true",    1,  0,  1},
    {(char *)"false",   1,  0,  0},
    {(char *)"yes",     1,  0,  1},
    {(char *)"no",      1,  0,  0},
    {(char *)"1",       1,  0,  1},
    {(char *)"0",       1,  0,  0},
    {NULL,          0,  0,  0}
};

CF_HAND(cf_bool)
{
    *vp = (int) search_nametab(GOD, bool_names, str);
    if (*vp < 0)
        *vp = (long) 0;
    return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * cf_option: Select one option from many choices.
 */

CF_HAND(cf_option)
{
    int i;

    i = search_nametab(GOD, (NAMETAB *) extra, str);
    if (i < 0)
    {
        cf_log_notfound(player, cmd, "Value", str);
        return -1;
    }
    *vp = i;
    return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * cf_string: Set string parameter.
 */

CF_HAND(cf_string)
{
    char *pc = (char *)vp;

    // The following should never happen because extra is always a non-zero
    // constant in the config table.
    //
    if (extra <= 0) return 1;

    // Copy the string to the buffer if it is not too big.
    //
    int retval = 0;
    unsigned int nStr = strlen(str);
    if (nStr >= extra)
    {
        nStr = extra - 1;
        if (mudstate.initializing)
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

/*
 * ---------------------------------------------------------------------------
 * * cf_string_dyn: Set string parameter using dynamically allocated memory.
 */

CF_HAND(cf_string_dyn)
{
    char **ppc = (char **)vp;

    // Allocate memory for buffer and copy string to it. If extra is non-zero,
    // then there is a size limitation as well.
    //
    int retval = 0;
    unsigned int nStr = strlen(str);
    if (extra && extra >= nStr)
    {                   
        nStr = extra - 1;
        if (mudstate.initializing)
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
    char *confbuff = (char *)MEMALLOC(nStr + 1, __FILE__, __LINE__);
    if (!confbuff)
    {
        if (mudstate.initializing)
        {
            Log.WriteString("ABORT! conf.cpp, failed to allocate memory in cf_string_dyn().\n");
            Log.Flush();
            abort();
        }
        else
        {
            notify(player, "Memory allocation failed, config unchanged");
            return 1;
        }
    }
    memcpy(confbuff, str, nStr + 1);
    confbuff[nStr] = '\0';

    // Free previous memory for buffer.
    //
    if (*ppc != NULL)
    {
        MEMFREE(*ppc, __FILE__, __LINE__);
    }
    *ppc = confbuff;

    return retval; 
}

/*
 * ---------------------------------------------------------------------------
 * * cf_alias: define a generic hash table alias.
 */

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

/*
 * ---------------------------------------------------------------------------
 * * cf_flagalias: define a flag alias.
 */

CF_HAND(cf_flagalias)
{
    TINY_STRTOK_STATE tts;
    Tiny_StrTokString(&tts, str);
    Tiny_StrTokControl(&tts, " \t=,");
    char *alias = Tiny_StrTokParse(&tts);
    char *orig = Tiny_StrTokParse(&tts);

    int success = 0;
    int *cp = hashfindLEN(orig, strlen(orig), &mudstate.flags_htab);
    if (cp != NULL)
    {
        hashaddLEN(alias, strlen(alias), cp, &mudstate.flags_htab);
        success++;
    }
    if (!success)
    {
        cf_log_notfound(player, cmd, "Flag", orig);
    }
    return ((success > 0) ? 0 : -1);
}

/*
 * ---------------------------------------------------------------------------
 * * cf_or_in_bits: OR in bits from namelist to a word.
 */

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
        f = search_nametab(GOD, (NAMETAB *) extra, sp);
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

/*
 * ---------------------------------------------------------------------------
 * * cf_modify_bits: set or clear bits in a flag word from a namelist.
 */
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
        f = search_nametab(GOD, (NAMETAB *) extra, sp);
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

/*
 * ---------------------------------------------------------------------------
 * * cf_set_bits: Clear flag word and then set specified bits from namelist.
 */

CF_HAND(cf_set_bits)
{
    int f, success, failure;

    /*
     * Walk through the tokens 
     */

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
        f = search_nametab(GOD, (NAMETAB *) extra, sp);
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

/*
 * ---------------------------------------------------------------------------
 * * cf_set_flags: Clear flag word and then set from a flags htab.
 */

CF_HAND(cf_set_flags)
{
    FLAGENT *fp;
    FLAGSET *fset;

    int success, failure;

    // Walk through the tokens.
    //
    success = failure = 0;
    TINY_STRTOK_STATE tts;
    Tiny_StrTokString(&tts, str);
    Tiny_StrTokControl(&tts, " \t");
    char *sp = Tiny_StrTokParse(&tts);
    fset = (FLAGSET *) vp;

    while (sp != NULL)
    {
        // Set the appropriate bit.
        //
        fp = (FLAGENT *) hashfindLEN(sp, strlen(sp), &mudstate.flags_htab);
        if (fp != NULL)
        {
            if (success == 0)
            {
                (*fset).word1 = 0;
                (*fset).word2 = 0;
                (*fset).word3 = 0;
            }
            if (fp->flagflag & FLAG_WORD3)
                (*fset).word3 |= fp->flagvalue;
            else if (fp->flagflag & FLAG_WORD2)
                (*fset).word2 |= fp->flagvalue;
            else
                (*fset).word1 |= fp->flagvalue;
            success++;
        }
        else
        {
            cf_log_notfound(player, cmd, "Entry", sp);
            failure++;
        }

        /*
         * Get the next token 
         */

        sp = Tiny_StrTokParse(&tts);
    }
    if ((success == 0) && (failure == 0))
    {
        (*fset).word1 = 0;
        (*fset).word2 = 0;
        (*fset).word3 = 0;
        return 0;
    }
    if (success > 0)
    {
        return ((failure == 0) ? 0 : 1);
    }
    return -1;
}

/*
 * ---------------------------------------------------------------------------
 * * cf_badname: Disallow use of player name/alias.
 */

CF_HAND(cf_badname)
{
    if (extra)
        badname_remove(str);
    else
        badname_add(str);
    return 0;
}

/* ---------------------------------------------------------------------------
 * sane_inet_addr: inet_addr() does not necessarily do reasonable checking
 * for sane syntax. On certain operating systems, if passed less than four
 * octets, it will cause a segmentation violation. This is unfriendly.
 * We take steps here to deal with it.
 *
 * This approach specifically disallows the Berkeley-only IP formats:
 *
 *    a.b.c (e.g., Class B 128.net.host)
 *    a.b   (e.g., class A: net.host)
 *    a     (single 32-bit number)
 *
 * Avoiding a SIGSEGV on certain operating systems is better than supporting
 * niche formats that are only available on Berkeley Unix.
 */
static unsigned long sane_inet_addr(char *str)
{
    int i;
    
    char *p = str;
    for (i = 1; (p = (char *) strchr(p, '.')) != NULL; i++, p++)
    {
        // Nothing
    }
    if (i < 4)
    {
        return INADDR_NONE;
    }
    else
    {
        return inet_addr(str);
    }
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

/*
 * ---------------------------------------------------------------------------
 * * cf_site: Update site information
 */

CF_HAND(cf_site)
{
    struct in_addr addr_num, mask_num;
    unsigned long ulMask;
    
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
            cf_log_syntax(player, cmd, "Missing host address or mask.", (char *)"");
            return -1;
        }
        mask_num.s_addr = sane_inet_addr(mask_txt);
        if (  mask_num.s_addr == INADDR_NONE
           || !isValidSubnetMask(ulMask = ntohl(mask_num.s_addr)))
        {
            cf_log_syntax(player, cmd, "Malformed mask address: %s", mask_txt);
            return -1;
        }
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
    addr_num.s_addr = sane_inet_addr(addr_txt);
    if (addr_num.s_addr == INADDR_NONE)
    {
        cf_log_syntax(player, cmd, "Malformed host address: %s", addr_txt);
        return -1;
    }
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
    SITE *site = (SITE *)MEMALLOC(sizeof(SITE), __FILE__, __LINE__);
    if (!site)
    {
        return -1;
    }
    
    // Initialize the site entry.
    //
    site->address.s_addr = addr_num.s_addr;
    site->mask.s_addr = mask_num.s_addr;
    site->flag = extra;
    site->next = NULL;
    
    // Link in the entry. Link it at the start if not initializing, at the
    // end if initializing. This is so that entries in the config file are
    // processed as you would think they would be, while entries made while
    // running are processed first.
    //
    if (mudstate.initializing)
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

/*
 * ---------------------------------------------------------------------------
 * * cf_cf_access: Set access on config directives
 */

CF_HAND(cf_cf_access)
{
    CONF *tp;
    char *ap;

    for (ap = str; *ap && !Tiny_IsSpace[(unsigned char)*ap]; ap++) ;
    if (*ap)
        *ap++ = '\0';

    for (tp = conftable; tp->pname; tp++) {
        if (!strcmp(tp->pname, str)) {
            return (cf_modify_bits(&tp->flags, ap, extra,
                           player, cmd));
        }
    }
    cf_log_notfound(player, cmd, "Config directive", str);
    return -1;
}

/*
 * ---------------------------------------------------------------------------
 * * cf_include: Read another config file.  Only valid during startup.
 */

CF_HAND(cf_include)
{
    FILE *fp;
    char *cp, *ap, *zp, *buf;

    extern int FDECL(cf_set, (char *, char *, dbref));


    if (!mudstate.initializing)
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

/* ---------------------------------------------------------------------------
 * conftable: Table for parsing the configuration file.
 */

CONF conftable[] =
{
    {(char *)"access",  cf_access,  CA_GOD,     NULL,   (long)access_nametab},
    {(char *)"alias",   cf_cmd_alias,   CA_GOD,     (int *)&mudstate.command_htab,  0},
    {(char *)"attr_access",     cf_attr_access, CA_GOD,     NULL,   (long)attraccess_nametab},
    {(char *)"attr_alias",  cf_alias,   CA_GOD,     (int *)&mudstate.attr_name_htab,0},
    {(char *)"attr_cmd_access", cf_acmd_access, CA_GOD,     NULL, (long)access_nametab},
    {(char *)"bad_name",    cf_badname, CA_GOD,     NULL,               0},
    {(char *)"badsite_file", cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.site_file,   0},
    {(char *)"cache_names",    cf_bool,    CA_DISABLED,    &mudconf.cache_names,       0},
    {(char *)"check_interval",    cf_int,     CA_GOD,     &mudconf.check_interval,    0},
    {(char *)"check_offset",    cf_int,     CA_GOD,     &mudconf.check_offset,      0},
    {(char *)"clone_copies_cost",    cf_bool,    CA_GOD,     &mudconf.clone_copy_cost,   0},
    {(char *)"comsys_database",    cf_string_dyn,  CA_GOD,     (int *)&mudconf.comsys_db,   0},
    {(char *)"command_quota_increment",    cf_int,     CA_GOD,     &mudconf.cmd_quota_incr,    0},
    {(char *)"command_quota_max",    cf_int,     CA_GOD,     &mudconf.cmd_quota_max,     0},
    {(char *)"compress_program",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.compress, 0},
    {(char *)"compression",    cf_bool,    CA_GOD,     &mudconf.compress_db,       0},
    {(char *)"concentrator_port",    cf_int,     CA_DISABLED,    &mudconf.conc_port,     0},
    {(char *)"config_access",    cf_cf_access,   CA_GOD,     NULL,    (long)access_nametab},
    {(char *)"conn_timeout",    cf_int,     CA_GOD,     &mudconf.conn_timeout,      0},
    {(char *)"connect_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.conn_file,   0},
    {(char *)"connect_reg_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.creg_file,   0},
    {(char *)"crash_database",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.crashdb,  0},
    {(char *)"create_max_cost",    cf_int,     CA_GOD,     &mudconf.createmax,     0},
    {(char *)"create_min_cost",    cf_int,     CA_GOD,     &mudconf.createmin,     0},
    {(char *)"dark_sleepers",    cf_bool,    CA_GOD,     &mudconf.dark_sleepers,     0},
    {(char *)"default_home",    cf_int,     CA_GOD,     &mudconf.default_home,      0},
    {(char *)"dig_cost",    cf_int,     CA_GOD,     &mudconf.digcost,       0},
    {(char *)"down_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.down_file,   0},
    {(char *)"down_motd_message",    cf_string,  CA_GOD,     (int *)mudconf.downmotd_msg,    GBUF_SIZE},
    {(char *)"dump_interval",    cf_int,     CA_GOD,     &mudconf.dump_interval,     0},
    {(char *)"dump_message",    cf_string,  CA_GOD,     (int *)mudconf.dump_msg,    128},
    {(char *)"postdump_message",    cf_string,  CA_GOD,     (int *)mudconf.postdump_msg,    128},
    {(char *)"pueblo_message",    cf_string,      CA_GOD,         (int *) mudconf.pueblo_msg,     GBUF_SIZE},
    {(char *)"dump_offset",    cf_int,     CA_GOD,     &mudconf.dump_offset,       0},
    {(char *)"earn_limit",    cf_int,     CA_GOD,     &mudconf.paylimit,      0},
    {(char *)"examine_flags",    cf_bool,    CA_GOD,     &mudconf.ex_flags,      0},
    {(char *)"examine_public_attrs",    cf_bool,    CA_GOD,     &mudconf.exam_public,       0},
    {(char *)"exit_flags",    cf_set_flags,   CA_GOD,     (int *)&mudconf.exit_flags, 0},
    {(char *)"exit_quota",    cf_int,     CA_GOD,     &mudconf.exit_quota,        0},
    {(char *)"events_daily_hour",    cf_int,     CA_GOD,     &mudconf.events_daily_hour, 0},
    {(char *)"fascist_teleport",    cf_bool,    CA_GOD,     &mudconf.fascist_tport,     0},
    {(char *)"fixed_home_message",    cf_string,  CA_DISABLED,    (int *)mudconf.fixed_home_msg,  128},
    {(char *)"fixed_tel_message",    cf_string,  CA_DISABLED,    (int *)mudconf.fixed_tel_msg,   128},
    {(char *)"find_money_chance",    cf_int,     CA_GOD,     &mudconf.payfind,       0},
    {(char *)"flag_access",   cf_flag_access, CA_GOD,     NULL,               0},
    {(char *)"flag_alias",    cf_flagalias,   CA_GOD,     NULL,               0},
    {(char *)"forbid_site",    cf_site,    CA_GOD,     (int *)&mudstate.access_list,    H_FORBIDDEN},
#if !defined(VMS) && !defined(WIN32)
    {(char *)"fork_dump",    cf_bool,    CA_GOD,     &mudconf.fork_dump,     0},
    {(char *)"fork_vfork",    cf_bool,    CA_GOD,     &mudconf.fork_vfork,        0},
#endif // WIN32
    {(char *)"full_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.full_file,   0},
    {(char *)"full_motd_message",    cf_string,  CA_GOD,     (int *)mudconf.fullmotd_msg,    GBUF_SIZE},
    {(char *)"function_access",    cf_func_access, CA_GOD,     NULL,    (long)access_nametab},
    {(char *)"function_alias",    cf_alias,   CA_GOD,     (int *)&mudstate.func_htab, 0},
    {(char *)"function_invocation_limit",    cf_int,         CA_GOD,         &mudconf.func_invk_lim,     0},
    {(char *)"function_recursion_limit",    cf_int,         CA_GOD,         &mudconf.func_nest_lim,     0},
    {(char *)"game_dir_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.game_dir,  0},
    {(char *)"game_pag_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.game_pag,  0},
    {(char *)"good_name",    cf_badname, CA_GOD,     NULL,               1},
    {(char *)"guest_char_num",    cf_int,     CA_DISABLED,    &mudconf.guest_char,        0},
    {(char *)"guest_nuker",        cf_int,         CA_GOD,         &mudconf.guest_nuker,           0},
    {(char *)"guest_prefix",        cf_string,      CA_DISABLED,    (int *)mudconf.guest_prefix,    32},
    {(char *)"number_guests",        cf_int,         CA_DISABLED,    &mudconf.number_guests,         0},
    {(char *)"guest_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.guest_file,  0},
    {(char *)"guests_channel",    cf_string,  CA_DISABLED,    (int *)mudconf.guests_channel,  32},
    {(char *)"guest_site",              cf_site,        CA_GOD,     (int *)&mudstate.access_list,   H_GUEST},
    {(char *)"have_comsys",    cf_bool,    CA_DISABLED,    &mudconf.have_comsys,       0},
    {(char *)"have_mailer",    cf_bool,    CA_DISABLED,    &mudconf.have_mailer,       0},
    {(char *)"have_zones",    cf_bool,    CA_DISABLED,    &mudconf.have_zones,        0},
    {(char *)"help_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.help_file,   0},
    {(char *)"help_index",    cf_string_dyn,  CA_DISABLED,   (int *)&mudconf.help_indx,   0},
    {(char *)"hostnames",    cf_bool,    CA_GOD,     &mudconf.use_hostname,      0},
    {(char *)"use_http",    cf_bool,    CA_DISABLED,    &mudconf.use_http,      0},
    {(char *)"idle_wiz_dark",    cf_bool,    CA_GOD,     &mudconf.idle_wiz_dark,     0},
    {(char *)"idle_interval",    cf_int,     CA_GOD,     &mudconf.idle_interval,     0},
    {(char *)"idle_timeout",    cf_int,     CA_GOD,     &mudconf.idle_timeout,      0},
    {(char *)"include",    cf_include, CA_DISABLED,    NULL,               0},
    {(char *)"indent_desc",    cf_bool,    CA_GOD,     &mudconf.indent_desc,       0},
    {(char *)"initial_size",    cf_int,     CA_DISABLED,    &mudconf.init_size,     0},
    {(char *)"input_database",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.indb,    0},
    {(char *)"kill_guarantee_cost",    cf_int,     CA_GOD,     &mudconf.killguarantee,     0},
    {(char *)"kill_max_cost",    cf_int,     CA_GOD,     &mudconf.killmax,       0},
    {(char *)"kill_min_cost",    cf_int,     CA_GOD,     &mudconf.killmin,       0},
    {(char *)"lag_maximum",             cf_int,         CA_GOD,     &mudconf.max_cmdsecs,       0},
    {(char *)"link_cost",    cf_int,     CA_GOD,     &mudconf.linkcost,      0},
    {(char *)"list_access",    cf_ntab_access, CA_GOD,     (int *)list_names,    (long)access_nametab},
    {(char *)"lock_recursion_limit",    cf_int,         CA_WIZARD,         &mudconf.lock_nest_lim,      0},
    {(char *)"log",    cf_modify_bits, CA_GOD,     &mudconf.log_options,    (long)logoptions_nametab},
    {(char *)"log_options",    cf_modify_bits, CA_GOD,     &mudconf.log_info,    (long)logdata_nametab},
    {(char *)"logout_cmd_access",    cf_ntab_access, CA_GOD,     (int *)logout_cmdtable,    (long)access_nametab},
    {(char *)"logout_cmd_alias",    cf_alias,   CA_GOD,     (int *)&mudstate.logout_cmd_htab,0},
    {(char *)"look_obey_terse",    cf_bool,    CA_GOD,     &mudconf.terse_look,        0},
    {(char *)"machine_command_cost",    cf_int,     CA_GOD,     &mudconf.machinecost,       0},
    {(char *)"mail_database",    cf_string_dyn,  CA_GOD,     (int *)&mudconf.mail_db,     0},
    {(char *)"mail_expiration",    cf_int,     CA_GOD,     &mudconf.mail_expiration,   0},
    {(char *)"master_room",    cf_int,     CA_GOD,     &mudconf.master_room,       0},
    {(char *)"match_own_commands",    cf_bool,    CA_GOD,     &mudconf.match_mine,        0},
    {(char *)"max_players",    cf_int,     CA_GOD,     &mudconf.max_players,       0},
    {(char *)"max_cache_size", cf_int,     CA_GOD,     (int *)&mudconf.max_cache_size,    0},
    {(char *)"money_name_plural",    cf_string,  CA_GOD,     (int *)mudconf.many_coins,  32},
    {(char *)"money_name_singular",    cf_string,  CA_GOD,     (int *)mudconf.one_coin,    32},
    {(char *)"motd_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.motd_file,   0},
    {(char *)"motd_message",    cf_string,  CA_GOD,     (int *)mudconf.motd_msg,    GBUF_SIZE},
    {(char *)"mud_name",    cf_string,  CA_GOD,     (int *)mudconf.mud_name,    32},
    {(char *)"news_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.news_file,   0},
    {(char *)"news_index",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.news_indx,   0},
    {(char *)"newuser_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.crea_file,   0},
    {(char *)"notify_recursion_limit",    cf_int,         CA_GOD,         &mudconf.ntfy_nest_lim,     0},
    {(char *)"open_cost",    cf_int,     CA_GOD,     &mudconf.opencost,      0},
    {(char *)"output_database",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.outdb,   0},
    {(char *)"output_limit",    cf_int,     CA_GOD,     &mudconf.output_limit,      0},
    {(char *)"page_cost",    cf_int,     CA_GOD,     &mudconf.pagecost,      0},
    {(char *)"paranoid_allocate",    cf_bool,    CA_GOD,     &mudconf.paranoid_alloc,    0},
    {(char *)"parent_recursion_limit",    cf_int,     CA_GOD,     &mudconf.parent_nest_lim,   0},
    {(char *)"paycheck",    cf_int,     CA_GOD,     &mudconf.paycheck,      0},
    {(char *)"pemit_far_players",    cf_bool,    CA_GOD,     &mudconf.pemit_players,     0},
    {(char *)"pemit_any_object",    cf_bool,    CA_GOD,     &mudconf.pemit_any,     0},
    {(char *)"permit_site",    cf_site,    CA_GOD,     (int *)&mudstate.access_list,   0},
    {(char *)"player_flags",    cf_set_flags,   CA_GOD,     (int *)&mudconf.player_flags,   0},
    {(char *)"player_listen",    cf_bool,    CA_GOD,     &mudconf.player_listen,     0},
    {(char *)"player_match_own_commands",    cf_bool,    CA_GOD,     &mudconf.match_mine_pl,     0},
    {(char *)"player_name_spaces",    cf_bool,    CA_GOD,     &mudconf.name_spaces,       0},
    {(char *)"player_queue_limit",    cf_int,     CA_GOD,     &mudconf.queuemax,      0},
    {(char *)"player_quota",    cf_int,     CA_GOD,     &mudconf.player_quota,      0},
    {(char *)"player_starting_home",    cf_int,     CA_GOD,     &mudconf.start_home,        0},
    {(char *)"player_starting_room",    cf_int,     CA_GOD,     &mudconf.start_room,        0},
    {(char *)"plushelp_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.plushelp_file,   0},
    {(char *)"plushelp_index",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.plushelp_indx,   0},
    {(char *)"staffhelp_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.staffhelp_file,  0},
    {(char *)"staffhelp_index",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.staffhelp_indx,  0},
    {(char *)"public_channel",    cf_string,  CA_DISABLED,    (int *)mudconf.public_channel,  32},
    {(char *)"wiznews_file",        cf_string_dyn,      CA_DISABLED,    (int *)&mudconf.wiznews_file,    0},
    {(char *)"wiznews_index",        cf_string_dyn,      CA_DISABLED,    (int *)&mudconf.wiznews_indx,    0},
    {(char *)"port",    cf_int,     CA_DISABLED,    &mudconf.port,          0},
    {(char *)"public_flags",    cf_bool,    CA_GOD,     &mudconf.pub_flags,     0},
    {(char *)"queue_active_chunk",    cf_int,     CA_GOD,     &mudconf.active_q_chunk,    0},
    {(char *)"queue_idle_chunk",    cf_int,     CA_GOD,     &mudconf.queue_chunk,       0},
    {(char *)"quiet_look",    cf_bool,    CA_GOD,     &mudconf.quiet_look,        0},
    {(char *)"quiet_whisper",    cf_bool,    CA_GOD,     &mudconf.quiet_whisper,     0},
    {(char *)"quit_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.quit_file,   0},
    {(char *)"quotas",    cf_bool,    CA_GOD,     &mudconf.quotas,        0},
    {(char *)"read_remote_desc",    cf_bool,    CA_GOD,     &mudconf.read_rem_desc,     0},
    {(char *)"read_remote_name",    cf_bool,    CA_GOD,     &mudconf.read_rem_name,     0},
    {(char *)"register_create_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.regf_file,   0},
    {(char *)"register_site",    cf_site,    CA_GOD,     (int *)&mudstate.access_list,    H_REGISTRATION},
    {(char *)"allow_guest_from_registered_site", cf_bool,    CA_GOD,     &mudconf.allow_guest_from_registered_site, 1},
    {(char *)"retry_limit",    cf_int,     CA_GOD,     &mudconf.retry_limit,       0},
    {(char *)"robot_cost",    cf_int,     CA_GOD,     &mudconf.robotcost,     0},
    {(char *)"robot_flags",    cf_set_flags,   CA_GOD,     (int *)&mudconf.robot_flags,    0},
    {(char *)"robot_speech",    cf_bool,    CA_GOD,     &mudconf.robot_speak,       0},
    {(char *)"room_flags",    cf_set_flags,   CA_GOD,     (int *)&mudconf.room_flags, 0},
    {(char *)"room_quota",    cf_int,     CA_GOD,     &mudconf.room_quota,        0},
    {(char *)"sacrifice_adjust",    cf_int,     CA_GOD,     &mudconf.sacadjust,     0},
    {(char *)"sacrifice_factor",    cf_int,     CA_GOD,     &mudconf.sacfactor,     0},
    {(char *)"safer_passwords",     cf_bool,    CA_GOD,     &mudconf.safer_passwords,   0},
    {(char *)"search_cost",    cf_int,     CA_GOD,     &mudconf.searchcost,        0},
    {(char *)"see_owned_dark",    cf_bool,    CA_GOD,     &mudconf.see_own_dark,      0},
    {(char *)"signal_action",    cf_option,  CA_DISABLED,    &mudconf.sig_action,    (long)sigactions_nametab},
    {(char *)"site_chars",    cf_int,     CA_GOD,     (int *)&mudconf.site_chars,        0},
    {(char *)"space_compress",    cf_bool,    CA_GOD,     &mudconf.space_compress,    0},
    {(char *)"stack_limit",    cf_int,     CA_GOD,     &mudconf.stack_limit,       0},
    {(char *)"starting_money",    cf_int,     CA_GOD,     &mudconf.paystart,      0},
    {(char *)"starting_quota",    cf_int,     CA_GOD,     &mudconf.start_quota,       0},
    {(char *)"status_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.status_file, 0},
    {(char *)"suspect_site",    cf_site,    CA_GOD,     (int *)&mudstate.suspect_list,    H_SUSPECT},
    {(char *)"sweep_dark",    cf_bool,    CA_GOD,     &mudconf.sweep_dark,        0},
    {(char *)"switch_default_all",    cf_bool,    CA_GOD,     &mudconf.switch_df_all,     0},
    {(char *)"terse_shows_contents",    cf_bool,    CA_GOD,     &mudconf.terse_contents,    0},
    {(char *)"terse_shows_exits",    cf_bool,    CA_GOD,     &mudconf.terse_exits,       0},
    {(char *)"terse_shows_move_messages",    cf_bool,    CA_GOD,     &mudconf.terse_movemsg,     0},
    {(char *)"thing_flags",    cf_set_flags,   CA_GOD,     (int *)&mudconf.thing_flags,    0},
    {(char *)"thing_quota",    cf_int,     CA_GOD,     &mudconf.thing_quota,       0},
    {(char *)"timeslice",    cf_int,     CA_GOD,     &mudconf.timeslice,     0},
    {(char *)"trace_output_limit",    cf_int,     CA_GOD,     &mudconf.trace_limit,       0},
    {(char *)"trace_topdown",    cf_bool,    CA_GOD,     &mudconf.trace_topdown,     0},
    {(char *)"trust_site",    cf_site,    CA_GOD,     (int *)&mudstate.suspect_list,  0},
    {(char *)"uncompress_program",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.uncompress,  0},
    {(char *)"unowned_safe",    cf_bool,    CA_GOD,     &mudconf.safe_unowned,      0},
    {(char *)"user_attr_access",    cf_modify_bits, CA_GOD,     &mudconf.vattr_flags,    (long)attraccess_nametab},
    {(char *)"wait_cost",    cf_int,     CA_GOD,     &mudconf.waitcost,      0},
    {(char *)"wizard_help_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.whelp_file,  0},
    {(char *)"wizard_help_index",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.whelp_indx,  0},
    {(char *)"wizard_motd_file",    cf_string_dyn,  CA_DISABLED,    (int *)&mudconf.wizmotd_file,    0},
    {(char *)"wizard_motd_message",    cf_string,  CA_GOD,     (int *)mudconf.wizmotd_msg, GBUF_SIZE},
    {(char *)"zone_recursion_limit",    cf_int,     CA_GOD,     &mudconf.zone_nest_lim,     0},
    { NULL,    NULL,       0,      NULL,               0}
};

/*
 * ---------------------------------------------------------------------------
 * * cf_set: Set config parameter.
 */

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
            if (  !mudstate.initializing
               && !check_access(player, tp->flags))
            {
                notify(player, NOPERM_MESSAGE);
                return -1;
            }
            if (!mudstate.initializing)
            {
                buff = alloc_lbuf("cf_set");
                strcpy(buff, ap);
            }
            i = tp->interpreter(tp->loc, ap, tp->extra, player, cp);
            if (!mudstate.initializing)
            {
                STARTLOG(LOG_CONFIGMODS, "CFG", "UPDAT");
                log_name(player);
                log_text((char *)" entered config directive: ");
                log_text(cp);
                log_text((char *)" with args '");
                log_text(buff);
                log_text((char *)"'.  Status: ");
                switch (i)
                {
                case 0:
                    log_text((char *)"Success.");
                    break;

                case 1:
                    log_text((char *)"Partial success.");
                    break;

                case -1:
                    log_text((char *)"Failure.");
                    break;

                default:
                    log_text((char *)"Strange.");
                }
                ENDLOG;
                free_lbuf(buff);
            }
            return i;
        }
    }

    // Config directive not found.  Complain about it.
    //
    cf_log_notfound(player, (char *)"Set", "Config directive", cp);
    return -1;
}

/*
 * ---------------------------------------------------------------------------
 * * do_admin: Command handler to set config params at runtime 
 */

void do_admin(dbref player, dbref cause, int extra, char *kw, char *value)
{
    int i;

    i = cf_set(kw, value, player);
    if ((i >= 0) && !Quiet(player))
        notify(player, "Set.");
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * cf_read: Read in config parameters from named file
 */

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

    mudstate.initializing = 1;
    retval = cf_include(NULL, mudconf.config_file, 0, 0, (char *)"init");
    mudstate.initializing = 0;

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
            char *buff = (char *)MEMALLOC(nInDB + nSuffix + 1, __FILE__, __LINE__);
            if (!buff)
            {
                Log.WriteString("ABORT! conf.cpp, failed to allocate memory in cf_read().\n");
                Log.Flush();
                abort();
            }
            memcpy(buff, mudconf.indb, nInDB);
            memcpy(buff + nInDB, pSuffix, nSuffix+1);
            MEMFREE(*p, __FILE__, __LINE__);
            *p = buff;
        }
    }
    return retval;
}

/*
 * ---------------------------------------------------------------------------
 * * list_cf_access: List access to config directives.
 */

void list_cf_access(dbref player)
{
    CONF *tp;
    char *buff;

    buff = alloc_mbuf("list_cf_access");
    for (tp = conftable; tp->pname; tp++) {
        if (God(player) || check_access(player, tp->flags)) {
            sprintf(buff, "%s:", tp->pname);
            listset_nametab(player, access_nametab, tp->flags,
                    buff, 1);
        }
    }
    free_mbuf(buff);
}

#endif /*
        * * STANDALONE  
        */
