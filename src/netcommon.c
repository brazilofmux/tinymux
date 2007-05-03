/*
 * netcommon.c 
 */
/*
 * $Id: netcommon.c,v 1.2 1997/04/16 06:01:27 dpassmor Exp $ 
 */

/*
 * This file contains routines used by the networking code that do not
 * depend on the implementation of the networking code.  The network-specific
 * portions of the descriptor data structure are not used.
 */

#include "copyright.h"
#include "autoconf.h"

#include <time.h>
#include "db.h"
#include "mudconf.h"
#include "file_c.h"
#include "interface.h"
#include "command.h"
#include "externs.h"
#include "alloc.h"
#include "attrs.h"
#include "mguests.h"
#include "ansi.h"
#include "mail.h"
#include "powers.h"
#include "alloc.h"
#include "config.h"

extern int FDECL(process_output, (DESC * d));
extern void FDECL(handle_prog, (DESC *, char *));

#ifdef CONCENTRATE
extern void FDECL(do_becomeconc, (DESC *, char *));
extern void FDECL(do_makeid, (DESC *));
extern void FDECL(do_connectid, (DESC *, long int, char *));
extern void FDECL(do_killid, (DESC *, long int));

#endif

/*
 * ---------------------------------------------------------------------------
 * * make_portlist: Make a list of ports for PORTS().
 */

void make_portlist(player, target, buff, bufc)
dbref player;
dbref target;
char *buff, **bufc;
{
	DESC *d;
	int i = 0;
	
	DESC_ITER_CONN(d) {
		if (d->player == target) {
			safe_str(tprintf("%d ", d->descriptor), buff, bufc);
			i = 1;
		}
	}
	if (i)
		(*bufc)--;
	**bufc = '\0';
}

/*
 * ---------------------------------------------------------------------------
 * * timeval_sub: return difference between two times as a timeval
 */

struct timeval timeval_sub(now, then)
struct timeval now, then;

{
	now.tv_sec -= then.tv_sec;
	now.tv_usec -= then.tv_usec;
	if (now.tv_usec < 0) {
		now.tv_usec += 1000000;
		now.tv_sec--;
	}
	return now;
}

/*
 * ---------------------------------------------------------------------------
 * * msec_diff: return difference between two times in msec
 */

int msec_diff(now, then)
struct timeval now, then;
{
	return ((now.tv_sec - then.tv_sec) * 1000 +
		(now.tv_usec - then.tv_usec) / 1000);
}

/*
 * ---------------------------------------------------------------------------
 * * msec_add: add milliseconds to a timeval
 */

struct timeval msec_add(t, x)
struct timeval t;
int x;
{
	t.tv_sec += x / 1000;
	t.tv_usec += (x % 1000) * 1000;
	if (t.tv_usec >= 1000000) {
		t.tv_sec += t.tv_usec / 1000000;
		t.tv_usec = t.tv_usec % 1000000;
	}
	return t;
}

/*
 * ---------------------------------------------------------------------------
 * * update_quotas: Update timeslice quotas
 */

struct timeval update_quotas(last, current)
struct timeval last, current;
{
	int nslices;
	DESC *d;

	nslices = msec_diff(current, last) / mudconf.timeslice;

	if (nslices > 0) {
		DESC_ITER_ALL(d) {
			d->quota += mudconf.cmd_quota_incr * nslices;
			if (d->quota > mudconf.cmd_quota_max)
				d->quota = mudconf.cmd_quota_max;
		}
	}
	return msec_add(last, nslices * mudconf.timeslice);
}

/* raw_notify_html() -- raw_notify() without the newline */
void raw_notify_html(player, msg)
dbref player;
const char *msg;
{
DESC *d;

	if (!msg || !*msg)
		return;
	if (mudstate.inpipe && (player == mudstate.poutobj)) {
		safe_str(msg, mudstate.poutnew, &mudstate.poutbufc);
		return;
	}
	if (!Connected(player))
		return;

	DESC_ITER_PLAYER(player, d) {
		queue_string(d, msg);
	}
}

/*
 * ---------------------------------------------------------------------------
 * * raw_notify: write a message to a player
 */

void raw_notify(player, msg)
dbref player;
const char *msg;
{
	DESC *d;

	if (!msg || !*msg)
		return;

	if (mudstate.inpipe && (player == mudstate.poutobj)) {
		safe_str(msg, mudstate.poutnew, &mudstate.poutbufc);
		safe_str("\r\n", mudstate.poutnew, &mudstate.poutbufc);
		return;
	}
	
	if (!Connected(player))
		return;

	DESC_ITER_PLAYER(player, d) {
		queue_string(d, msg);
		queue_write(d, "\r\n", 2);
	}
}

void raw_notify_newline(player)
dbref player;
{
	DESC *d;

	if (mudstate.inpipe && (player == mudstate.poutobj)) {
		safe_str("\r\n", mudstate.poutnew, &mudstate.poutbufc);
		return;
	}
	if (!Connected(player))
		return;

	DESC_ITER_PLAYER(player, d) {
		queue_write(d, "\r\n", 2);
	}
}

/*
 * ---------------------------------------------------------------------------
 * * raw_broadcast: Send message to players who have indicated flags
 */

#ifdef NEED_VSPRINTF_DCL
extern char *FDECL(vsprintf, (char *, char *, va_list));

#endif

#ifdef STDC_HEADERS
void raw_broadcast(int inflags, char *template,...)
#else
void raw_broadcast(va_alist)
va_dcl

#endif

{
	char *buff;
	DESC *d;
	va_list ap;

#ifdef STDC_HEADERS
	va_start(ap, template);
#else
	int inflags;
	char *template;

	va_start(ap);
	inflags = va_arg(ap, int);
	template = va_arg(ap, char *);

#endif
	if (!template || !*template)
		return;

	buff = alloc_lbuf("raw_broadcast");
	vsprintf(buff, template, ap);

	DESC_ITER_CONN(d) {
		if ((Flags(d->player) & inflags) == inflags) {
			queue_string(d, buff);
			queue_write(d, "\r\n", 2);
			process_output(d);
		}
	}
	free_lbuf(buff);
	va_end(ap);
}

/*
 * ---------------------------------------------------------------------------
 * * clearstrings: clear out prefix and suffix strings
 */

void clearstrings(d)
DESC *d;
{
	if (d->output_prefix) {
		free_lbuf(d->output_prefix);
		d->output_prefix = NULL;
	}
	if (d->output_suffix) {
		free_lbuf(d->output_suffix);
		d->output_suffix = NULL;
	}
}

/*
 * ---------------------------------------------------------------------------
 * * queue_write: Add text to the output queue for the indicated descriptor.
 */

