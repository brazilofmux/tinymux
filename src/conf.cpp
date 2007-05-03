/*
 * conf.c:      set up configuration information and static data 
 */
/*
 * $Id: conf.c,v 1.2 1997/04/16 06:00:47 dpassmor Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"

#include "mudconf.h"
#include "db.h"
#include "externs.h"
#include "interface.h"
#include "command.h"
#include "htab.h"
#include "alloc.h"
#include "attrs.h"
#include "flags.h"
#include "powers.h"
#include "udb_defs.h"

/*
 * ---------------------------------------------------------------------------
 * * CONFPARM: Data used to find fields in CONFDATA.
 */

typedef struct confparm CONF;
struct confparm {
	char *pname;		/*
				 * parm name 
				 */
	int (*interpreter) ();	/*
				 * routine to interp parameter 
				 */
	int flags;		/*
				 * control flags 
				 */
	int *loc;		/*
				 * where to store value 
				 */
	long extra;		/*
				 * extra data for interpreter 
				 */
};

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

/*
 * ---------------------------------------------------------------------------
 * * cf_init: Initialize mudconf to default values.
 */

void NDECL(cf_init)
{
#ifndef STANDALONE
	int i;

	StringCopy(mudconf.indb, "tinymush.db");
	StringCopy(mudconf.outdb, "");
	StringCopy(mudconf.crashdb, "");
	StringCopy(mudconf.gdbm, "");
	StringCopy(mudconf.mail_db, "mail.db");
	StringCopy(mudconf.comsys_db, "comsys.db");
	mudconf.compress_db = 0;
	StringCopy(mudconf.compress, "gzip");
	StringCopy(mudconf.uncompress, "gzip -d");
	StringCopy(mudconf.status_file, "shutdown.status");
	mudconf.port = 6250;
	mudconf.conc_port = 6251;
	mudconf.init_size = 1000;
	mudconf.guest_char = -1;
	mudconf.guest_nuker = 1;
	mudconf.number_guests = 30;
	StringCopy(mudconf.guest_prefix, "Guest");
	StringCopy(mudconf.guest_file, "text/guest.txt");
	StringCopy(mudconf.conn_file, "text/connect.txt");
	StringCopy(mudconf.creg_file, "text/register.txt");
	StringCopy(mudconf.regf_file, "text/create_reg.txt");
	StringCopy(mudconf.motd_file, "text/motd.txt");
	StringCopy(mudconf.wizmotd_file, "text/wizmotd.txt");
	StringCopy(mudconf.quit_file, "text/quit.txt");
	StringCopy(mudconf.down_file, "text/down.txt");
	StringCopy(mudconf.full_file, "text/full.txt");
	StringCopy(mudconf.site_file, "text/badsite.txt");
	StringCopy(mudconf.crea_file, "text/newuser.txt");
	StringCopy(mudconf.help_file, "text/help.txt");
	StringCopy(mudconf.help_indx, "text/help.indx");
	StringCopy(mudconf.news_file, "text/news.txt");
	StringCopy(mudconf.news_indx, "text/news.indx");
	StringCopy(mudconf.whelp_file, "text/wizhelp.txt");
	StringCopy(mudconf.whelp_indx, "text/wizhelp.indx");
	StringCopy(mudconf.plushelp_file, "text/plushelp.txt");
	StringCopy(mudconf.plushelp_indx, "text/plushelp.indx");
	StringCopy(mudconf.wiznews_file, "text/wiznews.txt");
	StringCopy(mudconf.wiznews_indx, "text/wiznews.indx");
	StringCopy(mudconf.motd_msg, "");
	StringCopy(mudconf.wizmotd_msg, "");
	StringCopy(mudconf.downmotd_msg, "");
	StringCopy(mudconf.fullmotd_msg, "");
	StringCopy(mudconf.dump_msg, "");
	StringCopy(mudconf.postdump_msg, "");
	StringCopy(mudconf.fixed_home_msg, "");
	StringCopy(mudconf.fixed_tel_msg, "");
	StringCopy(mudconf.public_channel, "Public");
	StringCopy(mudconf.guests_channel, "Guests");
	StringCopy(mudconf.pueblo_msg, "</xch_mudtext><img xch_mode=html>");
	mudconf.indent_desc = 0;
       	mudconf.name_spaces = 1;
	mudconf.fork_dump = 1;
	mudconf.fork_vfork = 0;
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
	mudconf.queue_chunk = 3;
	mudconf.active_q_chunk = 0;
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
	mudconf.match_mine = 0;
	mudconf.match_mine_pl = 0;
	mudconf.switch_df_all = 1;
	mudconf.fascist_tport = 0;
	mudconf.terse_look = 1;
	mudconf.terse_contents = 1;
	mudconf.terse_exits = 1;
	mudconf.terse_movemsg = 1;
	mudconf.trace_topdown = 1;
	mudconf.trace_limit = 200;
	mudconf.safe_unowned = 0;
	/*
	 * -- ??? Running SC on a non-SC DB may cause problems 
	 */
	mudconf.space_compress = 1;
	mudconf.start_room = 0;
	mudconf.start_home = -1;
	mudconf.default_home = -1;
	mudconf.master_room = -1;
	mudconf.player_flags.word1 = 0;
	mudconf.player_flags.word2 = 0;
	mudconf.room_flags.word1 = 0;
	mudconf.room_flags.word2 = 0;
	mudconf.exit_flags.word1 = 0;
	mudconf.exit_flags.word2 = 0;
	mudconf.thing_flags.word1 = 0;
	mudconf.thing_flags.word2 = 0;
	mudconf.robot_flags.word1 = ROBOT;
	mudconf.robot_flags.word2 = 0;
	mudconf.vattr_flags = AF_ODARK;
	StringCopy(mudconf.mud_name, "TinyMUX");
	StringCopy(mudconf.one_coin, "penny");
	StringCopy(mudconf.many_coins, "pennies");
	mudconf.timeslice = 1000;
	mudconf.cmd_quota_max = 100;
	mudconf.cmd_quota_incr = 1;
	mudconf.control_flags = 0xffffffff;	/*
						 * Everything for now... 
						 */
	mudconf.log_options = LOG_ALWAYS | LOG_BUGS | LOG_SECURITY |
		LOG_NET | LOG_LOGIN | LOG_DBSAVES | LOG_CONFIGMODS |
		LOG_SHOUTS | LOG_STARTUP | LOG_WIZARD |
		LOG_PROBLEMS | LOG_PCREATES;
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
	mudconf.cache_trim = 0;
	mudconf.cache_depth = CACHE_DEPTH;
	mudconf.cache_width = CACHE_WIDTH;
	mudconf.cache_names = 1;

	mudstate.events_flag = 0;
	mudstate.initializing = 0;
	mudstate.panicking = 0;
	mudstate.dumping = 0;
	mudstate.logging = 0;
	mudstate.epoch = 0;
	mudstate.generation = 0;
	mudstate.curr_player = NOTHING;
	mudstate.curr_enactor = NOTHING;
	mudstate.shutdown_flag = 0;
	mudstate.attr_next = A_USER_START;
	mudstate.debug_cmd = (char *)"< init >";
	StringCopy(mudstate.doing_hdr, "Doing");
	mudstate.access_list = NULL;
	mudstate.suspect_list = NULL;
	mudstate.qfirst = NULL;
	mudstate.qlast = NULL;
	mudstate.qlfirst = NULL;
	mudstate.qllast = NULL;
	mudstate.qwait = NULL;
	mudstate.qsemfirst = NULL;
	mudstate.qsemlast = NULL;
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
	mudstate.mod_size = 0;
	mudstate.mod_al_id = NOTHING;
	mudstate.olist_head = NULL;
	mudstate.olist_tail = NULL;
	mudstate.olist_cblock = NULL;
	mudstate.olist_count = 0;
	mudstate.olist_citm = 0;
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
		mudstate.global_regs[i] = NULL;
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
	mudconf.start_home = -1;
	mudconf.default_home = -1;
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
	mudconf.cache_trim = 0;

	mudstate.logging = 0;
	mudstate.attr_next = A_USER_START;
	mudstate.iter_alist.data = NULL;
	mudstate.iter_alist.len = 0;
	mudstate.iter_alist.next = NULL;
	mudstate.mod_alist = NULL;
	mudstate.mod_size = 0;
	mudstate.mod_al_id = NOTHING;
	mudstate.min_size = 0;
	mudstate.db_top = 0;
	mudstate.db_size = 0;
	mudstate.freelist = NOTHING;
	mudstate.markbits = NULL;
#endif /*
        * * STANDALONE  
        */
}

