/*
 * wiz.c -- Wizard-only commands 
 */
/*
 * $Id: wiz.c,v 1.2 1997/04/16 06:02:09 dpassmor Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"

#include "mudconf.h"
#include "config.h"
#include "file_c.h"
#include "db.h"
#include "interface.h"
#include "match.h"
#include "externs.h"
#include "command.h"
#include "htab.h"
#include "alloc.h"
#include "attrs.h"
#include "powers.h"

extern char *FDECL(crypt, (const char *, const char *));

void do_teleport(player, cause, key, arg1, arg2)
dbref player, cause;
int key;
char *arg1, *arg2;
{
	dbref victim, destination, loc;
	char *to;
	int hush = 0;

	if (((Fixed(player)) || (Fixed(Owner(player)))) &&
	    !(Tel_Anywhere(player))) {
		notify(player, mudconf.fixed_tel_msg);
		return;
	}
	/*
	 * get victim 
	 */

	if (*arg2 == '\0') {
		victim = player;
		to = arg1;
	} else {
		init_match(player, arg1, NOTYPE);
		match_everything(MAT_NO_EXITS);
		victim = noisy_match_result();

		if (victim == NOTHING)
			return;
		to = arg2;
	}

	/*
	 * Validate type of victim 
	 */

	if (!Has_location(victim)) {
		notify_quiet(player, "You can't teleport that.");
		return;
	}
	/*
	 * Fail if we don't control the victim or the victim's location 
	 */

	if (!Controls(player, victim) && !Controls(player, Location(victim)) && !Tel_Anything(player)) {
		notify_quiet(player, "Permission denied.");
		return;
	}
	/*
	 * Check for teleporting home 
	 */

	if (!string_compare(to, "home")) {
		(void)move_via_teleport(victim, HOME, cause, 0);
		return;
	}
	/*
	 * Find out where to send the victim 
	 */

	init_match(player, to, NOTYPE);
	match_everything(0);
	destination = match_result();

	switch (destination) {
	case NOTHING:
		notify_quiet(player, "No match.");
		return;
	case AMBIGUOUS:
		notify_quiet(player,
			     "I don't know which destination you mean!");
		return;
	default:
		if (victim == destination) {
			notify_quiet(player, "Bad destination.");
			return;
		}
	}

	/*
	 * If fascist teleport is on, you must control the victim's ultimate
	 * * * * * location (after LEAVEing any objects) or it must be
	 * JUMP_OK.  
	 */

	if (mudconf.fascist_tport) {
		loc = where_room(victim);
		if (!Good_obj(loc) || !isRoom(loc) ||
		    ((!Controls(player, loc) && !Jump_ok(loc))) && !Tel_Anywhere(player)) {
			notify_quiet(player, "Permission denied.");
			return;
		}
	}
	if (Has_contents(destination)) {

		/*
		 * You must control the destination, or it must be a JUMP_OK
		 * * * * * room where you pass its TELEPORT lock. 
		 */

		if (((!Controls(player, destination) &&
		      !Jump_ok(destination)) && !Tel_Anywhere(player)) ||
		    !could_doit(player, destination, A_LTPORT)) {

			/*
			 * Nope, report failure 
			 */

			if (player != victim)
				notify_quiet(player, "Permission denied.");
			did_it(victim, destination,
			       A_TFAIL, "You can't teleport there!",
			       A_OTFAIL, 0, A_ATFAIL, (char **)NULL, 0);
			return;
		}
		/*
		 * We're OK, do the teleport 
		 */

		if (key & TELEPORT_QUIET)
			hush = HUSH_ENTER | HUSH_LEAVE;

		if (move_via_teleport(victim, destination, cause, hush)) {
			if (player != victim) {
				if (!Quiet(player))
					notify_quiet(player, "Teleported.");
			}
		}
	} else if (isExit(destination)) {
		if (Exits(destination) == Location(victim)) {
			move_exit(victim, destination, 0,
				  "You can't go that way.", 0);
		} else {
			notify_quiet(player, "I can't find that exit.");
		}
	}
}

/*
 * ---------------------------------------------------------------------------
 * * do_force_prefixed: Interlude to do_force for the # command
 */

void do_force_prefixed(player, cause, key, command, args, nargs)
dbref player, cause;
int key, nargs;
char *command, *args[];
{
	char *cp;

	cp = parse_to(&command, ' ', 0);
	if (!command)
		return;
	while (*command && isspace(*command))
		command++;
	if (*command)
		do_force(player, cause, key, cp, command, args, nargs);
}