void queue_write(d, b, n)
DESC *d;
const char *b;
int n;
{
	int left;
	char *buf;
	TBLOCK *tp;

	if (n <= 0)
		return;

	if (d->output_size + n > mudconf.output_limit)
		process_output(d);

	left = mudconf.output_limit - d->output_size - n;
	if (left < 0) {
		tp = d->output_head;
		if (tp == NULL) {
			STARTLOG(LOG_PROBLEMS, "QUE", "WRITE")
				log_text((char *)"Flushing when output_head is null!");
			ENDLOG
		} else {
			STARTLOG(LOG_NET, "NET", "WRITE")
				buf = alloc_lbuf("queue_write.LOG");
			sprintf(buf,
				"[%d/%s] Output buffer overflow, %d chars discarded by ",
				d->descriptor, d->addr, tp->hdr.nchars);
			log_text(buf);
			free_lbuf(buf);
			log_name(d->player);
			ENDLOG
				d->output_size -= tp->hdr.nchars;
			d->output_head = tp->hdr.nxt;
			d->output_lost += tp->hdr.nchars;
			if (d->output_head == NULL)
				d->output_tail = NULL;
			free(tp);
		}
	}
	/*
	 * Allocate an output buffer if needed 
	 */

	if (d->output_head == NULL) {
		tp = (TBLOCK *) malloc(OUTPUT_BLOCK_SIZE);
		tp->hdr.nxt = NULL;
		tp->hdr.start = tp->data;
		tp->hdr.end = tp->data;
		tp->hdr.nchars = 0;
		d->output_head = tp;
		d->output_tail = tp;
	} else {
		tp = d->output_tail;
	}

	/*
	 * Now tp points to the last buffer in the chain 
	 */

	d->output_size += n;
	d->output_tot += n;
	do {

		/*
		 * See if there is enough space in the buffer to hold the
		 * string.  If so, copy it and update the pointers.. 
		 */

		left = OUTPUT_BLOCK_SIZE - (tp->hdr.end - (char *)tp + 1);
		if (n <= left) {
			StringCopyTrunc(tp->hdr.end, b, n);
			tp->hdr.end += n;
			tp->hdr.nchars += n;
			n = 0;
		} else {

			/*
			 * It didn't fit.  Copy what will fit and then
			 * allocate * another buffer and retry. 
			 */

			if (left > 0) {
				StringCopyTrunc(tp->hdr.end, b, left);
				tp->hdr.end += left;
				tp->hdr.nchars += left;
				b += left;
				n -= left;
			}
			tp = (TBLOCK *) malloc(OUTPUT_BLOCK_SIZE);
			tp->hdr.nxt = NULL;
			tp->hdr.start = tp->data;
			tp->hdr.end = tp->data;
			tp->hdr.nchars = 0;
			d->output_tail->hdr.nxt = tp;
			d->output_tail = tp;
		}
	} while (n > 0);
}

void queue_string(d, s)
DESC *d;
const char *s;
{
	char *new;

	if (!Ansi(d->player) && index(s, ESC_CHAR))
		new = strip_ansi(s);
	else if (NoBleed(d->player))
		new = normal_to_white(s);
	else
		new = (char *)s;
	queue_write(d, new, strlen(new));
}

void freeqs(d)
DESC *d;
{
	TBLOCK *tb, *tnext;
	CBLK *cb, *cnext;

	tb = d->output_head;
	while (tb) {
		tnext = tb->hdr.nxt;
		free(tb);
		tb = tnext;
	}
	d->output_head = NULL;
	d->output_tail = NULL;

	cb = d->input_head;
	while (cb) {
		cnext = (CBLK *) cb->hdr.nxt;
		free_lbuf(cb);
		cb = cnext;
	}

	d->input_head = NULL;
	d->input_tail = NULL;

	if (d->raw_input)
		free_lbuf(d->raw_input);
	d->raw_input = NULL;
	d->raw_input_at = NULL;
}

/*
 * ---------------------------------------------------------------------------
 * * desc_addhash: Add a net descriptor to its player hash list.
 */

void desc_addhash(d)
DESC *d;
{
	dbref player;
	DESC *hdesc;

	player = d->player;
	hdesc = (DESC *) nhashfind((int)player, &mudstate.desc_htab);
	if (hdesc == NULL) {
		d->hashnext = NULL;
		nhashadd((int)player, (int *)d, &mudstate.desc_htab);
	} else {
		d->hashnext = hdesc;
		nhashrepl((int)player, (int *)d, &mudstate.desc_htab);
	}
}

/*
 * ---------------------------------------------------------------------------
 * * desc_delhash: Remove a net descriptor from its player hash list.
 */

static void desc_delhash(d)
DESC *d;
{
	DESC *hdesc, *last;
	dbref player;

	player = d->player;
	last = NULL;
	hdesc = (DESC *) nhashfind((int)player, &mudstate.desc_htab);
	while (hdesc != NULL) {
		if (d == hdesc) {
			if (last == NULL) {
				if (d->hashnext == NULL) {
					nhashdelete((int)player,
						    &mudstate.desc_htab);
				} else {
					nhashrepl((int)player,
						  (int *)d->hashnext,
						  &mudstate.desc_htab);
				}
			} else {
				last->hashnext = d->hashnext;
			}
			break;
		}
		last = hdesc;
		hdesc = hdesc->hashnext;
	}
	d->hashnext = NULL;
}

void welcome_user(d)
DESC *d;
{
	if (d->host_info & H_REGISTRATION)
		fcache_dump(d, FC_CONN_REG);
	else
		fcache_dump(d, FC_CONN);
}

void save_command(d, command)
DESC *d;
CBLK *command;
{

	command->hdr.nxt = NULL;
	if (d->input_tail == NULL)
		d->input_head = command;
	else
		d->input_tail->hdr.nxt = command;
	d->input_tail = command;
}

static void set_userstring(userstring, command)
char **userstring;
const char *command;
{
	while (*command && isascii(*command) && isspace(*command))
		command++;
	if (!*command) {
		if (*userstring != NULL) {
			free_lbuf(*userstring);
			*userstring = NULL;
		}
	} else {
		if (*userstring == NULL) {
			*userstring = alloc_lbuf("set_userstring");
		}
		StringCopy(*userstring, command);
	}
}

static void parse_connect(msg, command, user, pass)
const char *msg;
char *command, *user, *pass;
{
	char *p;

	if (strlen(msg) > MBUF_SIZE) {
		*command = '\0';
		*user = '\0';
		*pass = '\0';
		return;
	}
	while (*msg && isascii(*msg) && isspace(*msg))
		msg++;
	p = command;
	while (*msg && isascii(*msg) && !isspace(*msg))
		*p++ = *msg++;
	*p = '\0';
	while (*msg && isascii(*msg) && isspace(*msg))
		msg++;
	p = user;
	if (mudconf.name_spaces && (*msg == '\"')) {
		for (; *msg && (*msg == '\"' || isspace(*msg)); msg++) ;
		while (*msg && *msg != '\"') {
			while (*msg && !isspace(*msg) && (*msg != '\"'))
				*p++ = *msg++;
			if (*msg == '\"')
				break;
			while (*msg && isspace(*msg))
				msg++;
			if (*msg && (*msg != '\"'))
				*p++ = ' ';
		}
		for (; *msg && *msg == '\"'; msg++) ;
	} else
		while (*msg && isascii(*msg) && !isspace(*msg))
			*p++ = *msg++;
	*p = '\0';
	while (*msg && isascii(*msg) && isspace(*msg))
		msg++;
	p = pass;
	while (*msg && isascii(*msg) && !isspace(*msg))
		*p++ = *msg++;
	*p = '\0';
}

static const char *time_format_1(dt)
time_t dt;
{
	register struct tm *delta;
	static char buf[64];

	if (dt < 0)
		dt = 0;

	delta = gmtime(&dt);
	if (delta->tm_yday > 0) {
		sprintf(buf, "%dd %02d:%02d",
			delta->tm_yday, delta->tm_hour, delta->tm_min);
	} else {
		sprintf(buf, "%02d:%02d",
			delta->tm_hour, delta->tm_min);
	}
	return buf;
}

