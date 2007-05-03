
/*
 * $Id: predicates.c,v 1.2 1997/04/16 06:01:34 dpassmor Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"

#include <signal.h>
#include "mudconf.h"
#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"
#include "match.h"
#include "command.h"
#include "alloc.h"
#include "attrs.h"
#include "powers.h"
#include "ansi.h"
#include "patchlevel.h"
#include "htab.h"

extern dbref FDECL(match_thing, (dbref, char *));
extern int FDECL(do_command, (DESC *, char *, int));
extern void NDECL(dump_database);
extern void NDECL(dump_restart_db);
extern int slave_pid;
extern int slave_socket;

#ifdef NEED_VSPRINTF_DCL
extern char *FDECL(vsprintf, (char *, char *, va_list));

#endif

#ifdef STDC_HEADERS
char *tprintf(char *format,...)
#else
char *tprintf(va_alist)
va_dcl

#endif

{
	static char buff[20000];
	va_list ap;

#ifdef STDC_HEADERS
	va_start(ap, format);
#else
	char *format;

	va_start(ap);
	format = va_arg(ap, char *);

#endif
	vsprintf(buff, format, ap);
	va_end(ap);
	buff[LBUF_SIZE - 1] = '\0';
	return buff;
}

#ifdef STDC_HEADERS
void safe_tprintf_str(char *str, char **bp, char *format,...)
#else
void safe_tprintf_str(va_alist)
va_dcl

#endif

{
	static char buff[20000];
	va_list ap;

#ifdef STDC_HEADERS
	va_start(ap, format);
#else
	char *str;
	char **bp;
	char *format;

	va_start(ap);
	str = va_arg(ap, char *);
	bp = va_arg(ap, char **);
	format = va_arg(ap, char *);

#endif
	/*
	 * Sigh, don't we wish _all_ vsprintf's returned int... 
	 */

	vsprintf(buff, format, ap);
	va_end(ap);
	buff[LBUF_SIZE - 1] = '\0';
	safe_str(buff, str, bp);
	**bp = '\0';
}

/*
 * ---------------------------------------------------------------------------
 * * insert_first, remove_first: Insert or remove objects from lists.
 */

dbref insert_first(head, thing)
dbref head, thing;
{
	s_Next(thing, head);
	return thing;
}

dbref remove_first(head, thing)
dbref head, thing;
{
	dbref prev;

	if (head == thing)
		return (Next(thing));

	DOLIST(prev, head) {
		if (Next(prev) == thing) {
			s_Next(prev, Next(thing));
			return head;
		}
	}
	return head;
}

/*
 * ---------------------------------------------------------------------------
 * * reverse_list: Reverse the order of members in a list.
 */

dbref reverse_list(list)
dbref list;
{
	dbref newlist, rest;

	newlist = NOTHING;
	while (list != NOTHING) {
		rest = Next(list);
		s_Next(list, newlist);
		newlist = list;
		list = rest;
	}
	return newlist;
}

/*
 * ---------------------------------------------------------------------------
 * * member - indicate if thing is in list
 */