/*
 * ---------------------------------------------------------------------------
 * * do_force: Force an object to do something.
 */

void do_force(player, cause, key, what, command, args, nargs)
dbref player, cause;
int key, nargs;
char *what, *command, *args[];
{
	dbref victim;

	if ((victim = match_controlled(player, what)) == NOTHING)
		return;

	/*
	 * force victim to do command 
	 */

	wait_que(victim, player, 0, NOTHING, 0, command, args, nargs,
		 mudstate.global_regs);
}

/*
 * ---------------------------------------------------------------------------
 * * do_toad: Turn a player into an object.
 */

void do_toad(player, cause, key, toad, newowner)
dbref player, cause;
int key;
char *toad, *newowner;
{
	dbref victim, recipient, loc, aowner;
	char *buf;
	int count, aflags;

	init_match(player, toad, TYPE_PLAYER);
	match_neighbor();
	match_absolute();
	match_player();
	if ((victim = noisy_match_result()) == NOTHING)
		return;

	if (!isPlayer(victim)) {
		notify_quiet(player, "Try @destroy instead.");
		return;
	}
	if (No_Destroy(victim)) {
		notify_quiet(player, "You can't toad that player.");
		return;
	}
	if ((newowner != NULL) && *newowner) {
		init_match(player, newowner, TYPE_PLAYER);
		match_neighbor();
		match_absolute();
		match_player();
		if ((recipient = noisy_match_result()) == NOTHING)
			return;
	} else {
		recipient = player;
	}

	STARTLOG(LOG_WIZARD, "WIZ", "TOAD")
		log_name_and_loc(victim);
	log_text((char *)" was @toaded by ");
	log_name(player);
	ENDLOG

	/*
	 * Clear everything out 
	 */

		if (key & TOAD_NO_CHOWN) {
		count = -1;
	} else {
		count = chown_all(victim, recipient);
		s_Owner(victim, recipient);	/*
						 * you get it 
						 */
	}
	s_Flags(victim, TYPE_THING | HALT);
	s_Flags2(victim, 0);
	s_Flags3(victim, 0);
	s_Pennies(victim, 1);

	/*
	 * notify people 
	 */

	loc = Location(victim);
	buf = alloc_mbuf("do_toad");
	sprintf(buf, "%s has been turned into a slimy toad!", Name(victim));
	notify_except2(loc, player, victim, player, buf);
	sprintf(buf, "You toaded %s! (%d objects @chowned)", Name(victim),
		count + 1);
	notify_quiet(player, buf);

	/*
	 * Zap the name from the name hash table 
	 */

	delete_player_name(victim, Name(victim));
	sprintf(buf, "a slimy toad named %s", Name(victim));
	s_Name(victim, buf);
	free_mbuf(buf);

	/*
	 * Zap the alias too 
	 */

	buf = atr_pget(victim, A_ALIAS, &aowner, &aflags);
	delete_player_name(victim, buf);
	free_lbuf(buf);

	count = boot_off(victim,
			 (char *)"You have been turned into a slimy toad!");
	notify_quiet(player, tprintf("%d connection%s closed.",
				     count, (count == 1 ? "" : "s")));
}

void do_newpassword(player, cause, key, name, password)
dbref player, cause;
int key;
char *name, *password;
{
	dbref victim;
	char *buf;

	if ((victim = lookup_player(player, name, 0)) == NOTHING) {
		notify_quiet(player, "No such player.");
		return;
	}
	if (*password != '\0' && !ok_password(password)) {

		/*
		 * Can set null passwords, but not bad passwords 
		 */
		notify_quiet(player, "Bad password");
		return;
	}
	if (God(victim)) {
		notify_quiet(player, "You cannot change that player's password.");
		return;
	}
	STARTLOG(LOG_WIZARD, "WIZ", "PASS")
		log_name(player);
	log_text((char *)" changed the password of ");
	log_name(victim);
	ENDLOG

	/*
	 * it's ok, do it 
	 */

		s_Pass(victim, crypt((const char *)password, "XX"));
	buf = alloc_lbuf("do_newpassword");
	notify_quiet(player, "Password changed.");
	sprintf(buf, "Your password has been changed by %s.", Name(player));
	notify_quiet(victim, buf);
	free_lbuf(buf);
}