#ifndef STANDALONE

/*
 * ---------------------------------------------------------------------------
 * * cf_log_notfound: Log a 'parameter not found' error.
 */

void cf_log_notfound(player, cmd, thingname, thing)
dbref player;
char *cmd, *thing;
const char *thingname;
{
	char *buff;

	if (mudstate.initializing) {
		STARTLOG(LOG_STARTUP, "CNF", "NFND")
			buff = alloc_lbuf("cf_log_notfound.LOG");
		sprintf(buff, "%s: %s %s not found",
			cmd, thingname, thing);
		log_text(buff);
		free_lbuf(buff);
		ENDLOG
	} else {
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

void cf_log_syntax(player, cmd, template, arg)
dbref player;
char *cmd, *arg;
const char *template;
{
	char *buff;

	if (mudstate.initializing) {
		STARTLOG(LOG_STARTUP, "CNF", "SYNTX")
			buff = alloc_lbuf("cf_log_syntax.LOG");
		sprintf(buff, template, arg);
		log_text(cmd);
		log_text((char *)": ");
		log_text(buff);
		free_lbuf(buff);
		ENDLOG
	} else {
		buff = alloc_lbuf("cf_log_syntax");
		sprintf(buff, template, arg);
		notify(player, buff);
		free_lbuf(buff);
	}
}

/*
 * ---------------------------------------------------------------------------
 * * cf_status_from_succfail: Return command status from succ and fail info
 */

int cf_status_from_succfail(player, cmd, success, failure)
dbref player;
char *cmd;
int success, failure;
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

	if (failure == 0) {
		if (mudstate.initializing) {
			STARTLOG(LOG_STARTUP, "CNF", "NDATA")
				buff = alloc_lbuf("cf_status_from_succfail.LOG");
			sprintf(buff, "%s: Nothing to set", cmd);
			log_text(buff);
			free_lbuf(buff);
			ENDLOG
		} else {
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
	/*
	 * Copy the numeric value to the parameter 
	 */

	sscanf(str, "%d", vp);
	return 0;
}
/* *INDENT-OFF* */

/* ---------------------------------------------------------------------------
 * cf_bool: Set boolean parameter.
 */

NAMETAB bool_names[] = {
{(char *)"true",	1,	0,	1},
{(char *)"false",	1,	0,	0},
{(char *)"yes",		1,	0,	1},
{(char *)"no",		1,	0,	0},
{(char *)"1",		1,	0,	1},
{(char *)"0",		1,	0,	0},
{NULL,			0,	0,	0}};

/* *INDENT-ON* */








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
	if (i < 0) {
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
	int retval;
	char *buff;

	/*
	 * Copy the string to the buffer if it is not too big 
	 */

	retval = 0;
	if (strlen(str) >= extra) {
		str[extra - 1] = '\0';
		if (mudstate.initializing) {
			STARTLOG(LOG_STARTUP, "CNF", "NFND")
				buff = alloc_lbuf("cf_string.LOG");
			sprintf(buff, "%s: String truncated", cmd);
			log_text(buff);
			free_lbuf(buff);
			ENDLOG
		} else {
			notify(player, "String truncated");
		}
		retval = 1;
	}
	StringCopy((char *)vp, str);
	return retval;
}

/*
 * ---------------------------------------------------------------------------
 * * cf_alias: define a generic hash table alias.
 */

CF_HAND(cf_alias)
{
	char *alias, *orig, *p;
	int *cp;

	alias = strtok(str, " \t=,");
	orig = strtok(NULL, " \t=,");
	if (orig) {
		for (p = orig; *p; p++)
			*p = ToLower(*p);
		cp = hashfind(orig, (HASHTAB *) vp);
		if (cp == NULL) {
			for (p = orig; *p; p++)
				*p = ToUpper(*p);
			cp = hashfind(orig, (HASHTAB *) vp);
			if (cp == NULL) {
				cf_log_notfound(player, cmd, "Entry", orig);
				return -1;
			}
		}
	}
	
	hashadd(alias, cp, (HASHTAB *) vp);
	return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * cf_flagalias: define a flag alias.
 */

CF_HAND(cf_flagalias)
{
	char *alias, *orig;
	int *cp, success;

	success = 0;
	alias = strtok(str, " \t=,");
	orig = strtok(NULL, " \t=,");

	cp = hashfind(orig, &mudstate.flags_htab);
	if (cp != NULL) {
		hashadd(alias, cp, &mudstate.flags_htab);
		success++;
	}
	if (!success)
		cf_log_notfound(player, cmd, "Flag", orig);
	return ((success > 0) ? 0 : -1);
}

/*
 * ---------------------------------------------------------------------------
 * * cf_or_in_bits: OR in bits from namelist to a word.
 */

CF_HAND(cf_or_in_bits)
{
	char *sp;
	int f, success, failure;

	/*
	 * Walk through the tokens 
	 */

	success = failure = 0;
	sp = strtok(str, " \t");
	while (sp != NULL) {

		/*
		 * Set the appropriate bit 
		 */

		f = search_nametab(GOD, (NAMETAB *) extra, sp);
		if (f > 0) {
			*vp |= f;
			success++;
		} else {
			cf_log_notfound(player, cmd, "Entry", sp);
			failure++;
		}

		/*
		 * Get the next token 
		 */

		sp = strtok(NULL, " \t");
	}
	return cf_status_from_succfail(player, cmd, success, failure);
}

/*
 * ---------------------------------------------------------------------------
 * * cf_modify_bits: set or clear bits in a flag word from a namelist.
 */
CF_HAND(cf_modify_bits)
{
	char *sp;
	int f, negate, success, failure;

	/*
	 * Walk through the tokens 
	 */

	success = failure = 0;
	sp = strtok(str, " \t");
	while (sp != NULL) {

		/*
		 * Check for negation 
		 */

		negate = 0;
		if (*sp == '!') {
			negate = 1;
			sp++;
		}
		/*
		 * Set or clear the appropriate bit 
		 */

		f = search_nametab(GOD, (NAMETAB *) extra, sp);
		if (f > 0) {
			if (negate)
				*vp &= ~f;
			else
				*vp |= f;
			success++;
		} else {
			cf_log_notfound(player, cmd, "Entry", sp);
			failure++;
		}

		/*
		 * Get the next token 
		 */

		sp = strtok(NULL, " \t");
	}
	return cf_status_from_succfail(player, cmd, success, failure);
}

/*
 * ---------------------------------------------------------------------------
 * * cf_set_bits: Clear flag word and then set specified bits from namelist.
 */

CF_HAND(cf_set_bits)
{
	char *sp;
	int f, success, failure;

	/*
	 * Walk through the tokens 
	 */

	success = failure = 0;
	*vp = 0;
	sp = strtok(str, " \t");
	while (sp != NULL) {

		/*
		 * Set the appropriate bit 
		 */

		f = search_nametab(GOD, (NAMETAB *) extra, sp);
		if (f > 0) {
			*vp |= f;
			success++;
		} else {
			cf_log_notfound(player, cmd, "Entry", sp);
			failure++;
		}

		/*
		 * Get the next token 
		 */

		sp = strtok(NULL, " \t");
	}
	return cf_status_from_succfail(player, cmd, success, failure);
}

/*
 * ---------------------------------------------------------------------------
 * * cf_set_flags: Clear flag word and then set from a flags htab.
 */

CF_HAND(cf_set_flags)
{
	char *sp;
	FLAGENT *fp;
	FLAGSET *fset;

	int success, failure;

	/*
	 * Walk through the tokens 
	 */

	success = failure = 0;
	sp = strtok(str, " \t");
	fset = (FLAGSET *) vp;

	while (sp != NULL) {

		/*
		 * Set the appropriate bit 
		 */

		fp = (FLAGENT *) hashfind(sp, &mudstate.flags_htab);
		if (fp != NULL) {
			if (success == 0) {
				(*fset).word1 = 0;
				(*fset).word2 = 0;
			}
			if (fp->flagflag & FLAG_WORD3)
				(*fset).word3 |= fp->flagvalue;
			else if (fp->flagflag & FLAG_WORD2)
				(*fset).word2 |= fp->flagvalue;
			else
				(*fset).word1 |= fp->flagvalue;
			success++;
		} else {
			cf_log_notfound(player, cmd, "Entry", sp);
			failure++;
		}

		/*
		 * Get the next token 
		 */

		sp = strtok(NULL, " \t");
	}
	if ((success == 0) && (failure == 0)) {
		(*fset).word1 = 0;
		(*fset).word2 = 0;
		return 0;
	}
	if (success > 0)
		return ((failure == 0) ? 0 : 1);
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

/*
 * ---------------------------------------------------------------------------
 * * cf_site: Update site information
 */

CF_AHAND(cf_site)
{
	SITE *site, *last, *head;
	char *addr_txt, *mask_txt;
	struct in_addr addr_num, mask_num;

	addr_txt = strtok(str, " \t=,");
	mask_txt = NULL;
	if (addr_txt)
		mask_txt = strtok(NULL, " \t=,");
	if (!addr_txt || !*addr_txt || !mask_txt || !*mask_txt) {
		cf_log_syntax(player, cmd, "Missing host address or mask.",
			      (char *)"");
		return -1;
	}
	addr_num.s_addr = inet_addr(addr_txt);
	mask_num.s_addr = inet_addr(mask_txt);

	if (addr_num.s_addr == -1) {
		cf_log_syntax(player, cmd, "Bad host address: %s", addr_txt);
		return -1;
	}
	head = (SITE *) * vp;
	/*
	 * Parse the access entry and allocate space for it 
	 */

	site = (SITE *) malloc(sizeof(SITE));

	/*
	 * Initialize the site entry 
	 */

	site->address.s_addr = addr_num.s_addr;
	site->mask.s_addr = mask_num.s_addr;
	site->flag = extra;
	site->next = NULL;

	/*
	 * Link in the entry.  Link it at the start if not initializing, at * 
	 * 
	 * *  * *  * *  * *  * * the end if initializing.  This is so that
	 * entries  * in * the * config * * * file are processed as you would 
	 * think they * * would be, * while * entries * * made while running
	 * are processed * * first. 
	 */

	if (mudstate.initializing) {
		if (head == NULL) {
			*vp = (long *)site;
		} else {
			for (last = head; last->next; last = last->next) ;
			last->next = site;
		}
	} else {
		site->next = head;
		*vp = (long *)site;
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

	for (ap = str; *ap && !isspace(*ap); ap++) ;
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

	fp = fopen(str, "r");
	if (fp == NULL) {
		cf_log_notfound(player, cmd, "Config file", str);
		return -1;
	}
	buf = alloc_lbuf("cf_include");
	fgets(buf, LBUF_SIZE, fp);
	while (!feof(fp)) {
		cp = buf;
		if (*cp == '#') {
			fgets(buf, LBUF_SIZE, fp);
			continue;
		}
		/*
		 * Not a comment line.  Strip off the NL and any characters * 
		 * 
		 * *  * *  * *  * *  * * following it.  Then, split the line
		 * into * the *  * command and  * *  * argument portions
		 * (separated * by a * * space).  Also, trim  * off * * the
		 * trailing * comment, if * * any (delimited by #) 
		 */

		for (cp = buf; *cp && *cp != '\n'; cp++) ;
		*cp = '\0';	/*
				 * strip \n 
				 */
		for (cp = buf; *cp && isspace(*cp); cp++) ;	/*
								 * strip * *
								 * * * *
								 * spaces  
								 */
		for (ap = cp; *ap && !isspace(*ap); ap++) ;	/*
								 * skip over
								 * * * * * *
								 * * * *
								 * command 
								 */
		if (*ap)
			*ap++ = '\0';	/*
					 * trim command 
					 */
		for (; *ap && isspace(*ap); ap++) ;	/*
							 * skip spaces 
							 */
		for (zp = ap; *zp && (*zp != '#'); zp++) ;	/*
								 * find * * * 
								 * 
								 * *  * *
								 * comment 
								 */
		if (*zp)
			*zp = '\0';	/*
					 * zap comment 
					 */
		for (zp = zp - 1; zp >= ap && isspace(*zp); zp--)
			*zp = '\0';	/*
					 * zap trailing spcs 
					 */

		cf_set(cp, ap, player);
		fgets(buf, LBUF_SIZE, fp);
	}
	free_lbuf(buf);
	fclose(fp);
	return 0;
}

extern CF_HDCL(cf_access);
extern CF_HDCL(cf_cmd_alias);
extern CF_HDCL(cf_acmd_access);
extern CF_HDCL(cf_attr_access);
extern CF_HDCL(cf_func_access);
/* *INDENT-OFF* */

/* ---------------------------------------------------------------------------
 * conftable: Table for parsing the configuration file.
 */

CONF conftable[] = {
{(char *)"access",
	cf_access,	CA_GOD,		NULL,
	(long)access_nametab},
{(char *)"alias",
	cf_cmd_alias,	CA_GOD,		(int *)&mudstate.command_htab,	0},
{(char *)"attr_access",
	cf_attr_access,	CA_GOD,		NULL,
	(long)attraccess_nametab},
{(char *)"attr_alias",
	cf_alias,	CA_GOD,		(int *)&mudstate.attr_name_htab,0},
{(char *)"attr_cmd_access",
	cf_acmd_access,	CA_GOD,		NULL,
	(long)access_nametab},
{(char *)"bad_name",
	cf_badname,	CA_GOD,		NULL,				0},
{(char *)"badsite_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.site_file,	32},
{(char *)"cache_depth",
	cf_int,		CA_DISABLED,	&mudconf.cache_depth,		0},
{(char *)"cache_names",
	cf_bool,	CA_DISABLED,	&mudconf.cache_names,		0},
{(char *)"cache_trim",
	cf_bool,	CA_GOD,		&mudconf.cache_trim,		0},
{(char *)"cache_width",
	cf_int,		CA_DISABLED,	&mudconf.cache_width,		0},
{(char *)"check_interval",
	cf_int,		CA_GOD,		&mudconf.check_interval,	0},
{(char *)"check_offset",
	cf_int,		CA_GOD,		&mudconf.check_offset,		0},
{(char *)"clone_copies_cost",
	cf_bool,	CA_GOD,		&mudconf.clone_copy_cost,	0},
{(char *)"comsys_database",
	cf_string,	CA_GOD,		(int *)mudconf.comsys_db,	128},
{(char *)"command_quota_increment",
	cf_int,		CA_GOD,		&mudconf.cmd_quota_incr,	0},
{(char *)"command_quota_max",
	cf_int,		CA_GOD,		&mudconf.cmd_quota_max,		0},
{(char *)"compress_program",
	cf_string,	CA_DISABLED,	(int *)mudconf.compress,	128},
{(char *)"compression",
	cf_bool,	CA_GOD,		&mudconf.compress_db,		0},
{(char *)"concentrator_port",
	cf_int,		CA_DISABLED,	&mudconf.conc_port,		0},
{(char *)"config_access",
	cf_cf_access,	CA_GOD,		NULL,
	(long)access_nametab},
{(char *)"conn_timeout",
	cf_int,		CA_GOD,		&mudconf.conn_timeout,		0},
{(char *)"connect_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.conn_file,	32},
{(char *)"connect_reg_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.creg_file,	32},
{(char *)"crash_database",
	cf_string,	CA_DISABLED,	(int *)mudconf.crashdb,		128},
{(char *)"create_max_cost",
	cf_int,		CA_GOD,		&mudconf.createmax,		0},
{(char *)"create_min_cost",
	cf_int,		CA_GOD,		&mudconf.createmin,		0},
{(char *)"dark_sleepers",
	cf_bool,	CA_GOD,		&mudconf.dark_sleepers,		0},
{(char *)"default_home",
	cf_int,		CA_GOD,		&mudconf.default_home,		0},
{(char *)"dig_cost",
	cf_int,		CA_GOD,		&mudconf.digcost,		0},
{(char *)"down_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.down_file,	32},
{(char *)"down_motd_message",
	cf_string,	CA_GOD,		(int *)mudconf.downmotd_msg,	4096},
{(char *)"dump_interval",
	cf_int,		CA_GOD,		&mudconf.dump_interval,		0},
{(char *)"dump_message",
	cf_string,	CA_GOD,		(int *)mudconf.dump_msg,	128},
{(char *)"postdump_message",
	cf_string,	CA_GOD,		(int *)mudconf.postdump_msg,	128},
{(char *)"pueblo_message",
	cf_string,      CA_GOD,         (int *) mudconf.pueblo_msg,     1024},
{(char *)"dump_offset",
	cf_int,		CA_GOD,		&mudconf.dump_offset,		0},
{(char *)"earn_limit",
	cf_int,		CA_GOD,		&mudconf.paylimit,		0},
{(char *)"examine_flags",
	cf_bool,	CA_GOD,		&mudconf.ex_flags,		0},
{(char *)"examine_public_attrs",
	cf_bool,	CA_GOD,		&mudconf.exam_public,		0},
{(char *)"exit_flags",
	cf_set_flags,	CA_GOD,		(int *)&mudconf.exit_flags,	0},
{(char *)"exit_quota",
	cf_int,		CA_GOD,		&mudconf.exit_quota,		0},
{(char *)"events_daily_hour",
	cf_int,		CA_GOD,		&mudconf.events_daily_hour,	0},
{(char *)"fascist_teleport",
	cf_bool,	CA_GOD,		&mudconf.fascist_tport,		0},
{(char *)"fixed_home_message",
	cf_string,	CA_DISABLED,	(int *)mudconf.fixed_home_msg,	128},
{(char *)"fixed_tel_message",
	cf_string,	CA_DISABLED,	(int *)mudconf.fixed_tel_msg,	128},
{(char *)"find_money_chance",
	cf_int,		CA_GOD,		&mudconf.payfind, 		0},
{(char *)"flag_alias",
	cf_flagalias,	CA_GOD,		NULL,				0},
{(char *)"forbid_site",
	cf_site,	CA_GOD,		(int *)&mudstate.access_list,
	H_FORBIDDEN},
{(char *)"fork_dump",
	cf_bool,	CA_GOD,		&mudconf.fork_dump,		0},
{(char *)"fork_vfork",
	cf_bool,	CA_GOD,		&mudconf.fork_vfork,		0},
{(char *)"full_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.full_file,	32},
{(char *)"full_motd_message",
	cf_string,	CA_GOD,		(int *)mudconf.fullmotd_msg,	4096},
{(char *)"function_access",
	cf_func_access,	CA_GOD,		NULL,
	(long)access_nametab},
{(char *)"function_alias",
	cf_alias,	CA_GOD,		(int *)&mudstate.func_htab,	0},
{(char *)"function_invocation_limit",
	cf_int,         CA_GOD,         &mudconf.func_invk_lim,		0},
{(char *)"function_recursion_limit",
	cf_int,         CA_GOD,         &mudconf.func_nest_lim,		0},
{(char *)"gdbm_database",
	cf_string,	CA_DISABLED,	(int *)mudconf.gdbm,		128},
{(char *)"good_name",
	cf_badname,	CA_GOD,		NULL,				1},
{(char *)"guest_char_num",
	cf_int,		CA_DISABLED,	&mudconf.guest_char,		0},
{(char *)"guest_nuker",
        cf_int,         CA_GOD,         &mudconf.guest_nuker,           0},
{(char *)"guest_prefix",
        cf_string,      CA_DISABLED,    (int *)mudconf.guest_prefix,    32},
{(char *)"number_guests",
        cf_int,         CA_DISABLED,    &mudconf.number_guests,         0},
{(char *)"guest_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.guest_file,	32},
{(char *)"guests_channel",
	cf_string,	CA_DISABLED,	(int *)mudconf.guests_channel,	32},
{(char *)"have_comsys",
	cf_bool,	CA_DISABLED,	&mudconf.have_comsys,		0},
{(char *)"have_mailer",
	cf_bool,	CA_DISABLED,	&mudconf.have_mailer,		0},
{(char *)"have_zones",
	cf_bool,	CA_DISABLED,	&mudconf.have_zones,		0},
{(char *)"help_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.help_file,	32},
{(char *)"help_index",
	cf_string,	CA_DISABLED,	(int *)mudconf.help_indx,	32},
{(char *)"hostnames",
	cf_bool,	CA_GOD,		&mudconf.use_hostname,		0},
{(char *)"use_http",
	cf_bool,	CA_DISABLED,	&mudconf.use_http,		0},
{(char *)"idle_wiz_dark",
	cf_bool,	CA_GOD,		&mudconf.idle_wiz_dark,		0},
{(char *)"idle_interval",
	cf_int,		CA_GOD,		&mudconf.idle_interval,		0},
{(char *)"idle_timeout",
	cf_int,		CA_GOD,		&mudconf.idle_timeout,		0},
{(char *)"include",
	cf_include,	CA_DISABLED,	NULL,				0},
{(char *)"indent_desc",
	cf_bool,	CA_GOD,		&mudconf.indent_desc,		0},
{(char *)"initial_size",
	cf_int,		CA_DISABLED,	&mudconf.init_size,		0},
{(char *)"input_database",
	cf_string,	CA_DISABLED, 	(int *)mudconf.indb,		128},
{(char *)"kill_guarantee_cost",
	cf_int,		CA_GOD,		&mudconf.killguarantee,		0},
{(char *)"kill_max_cost",
	cf_int,		CA_GOD,		&mudconf.killmax,		0},
{(char *)"kill_min_cost",
	cf_int,		CA_GOD,		&mudconf.killmin,		0},
{(char *)"link_cost",
	cf_int,		CA_GOD,		&mudconf.linkcost,		0},
{(char *)"list_access",
	cf_ntab_access,	CA_GOD,		(int *)list_names,
	(long)access_nametab},
{(char *)"lock_recursion_limit",
	cf_int,         CA_WIZARD,         &mudconf.lock_nest_lim,		0},
{(char *)"log",
	cf_modify_bits,	CA_GOD,		&mudconf.log_options,
	(long)logoptions_nametab},
{(char *)"log_options",
	cf_modify_bits,	CA_GOD,		&mudconf.log_info,
	(long)logdata_nametab},
{(char *)"logout_cmd_access",
	cf_ntab_access,	CA_GOD,		(int *)logout_cmdtable,
	(long)access_nametab},
{(char *)"logout_cmd_alias",
	cf_alias,	CA_GOD,		(int *)&mudstate.logout_cmd_htab,0},
{(char *)"look_obey_terse",
	cf_bool,	CA_GOD,		&mudconf.terse_look,		0},
{(char *)"machine_command_cost",
	cf_int,		CA_GOD,		&mudconf.machinecost,		0},
{(char *)"mail_database",
	cf_string,	CA_GOD,		(int *)mudconf.mail_db,		128},
{(char *)"mail_expiration",
	cf_int,		CA_GOD,		&mudconf.mail_expiration,	0},
{(char *)"master_room",
	cf_int,		CA_GOD,		&mudconf.master_room,		0},
{(char *)"match_own_commands",
	cf_bool,	CA_GOD,		&mudconf.match_mine,		0},
{(char *)"max_players",
	cf_int,		CA_GOD,		&mudconf.max_players,		0},
{(char *)"money_name_plural",
	cf_string,	CA_GOD,		(int *)mudconf.many_coins,	32},
{(char *)"money_name_singular",
	cf_string,	CA_GOD,		(int *)mudconf.one_coin,	32},
{(char *)"motd_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.motd_file,	32},
{(char *)"motd_message",
	cf_string,	CA_GOD,		(int *)mudconf.motd_msg,	4096},
{(char *)"mud_name",
	cf_string,	CA_GOD,		(int *)mudconf.mud_name,	32},
{(char *)"news_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.news_file,	32},
{(char *)"news_index",
	cf_string,	CA_DISABLED,	(int *)mudconf.news_indx,	32},
{(char *)"newuser_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.crea_file,	32},
{(char *)"notify_recursion_limit",
	cf_int,         CA_GOD,         &mudconf.ntfy_nest_lim,		0},
{(char *)"open_cost",
	cf_int,		CA_GOD,		&mudconf.opencost,		0},
{(char *)"output_database",
	cf_string,	CA_DISABLED,	(int *)mudconf.outdb,		128},
{(char *)"output_limit",
	cf_int,		CA_GOD,		&mudconf.output_limit,		0},
{(char *)"page_cost",
	cf_int,		CA_GOD,		&mudconf.pagecost,		0},
{(char *)"paranoid_allocate",
	cf_bool,	CA_GOD,		&mudconf.paranoid_alloc,	0},
{(char *)"parent_recursion_limit",
	cf_int,		CA_GOD,		&mudconf.parent_nest_lim,	0},
{(char *)"paycheck",
	cf_int,		CA_GOD,		&mudconf.paycheck,		0},
{(char *)"pemit_far_players",
	cf_bool,	CA_GOD,		&mudconf.pemit_players,		0},
{(char *)"pemit_any_object",
	cf_bool,	CA_GOD,		&mudconf.pemit_any,		0},
{(char *)"permit_site",
	cf_site,	CA_GOD,		(int *)&mudstate.access_list,	0},
{(char *)"player_flags",
	cf_set_flags,	CA_GOD,		(int *)&mudconf.player_flags,	0},
{(char *)"player_listen",
	cf_bool,	CA_GOD,		&mudconf.player_listen,		0},
{(char *)"player_match_own_commands",
	cf_bool,	CA_GOD,		&mudconf.match_mine_pl,		0},
{(char *)"player_name_spaces",
	cf_bool,	CA_GOD,		&mudconf.name_spaces,		0},
{(char *)"player_queue_limit",
	cf_int,		CA_GOD,		&mudconf.queuemax,		0},
{(char *)"player_quota",
	cf_int,		CA_GOD,		&mudconf.player_quota,		0},
{(char *)"player_starting_home",
	cf_int,		CA_GOD,		&mudconf.start_home,		0},
{(char *)"player_starting_room",
	cf_int,		CA_GOD,		&mudconf.start_room,		0},
{(char *)"plushelp_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.plushelp_file,	32},
{(char *)"plushelp_index",
	cf_string,	CA_DISABLED,	(int *)mudconf.plushelp_indx,	32},
{(char *)"public_channel",
	cf_string,	CA_DISABLED,	(int *)mudconf.public_channel,	32},
{(char *)"wiznews_file",
        cf_string,      CA_DISABLED,    (int *)mudconf.wiznews_file,    32},
{(char *)"wiznews_index",
        cf_string,      CA_DISABLED,    (int *)mudconf.wiznews_indx,    32},
{(char *)"port",
	cf_int,		CA_DISABLED,	&mudconf.port,			0},
{(char *)"public_flags",
	cf_bool,	CA_GOD,		&mudconf.pub_flags,		0},
{(char *)"queue_active_chunk",
	cf_int,		CA_GOD,		&mudconf.active_q_chunk,	0},
{(char *)"queue_idle_chunk",
	cf_int,		CA_GOD,		&mudconf.queue_chunk,		0},
{(char *)"quiet_look",
	cf_bool,	CA_GOD,		&mudconf.quiet_look,		0},
{(char *)"quiet_whisper",
	cf_bool,	CA_GOD,		&mudconf.quiet_whisper,		0},
{(char *)"quit_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.quit_file,	32},
{(char *)"quotas",
	cf_bool,	CA_GOD,		&mudconf.quotas,		0},
{(char *)"read_remote_desc",
	cf_bool,	CA_GOD,		&mudconf.read_rem_desc,		0},
{(char *)"read_remote_name",
	cf_bool,	CA_GOD,		&mudconf.read_rem_name,		0},
{(char *)"register_create_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.regf_file,	32},
{(char *)"register_site",
	cf_site,	CA_GOD,		(int *)&mudstate.access_list,
	H_REGISTRATION},
{(char *)"retry_limit",
	cf_int,		CA_GOD,		&mudconf.retry_limit,		0},
{(char *)"robot_cost",
	cf_int,		CA_GOD,		&mudconf.robotcost,		0},
{(char *)"robot_flags",
	cf_set_flags,	CA_GOD,		(int *)&mudconf.robot_flags,	0},
{(char *)"robot_speech",
	cf_bool,	CA_GOD,		&mudconf.robot_speak,		0},
{(char *)"room_flags",
	cf_set_flags,	CA_GOD,		(int *)&mudconf.room_flags,	0},
{(char *)"room_quota",
	cf_int,		CA_GOD,		&mudconf.room_quota,		0},
{(char *)"sacrifice_adjust",
	cf_int,		CA_GOD,		&mudconf.sacadjust,		0},
{(char *)"sacrifice_factor",
	cf_int,		CA_GOD,		&mudconf.sacfactor,		0},
{(char *)"search_cost",
	cf_int,		CA_GOD,		&mudconf.searchcost,		0},
{(char *)"see_owned_dark",
	cf_bool,	CA_GOD,		&mudconf.see_own_dark,		0},
{(char *)"signal_action",
	cf_option,	CA_DISABLED,	&mudconf.sig_action,
	(long)sigactions_nametab},
{(char *)"site_chars",
	cf_int,		CA_GOD,		&mudconf.site_chars,		0},
{(char *)"space_compress",
	cf_bool,	CA_GOD,		&mudconf.space_compress,	0},
{(char *)"stack_limit",
	cf_int,		CA_GOD,		&mudconf.stack_limit,		0},
{(char *)"starting_money",
	cf_int,		CA_GOD,		&mudconf.paystart,		0},
{(char *)"starting_quota",
	cf_int,		CA_GOD,		&mudconf.start_quota,		0},
{(char *)"status_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.status_file,	128},
{(char *)"suspect_site",
	cf_site,	CA_GOD,		(int *)&mudstate.suspect_list,
	H_SUSPECT},
{(char *)"sweep_dark",
	cf_bool,	CA_GOD,		&mudconf.sweep_dark,		0},
{(char *)"switch_default_all",
	cf_bool,	CA_GOD,		&mudconf.switch_df_all,		0},
{(char *)"terse_shows_contents",
	cf_bool,	CA_GOD,		&mudconf.terse_contents,	0},
{(char *)"terse_shows_exits",
	cf_bool,	CA_GOD,		&mudconf.terse_exits,		0},
{(char *)"terse_shows_move_messages",
	cf_bool,	CA_GOD,		&mudconf.terse_movemsg,		0},
{(char *)"thing_flags",
	cf_set_flags,	CA_GOD,		(int *)&mudconf.thing_flags,	0},
{(char *)"thing_quota",
	cf_int,		CA_GOD,		&mudconf.thing_quota,		0},
{(char *)"timeslice",
	cf_int,		CA_GOD,		&mudconf.timeslice,		0},
{(char *)"trace_output_limit",
	cf_int,		CA_GOD,		&mudconf.trace_limit,		0},
{(char *)"trace_topdown",
	cf_bool,	CA_GOD,		&mudconf.trace_topdown,		0},
{(char *)"trust_site",
	cf_site,	CA_GOD,		(int *)&mudstate.suspect_list,	0},
{(char *)"uncompress_program",
	cf_string,	CA_DISABLED,	(int *)mudconf.uncompress,	128},
{(char *)"unowned_safe",
	cf_bool,	CA_GOD,		&mudconf.safe_unowned,		0},
{(char *)"user_attr_access",
	cf_modify_bits,	CA_GOD,		&mudconf.vattr_flags,
	(long)attraccess_nametab},
{(char *)"wait_cost",
	cf_int,		CA_GOD,		&mudconf.waitcost,		0},
{(char *)"wizard_help_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.whelp_file,	32},
{(char *)"wizard_help_index",
	cf_string,	CA_DISABLED,	(int *)mudconf.whelp_indx,	32},
{(char *)"wizard_motd_file",
	cf_string,	CA_DISABLED,	(int *)mudconf.wizmotd_file,	32},
{(char *)"wizard_motd_message",
	cf_string,	CA_GOD,		(int *)mudconf.wizmotd_msg,	4096},
{(char *)"zone_recursion_limit",
	cf_int,		CA_GOD,		&mudconf.zone_nest_lim,		0},
{ NULL,
	NULL,		0,		NULL,				0}};

/* *INDENT-ON* */








/*
 * ---------------------------------------------------------------------------
 * * cf_set: Set config parameter.
 */

int cf_set(cp, ap, player)
char *cp, *ap;
dbref player;
{
	CONF *tp;
	int i;
	char *buff;

	/*
	 * Search the config parameter table for the command. If we find it,
	 * * * * * * call the handler to parse the argument. 
	 */

	for (tp = conftable; tp->pname; tp++) {
		if (!strcmp(tp->pname, cp)) {
			if (!mudstate.initializing &&
			    !check_access(player, tp->flags)) {
				notify(player,
				       "Permission denied.");
				return (-1);
			}
			if (!mudstate.initializing) {
				buff = alloc_lbuf("cf_set");
				StringCopy(buff, ap);
			}
			i = tp->interpreter(tp->loc, ap, tp->extra, player, cp);
			if (!mudstate.initializing) {
				STARTLOG(LOG_CONFIGMODS, "CFG", "UPDAT")
					log_name(player);
				log_text((char *)" entered config directive: ");
				log_text(cp);
				log_text((char *)" with args '");
				log_text(buff);
				log_text((char *)"'.  Status: ");
				switch (i) {
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
				ENDLOG
					free_lbuf(buff);
			}
			return i;
		}
	}

	/*
	 * Config directive not found.  Complain about it. 
	 */

	cf_log_notfound(player, (char *)"Set", "Config directive", cp);
	return (-1);
}

/*
 * ---------------------------------------------------------------------------
 * * do_admin: Command handler to set config params at runtime 
 */

void do_admin(player, cause, extra, kw, value)
dbref player, cause;
int extra;
char *kw, *value;
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

int cf_read(fn)
char *fn;
{
	int retval;

	StringCopy(mudconf.config_file, fn);
	mudstate.initializing = 1;
	retval = cf_include(NULL, fn, 0, 0, (char *)"init");
	mudstate.initializing = 0;

	/*
	 * Fill in missing DB file names 
	 */

	if (!*mudconf.outdb) {
		StringCopy(mudconf.outdb, mudconf.indb);
		strcat(mudconf.outdb, ".out");
	}
	if (!*mudconf.crashdb) {
		StringCopy(mudconf.crashdb, mudconf.indb);
		strcat(mudconf.crashdb, ".CRASH");
	}
	if (!*mudconf.gdbm) {
		StringCopy(mudconf.gdbm, mudconf.indb);
		strcat(mudconf.gdbm, ".gdbm");
	}
	return retval;
}

/*
 * ---------------------------------------------------------------------------
 * * list_cf_access: List access to config directives.
 */

void list_cf_access(player)
dbref player;
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