int member(thing, list)
dbref thing, list;
{
	DOLIST(list, list) {
		if (list == thing)
			return 1;
	}
	return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * is_integer, is_number: see if string contains just a number.
 */

int is_integer(str)
char *str;
{
	while (*str && isspace(*str))
		str++;		/*
				 * Leading spaces 
				 */
	if (*str == '-') {	/*
				 * Leading minus 
				 */
		str++;
		if (!*str)
			return 0;	/*
					 * but not if just a minus 
					 */
	}
	if (!isdigit(*str))	/*
				 * Need at least 1 integer 
				 */
		return 0;
	while (*str && isdigit(*str))
		str++;		/*
				 * The number (int) 
				 */
	while (*str && isspace(*str))
		str++;		/*
				 * Trailing spaces 
				 */
	return (*str ? 0 : 1);
}

int is_number(str)
char *str;
{
	int got_one;

	while (*str && isspace(*str))
		str++;		/*
				 * Leading spaces 
				 */
	if (*str == '-') {	/*
				 * Leading minus 
				 */
		str++;
		if (!*str)
			return 0;	/*
					 * but not if just a minus 
					 */
	}
	got_one = 0;
	if (isdigit(*str))
		got_one = 1;	/*
				 * Need at least one digit 
				 */
	while (*str && isdigit(*str))
		str++;		/*
				 * The number (int) 
				 */
	if (*str == '.')
		str++;		/*
				 * decimal point 
				 */
	if (isdigit(*str))
		got_one = 1;	/*
				 * Need at least one digit 
				 */
	while (*str && isdigit(*str))
		str++;		/*
				 * The number (fract) 
				 */
	while (*str && isspace(*str))
		str++;		/*
				 * Trailing spaces 
				 */
	return ((*str || !got_one) ? 0 : 1);
}

#ifndef STANDALONE

int could_doit(player, thing, locknum)
dbref player, thing;
int locknum;
{
	char *key;
	dbref aowner;
	int aflags, doit;

	/*
	 * no if nonplayer trys to get key 
	 */

	if (!isPlayer(player) && Key(thing)) {
		return 0;
	}
	if (Pass_Locks(player))
		return 1;

	key = atr_get(thing, locknum, &aowner, &aflags);
	doit = eval_boolexp_atr(player, thing, thing, key);
	free_lbuf(key);
	return doit;
}

int can_see(player, thing, can_see_loc)
dbref player, thing;
int can_see_loc;
{
	/*
	 * Don't show if all the following apply: * Sleeping players should * 
	 * 
	 * *  * * not be seen. * The thing is a disconnected player. * The
	 * player * is  *  * * not a puppet. 
	 */

	if (mudconf.dark_sleepers && isPlayer(thing) &&
	    !Connected(thing) && !Puppet(thing)) {
		return 0;
	}
	/*
	 * You don't see yourself or exits 
	 */

	if ((player == thing) || isExit(thing)) {
		return 0;
	}
	/*
	 * If loc is not dark, you see it if it's not dark or you control it.
	 * * * * * If loc is dark, you see it if you control it.  Seeing your
	 * * own * * * dark objects is controlled by mudconf.see_own_dark. *
	 * In * dark *  * locations, you also see things that are LIGHT and
	 * !DARK. 
	 */

	if (can_see_loc) {
		return (!Dark(thing) ||
			(mudconf.see_own_dark && MyopicExam(player, thing)));
	} else {
		return ((Light(thing) && !Dark(thing)) ||
			(mudconf.see_own_dark && MyopicExam(player, thing)));
	}
}

static int pay_quota(who, cost)
dbref who;
int cost;
{
	dbref aowner;
	int quota, aflags;
	char buf[20], *quota_str;

	/*
	 * If no cost, succeed 
	 */

	if (cost <= 0)
		return 1;

	/*
	 * determine quota 
	 */

	quota = atoi(quota_str = atr_get(Owner(who), A_RQUOTA, &aowner, &aflags));
	free_lbuf(quota_str);

	/*
	 * enough to build?  Wizards always have enough. 
	 */

	quota -= cost;
	if ((quota < 0) && !Free_Quota(who) && !Free_Quota(Owner(who)))
		return 0;

	/*
	 * dock the quota 
	 */
	sprintf(buf, "%d", quota);
	atr_add_raw(Owner(who), A_RQUOTA, buf);

	return 1;
}

int canpayfees(player, who, pennies, quota)
dbref player, who;
int pennies, quota;
{
	if (!Wizard(who) && !Wizard(Owner(who)) &&
	    !Free_Money(who) && !Free_Money(Owner(who)) &&
	    (Pennies(Owner(who)) < pennies)) {
		if (player == who) {
			notify(player,
			       tprintf("Sorry, you don't have enough %s.",
				       mudconf.many_coins));
		} else {
			notify(player,
			tprintf("Sorry, that player doesn't have enough %s.",
				mudconf.many_coins));
		}
		return 0;
	}
	if (mudconf.quotas) {
		if (!pay_quota(who, quota)) {
			if (player == who) {
				notify(player,
				       "Sorry, your building contract has run out.");
			} else {
				notify(player,
				       "Sorry, that player's building contract has run out.");
			}
			return 0;
		}
	}
	payfor(who, pennies);
	return 1;
}

int payfor(who, cost)
dbref who;
int cost;
{
	dbref tmp;

	if (Wizard(who) || Wizard(Owner(who)) ||
	    Free_Money(who) || Free_Money(Owner(who)) ||
	    Immortal(who) || Immortal(Owner(who))) {
		return 1;
	}
	who = Owner(who);
	if ((tmp = Pennies(who)) >= cost) {
		s_Pennies(who, tmp - cost);
		return 1;
	}
	return 0;
}

#endif /*
        * STANDALONE 
        */

void add_quota(who, payment)
dbref who;
int payment;
{
	dbref aowner;
	int aflags;
	char buf[20], *quota;

	quota = atr_get(who, A_RQUOTA, &aowner, &aflags);
	sprintf(buf, "%d", atoi(quota) + payment);
	free_lbuf(quota);
	atr_add_raw(who, A_RQUOTA, buf);
}

void giveto(who, pennies)
dbref who;
int pennies;
{
	if (Wizard(who) || Wizard(Owner(who)) ||
	    Free_Money(who) || Free_Money(Owner(who)) ||
	    Immortal(who) || Immortal(Owner(who))) {
		return;
	}
	who = Owner(who);
	s_Pennies(who, Pennies(who) + pennies);
}

int ok_name(name)
const char *name;
{
	const char *cp;

	/* Disallow pure ANSI names */
	
	if (strlen(strip_ansi(name)) == 0)
		return 0;
		
	/* Disallow leading spaces */

	if (isspace(*name))
		return 0;

	/*
	 * Only printable characters 
	 */

	for (cp = name; cp && *cp; cp++) {
		if ((!isprint(*cp)) && (*cp != ESC_CHAR))
			return 0;
	}

	/*
	 * Disallow trailing spaces 
	 */
	cp--;
	if (isspace(*cp))
		return 0;

	/*
	 * Exclude names that start with or contain certain magic cookies 
	 */

	return (name &&
		*name &&
		*name != LOOKUP_TOKEN &&
		*name != NUMBER_TOKEN &&
		*name != NOT_TOKEN &&
		!index(name, ARG_DELIMITER) &&
		!index(name, AND_TOKEN) &&
		!index(name, OR_TOKEN) &&
		string_compare(name, "me") &&
		string_compare(name, "home") &&
		string_compare(name, "here"));
}

int ok_player_name(name)
const char *name;
{
	const char *cp, *good_chars;

	/*
	 * No leading spaces 
	 */

	if (isspace(*name))
		return 0;

	/*
	 * Not too long and a good name for a thing 
	 */

	if (!ok_name(name) || (strlen(name) >= PLAYER_NAME_LIMIT))
		return 0;

#ifndef STANDALONE
	if (mudconf.name_spaces)
		good_chars = " `$_-.,'";
	else
		good_chars = "`$_-.,'";
#else
	good_chars = " `$_-.,'";
#endif
	/*
	 * Make sure name only contains legal characters 
	 */

	for (cp = name; cp && *cp; cp++) {
		if (isalnum(*cp))
			continue;
		if ((!index(good_chars, *cp)) || (*cp == ESC_CHAR))
			return 0;
	}
	return 1;
}

int ok_attr_name(attrname)
const char *attrname;
{
	const char *scan;

	if (!isalpha(*attrname))
		return 0;
	for (scan = attrname; *scan; scan++) {
		if (isalnum(*scan))
			continue;
		if (!(index("'?!`/-_.@#$^&~=+<>()%", *scan)))
			return 0;
	}
	return 1;
}

int ok_password(password)
const char *password;
{
	const char *scan;

	if (*password == '\0')
		return 0;

	for (scan = password; *scan; scan++) {
		if (!(isprint(*scan) && !isspace(*scan))) {
			return 0;
		}
	}

	/*
	 * Needed.  Change it if you like, but be sure yours is the same. 
	 */
	if ((strlen(password) == 13) &&
	    (password[0] == 'X') &&
	    (password[1] == 'X'))
		return 0;

	return 1;
}

#ifndef STANDALONE

/*
 * ---------------------------------------------------------------------------
 * * handle_ears: Generate the 'grows ears' and 'loses ears' messages.
 */

void handle_ears(thing, could_hear, can_hear)
dbref thing;
int could_hear, can_hear;
{
	char *buff, *bp;
	int gender;
	static const char *poss[5] =
	{"", "its", "her", "his", "their"};

	if (!could_hear && can_hear) {
		buff = alloc_lbuf("handle_ears.grow");
		StringCopy(buff, Name(thing));
		if (isExit(thing)) {
			for (bp = buff; *bp && (*bp != ';'); bp++) ;
			*bp = '\0';
		}
		gender = get_gender(thing);
		notify_check(thing, thing,
			     tprintf("%s grow%s ears and can now hear.",
				     buff, (gender == 4) ? "" : "s"),
			     (MSG_ME | MSG_NBR | MSG_LOC | MSG_INV));
		free_lbuf(buff);
	} else if (could_hear && !can_hear) {
		buff = alloc_lbuf("handle_ears.lose");
		StringCopy(buff, Name(thing));
		if (isExit(thing)) {
			for (bp = buff; *bp && (*bp != ';'); bp++) ;
			*bp = '\0';
		}
		gender = get_gender(thing);
		notify_check(thing, thing,
			     tprintf("%s lose%s %s ears and become%s deaf.",
				     buff, (gender == 4) ? "" : "s",
				     poss[gender], (gender == 4) ? "" : "s"),
			     (MSG_ME | MSG_NBR | MSG_LOC | MSG_INV));
		free_lbuf(buff);
	}
}

/*
 * for lack of better place the @switch code is here 
 */

void do_switch(player, cause, key, expr, args, nargs, cargs, ncargs)
dbref player, cause;
int key, nargs, ncargs;
char *expr, *args[], *cargs[];
{
	int a, any;
	char *buff, *bp, *str;

	if (!expr || (nargs <= 0))
		return;

	if (key == SWITCH_DEFAULT) {
		if (mudconf.switch_df_all)
			key = SWITCH_ANY;
		else
			key = SWITCH_ONE;
	}
	/*
	 * now try a wild card match of buff with stuff in coms 
	 */

	any = 0;
	buff = bp = alloc_lbuf("do_switch");
	for (a = 0; (a < (nargs - 1)) && args[a] && args[a + 1]; a += 2) {
		bp = buff;
		str = args[a];
		exec(buff, &bp, 0, player, cause, EV_FCHECK | EV_EVAL | EV_TOP, &str,
		     cargs, ncargs);
		*bp = '\0';
		if (wild_match(buff, expr)) {
			wait_que(player, cause, 0, NOTHING, 0, args[a + 1],
				 cargs, ncargs, mudstate.global_regs);
			if (key == SWITCH_ONE) {
				free_lbuf(buff);
				return;
			}
			any = 1;
		}
	}
	free_lbuf(buff);
	if ((a < nargs) && !any && args[a])
		wait_que(player, cause, 0, NOTHING, 0, args[a], cargs, ncargs,
			 mudstate.global_regs);
}

void do_addcommand(player, cause, key, name, command)
dbref player, cause;
int key;
char *name, *command;
{
CMDENT *old, *cmd;
ADDENT *add, *nextp;

dbref thing;
int atr;
char *s;

	if (!*name) {
		notify(player, "Sorry.");
		return;
	}
	
	if (!parse_attrib(player, command, &thing, &atr) || (atr == NOTHING)) {
		notify(player, "No such attribute.");
		return;
	}
	
	/* Let's make this case insensitive... */
	
	for (s = name; *s; s++) {
		*s = tolower(*s);
	}
	 
	old = (CMDENT *)hashfind(name, &mudstate.command_htab);
	
	if (old && (old->callseq & CS_ADDED)) {
		
		/* If it's already found in the hash table, and it's being
		   added using the same object and attribute... */
		   
		for (nextp = (ADDENT *)old->handler; nextp != NULL; nextp = nextp->next) {
			if ((nextp->thing == thing) && (nextp->atr == atr)) {
				notify(player, tprintf("%s already added.", name));
				return;
			}
		}
		
		/* else tack it on to the existing entry... */
		
		add = (ADDENT *)malloc(sizeof(ADDENT));
		add->thing = thing;
		add->atr = atr;
		add->name = (char *)strdup(name);
		add->next = (ADDENT *)old->handler;
		old->handler = (void *)add;
	} else {
		if (old) {
			/* Delete the old built-in and rename it __name */
			hashdelete(name, &mudstate.command_htab);
		}
		
		cmd = (CMDENT *) malloc(sizeof(CMDENT));
		
		cmd->cmdname = (char *)strdup(name);
		cmd->switches = NULL;
		cmd->perms = 0;
		cmd->extra = 0;
		if (old && (old->callseq & CS_LEADIN)) {
			cmd->callseq = CS_ADDED|CS_ONE_ARG|CS_LEADIN;
		} else {
			cmd->callseq = CS_ADDED|CS_ONE_ARG;
		}
		add = (ADDENT *)malloc(sizeof(ADDENT));
		add->thing = thing;
		add->atr = atr;
		add->name = (char *)strdup(name);
		add->next = NULL;
		cmd->handler = (void *)add;
	
		hashadd(name, (int *)cmd, &mudstate.command_htab);
		
		if (old) {
			/* Fix any aliases of this command. */
			hashreplall((int *)old, (int *)cmd, &mudstate.command_htab);
			hashadd(tprintf("__%s", name), (int *)old, &mudstate.command_htab);
		}
	}
	
	/* We reset the one letter commands here so you can overload them */
	
	set_prefix_cmds();
	notify(player, tprintf("%s added.", name));
}

void do_listcommands(player, cause, key, name)
dbref player, cause;
int key;
char *name;
{
CMDENT *old;
ADDENT *add, *nextp;
int didit = 0;

char *s, *keyname;

	/* Let's make this case insensitive... */
	
	for (s = name; *s; s++) {
		*s = tolower(*s);
	}
	 
	if (*name) {
		old = (CMDENT *)hashfind(name, &mudstate.command_htab);
		
		if (old && (old->callseq & CS_ADDED)) {
			
			/* If it's already found in the hash table, and it's being
			   added using the same object and attribute... */
			   
			for (nextp = (ADDENT *)old->handler; nextp != NULL; nextp = nextp->next) {
				notify(player, tprintf("%s: #%d/%s", nextp->name, nextp->thing, ((ATTR *)atr_num(nextp->atr))->name));
			}
		} else {
			notify(player, tprintf("%s not found in command table.",name));
		}
		return;
	} else {
		for (keyname = hash_firstkey(&mudstate.command_htab); keyname != NULL;
		     keyname = hash_nextkey(&mudstate.command_htab)) {

			old = (CMDENT *)hashfind(keyname, &mudstate.command_htab);
		
			if (old && (old->callseq & CS_ADDED)) {
				
				for (nextp = (ADDENT *)old->handler; nextp != NULL; nextp = nextp->next) {
					if (strcmp(keyname, nextp->name))
						continue;
					notify(player, tprintf("%s: #%d/%s", nextp->name, nextp->thing, ((ATTR *)atr_num(nextp->atr))->name));
					didit = 1;
				}
			}
		}
	}
	if (!didit)
		notify(player, "No added commands found in command table.");
}

void do_delcommand(player, cause, key, name, command)
dbref player, cause;
int key;
char *name, *command;
{
CMDENT *old, *cmd;
ADDENT *prev = NULL, *nextp;

dbref thing;
int atr;
char *s;

	if (!*name) {
		notify(player, "Sorry.");
		return;
	}
	
	if (*command) {
		if (!parse_attrib(player, command, &thing, &atr) || (atr == NOTHING)) {
			notify(player, "No such attribute.");
			return;
		}
	}
	
	/* Let's make this case insensitive... */
	
	for (s = name; *s; s++) {
		*s = tolower(*s);
	}
	 
	old = (CMDENT *)hashfind(name, &mudstate.command_htab);
	
	if (old && (old->callseq & CS_ADDED)) {
		if (!*command) {
			for (prev = (ADDENT *)old->handler; prev != NULL; prev = nextp) {
				nextp = prev->next;
				/* Delete it! */
				free(prev->name);
				free(prev);
			}
			hashdelete(name, &mudstate.command_htab);
			if ((cmd = (CMDENT *)hashfind(tprintf("__%s", name), &mudstate.command_htab)) != NULL) {
				hashdelete(tprintf("__%s", name), &mudstate.command_htab);
				hashadd(name, (int *)cmd, &mudstate.command_htab);
				hashreplall((int *)old, (int *)cmd, &mudstate.command_htab);
			}
			free(old);
			set_prefix_cmds();
			notify(player, "Done.");
			return;
		} else {
			for (nextp = (ADDENT *)old->handler; nextp != NULL; nextp = nextp->next) {
				if ((nextp->thing == thing) && (nextp->atr == atr)) {
					/* Delete it! */
					free(nextp->name);
					if (!prev) {
						if (!nextp->next) {
							hashdelete(name, &mudstate.command_htab);
							if ((cmd = (CMDENT *)hashfind(tprintf("__%s", name), &mudstate.command_htab)) != NULL) {
								hashdelete(tprintf("__%s", name), &mudstate.command_htab);
								hashadd(name, (int *)cmd, &mudstate.command_htab);
								hashreplall((int *)old, (int *)cmd, &mudstate.command_htab);
							}
							free(old);
						} else {
							old->handler = (void *)nextp->next;
							free(nextp);
						}
					} else {
						prev->next = nextp->next;
						free(nextp);
					}
					set_prefix_cmds();
					notify(player, "Done.");
					return;
				}
				prev = nextp;
			}
			notify(player, "Command not found in command table.");
		}
	} else {
		notify(player, "Command not found in command table.");
	}
}

/*
 * @prog 'glues' a user's input to a command. Once executed, the first string
 * input from any of the doers's logged in descriptors, will go into
 * A_PROGMSG, which can be substituted in <command> with %0. Commands already
 * queued by the doer will be processed normally.
 */

void handle_prog(d, message)
DESC *d;
char *message;
{
	DESC *all;
	PROG *prog;
	char *cmd;
	dbref aowner;
	int aflags, i;

	/*
	 * Allow the player to pipe a command while in interactive mode. 
	 */

	if (*message == '|') {
		do_command(d, message + 1, 1);
		/* Use telnet protocol's GOAHEAD command to show prompt */
		if (d->program_data != NULL)
			queue_string(d, tprintf("%s>%s \377\371", ANSI_HILITE, ANSI_NORMAL));
		return;
	}
	cmd = atr_get(d->player, A_PROGCMD, &aowner, &aflags);
	wait_que(d->program_data->wait_cause, d->player, 0, NOTHING, 0, cmd, (char **)&message,
		 1, (char **)d->program_data->wait_regs);

	/* First, set 'all' to a descriptor we find for this player */
	

	all = (DESC *)nhashfind(d->player, &mudstate.desc_htab) ;

	for (i = 0; i < MAX_GLOBAL_REGS; i++) {
		free_lbuf(all->program_data->wait_regs[i]);
	}
	free(all->program_data);

	/* Set info for all player descriptors to NULL */
	
	DESC_ITER_PLAYER(d->player, all)
		all->program_data = NULL;
	
	atr_clr(d->player, A_PROGCMD);
	free_lbuf(cmd);
}

void do_quitprog(player, cause, key, name)
dbref player, cause;
int key;
char *name;
{
	DESC *d;
	dbref doer;
	int i, isprog = 0;

	if (*name) {
		doer = match_thing(player, name);
	} else {
		doer = player;
	}

	if (!(Prog(player) || Prog(Owner(player))) && (player != doer)) {
		notify(player, "Permission denied.");
		return;
	}
	if (!isPlayer(doer) || !Good_obj(doer)) {
		notify(player, "That is not a player.");
		return;
	}
	if (!Connected(doer)) {
		notify(player, "That player is not connected.");
		return;
	}
	DESC_ITER_PLAYER(doer, d) {
		if (d->program_data != NULL) {
			isprog = 1;
		}
	}

	if (!isprog) {
		notify(player, "Player is not in an @program.");
		return;
	}

	d = (DESC *)nhashfind(doer, &mudstate.desc_htab) ;

	for (i = 0; i < MAX_GLOBAL_REGS; i++) {
		free_lbuf(d->program_data->wait_regs[i]);
	}
	free(d->program_data);

	/* Set info for all player descriptors to NULL */
	
	DESC_ITER_PLAYER(doer, d)
		d->program_data = NULL;

	atr_clr(doer, A_PROGCMD);
	notify(player, "@program cleared.");
	notify(doer, "Your @program has been terminated.");
}

void do_prog(player, cause, key, name, command)
dbref player, cause;
int key;
char *name, *command;
{
	DESC *d;
	PROG *program;
	int i, atr, aflags;
	dbref doer, thing, aowner;
	ATTR *ap;
	char *attrib, *msg;

	if (!name || !*name) {
		notify(player, "No players specified.");
		return;
	}
	doer = match_thing(player, name);

	if (!(Prog(player) || Prog(Owner(player))) && (player != doer)) {
		notify(player, "Permission denied.");
		return;
	}
	if (!isPlayer(doer) || !Good_obj(doer)) {
		notify(player, "That is not a player.");
		return;
	}
	if (!Connected(doer)) {
		notify(player, "That player is not connected.");
		return;
	}
	msg = command;
	attrib = parse_to(&msg, ':', 1);

	if (msg && *msg) {
		notify(doer, msg);
	}
	parse_attrib(player, attrib, &thing, &atr);
	if (atr != NOTHING) {
		if (!atr_get_info(thing, atr, &aowner, &aflags)) {
			notify(player, "Attribute not present on object.");
			return;
		}
		ap = atr_num(atr);
		if (God(player) || (!God(thing) && See_attr(player, thing, ap, aowner, aflags) &&
			   (Wizard(player) || (aowner == Owner(player))))) {
			atr_add_raw(doer, A_PROGCMD, atr_get_raw(thing, atr));
		} else {
			notify(player, "Permission denied.");
			return;
		}
	} else {
		notify(player, "No such attribute.");
		return;
	}

	/*
	 * Check to see if the cause already has an @prog input pending 
	 */
	DESC_ITER_PLAYER(doer, d) {
		if (d->program_data != NULL) {
			notify(player, "Input already pending.");
			return;
		}
	}

	program = (PROG *) malloc(sizeof(PROG));
	program->wait_cause = player;
	for (i = 0; i < MAX_GLOBAL_REGS; i++) {
		program->wait_regs[i] = alloc_lbuf("prog_regs");
		StringCopy(program->wait_regs[i], mudstate.global_regs[i]);
	}

	/*
	 * Now, start waiting. 
	 */
	DESC_ITER_PLAYER(doer, d) {
		d->program_data = program;

		/*
		 * Use telnet protocol's GOAHEAD command to show prompt 
		 */
		queue_string(d, tprintf("%s>%s \377\371", ANSI_HILITE, ANSI_NORMAL));
	}

}

/*
 * ---------------------------------------------------------------------------
 * * do_restart: Restarts the game.
 */

void do_restart(player, cause, key)
{
	char outdb[128];
	char indb[128];
	int stat;
	
	if (mudstate.dumping) {
		notify(player, "Dumping. Please try again later.");
		return;
	}
	
	raw_broadcast(0, "Game: Restart by %s, please wait.", Name(Owner(player)));
	STARTLOG(LOG_ALWAYS, "WIZ", "RSTRT")
		log_text((char *)"Restart by ");
	log_name(player);
	ENDLOG
	
	dump_database_internal(2);
	
	SYNC;
	CLOSE;

	shutdown(slave_socket, 2);
	kill(slave_pid, SIGKILL);
	alarm(0);
	dump_restart_db();
	execl("bin/netmux", "netmux", mudconf.config_file, NULL);
}

/*
 * ---------------------------------------------------------------------------
 * * do_comment: Implement the @@ (comment) command. Very cpu-intensive :-)
 */

void do_comment(player, cause, key)
dbref player, cause;
int key;
{
}

static dbref promote_dflt(old, new)
dbref old, new;
{
	switch (new) {
	case NOPERM:
		return NOPERM;
	case AMBIGUOUS:
		if (old == NOPERM)
			return old;
		else
			return new;
	}

	if ((old == NOPERM) || (old == AMBIGUOUS))
		return old;

	return NOTHING;
}

dbref match_possessed(player, thing, target, dflt, check_enter)
dbref player, thing, dflt;
char *target;
int check_enter;
{
	dbref result, result1;
	int control;
	char *buff, *start, *place, *s1, *d1, *temp;

	/*
	 * First, check normally 
	 */

	if (Good_obj(dflt))
		return dflt;

	/*
	 * Didn't find it directly.  Recursively do a contents check 
	 */

	start = target;
	while (*target) {

		/*
		 * Fail if no ' characters 
		 */

		place = target;
		target = (char *)index(place, '\'');
		if ((target == NULL) || !*target)
			return dflt;

		/*
		 * If string started with a ', skip past it 
		 */

		if (place == target) {
			target++;
			continue;
		}
		/*
		 * If next character is not an s or a space, skip past 
		 */

		temp = target++;
		if (!*target)
			return dflt;
		if ((*target != 's') && (*target != 'S') && (*target != ' '))
			continue;

		/*
		 * If character was not a space make sure the following * * * 
		 * 
		 * * character is a space. 
		 */

		if (*target != ' ') {
			target++;
			if (!*target)
				return dflt;
			if (*target != ' ')
				continue;
		}
		/*
		 * Copy the container name to a new buffer so we can * * * *
		 * terminate it. 
		 */

		buff = alloc_lbuf("is_posess");
		for (s1 = start, d1 = buff; *s1 && (s1 < temp); *d1++ = (*s1++)) ;
		*d1 = '\0';

		/*
		 * Look for the container here and in our inventory.  Skip *
		 * * * * past if we can't find it. 
		 */

		init_match(thing, buff, NOTYPE);
		if (player == thing) {
			match_neighbor();
			match_possession();
		} else {
			match_possession();
		}
		result1 = match_result();

		free_lbuf(buff);
		if (!Good_obj(result1)) {
			dflt = promote_dflt(dflt, result1);
			continue;
		}
		/*
		 * If we don't control it and it is either dark or opaque, *
		 * * * * skip past. 
		 */

		control = Controls(player, result1);
		if ((Dark(result1) || Opaque(result1)) && !control) {
			dflt = promote_dflt(dflt, NOTHING);
			continue;
		}
		/*
		 * Validate object has the ENTER bit set, if requested 
		 */

		if ((check_enter) && !Enter_ok(result1) && !control) {
			dflt = promote_dflt(dflt, NOPERM);
			continue;
		}
		/*
		 * Look for the object in the container 
		 */

		init_match(result1, target, NOTYPE);
		match_possession();
		result = match_result();
		result = match_possessed(player, result1, target, result,
					 check_enter);
		if (Good_obj(result))
			return result;
		dflt = promote_dflt(dflt, result);
	}
	return dflt;
}

/*
 * ---------------------------------------------------------------------------
 * * parse_range: break up <what>,<low>,<high> syntax
 */

void parse_range(name, low_bound, high_bound)
char **name;
dbref *low_bound, *high_bound;
{
	char *buff1, *buff2;

	buff1 = *name;
	if (buff1 && *buff1)
		*name = parse_to(&buff1, ',', EV_STRIP_TS);
	if (buff1 && *buff1) {
		buff2 = parse_to(&buff1, ',', EV_STRIP_TS);
		if (buff1 && *buff1) {
			while (*buff1 && isspace(*buff1))
				buff1++;
			if (*buff1 == NUMBER_TOKEN)
				buff1++;
			*high_bound = atoi(buff1);
			if (*high_bound >= mudstate.db_top)
				*high_bound = mudstate.db_top - 1;
		} else {
			*high_bound = mudstate.db_top - 1;
		}
		while (*buff2 && isspace(*buff2))
			buff2++;
		if (*buff2 == NUMBER_TOKEN)
			buff2++;
		*low_bound = atoi(buff2);
		if (*low_bound < 0)
			*low_bound = 0;
	} else {
		*low_bound = 0;
		*high_bound = mudstate.db_top - 1;
	}
}

int parse_thing_slash(player, thing, after, it)
dbref player, *it;
char *thing, **after;
{
	char *str;

	/*
	 * get name up to / 
	 */
	for (str = thing; *str && (*str != '/'); str++) ;

	/*
	 * If no / in string, return failure 
	 */

	if (!*str) {
		*after = NULL;
		*it = NOTHING;
		return 0;
	}
	*str++ = '\0';
	*after = str;

	/*
	 * Look for the object 
	 */

	init_match(player, thing, NOTYPE);
	match_everything(MAT_EXIT_PARENTS);
	*it = match_result();

	/*
	 * Return status of search 
	 */

	return (Good_obj(*it));
}

extern NAMETAB lock_sw[];

int get_obj_and_lock(player, what, it, attr, errmsg, bufc)
dbref player, *it;
char *what, *errmsg, **bufc;
ATTR **attr;
{
	char *str, *tbuf;
	int anum;

	tbuf = alloc_lbuf("get_obj_and_lock");
	StringCopy(tbuf, what);
	if (parse_thing_slash(player, tbuf, &str, it)) {

		/*
		 * <obj>/<lock> syntax, use the named lock 
		 */

		anum = search_nametab(player, lock_sw, str);
		if (anum < 0) {
			free_lbuf(tbuf);
			safe_str("#-1 LOCK NOT FOUND", errmsg, bufc);
			return 0;
		}
	} else {

		/*
		 * Not <obj>/<lock>, do a normal get of the default lock 
		 */

		*it = match_thing(player, what);
		if (!Good_obj(*it)) {
			free_lbuf(tbuf);
			safe_str("#-1 NOT FOUND", errmsg, bufc);
			return 0;
		}
		anum = A_LOCK;
	}

	/*
	 * Get the attribute definition, fail if not found 
	 */

	free_lbuf(tbuf);
	*attr = atr_num(anum);
	if (!(*attr)) {
		safe_str("#-1 LOCK NOT FOUND", errmsg, bufc);
		return 0;
	}
	return 1;
}

#endif /*
        * STANDALONE 
        */

/*
 * ---------------------------------------------------------------------------
 * * where_is: Returns place where obj is linked into a list.
 * * ie. location for players/things, source for exits, NOTHING for rooms.
 */

dbref where_is(what)
dbref what;
{
	dbref loc;

	if (!Good_obj(what))
		return NOTHING;

	switch (Typeof(what)) {
	case TYPE_PLAYER:
	case TYPE_THING:
		loc = Location(what);
		break;
	case TYPE_EXIT:
		loc = Exits(what);
		break;
	default:
		loc = NOTHING;
		break;
	}
	return loc;
}

/*
 * ---------------------------------------------------------------------------
 * * where_room: Return room containing player, or NOTHING if no room or
 * * recursion exceeded.  If player is a room, returns itself.
 */

dbref where_room(what)
dbref what;
{
	int count;

	for (count = mudconf.ntfy_nest_lim; count > 0; count--) {
		if (!Good_obj(what))
			break;
		if (isRoom(what))
			return what;
		if (!Has_location(what))
			break;
		what = Location(what);
	}
	return NOTHING;
}

int locatable(player, it, cause)
dbref player, it, cause;
{
	dbref loc_it, room_it;
	int findable_room;

	/*
	 * No sense if trying to locate a bad object 
	 */

	if (!Good_obj(it))
		return 0;

	loc_it = where_is(it);

	/*
	 * Succeed if we can examine the target, if we are the target, * if * 
	 * 
	 * *  * * we can examine the location, if a wizard caused the lookup, 
	 * * or  * *  * if the target caused the lookup. 
	 */

	if (Examinable(player, it) ||
	    Find_Unfindable(player) ||
	    (loc_it == player) ||
	    ((loc_it != NOTHING) &&
	     (Examinable(player, loc_it) || loc_it == where_is(player))) ||
	    Wizard(cause) ||
	    (it == cause))
		return 1;

	room_it = where_room(it);
	if (Good_obj(room_it))
		findable_room = !Hideout(room_it);
	else
		findable_room = 1;

	/*
	 * Succeed if we control the containing room or if the target is * *
	 * * * findable and the containing room is not unfindable. 
	 */

	if (((room_it != NOTHING) && Examinable(player, room_it)) ||
	    Find_Unfindable(player) || (Findable(it) && findable_room))
		return 1;

	/*
	 * We can't do it. 
	 */

	return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * nearby: Check if thing is nearby player (in inventory, in same room, or
 * * IS the room.
 */

int nearby(player, thing)
dbref player, thing;
{
	int thing_loc, player_loc;

	if (!Good_obj(player) || !Good_obj(thing))
		return 0;
	thing_loc = where_is(thing);
	if (thing_loc == player)
		return 1;
	player_loc = where_is(player);
	if ((thing_loc == player_loc) || (thing == player_loc))
		return 1;
	return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * exit_visible, exit_displayable: Is exit visible?
 */

int exit_visible(exit, player, key)	/*
					 * exit visible to lexits() 
					 */
dbref exit, player;
int key;
{
	if (key & VE_LOC_XAM)
		return 1;	/*
				 * Exam exit's loc 
				 */
	if (Examinable(player, exit))
		return 1;	/*
				 * Exam exit 
				 */
	if (Light(exit))
		return 1;	/*
				 * Exit is light 
				 */
	if (key & (VE_LOC_DARK | VE_BASE_DARK))
		return 0;	/*
				 * Dark Loc or base 
				 */
	if (Dark(exit))
		return 0;	/*
				 * Dark exit 
				 */
	return 1;		/*
				 * Default 
				 */
}

int exit_displayable(exit, player, key)		/*
						 * exit visible to look 
						 */
dbref exit, player;
int key;
{
	if (Dark(exit))
		return 0;	/*
				 * Dark exit 
				 */
	if (Light(exit))
		return 1;	/*
				 * Light exit 
				 */
	if (key & (VE_LOC_DARK | VE_BASE_DARK))
		return 0;	/*
				 * Dark Loc or base 
				 */
	return 1;		/*
				 * Default 
				 */
}
/*
 * ---------------------------------------------------------------------------
 * * next_exit: return next exit that is ok to see.
 */

dbref next_exit(player, this, exam_here)
dbref player, this;
int exam_here;
{
	if (isRoom(this))
		return NOTHING;
	if (isExit(this) && exam_here)
		return this;

	while ((this != NOTHING) && Dark(this) && !Light(this) &&
	       !Examinable(player, this))
		this = Next(this);

	return this;
}

#ifndef STANDALONE

/*
 * ---------------------------------------------------------------------------
 * * did_it: Have player do something to/with thing
 */

void did_it(player, thing, what, def, owhat, odef, awhat, args, nargs)
dbref player, thing;
int what, owhat, awhat, nargs;
char *args[];
const char *def, *odef;
{
	char *d, *buff, *act, *charges, *bp, *str;
	dbref loc, aowner;
	int num, aflags;

	/*
	 * message to player 
	 */

	if (what > 0) {
		d = atr_pget(thing, what, &aowner, &aflags);
		if (*d) {
			buff = bp = alloc_lbuf("did_it.1");
			str = d;
			exec(buff, &bp, 0, thing, player, EV_EVAL | EV_FIGNORE | EV_TOP,
			     &str, args, nargs);
			*bp = '\0';
			if (what == A_HTDESC) {
				safe_str("\r\n", buff, &bp);
				*bp = '\0';
				notify_html(player, buff);
			} else
				notify(player, buff);
			free_lbuf(buff);
		} else if (def) {
			notify(player, def);
		}
		free_lbuf(d);
	} else if ((what < 0) && def) {
		notify(player, def);
	}
	/*
	 * message to neighbors 
	 */

	if ((owhat > 0) && Has_location(player) &&
	    Good_obj(loc = Location(player))) {
		d = atr_pget(thing, owhat, &aowner, &aflags);
		if (*d) {
			buff = bp = alloc_lbuf("did_it.2");
			str = d;
			exec(buff, &bp, 0, thing, player, EV_EVAL | EV_FIGNORE | EV_TOP,
			     &str, args, nargs);
			*bp = '\0';
			if (*buff)
				notify_except2(loc, player, player, thing,
				       tprintf("%s %s", Name(player), buff));
			free_lbuf(buff);
		} else if (odef) {
			notify_except2(loc, player, player, thing,
				       tprintf("%s %s", Name(player), odef));
		}
		free_lbuf(d);
	} else if ((owhat < 0) && odef && Has_location(player) &&
		   Good_obj(loc = Location(player))) {
		notify_except2(loc, player, player, thing,
			       tprintf("%s %s", Name(player), odef));
	}
	/*
	 * do the action attribute 
	 */

	if (awhat > 0) {
		if (*(act = atr_pget(thing, awhat, &aowner, &aflags))) {
			charges = atr_pget(thing, A_CHARGES, &aowner, &aflags);
			if (*charges) {
				num = atoi(charges);
				if (num > 0) {
					buff = alloc_sbuf("did_it.charges");
					sprintf(buff, "%d", num - 1);
					atr_add_raw(thing, A_CHARGES, buff);
					free_sbuf(buff);
				} else if (*(buff = atr_pget(thing, A_RUNOUT, &aowner, &aflags))) {
					free_lbuf(act);
					act = buff;
				} else {
					free_lbuf(act);
					free_lbuf(buff);
					free_lbuf(charges);
					return;
				}
			}
			free_lbuf(charges);
			wait_que(thing, player, 0, NOTHING, 0, act, args, nargs,
				 mudstate.global_regs);
		}
		free_lbuf(act);
	}
}

/*
 * ---------------------------------------------------------------------------
 * * do_verb: Command interface to did_it.
 */

void do_verb(player, cause, key, victim_str, args, nargs)
dbref player, cause;
int key, nargs;
char *victim_str, *args[];
{
	dbref actor, victim, aowner;
	int what, owhat, awhat, nxargs, restriction, aflags, i;
	ATTR *ap;
	const char *whatd, *owhatd;
	char *xargs[10];

	/*
	 * Look for the victim 
	 */

	if (!victim_str || !*victim_str) {
		notify(player, "Nothing to do.");
		return;
	}
	/*
	 * Get the victim 
	 */

	init_match(player, victim_str, NOTYPE);
	match_everything(MAT_EXIT_PARENTS);
	victim = noisy_match_result();
	if (!Good_obj(victim))
		return;

	/*
	 * Get the actor.  Default is my cause 
	 */

	if ((nargs >= 1) && args[0] && *args[0]) {
		init_match(player, args[0], NOTYPE);
		match_everything(MAT_EXIT_PARENTS);
		actor = noisy_match_result();
		if (!Good_obj(actor))
			return;
	} else {
		actor = cause;
	}

	/*
	 * Check permissions.  There are two possibilities * 1: Player * * *
	 * controls both victim and actor.  In this case victim runs *    his 
	 * 
	 * *  * *  * * action list. * 2: Player controls actor.  In this case
	 * * victim * does  * not run his *    action list and any attributes
	 * * that * player cannot  * read from *    victim are defaulted. 
	 */

	if (!controls(player, actor)) {
		notify_quiet(player, "Permission denied,");
		return;
	}
	restriction = !controls(player, victim);

	what = -1;
	owhat = -1;
	awhat = -1;
	whatd = NULL;
	owhatd = NULL;
	nxargs = 0;

	/*
	 * Get invoker message attribute 
	 */

	if (nargs >= 2) {
		ap = atr_str(args[1]);
		if (ap && (ap->number > 0))
			what = ap->number;
	}
	/*
	 * Get invoker message default 
	 */

	if ((nargs >= 3) && args[2] && *args[2]) {
		whatd = args[2];
	}
	/*
	 * Get others message attribute 
	 */

	if (nargs >= 4) {
		ap = atr_str(args[3]);
		if (ap && (ap->number > 0))
			owhat = ap->number;
	}
	/*
	 * Get others message default 
	 */

	if ((nargs >= 5) && args[4] && *args[4]) {
		owhatd = args[4];
	}
	/*
	 * Get action attribute 
	 */

	if (nargs >= 6) {
		ap = atr_str(args[5]);
		if (ap)
			awhat = ap->number;
	}
	/*
	 * Get arguments 
	 */

	if (nargs >= 7) {
		parse_arglist(victim, actor, args[6], '\0',
		    EV_STRIP_LS | EV_STRIP_TS, xargs, 10, (char **)NULL, 0);
		for (nxargs = 0; (nxargs < 10) && xargs[nxargs]; nxargs++) ;
	}
	/*
	 * If player doesn't control both, enforce visibility restrictions 
	 */

	if (restriction) {
		ap = NULL;
		if (what != -1) {
			atr_get_info(victim, what, &aowner, &aflags);
			ap = atr_num(what);
		}
		if (!ap || !Read_attr(player, victim, ap, aowner, aflags) ||
		    ((ap->number == A_DESC) && !mudconf.read_rem_desc &&
		     !Examinable(player, victim) && !nearby(player, victim)))
			what = -1;

		ap = NULL;
		if (owhat != -1) {
			atr_get_info(victim, owhat, &aowner, &aflags);
			ap = atr_num(owhat);
		}
		if (!ap || !Read_attr(player, victim, ap, aowner, aflags) ||
		    ((ap->number == A_DESC) && !mudconf.read_rem_desc &&
		     !Examinable(player, victim) && !nearby(player, victim)))
			owhat = -1;

		awhat = 0;
	}
	/*
	 * Go do it 
	 */

	did_it(actor, victim, what, whatd, owhat, owhatd, awhat,
	       xargs, nxargs);

	/*
	 * Free user args 
	 */

	for (i = 0; i < nxargs; i++)
		free_lbuf(xargs[i]);

}

#endif /*
        * STANDALONE 
        */