void do_boot(player, cause, key, name)
dbref player, cause;
int key;
char *name;
{
	dbref victim;
	char *buf, *bp;
	int count;

	if (!(Can_Boot(player))) {
		notify(player, "Permission denied.");
		return;
	}
	if (key & BOOT_PORT) {
		if (is_number(name)) {
			victim = atoi(name);
		} else {
			notify_quiet(player, "That's not a number!");
			return;
		}
		STARTLOG(LOG_WIZARD, "WIZ", "BOOT")
			buf = alloc_sbuf("do_boot.port");
		sprintf(buf, "Port %d", victim);
		log_text(buf);
		log_text((char *)" was @booted by ");
		log_name(player);
		free_sbuf(buf);
		ENDLOG
	} else {
		init_match(player, name, TYPE_PLAYER);
		match_neighbor();
		match_absolute();
		match_player();
		if ((victim = noisy_match_result()) == NOTHING)
			return;

		if (God(victim)) {
			notify_quiet(player, "You cannot boot that player!");
			return;
		}
		if ((!isPlayer(victim) && !God(player)) ||
		    (player == victim)) {
			notify_quiet(player, "You can only boot off other players!");
			return;
		}
		STARTLOG(LOG_WIZARD, "WIZ", "BOOT")
			log_name_and_loc(victim);
		log_text((char *)" was @booted by ");
		log_name(player);
		ENDLOG
			notify_quiet(player, tprintf("You booted %s off!", Name(victim)));
	}
	if (key & BOOT_QUIET) {
		buf = NULL;
	} else {
		bp = buf = alloc_lbuf("do_boot.msg");
		safe_str(Name(player), buf, &bp);
		safe_str((char *)" gently shows you the door.", buf, &bp);
		*bp = '\0';
	}

	if (key & BOOT_PORT)
		count = boot_by_port(victim, !God(player), buf);
	else
		count = boot_off(victim, buf);
	notify_quiet(player, tprintf("%d connection%s closed.",
				     count, (count == 1 ? "" : "s")));
	if (buf)
		free_lbuf(buf);
}

/*
 * ---------------------------------------------------------------------------
 * * do_poor: Reduce the wealth of anyone over a specified amount.
 */

void do_poor(player, cause, key, arg1)
dbref player, cause;
int key;
char *arg1;
{
	dbref a;
	int amt, curamt;

	if (!is_number(arg1))
		return;
	amt = atoi(arg1);
	DO_WHOLE_DB(a) {
		if (isPlayer(a)) {
			curamt = Pennies(a);
			if (amt < curamt)
				s_Pennies(a, amt);
		}
	}
}

/*
 * ---------------------------------------------------------------------------
 * * do_cut: Chop off a contents or exits chain after the named item.
 */

void do_cut(player, cause, key, thing)
dbref player, cause;
int key;
char *thing;
{
	dbref object;

	object = match_controlled(player, thing);
	switch (object) {
	case NOTHING:
		notify_quiet(player, "No match.");
		break;
	case AMBIGUOUS:
		notify_quiet(player, "I don't know which one");
		break;
	default:
		s_Next(object, NOTHING);
		notify_quiet(player, "Cut.");
	}
}

/*
 * ---------------------------------------------------------------------------
 * * count_quota, mung_quota, show_quota, do_quota: Manage quotas.
 */

static int count_quota(player)
dbref player;
{
	int i, q;

	q = 0 - mudconf.player_quota;
	DO_WHOLE_DB(i) {
		if (Owner(i) != player)
			continue;
		if (Going(i) && (!isRoom(i)))
			continue;
		switch (Typeof(i)) {
		case TYPE_EXIT:
			q += mudconf.exit_quota;
			break;
		case TYPE_ROOM:
			q += mudconf.room_quota;
			break;
		case TYPE_THING:
			q += mudconf.thing_quota;
			break;
		case TYPE_PLAYER:
			q += mudconf.player_quota;
			break;
		}
	}
	return q;
}