static const char *time_format_2(dt)
time_t dt;
{
	register struct tm *delta;
	static char buf[64];

	if (dt < 0)
		dt = 0;

	delta = gmtime(&dt);
	if (delta->tm_yday > 0) {
		sprintf(buf, "%dd", delta->tm_yday);
	} else if (delta->tm_hour > 0) {
		sprintf(buf, "%dh", delta->tm_hour);
	} else if (delta->tm_min > 0) {
		sprintf(buf, "%dm", delta->tm_min);
	} else {
		sprintf(buf, "%ds", delta->tm_sec);
	}
	return buf;
}

static void announce_connect(player, d)
dbref player;
DESC *d;
{
	dbref loc, aowner, temp;
	dbref zone, obj;

	int aflags, num, key, count;
	char *buf, *time_str;
	DESC *dtemp;

	desc_addhash(d);

	count = 0;
	DESC_ITER_CONN(dtemp)
		count++;

	if (mudstate.record_players < count)
		mudstate.record_players = count;

	buf = atr_pget(player, A_TIMEOUT, &aowner, &aflags);
	if (buf) {
		d->timeout = atoi(buf);
		if (d->timeout <= 0)
			d->timeout = mudconf.idle_timeout;
	}
	free_lbuf(buf);

	loc = Location(player);
	s_Connected(player);

	if (d->flags & DS_PUEBLOCLIENT) {
		s_Html(player);
	}

	raw_notify(player, tprintf("\n%sMOTD:%s %s\n", ANSI_HILITE,
				   ANSI_NORMAL, mudconf.motd_msg));
	if (Wizard(player)) {
		raw_notify(player, tprintf("%sWIZMOTD:%s %s\n",
			    ANSI_HILITE, ANSI_NORMAL, mudconf.wizmotd_msg));
		if (!(mudconf.control_flags & CF_LOGIN)) {
			raw_notify(player, "*** Logins are disabled.");
		}
	}
	buf = atr_get(player, A_LPAGE, &aowner, &aflags);
	if (buf && *buf) {
		raw_notify(player, "Your PAGE LOCK is set.  You may be unable to receive some pages.");
	}
	num = 0;
	DESC_ITER_PLAYER(player, dtemp) num++;

	/*
	 * Reset vacation flag 
	 */
	s_Flags2(player, Flags2(player) & ~VACATION);

	if (num < 2) {
		sprintf(buf, "%s has connected.", Name(player));

		if (mudconf.have_comsys)
			do_comconnect(player);

		if (Dark(player)) {
			raw_broadcast(MONITOR,
				      (char *)"GAME: %s has DARK-connected.",
				      Name(player), 0, 0, 0, 0, 0);
		} else {
			raw_broadcast(MONITOR,
				      (char *)"GAME: %s has connected.",
				      Name(player), 0, 0, 0, 0, 0);
		}
	} else {
		sprintf(buf, "%s has reconnected.", Name(player));
		raw_broadcast(MONITOR,
			      (char *)"GAME: %s has reconnected.",
			      Name(player), 0, 0, 0, 0, 0);
	}

	key = MSG_INV;
	if ((loc != NOTHING) && !(Dark(player) && Wizard(player)))
		key |= (MSG_NBR | MSG_NBR_EXITS | MSG_LOC | MSG_FWDLIST);

	temp = mudstate.curr_enactor;
	mudstate.curr_enactor = player;
	notify_check(player, player, buf, key);
	free_lbuf(buf);
	if (Suspect(player)) {
		raw_broadcast(WIZARD, (char *)"[Suspect] %s has connected.",
			      Name(player), 0, 0, 0, 0, 0);
	}
	if (d->host_info & H_SUSPECT) {
		raw_broadcast(WIZARD,
			      (char *)"[Suspect site: %s] %s has connected.",
			      d->addr, Name(player), 0, 0, 0, 0);
	}
	buf = atr_pget(player, A_ACONNECT, &aowner, &aflags);
	if (buf)
		wait_que(player, player, 0, NOTHING, 0, buf, (char **)NULL, 0,
			 NULL);
	free_lbuf(buf);
	if (mudconf.master_room != NOTHING) {
		buf = atr_pget(mudconf.master_room, A_ACONNECT, &aowner,
			       &aflags);
		if (buf)
			wait_que(mudconf.master_room, player, 0, NOTHING, 0,
				 buf, (char **)NULL, 0, NULL);
		free_lbuf(buf);
		DOLIST(obj, Contents(mudconf.master_room)) {
			buf = atr_pget(obj, A_ACONNECT, &aowner, &aflags);
			if (buf) {
				wait_que(obj, player, 0, NOTHING, 0, buf,
					 (char **)NULL, 0, NULL);
			}
			free_lbuf(buf);
		}
	}
	/*
	 * do the zone of the player's location's possible aconnect 
	 */
	if (mudconf.have_zones && ((zone = Zone(loc)) != NOTHING)) {
		switch (Typeof(zone)) {
		case TYPE_THING:
			buf = atr_pget(zone, A_ACONNECT, &aowner, &aflags);
			if (buf) {
				wait_que(zone, player, 0, NOTHING, 0, buf,
					 (char **)NULL, 0, NULL);
			}
			free_lbuf(buf);
			break;
		case TYPE_ROOM:
			/*
			 * check every object in the room for a connect * * * 
			 * action 
			 */
			DOLIST(obj, Contents(zone)) {
				buf = atr_pget(obj, A_ACONNECT, &aowner, &aflags);
				if (buf) {
					wait_que(obj, player, 0, NOTHING, 0, buf,
						 (char **)NULL, 0, NULL);
				}
				free_lbuf(buf);
			}
			break;
		default:
			log_text(tprintf("Invalid zone #%d for %s(#%d) has bad type %d",
				 zone, Name(player), player, Typeof(zone)));
		}
	}
	time_str = ctime(&mudstate.now);
	time_str[strlen(time_str) - 1] = '\0';
	record_login(player, 1, time_str, d->addr, d->username);
	look_in(player, Location(player), (LK_SHOWEXIT | LK_OBEYTERSE | LK_SHOWVRML));
	mudstate.curr_enactor = temp;
}