static void mung_quotas(player, key, value)
dbref player;
int key, value;
{
	dbref aowner;
	int aq, rq, xq, aflags;
	char *buff;

	if (key & QUOTA_FIX) {

		/*
		 * Get value of stuff owned and good value, set other value * 
		 * 
		 * *  * *  * * from that. 
		 */

		xq = count_quota(player);
		if (key & QUOTA_TOT) {
			buff = atr_get(player, A_RQUOTA, &aowner, &aflags);
			aq = atoi(buff) + xq;
			atr_add_raw(player, A_QUOTA, tprintf("%d", aq));
			free_lbuf(buff);
		} else {
			buff = atr_get(player, A_QUOTA, &aowner, &aflags);
			rq = atoi(buff) - xq;
			atr_add_raw(player, A_RQUOTA, tprintf("%d", rq));
			free_lbuf(buff);
		}
	} else {

		/*
		 * Obtain (or calculate) current relative and absolute quota 
		 */

		buff = atr_get(player, A_QUOTA,
			       &aowner, &aflags);
		if (!*buff) {
			free_lbuf(buff);
			buff = atr_get(player, A_RQUOTA, &aowner, &aflags);
			rq = atoi(buff);
			free_lbuf(buff);
			aq = rq + count_quota(player);
		} else {
			aq = atoi(buff);
			free_lbuf(buff);
			buff = atr_get(player, A_RQUOTA, &aowner, &aflags);
			rq = atoi(buff);
			free_lbuf(buff);
		}

		/*
		 * Adjust values 
		 */

		if (key & QUOTA_REM) {
			aq += (value - rq);
			rq = value;
		} else {
			rq += (value - aq);
			aq = value;
		}

		/*
		 * Set both abs and relative quota 
		 */

		atr_add_raw(player, A_QUOTA, tprintf("%d", aq));
		atr_add_raw(player, A_RQUOTA, tprintf("%d", rq));
	}
}

static void show_quota(player, victim)
dbref player, victim;
{
	dbref aowner;
	int aq, rq, aflags;
	char *buff;

	buff = atr_get(victim, A_QUOTA, &aowner, &aflags);
	aq = atoi(buff);
	free_lbuf(buff);
	buff = atr_get(victim, A_RQUOTA, &aowner, &aflags);
	rq = aq - atoi(buff);
	free_lbuf(buff);
	if (!Free_Quota(victim))
		notify_quiet(player, tprintf("%-16s Quota: %9d  Used: %9d",
					     Name(victim), aq, rq));
	else
		notify_quiet(player, tprintf("%-16s Quota: UNLIMITED  Used: %9d",
					     Name(victim), rq));
}

void do_quota(player, cause, key, arg1, arg2)
dbref player, cause;
int key;
char *arg1, *arg2;
{
	dbref who;
	int set, value, i;

	if (!(mudconf.quotas | Quota(player))) {
		notify_quiet(player, "Quotas are not enabled.");
		return;
	}
	if ((key & QUOTA_TOT) && (key & QUOTA_REM)) {
		notify_quiet(player, "Illegal combination of switches.");
		return;
	}
	/*
	 * Show or set all quotas if requested 
	 */

	value = 0;
	set = 0;
	if (key & QUOTA_ALL) {
		if (arg1 && *arg1) {
			value = atoi(arg1);
			set = 1;
		} else if (key & (QUOTA_SET | QUOTA_FIX)) {
			value = 0;
			set = 1;
		}
		if (set) {
			STARTLOG(LOG_WIZARD, "WIZ", "QUOTA")
				log_name(player);
			log_text((char *)" changed everyone's quota");
			ENDLOG
		}
		DO_WHOLE_DB(i) {
			if (isPlayer(i)) {
				if (set)
					mung_quotas(i, key, value);
				show_quota(player, i);
			}
		}
		return;
	}
	/*
	 * Find out whose quota to show or set 
	 */

	if (!arg1 || *arg1 == '\0') {
		who = Owner(player);
	} else {
		who = lookup_player(player, arg1, 1);
		if (!Good_obj(who)) {
			notify_quiet(player, "Not found.");
			return;
		}
	}

	/*
	 * Make sure we have permission to do it 
	 */

	if (!Quota(player)) {
		if (arg2 && *arg2) {
			notify_quiet(player, "Permission denied.");
			return;
		}
		if (Owner(player) != who) {
			notify_quiet(player, "Permission denied.");
			return;
		}
	}
	if (arg2 && *arg2) {
		set = 1;
		value = atoi(arg2);
	} else if (key & QUOTA_FIX) {
		set = 1;
		value = 0;
	}
	if (set) {
		STARTLOG(LOG_WIZARD, "WIZ", "QUOTA")
			log_name(player);
		log_text((char *)" changed the quota of ");
		log_name(who);
		ENDLOG
			mung_quotas(who, key, value);
	}
	show_quota(player, who);
}