void announce_disconnect(player, d, reason)
dbref player;
DESC *d;
const char *reason;
{
	dbref loc, aowner, temp, zone, obj;
	int num, aflags, key;
	char *buf, *atr_temp;
	DESC *dtemp;
	char *argv[1];


	if (Suspect(player)) {
		raw_broadcast(WIZARD,
			      (char *)"[Suspect] %s has disconnected.",
			      Name(player), 0, 0, 0, 0, 0);
	}
	if (d->host_info & H_SUSPECT) {
		raw_broadcast(WIZARD,
			  (char *)"[Suspect site: %s] %s has disconnected.",
			      d->addr, Name(d->player), 0, 0, 0, 0);
	}
	loc = Location(player);
	num = 0;
	DESC_ITER_PLAYER(player, dtemp) num++;

	temp = mudstate.curr_enactor;
	mudstate.curr_enactor = player;

	if (num < 2) {
		buf = alloc_mbuf("announce_disconnect.only");

		sprintf(buf, "%s has disconnected.", Name(player));
		key = MSG_INV;
		if ((loc != NOTHING) && !(Dark(player) && Wizard(player)))
			key |= (MSG_NBR | MSG_NBR_EXITS | MSG_LOC | MSG_FWDLIST);
		notify_check(player, player, buf, key);
		free_mbuf(buf);

		if (mudconf.have_comsys)
			do_comdisconnect(player);

		if (mudconf.have_mailer)
			do_mail_purge(player);

		raw_broadcast(MONITOR, (char *)"GAME: %s has disconnected.", Name(player), 0, 0, 0, 0, 0);

		if (Guest(player) && mudconf.have_comsys)
			toast_player(player);

		argv[0] = (char *)reason;
		c_Connected(player);

		atr_temp = atr_pget(player, A_ADISCONNECT, &aowner, &aflags);
		if (atr_temp && *atr_temp)
			wait_que(player, player, 0, NOTHING, 0, atr_temp, argv, 1,
				 NULL);
		free_lbuf(atr_temp);
		if (mudconf.master_room != NOTHING) {
			atr_temp = atr_pget(mudconf.master_room, A_ADISCONNECT, &aowner,
					    &aflags);
			if (atr_temp)
				wait_que(mudconf.master_room, player, 0, NOTHING, 0,
					 atr_temp, (char **)NULL, 0, NULL);
			free_lbuf(atr_temp);
			DOLIST(obj, Contents(mudconf.master_room)) {
				atr_temp = atr_pget(obj, A_ADISCONNECT, &aowner, &aflags);
				if (atr_temp) {
					wait_que(obj, player, 0, NOTHING, 0, atr_temp,
						 (char **)NULL, 0, NULL);
				}
				free_lbuf(atr_temp);
			}
		}
		/*
		 * do the zone of the player's location's possible * * *
		 * adisconnect 
		 */
		if (mudconf.have_zones && ((zone = Zone(loc)) != NOTHING)) {
			switch (Typeof(zone)) {
			case TYPE_THING:
				atr_temp = atr_pget(zone, A_ADISCONNECT, &aowner, &aflags);
				if (atr_temp) {
					wait_que(zone, player, 0, NOTHING, 0, atr_temp,
						 (char **)NULL, 0, NULL);
				}
				free_lbuf(atr_temp);
				break;
			case TYPE_ROOM:
				/*
				 * check every object in the room for a * * * 
				 * connect action 
				 */
				DOLIST(obj, Contents(zone)) {
					atr_temp = atr_pget(obj, A_ADISCONNECT, &aowner, &aflags);
					if (atr_temp) {
						wait_que(obj, player, 0, NOTHING, 0, atr_temp,
						    (char **)NULL, 0, NULL);
					}
					free_lbuf(atr_temp);
				}
				break;
			default:
				log_text(tprintf("Invalid zone #%d for %s(#%d) has bad type %d",
				 zone, Name(player), player, Typeof(zone)));
			}
		}
		if (d->flags & DS_AUTODARK) {
			s_Flags(d->player, Flags(d->player) & ~DARK);
			d->flags &= ~DS_AUTODARK;
		}
		
		if (Guest(player))
			s_Flags(player, Flags(player) | DARK);	
	} else {
		buf = alloc_mbuf("announce_disconnect.partial");
		sprintf(buf, "%s has partially disconnected.", Name(player));
		key = MSG_INV;
		if ((loc != NOTHING) && !(Dark(player) && Wizard(player)))
			key |= (MSG_NBR | MSG_NBR_EXITS | MSG_LOC | MSG_FWDLIST);
		notify_check(player, player, buf, key);
		raw_broadcast(MONITOR, (char *)"GAME: %s has partially disconnected.", Name(player), 0, 0, 0, 0, 0);
		free_mbuf(buf);
	}

	mudstate.curr_enactor = temp;
	desc_delhash(d);
}

int boot_off(player, message)
dbref player;
char *message;
{
	DESC *d, *dnext;
	int count;

	count = 0;
	DESC_SAFEITER_PLAYER(player, d, dnext) {
		if (message && *message) {
			queue_string(d, message);
			queue_string(d, "\r\n");
		}
		shutdownsock(d, R_BOOT);
		count++;
	}
	return count;
}

int boot_by_port(port, no_god, message)
int port, no_god;
char *message;
{
	DESC *d, *dnext;
	int count;

	count = 0;
	DESC_SAFEITER_ALL(d, dnext) {
		if ((d->descriptor == port) && (!no_god || !God(d->player))) {
			if (message && *message) {
				queue_string(d, message);
				queue_string(d, "\r\n");
			}
			shutdownsock(d, R_BOOT);
			count++;
		}
	}
	return count;
}

/*
 * ---------------------------------------------------------------------------
 * * desc_reload: Reload parts of net descriptor that are based on db info.
 */

void desc_reload(player)
dbref player;
{
	DESC *d;
	char *buf;
	dbref aowner;
	FLAG aflags;

	DESC_ITER_PLAYER(player, d) {
		buf = atr_pget(player, A_TIMEOUT, &aowner, &aflags);
		if (buf) {
			d->timeout = atoi(buf);
			if (d->timeout <= 0)
				d->timeout = mudconf.idle_timeout;
		}
		free_lbuf(buf);
	}
}

/*
 * ---------------------------------------------------------------------------
 * * fetch_idle, fetch_connect: Return smallest idle time/largest connec time
 * * for a player (or -1 if not logged in)
 */

int fetch_idle(target)
dbref target;
{
	DESC *d;
	int result, idletime;

	result = -1;
	DESC_ITER_PLAYER(target, d) {
		idletime = (mudstate.now - d->last_time);
		if ((result == -1) || (idletime < result))
			result = idletime;
	}
	return result;
}

int fetch_connect(target)
dbref target;
{
	DESC *d;
	int result, conntime;

	result = -1;
	DESC_ITER_PLAYER(target, d) {
		conntime = (mudstate.now - d->connected_at);
		if (conntime > result)
			result = conntime;
	}
	return result;
}

void NDECL(check_idle)
{
	DESC *d, *dnext;
	time_t idletime;

	DESC_SAFEITER_ALL(d, dnext) {
		if (d->flags & DS_CONNECTED) {
			idletime = mudstate.now - d->last_time;
			if ((idletime > d->timeout) &&
			    !Can_Idle(d->player)) {
				queue_string(d,
					  "*** Inactivity Timeout ***\r\n");
				shutdownsock(d, R_TIMEOUT);
			} else if (mudconf.idle_wiz_dark &&
				   (idletime > mudconf.idle_timeout) &&
				   Can_Idle(d->player) && !Dark(d->player)) {
				s_Flags(d->player, Flags(d->player) | DARK);
				d->flags |= DS_AUTODARK;
			}
		} else {
			idletime = mudstate.now - d->connected_at;
#ifdef CONCENTRATE
			if ((idletime > mudconf.conn_timeout) && !(d->cstatus & C_CCONTROL)) {
#else
			if (idletime > mudconf.conn_timeout) {
#endif /*
        * CONCENTRATE 
        */
				queue_string(d,
					     "*** Login Timeout ***\r\n");
				shutdownsock(d, R_TIMEOUT);
			}
		}
	}
}

void NDECL(check_events)
{
	struct tm *ltime;
	dbref thing, parent;
	int lev;

	ltime = localtime(&mudstate.now);
	if ((ltime->tm_hour == mudconf.events_daily_hour)
	    && !(mudstate.events_flag & ET_DAILY)) {
		mudstate.events_flag = mudstate.events_flag | ET_DAILY;
		DO_WHOLE_DB(thing) {
			if (Going(thing))
				continue;

			ITER_PARENTS(thing, parent, lev) {
				if (Flags2(thing) & HAS_DAILY) {
					did_it(Owner(thing), thing, 0, NULL,
					    0, NULL, A_DAILY, (char **)NULL,
					       0);

					break;
				}
			}
		}
	}
	if (ltime->tm_hour == 23) {	/*
					 * Nightly resetting 
					 */
		mudstate.events_flag = 0;
	}
}
static char *trimmed_name(player)
dbref player;
{
	static char cbuff[18];

	if (strlen(Name(player)) <= 16)
		return Name(player);
	StringCopyTrunc(cbuff, Name(player), 16);
	cbuff[16] = '\0';
	return cbuff;
}
static char *trimmed_site(name)
char *name;
{
	static char buff[MBUF_SIZE];

	if ((strlen(name) <= mudconf.site_chars) || (mudconf.site_chars == 0))
		return name;
	StringCopyTrunc(buff, name, mudconf.site_chars);
	buff[mudconf.site_chars + 1] = '\0';
	return buff;
}
static void dump_users(e, match, key)
DESC *e;
char *match;
int key;
{
	DESC *d;
	int count;
	char *buf, *fp, *sp, flist[4], slist[4];
	dbref room_it;

	while (match && *match && isspace(*match))
		match++;
	if (!match || !*match)
		match = NULL;

	if (e->flags & DS_PUEBLOCLIENT)
		queue_string(e, "<pre>");

	buf = alloc_mbuf("dump_users");
	if (key == CMD_SESSION) {
		queue_string(e, "                               ");
		queue_string(e, "     Characters Input----  Characters Output---\r\n");
	}
	queue_string(e, "Player Name        On For Idle ");
	if (key == CMD_SESSION) {
		queue_string(e, "Port Pend  Lost     Total  Pend  Lost     Total\r\n");
	} else if ((e->flags & DS_CONNECTED) && (Wizard_Who(e->player)) &&
		   (key == CMD_WHO)) {
		queue_string(e, "  Room    Cmds   Host\r\n");
	} else {
		if (Wizard_Who(e->player))
			queue_string(e, "  ");
		else
			queue_string(e, " ");
		queue_string(e, mudstate.doing_hdr);
		queue_string(e, "\r\n");
	}
	count = 0;
	DESC_ITER_CONN(d) {
		if (!Hidden(d->player) ||
		    (e->flags & DS_CONNECTED) & Wizard_Who(e->player)) {
			count++;
			if (match && !(string_prefix(Name(d->player), match)))
				continue;
			if ((key == CMD_SESSION) &&
			    !(Wizard_Who(e->player) && (e->flags & DS_CONNECTED)) &&
			    (d->player != e->player))
				continue;

			/*
			 * Get choice flags for wizards 
			 */

			fp = flist;
			sp = slist;
			if ((e->flags & DS_CONNECTED) && Wizard_Who(e->player)) {
				if (Hidden(d->player)) {
					if (d->flags & DS_AUTODARK)
						*fp++ = 'd';
					else
						*fp++ = 'D';
				}
				if (!Findable(d->player)) {
					*fp++ = 'U';
				} else {
					room_it = where_room(d->player);
					if (Good_obj(room_it)) {
						if (Hideout(room_it))
							*fp++ = 'u';
					} else {
						*fp++ = 'u';
					}
				}

				if (Suspect(d->player))
					*fp++ = '+';
				if (d->host_info & H_FORBIDDEN)
					*sp++ = 'F';
				if (d->host_info & H_REGISTRATION)
					*sp++ = 'R';
				if (d->host_info & H_SUSPECT)
					*sp++ = '+';
			}
			*fp = '\0';
			*sp = '\0';

			if ((e->flags & DS_CONNECTED) &&
			    Wizard_Who(e->player) &&
			    (key == CMD_WHO)) {
				sprintf(buf,
				     "%-16s%9s %4s%-3s#%-6d%5d%3s%-25s\r\n",
					trimmed_name(d->player),
					time_format_1(mudstate.now - d->connected_at),
				 time_format_2(mudstate.now - d->last_time),
					flist,
				      Location(d->player), d->command_count,
					slist,
					trimmed_site(((d->username[0] != '\0') ? tprintf("%s@%s", d->username, d->addr) : d->addr)));
			} else if (key == CMD_SESSION) {
				sprintf(buf,
				  "%-16s%9s %4s%5d%5d%6d%10d%6d%6d%10d\r\n",
					trimmed_name(d->player),
					time_format_1(mudstate.now - d->connected_at),
				 time_format_2(mudstate.now - d->last_time),
					d->descriptor,
					d->input_size, d->input_lost,
					d->input_tot,
					d->output_size, d->output_lost,
					d->output_tot);
			} else if (Wizard_Who(e->player)) {
				sprintf(buf, "%-16s%9s %4s%-3s%s\r\n",
					trimmed_name(d->player),
					time_format_1(mudstate.now - d->connected_at),
				 time_format_2(mudstate.now - d->last_time),
					flist,
					d->doing);
			} else {
				sprintf(buf, "%-16s%9s %4s  %s\r\n",
					trimmed_name(d->player),
					time_format_1(mudstate.now - d->connected_at),
				 time_format_2(mudstate.now - d->last_time),
					d->doing);
			}
			queue_string(e, buf);
		}
	}

	/*
	 * sometimes I like the ternary operator.... 
	 */

	sprintf(buf, "%d Player%slogged in, %d record, %s maximum.\r\n", count,
		(count == 1) ? " " : "s ", mudstate.record_players,
		(mudconf.max_players == -1) ? "no" : tprintf("%d", mudconf.max_players));
	queue_string(e, buf);
	
	if (e->flags & DS_PUEBLOCLIENT)
		queue_string(e, "</pre>");
	
	free_mbuf(buf);
}

/*
 * ---------------------------------------------------------------------------
 * * do_doing: Set the doing string that appears in the WHO report.
 * * Idea from R'nice@TinyTIM.
 */

void do_doing(player, cause, key, arg)
dbref player, cause;
int key;
char *arg;
{
	DESC *d;
	char *c;
	int foundany, over;

	if (key == DOING_MESSAGE) {
		foundany = 0;
		over = 0;
		DESC_ITER_PLAYER(player, d) {
			c = d->doing;

			over = safe_copy_str(arg, d->doing, &c, 36);
			StringCopy(c, ANSI_NORMAL);
			foundany = 1;
		}
		if (foundany) {
			if (over) {
				notify(player,
				     tprintf("Warning: %d characters lost.",
					     over));
			}
			if (!Quiet(player))
				notify(player, "Set.");
		} else {
			notify(player, "Not connected.");
		}
	} else if (key == DOING_HEADER) {
		if (!(Can_Poll(player))) {
			notify(player, "Permission denied.");
			return;
		}
		if (!arg || !*arg) {
			StringCopy(mudstate.doing_hdr, "Doing");
			over = 0;
		} else {
			c = mudstate.doing_hdr;
			over = safe_copy_str(arg, mudstate.doing_hdr, &c, 40);
			*c = '\0';
		}
		if (over) {
			notify(player,
			     tprintf("Warning: %d characters lost.", over));
		}
		if (!Quiet(player))
			notify(player, "Set.");
	} else {
		notify(player, tprintf("Poll: %s", mudstate.doing_hdr));
	}
}
/* *INDENT-OFF* */

NAMETAB logout_cmdtable[] = {
{(char *)"DOING",	5,	CA_PUBLIC,	CMD_DOING},
{(char *)"LOGOUT",	6,	CA_PUBLIC,	CMD_LOGOUT},
{(char *)"OUTPUTPREFIX",12,	CA_PUBLIC,	CMD_PREFIX|CMD_NOxFIX},
{(char *)"OUTPUTSUFFIX",12,	CA_PUBLIC,	CMD_SUFFIX|CMD_NOxFIX},
{(char *)"QUIT",	4,	CA_PUBLIC,	CMD_QUIT},
{(char *)"SESSION",	7,	CA_PUBLIC,	CMD_SESSION},
{(char *)"WHO",		3,	CA_PUBLIC,	CMD_WHO},
{(char *)"PUEBLOCLIENT", 12,	CA_PUBLIC,      CMD_PUEBLOCLIENT},
{NULL,			0,	0,		0}};

/* *INDENT-ON* */

void NDECL(init_logout_cmdtab)
{
	NAMETAB *cp;

	/*
	 * Make the htab bigger than the number of entries so that we find
	 * things on the first check.  Remember that the admin can add
	 * aliases. 
	 */

	hashinit(&mudstate.logout_cmd_htab, 3 * HASH_FACTOR);
	for (cp = logout_cmdtable; cp->flag; cp++)
		hashadd(cp->name, (int *)cp, &mudstate.logout_cmd_htab);
}

static void failconn(logcode, logtype, logreason, d, disconnect_reason,
	      player, filecache, motd_msg, command, user, password, cmdsave)
const char *logcode, *logtype, *logreason;
char *motd_msg, *command, *user, *password, *cmdsave;
DESC *d;
int disconnect_reason, filecache;
dbref player;
{
	char *buff;

	STARTLOG(LOG_LOGIN | LOG_SECURITY, logcode, "RJCT")
		buff = alloc_mbuf("failconn.LOG");
	sprintf(buff, "[%d/%s] %s rejected to ",
		d->descriptor, d->addr, logtype);
	log_text(buff);
	free_mbuf(buff);
	if (player != NOTHING)
		log_name(player);
	else
		log_text(user);
	log_text((char *)" (");
	log_text((char *)logreason);
	log_text((char *)")");
	ENDLOG
		fcache_dump(d, filecache);
	if (*motd_msg) {
		queue_string(d, motd_msg);
		queue_write(d, "\r\n", 2);
	}
	free_lbuf(command);
	free_lbuf(user);
	free_lbuf(password);
	shutdownsock(d, disconnect_reason);
	mudstate.debug_cmd = cmdsave;
	return;
}

static const char *connect_fail =
"Either that player does not exist, or has a different password.\r\n";
static const char *create_fail =
"Either there is already a player with that name, or that name is illegal.\r\n";

static int check_connect(d, msg)
DESC *d;
char *msg;
{
	char *command, *user, *password, *buff, *cmdsave;
	dbref player, aowner;
	int aflags, nplayers;
	DESC *d2;
	char *p;

	cmdsave = mudstate.debug_cmd;
	mudstate.debug_cmd = (char *)"< check_connect >";

	/*
	 * Hide the password length from SESSION 
	 */

	d->input_tot -= (strlen(msg) + 1);

	/*
	 * Crack the command apart 
	 */

	command = alloc_lbuf("check_conn.cmd");
	user = alloc_lbuf("check_conn.user");
	password = alloc_lbuf("check_conn.pass");
	parse_connect(msg, command, user, password);

	if (!strncmp(command, "co", 2) || !strncmp(command, "cd", 2)) {
		if ((string_prefix(user, mudconf.guest_prefix)) &&
		    (mudconf.guest_char != NOTHING) &&
		    (mudconf.control_flags & CF_LOGIN)) {
			if ((p = make_guest(d)) == NULL) {
				queue_string(d, "All guests are tied up, please try again later.\n");
				free_lbuf(command);
				free_lbuf(user);
				free_lbuf(password);
				return 0;
			}
			StringCopy(user, p);
			StringCopy(password, mudconf.guest_prefix);
		}
		/*
		 * See if this connection would exceed the max #players 
		 */

		if (mudconf.max_players < 0) {
			nplayers = mudconf.max_players - 1;
		} else {
			nplayers = 0;
			DESC_ITER_CONN(d2)
				nplayers++;
		}

		player = connect_player(user, password, d->addr, d->username);
		if (player == NOTHING) {

			/*
			 * Not a player, or wrong password 
			 */

			queue_string(d, connect_fail);
			STARTLOG(LOG_LOGIN | LOG_SECURITY, "CON", "BAD")
				buff = alloc_lbuf("check_conn.LOG.bad");
			user[3800] = '\0';
			sprintf(buff, "[%d/%s] Failed connect to '%s'",
				d->descriptor, d->addr, user);
			log_text(buff);
			free_lbuf(buff);
			ENDLOG
				if (--(d->retries_left) <= 0) {
				free_lbuf(command);
				free_lbuf(user);
				free_lbuf(password);
				shutdownsock(d, R_BADLOGIN);
				mudstate.debug_cmd = cmdsave;
				return 0;
			}
		} else if (((mudconf.control_flags & CF_LOGIN) &&
			    (nplayers < mudconf.max_players)) ||
			   WizRoy(player) || God(player)) {

			if (!strncmp(command, "cd", 2) &&
			    (Wizard(player) || God(player)))
				s_Flags(player, Flags(player) | DARK);

			/*
			 * Logins are enabled, or wiz or god 
			 */

			STARTLOG(LOG_LOGIN, "CON", "LOGIN")
				buff = alloc_mbuf("check_conn.LOG.login");
			sprintf(buff, "[%d/%s] Connected to ",
				d->descriptor, d->addr);
			log_text(buff);
			log_name_and_loc(player);
			free_mbuf(buff);
			ENDLOG
				d->flags |= DS_CONNECTED;
			d->connected_at = time(0);
			d->player = player;

			/* Check to see if the player is currently running
			 * an @program. If so, drop the new descriptor into
			 * it.
			 */
			
			DESC_ITER_PLAYER(player, d2) {
				if (d2->program_data != NULL) {
					d->program_data = d2->program_data;
					break;
				}
			}
			
			/*
			 * Give the player the MOTD file and the settable * * 
			 * 
			 * * MOTD * message(s). Use raw notifies so the
			 * player * * * doesn't * try to match on the text. 
			 */

			if (Guest(player)) {
				fcache_dump(d, FC_CONN_GUEST);
			} else {
				buff = atr_get(player, A_LAST, &aowner, &aflags);
				if ((buff == NULL) || (*buff == '\0'))
					fcache_dump(d, FC_CREA_NEW);
				else
					fcache_dump(d, FC_MOTD);
				if (Wizard(player))
					fcache_dump(d, FC_WIZMOTD);
				free_lbuf(buff);
			}
			announce_connect(player, d);
			
			/* If stuck in an @prog, show the prompt */
			
			if (d->program_data != NULL)
				queue_string(d, ">\377\371");
				
		} else if (!(mudconf.control_flags & CF_LOGIN)) {
			failconn("CON", "Connect", "Logins Disabled", d,
				 R_GAMEDOWN, player, FC_CONN_DOWN,
			      mudconf.downmotd_msg, command, user, password,
				 cmdsave);
			return 0;
		} else {
			failconn("CON", "Connect", "Game Full", d,
				 R_GAMEFULL, player, FC_CONN_FULL,
			      mudconf.fullmotd_msg, command, user, password,
				 cmdsave);
			return 0;
		}
	} else if (!strncmp(command, "cr", 2)) {

		/*
		 * Enforce game down 
		 */

		if (!(mudconf.control_flags & CF_LOGIN)) {
			failconn("CRE", "Create", "Logins Disabled", d,
				 R_GAMEDOWN, NOTHING, FC_CONN_DOWN,
			      mudconf.downmotd_msg, command, user, password,
				 cmdsave);
			return 0;
		}
		/*
		 * Enforce max #players 
		 */

		if (mudconf.max_players < 0) {
			nplayers = mudconf.max_players;
		} else {
			nplayers = 0;
			DESC_ITER_CONN(d2)
				nplayers++;
		}
		if (nplayers > mudconf.max_players) {

			/*
			 * Too many players on, reject the attempt 
			 */

			failconn("CRE", "Create", "Game Full", d,
				 R_GAMEFULL, NOTHING, FC_CONN_FULL,
			      mudconf.fullmotd_msg, command, user, password,
				 cmdsave);
			return 0;
		}
		if (d->host_info & H_REGISTRATION) {
			fcache_dump(d, FC_CREA_REG);
		} else {
			player = create_player(user, password, NOTHING, 0, 0);
			if (player == NOTHING) {
				queue_string(d, create_fail);
				STARTLOG(LOG_SECURITY | LOG_PCREATES, "CON", "BAD")
					buff = alloc_mbuf("check_conn.LOG.badcrea");
				sprintf(buff,
					"[%d/%s] Create of '%s' failed",
					d->descriptor, d->addr, user);
				log_text(buff);
				free_mbuf(buff);
				ENDLOG
			} else {
				STARTLOG(LOG_LOGIN | LOG_PCREATES, "CON", "CREA")
					buff = alloc_mbuf("check_conn.LOG.create");
				sprintf(buff, "[%d/%s] Created ",
					d->descriptor, d->addr);
				log_text(buff);
				log_name(player);
				free_mbuf(buff);
				ENDLOG
					move_object(player, mudconf.start_room);
				d->flags |= DS_CONNECTED;
				d->connected_at = time(0);
				d->player = player;
				fcache_dump(d, FC_CREA_NEW);
				announce_connect(player, d);
			}
		}
	} else {
		welcome_user(d);
		STARTLOG(LOG_LOGIN | LOG_SECURITY, "CON", "BAD")
			buff = alloc_mbuf("check_conn.LOG.bad");
		msg[150] = '\0';
		sprintf(buff, "[%d/%s] Failed connect: '%s'",
			d->descriptor, d->addr, msg);
		log_text(buff);
		free_mbuf(buff);
		ENDLOG
	}
	free_lbuf(command);
	free_lbuf(user);
	free_lbuf(password);

	mudstate.debug_cmd = cmdsave;
	return 1;
}

int do_command(d, command, first)
DESC *d;
char *command;
int first;
{
	char *arg, *cmdsave;
	NAMETAB *cp;

	cmdsave = mudstate.debug_cmd;
	mudstate.debug_cmd = (char *)"< do_command >";
	d->last_time = mudstate.now;

	/*
	 * Split off the command from the arguments 
	 */

	arg = command;
	while (*arg && !isspace(*arg))
		arg++;
	if (*arg)
		*arg++ = '\0';

	/*
	 * Look up the command.  If we don't find it, turn it over to the * * 
	 * 
	 * *  * * normal logged-in command processor or to create/connect 
	 */

	if (!(d->flags & DS_CONNECTED)) {
		cp = (NAMETAB *) hashfind(command, &mudstate.logout_cmd_htab);
	} else
		cp = NULL;

#ifdef CONCENTRATE
	if (*arg)
		*--arg = ' ';	/*
				 * restore nullified space 
				 */
	if (!strncmp(command, "New Conn Pass: ",
		     sizeof("New Conn Pass ") - 1)) {
		do_becomeconc(d, command + sizeof("New Conn Pass: ") - 1);
		return 1;
	} else if (((d->cstatus & C_REMOTE) || (d->cstatus & C_CCONTROL)) && first) {
		if (!strncmp(command, "CONC ", sizeof("CONC ") - 1))
			log_text(command);
		else if (!strcmp(command, "New ID")) {
			do_makeid(d);
		} else if (!strncmp(command, "Conn ID: ",
				    sizeof("Conn ID: ") - 1)) {
			char *m, *n;

			m = command + sizeof("Conn ID: ") - 1;
			n = strchr(m, ' ');
			if (!n)
				queue_string(d, "Usage: Conn ID: <id> <hostname>\n");
			else
				do_connectid(d, atoi(command + sizeof("Conn ID: ") - 1), n + 1);
		} else if (!strncmp(command, "Kill ID: ",
				    sizeof("Kill ID: ") - 1))
			do_killid(d, atoi(command + sizeof("Kill ID: ") - 1));
		else {
			char *k;

			k = strchr(command, ' ');
			if (!k)
				return 1;
			else {
				struct descriptor_data *l;
				int j;

				*k = '\0';
				j = atoi(command);
				for (l = descriptor_list; l; l = l->next) {
					if (l->concid == j)
						break;
				}

				if (!l)
					queue_string(d, "I don't know that concid.\r\n");
				else {
					k++;
					if (!do_command(l, k, 0)) {
						return 0;
					}
				}
			}
		}

		return 1;
	}
	if (*arg)
		arg++;

#endif 
	/*
        * CONCENTRATE 
        */

	if (cp == NULL) {
		if (*arg)
			*--arg = ' ';	/*
					 * restore nullified space 
					 */
		if (d->flags & DS_CONNECTED) {
			d->command_count++;
			if (d->output_prefix) {
				queue_string(d, d->output_prefix);
				queue_write(d, "\r\n", 2);
			}
			mudstate.curr_player = d->player;
			mudstate.curr_enactor = d->player;
			process_command(d->player, d->player, 1,
					command, (char **)NULL, 0);
			if (d->output_suffix) {
				queue_string(d, d->output_suffix);
				queue_write(d, "\r\n", 2);
			}
			mudstate.debug_cmd = cmdsave;
			return 1;
		} else {
			mudstate.debug_cmd = cmdsave;
			return (check_connect(d, command));
		}
	}
	/*
	 * The command was in the logged-out command table.  Perform prefix * 
	 * 
	 * *  * *  * * and suffix processing, and invoke the command handler. 
	 */

	d->command_count++;
	if (!(cp->flag & CMD_NOxFIX)) {
		if (d->output_prefix) {
			queue_string(d, d->output_prefix);
			queue_write(d, "\r\n", 2);
		}
	}
	if ((!check_access(d->player, cp->perm)) ||
	    ((cp->perm & CA_PLAYER) && !(d->flags & DS_CONNECTED))) {
		queue_string(d, "Permission denied.\r\n");
	} else {
		mudstate.debug_cmd = cp->name;
		switch (cp->flag & CMD_MASK) {
		case CMD_QUIT:
			shutdownsock(d, R_QUIT);
			mudstate.debug_cmd = cmdsave;
			return 0;
		case CMD_LOGOUT:
			shutdownsock(d, R_LOGOUT);
			break;
		case CMD_WHO:
			dump_users(d, arg, CMD_WHO);
			break;
		case CMD_DOING:
			dump_users(d, arg, CMD_DOING);
			break;
		case CMD_SESSION:
			dump_users(d, arg, CMD_SESSION);
			break;
		case CMD_PREFIX:
			set_userstring(&d->output_prefix, arg);
			break;
		case CMD_SUFFIX:
			set_userstring(&d->output_suffix, arg);
			break;
		case CMD_PUEBLOCLIENT:
			/* Set the descriptor's flag */
			d->flags |= DS_PUEBLOCLIENT;
			/* If we're already connected, set the player's flag */
			if (d->player) {
				s_Html(d->player);
			}
			queue_string(d, mudconf.pueblo_msg);
			queue_string(d, "\r\n");
			break;
		default:
			STARTLOG(LOG_BUGS, "BUG", "PARSE")
				arg = alloc_lbuf("do_command.LOG");
			sprintf(arg,
				"Prefix command with no handler: '%s'",
				command);
			log_text(arg);
			free_lbuf(arg);
			ENDLOG
		}
	}
	if (!(cp->flag & CMD_NOxFIX)) {
		if (d->output_prefix) {
			queue_string(d, d->output_suffix);
			queue_write(d, "\r\n", 2);
		}
	}
	mudstate.debug_cmd = cmdsave;
	return 1;
}

void logged_out(player, cause, key, arg)
dbref player, cause;
int key;
char *arg;
{
	DESC *d;
	int idletime;

	DESC_ITER_PLAYER(player, d) {
		idletime = (mudstate.now - d->last_time);

		switch (key) {
		case CMD_QUIT:
			if (idletime == 0) {
				shutdownsock(d, R_QUIT);
				return;
			}
			break;
		case CMD_LOGOUT:
			if (idletime == 0) {
				shutdownsock(d, R_LOGOUT);
				return;
			}
			break;
		case CMD_WHO:
			if (idletime == 0) {
				dump_users(d, arg, CMD_WHO);
				return;
			}
			break;
		case CMD_DOING:
			if (idletime == 0) {
				dump_users(d, arg, CMD_DOING);
				return;
			}
			break;
		case CMD_SESSION:
			if (idletime == 0) {
				dump_users(d, arg, CMD_SESSION);
				return;
			}
			break;
		case CMD_PREFIX:
			if (idletime == 0) {
				set_userstring(&d->output_prefix, arg);
				return;
			}
			break;
		case CMD_SUFFIX:
			if (idletime == 0) {
				set_userstring(&d->output_suffix, arg);
				return;
			}
			break;
		case CMD_PUEBLOCLIENT:
			/* Set the descriptor's flag */
			d->flags |= DS_PUEBLOCLIENT;
			/* If we're already connected, set the player's flag */
			if (d->player) {
				s_Html(d->player);
			}
			queue_string(d, mudconf.pueblo_msg);
			queue_string(d, "\r\n");
			break;
		}
	}
}

void NDECL(process_commands)
{
	int nprocessed;
	DESC *d, *dnext;
	CBLK *t;
	char *cmdsave;

	cmdsave = mudstate.debug_cmd;
	mudstate.debug_cmd = (char *)"process_commands";

	do {
		nprocessed = 0;
		DESC_SAFEITER_ALL(d, dnext) {
			if (d->quota > 0 && (t = d->input_head)) {
				d->quota--;
				nprocessed++;
				d->input_head = (CBLK *) t->hdr.nxt;
				if (!d->input_head)
					d->input_tail = NULL;
				d->input_size -= (strlen(t->cmd) + 1);
				if (d->program_data != NULL)
					handle_prog(d, t->cmd);
				else
					do_command(d, t->cmd, 1);
				free_lbuf(t);
			}
		}
	} while (nprocessed > 0);
	mudstate.debug_cmd = cmdsave;
}

/*
 * ---------------------------------------------------------------------------
 * * site_check: Check for site flags in a site list.
 */

int site_check(host, site_list)
struct in_addr host;
SITE *site_list;
{
	SITE *this;

	for (this = site_list; this; this = this->next) {
		if ((host.s_addr & this->mask.s_addr) == this->address.s_addr)
			return this->flag;
	}
	return 0;
}

/*
 * --------------------------------------------------------------------------
 * * list_sites: Display information in a site list
 */

#define	S_SUSPECT	1
#define	S_ACCESS	2

static const char *stat_string(strtype, flag)
int strtype, flag;
{
	const char *str;

	switch (strtype) {
	case S_SUSPECT:
		if (flag)
			str = "Suspected";
		else
			str = "Trusted";
		break;
	case S_ACCESS:
		switch (flag) {
		case H_FORBIDDEN:
			str = "Forbidden";
			break;
		case H_REGISTRATION:
			str = "Registration";
			break;
		case 0:
			str = "Unrestricted";
			break;
		default:
			str = "Strange";
		}
		break;
	default:
		str = "Strange";
	}
	return str;
}

static void list_sites(player, site_list, header_txt, stat_type)
dbref player;
SITE *site_list;
const char *header_txt;
int stat_type;
{
	char *buff, *buff1, *str;
	SITE *this;

	buff = alloc_mbuf("list_sites.buff");
	buff1 = alloc_sbuf("list_sites.addr");
	sprintf(buff, "----- %s -----", header_txt);
	notify(player, buff);
	notify(player, "Address              Mask                 Status");
	for (this = site_list; this; this = this->next) {
		str = (char *)stat_string(stat_type, this->flag);
		StringCopy(buff1, inet_ntoa(this->mask));
		sprintf(buff, "%-20s %-20s %s",
			inet_ntoa(this->address), buff1, str);
		notify(player, buff);
	}
	free_mbuf(buff);
	free_sbuf(buff1);
}

/*
 * ---------------------------------------------------------------------------
 * * list_siteinfo: List information about specially-marked sites.
 */

void list_siteinfo(player)
dbref player;
{
	list_sites(player, mudstate.access_list, "Site Access",
		   S_ACCESS);
	list_sites(player, mudstate.suspect_list, "Suspected Sites",
		   S_SUSPECT);
}

/*
 * ---------------------------------------------------------------------------
 * * make_ulist: Make a list of connected user numbers for the LWHO function.
 */

void make_ulist(player, buff, bufc)
dbref player;
char *buff, **bufc;
{
	DESC *d;
	char *cp;

	cp = *bufc;
	DESC_ITER_CONN(d) {
		if (!WizRoy(player) && Hidden(d->player))
			continue;
		if (cp != *bufc)
			safe_chr(' ', buff, bufc);
		safe_chr('#', buff, bufc);
		safe_str(tprintf("%d", d->player), buff, bufc);
	}
}

/*
 * ---------------------------------------------------------------------------
 * * find_connected_name: Resolve a playername from the list of connected
 * * players using prefix matching.  We only return a match if the prefix
 * * was unique.
 */

dbref find_connected_name(player, name)
dbref player;
char *name;
{
	DESC *d;
	dbref found;

	found = NOTHING;
	DESC_ITER_CONN(d) {
		if (Good_obj(player) && !Wizard(player) && Hidden(d->player))
			continue;
		if (!string_prefix(Name(d->player), name))
			continue;
		if ((found != NOTHING) && (found != d->player))
			return NOTHING;
		found = d->player;
	}
	return found;
}