/*
 * --------------------------------------------------------------------------
 * * do_motd: Wizard-settable message of the day (displayed on connect)
 */

void do_motd(player, cause, key, message)
dbref player, cause;
int key;
char *message;
{
	int is_brief;

	is_brief = 0;
	if (key & MOTD_BRIEF) {
		is_brief = 1;
		key = key & ~MOTD_BRIEF;
		if (key == MOTD_ALL)
			key = MOTD_LIST;
		else if (key != MOTD_LIST)
			key |= MOTD_BRIEF;
	}
	switch (key) {
	case MOTD_ALL:
		StringCopy(mudconf.motd_msg, message);
		if (!Quiet(player))
			notify_quiet(player, "Set: MOTD.");
		break;
	case MOTD_WIZ:
		StringCopy(mudconf.wizmotd_msg, message);
		if (!Quiet(player))
			notify_quiet(player, "Set: Wizard MOTD.");
		break;
	case MOTD_DOWN:
		StringCopy(mudconf.downmotd_msg, message);
		if (!Quiet(player))
			notify_quiet(player, "Set: Down MOTD.");
		break;
	case MOTD_FULL:
		StringCopy(mudconf.fullmotd_msg, message);
		if (!Quiet(player))
			notify_quiet(player, "Set: Full MOTD.");
		break;
	case MOTD_LIST:
		if (Wizard(player)) {
			if (!is_brief) {
				notify_quiet(player,
					     "----- motd file -----");
				fcache_send(player, FC_MOTD);
				notify_quiet(player,
					     "----- wizmotd file -----");
				fcache_send(player, FC_WIZMOTD);
				notify_quiet(player,
					     "----- motd messages -----");
			}
			notify_quiet(player,
				     tprintf("MOTD: %s", mudconf.motd_msg));
			notify_quiet(player,
				     tprintf("Wizard MOTD: %s",
					     mudconf.wizmotd_msg));
			notify_quiet(player,
				     tprintf("Down MOTD: %s",
					     mudconf.downmotd_msg));
			notify_quiet(player,
				     tprintf("Full MOTD: %s",
					     mudconf.fullmotd_msg));
		} else {
			if (Guest(player))
				fcache_send(player, FC_CONN_GUEST);
			else
				fcache_send(player, FC_MOTD);
			notify_quiet(player, mudconf.motd_msg);
		}
		break;
	default:
		notify_quiet(player, "Illegal combination of switches.");
	}
}

/*
 * ---------------------------------------------------------------------------
 * * do_enable: enable or disable global control flags
 */
/* *INDENT-OFF* */

NAMETAB enable_names[] = {
{(char *)"building",		1,	CA_PUBLIC,	CF_BUILD},
{(char *)"checkpointing",	2,	CA_PUBLIC,	CF_CHECKPOINT},
{(char *)"cleaning",		2,	CA_PUBLIC,	CF_DBCHECK},
{(char *)"dequeueing",		1,	CA_PUBLIC,	CF_DEQUEUE},
{(char *)"idlechecking",	2,	CA_PUBLIC,	CF_IDLECHECK},
{(char *)"interpret",		2,	CA_PUBLIC,	CF_INTERP},
{(char *)"logins",		3,	CA_PUBLIC,	CF_LOGIN},
{(char *)"eventchecking",	2,	CA_PUBLIC,	CF_EVENTCHECK},
{ NULL,				0,	0,		0}};

/* *INDENT-ON* */






void do_global(player, cause, key, flag)
dbref player, cause;
int key;
char *flag;
{
	int flagvalue;

	/*
	 * Set or clear the indicated flag 
	 */

	flagvalue = search_nametab(player, enable_names, flag);
	if (flagvalue < 0) {
		notify_quiet(player, "I don't know about that flag.");
	} else if (key == GLOB_ENABLE) {
		mudconf.control_flags |= flagvalue;
		if (!Quiet(player))
			notify_quiet(player, "Enabled.");
	} else if (key == GLOB_DISABLE) {
		mudconf.control_flags &= ~flagvalue;
		if (!Quiet(player))
			notify_quiet(player, "Disabled.");
	} else {
		notify_quiet(player, "Illegal combination of switches.");
	}
}
