
/*
 * comsys.c 
 */
/*
 * $Id: comsys.c,v 1.5 1998/06/03 17:18:38 dpassmor Exp $ 
 */

#include <ctype.h>
#include <sys/types.h>

#include "copyright.h"
#include "autoconf.h"
#include "db.h"
#include "interface.h"
#include "attrs.h"
#include "match.h"
#include "config.h"
#include "externs.h"
#include "flags.h"
#include "powers.h"

#include "comsys.h"

char *load_comsys(filename)
char *filename;
{
	FILE *fp;
	int i;
	char buffer[200];

	for (i = 0; i < NUM_COMSYS; i++)
		comsys_table[i] = NULL;

	if (!(fp = fopen(filename, "r"))) {
		fprintf(stderr, "Error: Couldn't find %s.\n", filename);
	} else {
		fprintf(stderr, "LOADING: %s\n", filename);
		if (fscanf(fp, "*** Begin %s ***\n", buffer) == 1 &&
		    !strcmp(buffer, "COMMAC")) {
			load_old_channels(fp);
		} else if (!strcmp(buffer, "CHANNELS")) {
			load_channels(fp);
		} else {
			fprintf(stderr, "Error: Couldn't find Begin CHANNELS in %s.", filename);
			return;
		}

		if (fscanf(fp, "*** Begin %s ***\n", buffer) == 1 &&
		    !strcmp(buffer, "COMSYS")) {
			load_comsystem(fp);
		} else {
			fprintf(stderr, "Error: Couldn't find Begin COMSYS in %s.", filename);
			return;
		}

		fclose(fp);
		fprintf(stderr, "LOADING: %s (done)\n", filename);
	}
}

char *save_comsys(filename)
char *filename;
{
	FILE *fp;
	char buffer[500];

	sprintf(buffer, "%s.#", filename);
	if (!(fp = fopen(buffer, "w"))) {
		fprintf(stderr, "Unable to open %s for writing.\n", buffer);
		return;
	}
	fprintf(fp, "*** Begin CHANNELS ***\n");
	save_channels(fp);

	fprintf(fp, "*** Begin COMSYS ***\n");
	save_comsystem(fp);

	fclose(fp);
	rename(buffer, filename);
}

char *load_channels(fp)
FILE *fp;
{
	int i, j;
	char buffer[100];
	int np;
	struct comsys *c;
	char *t;
	char in;

	fscanf(fp, "%d\n", &np);
	for (i = 0; i < np; i++) {
		c = create_new_comsys();
		fscanf(fp, "%d %d\n",
		       &(c->who), &(c->numchannels));
		c->maxchannels = c->numchannels;
		if (c->maxchannels > 0) {
			c->alias = (char *)malloc(c->maxchannels * 6);
			c->channels = (char **)malloc(sizeof(char *) * c->maxchannels);

			for (j = 0; j < c->numchannels; j++) {
				t = c->alias + j * 6;
				while ((in = fgetc(fp)) != ' ')
					*t++ = in;
				*t = 0;

				fscanf(fp, "%[^\n]\n", buffer);

				c->channels[j] = (char *)malloc(strlen(buffer) + 1);
				StringCopy(c->channels[j], buffer);
			}
			sort_com_aliases(c);
		} else {
			c->alias = NULL;
			c->channels = NULL;
		}
		if ((Typeof(c->who) == TYPE_PLAYER) || (!God(Owner(c->who))) ||
		    ((!Going(c->who))))
			add_comsys(c);
		purge_comsystem();
	}
}

char *load_old_channels(fp)
FILE *fp;
{
	int i, j, k;
	char buffer[100];
	int np;
	struct comsys *c;
	char *t;
	char in;

	fscanf(fp, "%d\n", &np);
	for (i = 0; i < np; i++) {
		c = create_new_comsys();
		/* Trash the old values! */
		fscanf(fp, "%d %d %d %d %d %d %d %d\n",
		       &(c->who), &(c->numchannels), &k, &k, &k, &k, &k, &k);
		c->maxchannels = c->numchannels;
		if (c->maxchannels > 0) {
			c->alias = (char *)malloc(c->maxchannels * 6);
			c->channels = (char **)malloc(sizeof(char *) * c->maxchannels);

			for (j = 0; j < c->numchannels; j++) {
				t = c->alias + j * 6;
				while ((in = fgetc(fp)) != ' ')
					*t++ = in;
				*t = 0;

				fscanf(fp, "%[^\n]\n", buffer);

				c->channels[j] = (char *)malloc(strlen(buffer) + 1);
				StringCopy(c->channels[j], buffer);
			}
			sort_com_aliases(c);
		} else {
			c->alias = NULL;
			c->channels = NULL;
		}
		if ((Typeof(c->who) == TYPE_PLAYER) || (!God(Owner(c->who))) ||
		    ((!Going(c->who))))
			add_comsys(c);
		purge_comsystem();
	}
}

char *purge_comsystem()
{
	struct comsys *c;
	struct comsys *d;
	int i;

#ifdef ABORT_PURGE_COMSYS
	return;
#endif /*
        * * ABORT_PURGE_COMSYS  
        */

	for (i = 0; i < NUM_COMSYS; i++) {
		c = comsys_table[i];
		while (c) {
			d = c;
			c = c->next;
			if (d->numchannels == 0) {
				del_comsys(d->who);
				continue;
			}
/*
 * if ((Typeof(d->who) != TYPE_PLAYER) && (God(Owner(d->who))) &&
 * * (Going(d->who))) 
 */
			if (Typeof(d->who) == TYPE_PLAYER)
				continue;
			if (God(Owner(d->who)) && Going(d->who)) {
				del_comsys(d->who);
				continue;
			}
		}
	}
}

char *save_channels(fp)
FILE *fp;
{
	int np;
	struct comsys *c;
	int i, j;

	purge_comsystem();
	np = 0;
	for (i = 0; i < NUM_COMSYS; i++) {
		c = comsys_table[i];
		while (c) {
			np++;
			c = c->next;
		}
	}

	fprintf(fp, "%d\n", np);
	for (i = 0; i < NUM_COMSYS; i++) {
		c = comsys_table[i];
		while (c) {
			fprintf(fp, "%d %d\n", c->who, c->numchannels);
			for (j = 0; j < c->numchannels; j++) {
				fprintf(fp, "%s %s\n", c->alias + j * 6, c->channels[j]);
			}
			c = c->next;
		}
	}
}

struct comsys *create_new_comsys()
{
	struct comsys *c;
	int i;

	c = (struct comsys *)malloc(sizeof(struct comsys));

	c->who = -1;
	c->numchannels = 0;
	c->maxchannels = 0;
	c->alias = NULL;
	c->channels = NULL;

	c->next = NULL;
	return c;
}

struct comsys *get_comsys(which)
dbref which;
{
	struct comsys *c;

	if (which < 0)
		return NULL;

	c = comsys_table[which % NUM_COMSYS];

	while (c && (c->who != which))
		c = c->next;

	if (!c) {
		c = create_new_comsys();
		c->who = which;
		add_comsys(c);
	}
	return c;
}

char *add_comsys(c)
struct comsys *c;
{
	if (c->who < 0)
		return;

	c->next = comsys_table[c->who % NUM_COMSYS];
	comsys_table[c->who % NUM_COMSYS] = c;
}

char *del_comsys(who)
dbref who;
{
	struct comsys *c;
	struct comsys *last;

	if (who < 0)
		return;

	c = comsys_table[who % NUM_COMSYS];

	if (c == NULL)
		return;

	if (c->who == who) {
		comsys_table[who % NUM_COMSYS] = c->next;
		destroy_comsys(c);
		return;
	}
	last = c;
	c = c->next;
	while (c) {
		if (c->who == who) {
			last->next = c->next;
			destroy_comsys(c);
			return;
		}
		last = c;
		c = c->next;
	}
}

char *destroy_comsys(c)
struct comsys *c;
{
	int i;

	free(c->alias);
	for (i = 0; i < c->numchannels; i++)
		free(c->channels[i]);
	free(c->channels);
	free(c);
}

char *sort_com_aliases(c)
struct comsys *c;
{
	int i;
	int cont;
	char buffer[10];
	char *s;

	cont = 1;
	while (cont) {
		cont = 0;
		for (i = 0; i < c->numchannels - 1; i++)
			if (strcasecmp(c->alias + i * 6, c->alias + (i + 1) * 6) > 0) {
				StringCopy(buffer, c->alias + i * 6);
				StringCopy(c->alias + i * 6, c->alias + (i + 1) * 6);
				StringCopy(c->alias + (i + 1) * 6, buffer);
				s = c->channels[i];
				c->channels[i] = c->channels[i + 1];
				c->channels[i + 1] = s;
				cont = 1;
			}
	}
}

/*
 * This is the hash table for channel names 
 */

void NDECL(init_chantab)
{
	hashinit(&mudstate.channel_htab, 30 * HASH_FACTOR);
}

char *get_channel_from_alias(player, alias)
dbref player;
char *alias;
{
	struct comsys *c;
	int first, last, current;
	int dir;

	c = get_comsys(player);

	first = 0;
	last = c->numchannels - 1;
	dir = 1;

	while (dir && (first <= last)) {
		current = (first + last) / 2;
		dir = strcmp(alias, c->alias + 6 * current);
		if (dir < 0)
			last = current - 1;
		else
			first = current + 1;
	}

	if (!dir)
		return c->channels[current];
	else
		return "";
}

char *load_comsystem(fp)
FILE *fp;
{
	int i, j, dummy;
	int nc, new = 0;
	struct channel *ch;
	struct comuser *user;
	char temp[1000];
	char buf[8];

	num_channels = 0;


	fgets(buf, sizeof(buf), fp);
	if (!strncmp(buf, "+V1", 3)) {
		new = 1;
		fscanf(fp, "%d\n", &nc);
	} else {
		nc = atoi(buf);
	}

	num_channels = nc;

	for (i = 0; i < nc; i++) {
		ch = (struct channel *)malloc(sizeof(struct channel));

		fscanf(fp, "%[^\n]\n", temp);

		StringCopy(ch->name, temp);
		ch->on_users = NULL;

		hashadd(ch->name, (int *)ch, &mudstate.channel_htab);

		if (new) {
			fscanf(fp, "%d %d %d %d %d %d %d %d\n",
			       &(ch->type), &(ch->temp1), &(ch->temp2),
			       &(ch->charge), &(ch->charge_who),
			       &(ch->amount_col), &(ch->num_messages), &(ch->chan_obj));
		} else {
			fscanf(fp, "%d %d %d %d %d %d %d %d %d %d\n",
			  &(ch->type), &(dummy), &(ch->temp1), &(ch->temp2),
			       &(dummy), &(ch->charge), &(ch->charge_who),
			       &(ch->amount_col), &(ch->num_messages), &(ch->chan_obj));
		}

		fscanf(fp, "%d\n", &(ch->num_users));
		ch->max_users = ch->num_users;
		if (ch->num_users > 0) {
			ch->users = (struct comuser **)
				calloc(ch->max_users, sizeof(struct comuser *));

			for (j = 0; j < ch->num_users; j++) {
				user = (struct comuser *)malloc(sizeof(struct comuser));

				ch->users[j] = user;

				if (new) {
					fscanf(fp, "%d %d\n", &(user->who), &(user->on));
				} else {
					fscanf(fp, "%d %d %d", &(user->who),
					       &(dummy), &(dummy));
					fscanf(fp, "%d\n", &(user->on));
				}

				fscanf(fp, "%[^\n]\n", temp);

				if (strlen(temp + 2) > 0) {
					user->title = (char *)malloc(strlen(temp + 2) + 1);
					StringCopy(user->title, temp + 2);
				} else {
					user->title = (char *)malloc(1);
					user->title[0] = 0;
				}
				if (!(isPlayer(user->who)) && !(Going(user->who) &&
						 (God(Owner(user->who))))) {
					do_joinchannel(user->who, ch);
					user->on_next = ch->on_users;
					ch->on_users = user;
				} else {
					user->on_next = ch->on_users;
					ch->on_users = user;
				}
			}
			sort_users(ch);
		} else
			ch->users = NULL;
	}
}

char *save_comsystem(fp)
FILE *fp;
{
	struct channel *ch;
	struct comuser *user;
	int i, j;

	fprintf(fp, "+V1\n");
	fprintf(fp, "%d\n", num_channels);
	for (ch = (struct channel *)hash_firstentry(&mudstate.channel_htab);
	ch; ch = (struct channel *)hash_nextentry(&mudstate.channel_htab)) {
		fprintf(fp, "%s\n", ch->name);

		fprintf(fp, "%d %d %d %d %d %d %d %d\n",
			ch->type, ch->temp1, ch->temp2,
			ch->charge, ch->charge_who,
			ch->amount_col, ch->num_messages, ch->chan_obj);

		fprintf(fp, "%d\n", ch->num_users);
		for (j = 0; j < ch->num_users; j++) {
			user = ch->users[j];
			fprintf(fp, "%d %d\n", user->who, user->on);
			if (strlen(user->title))
				fprintf(fp, "t:%s\n", user->title);
			else
				fprintf(fp, "t:\n");
		}
	}
}

char *do_processcom(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
	char *mess, *bp;
	struct channel *ch;
	struct comuser *user;

	if ((strlen(arg1) + strlen(arg2)) > 3500) {
		arg2[3500] = '\0';
	}
	if (!*arg2) {
		raw_notify(player, "No message.");
		return;
	}
	if (!(ch = select_channel(arg1))) {
		raw_notify(player, tprintf("Unknown channel %s.", arg1));
		return;
	}
	if (!(user = select_user(ch, player))) {
		raw_notify(player,
			   "You are not listed as on that channel.  Delete this alias and readd.");
		return;
	}
	if (!strcmp(arg2, "on")) {
		do_joinchannel(player, ch);
	} else if (!strcmp(arg2, "off")) {
		do_leavechannel(player, ch);
	} else if (!user->on) {
		raw_notify(player, tprintf("You must be on %s to do that.", arg1));
		return;
	} else if (!strcmp(arg2, "who")) {
		do_comwho(player, ch);
	} else if (!(do_test_access(player, CHANNEL_TRANSMIT, ch))) {
		raw_notify(player, "That channel type cannot be transmitted on.");
		return;
	} else {
		if (!payfor(player, Guest(player) ? 0 : ch->charge)) {
			notify(player, tprintf("You don't have enough %s.",
					       mudconf.many_coins));
			return;
		} else {
			ch->amount_col += ch->charge;
			giveto(ch->charge_who, ch->charge);
		}

		bp = mess = alloc_lbuf("do_processcom");

		if ((*arg2) == ':') {
			if (strlen(user->title))
				safe_tprintf_str(mess, &bp, "[%s] %s %s %s", arg1, user->title, Name(player), arg2 + 1);
			else
				safe_tprintf_str(mess, &bp, "[%s] %s %s", arg1, Name(player), arg2 + 1);
		} else if ((*arg2) == ';') {
			if (strlen(user->title))
				safe_tprintf_str(mess, &bp, "[%s] %s %s%s", arg1, user->title, Name(player), arg2 + 1);
			else
				safe_tprintf_str(mess, &bp, "[%s] %s%s", arg1, Name(player), arg2 + 1);
		} else if (strlen(user->title))
			safe_tprintf_str(mess, &bp, 
				"[%s] %s %s says, \"%s\"", arg1, user->title, Name(player), arg2);
		else
			safe_tprintf_str(mess, &bp, "[%s] %s says, \"%s\"", arg1, Name(player), arg2);

		do_comsend(ch, mess);
		free_lbuf(mess);
	}
}

char *do_comsend(ch, mess)
struct channel *ch;
char *mess;
{
	struct comuser *user;

	ch->num_messages++;
	for (user = ch->on_users; user; user = user->on_next) {
		if (user->on)
			if (do_test_access(user->who, CHANNEL_RECIEVE, ch)) {
				if ((Typeof(user->who) == TYPE_PLAYER) && 
				    Connected(user->who)) 
					raw_notify(user->who, mess);
				else
					notify(user->who, mess);
			}
	}
}

char *do_joinchannel(player, ch)
dbref player;
struct channel *ch;
{
	char temp[1000];
	struct comuser *user;
	struct comuser **cu;
	int i;

	user = select_user(ch, player);

	if (!user) {
		ch->num_users++;
		if (ch->num_users >= ch->max_users) {
			ch->max_users += 10;
			cu = (struct comuser **)
				malloc(sizeof(struct comuser *) * ch->max_users);

			for (i = 0; i < (ch->num_users - 1); i++)
				cu[i] = ch->users[i];
			free(ch->users);
			ch->users = cu;
		}
		user = (struct comuser *)malloc(sizeof(struct comuser));

		for (i = ch->num_users - 1; i > 0 && ch->users[i - 1]->who > player; i--)
			ch->users[i] = ch->users[i - 1];
		ch->users[i] = user;

		user->who = player;
		user->on = 1;
		user->title = (char *)malloc(1);
		user->title[0] = 0;

/*
 * if (Connected(player))&&(isPlayer(player)) 
 */
		if UNDEAD
			(player) {
			user->on_next = ch->on_users;
			ch->on_users = user;
			}
	} else if (!user->on) {
		user->on = 1;
	} else {
		raw_notify(player, tprintf("You are already on channel %s.", ch->name));
		return;
	}

	if (!Dark(player)) {
		sprintf(temp, "[%s] %s has joined this channel.", ch->name,
			Name(player));
		do_comsend(ch, temp);
	}
}

char *do_leavechannel(player, ch)
dbref player;
struct channel *ch;
{
	struct comuser *user;
	char temp[1000];

	user = select_user(ch, player);

	raw_notify(player, tprintf("You have left channel %s.", ch->name));
	
	if ((user->on) && (!Dark(player)))
	{ 
		sprintf(temp, "[%s] %s has left this channel.", ch->name,
			Name(player));
		do_comsend(ch, temp);
	}
	user->on = 0;
}

char *do_comwho(player, ch)
dbref player;
struct channel *ch;
{
	struct comuser *user;
	char *buff;

	raw_notify(player, "-- Players --");
	for (user = ch->on_users; user; user = user->on_next) {
		if (Typeof(user->who) == TYPE_PLAYER)
			if (Connected(user->who) && (!Dark(user->who) || Wizard_Who(player))) {
				if (user->on) {
					buff = unparse_object(player, user->who, 0);
					raw_notify(player, tprintf("%s", buff));
					free_lbuf(buff);
				}
			} else if (!Dark(user->who))
				do_comdisconnectchannel(user->who, ch->name);
	}
	raw_notify(player, "-- Objects --");
	for (user = ch->on_users; user; user = user->on_next) {
		if (Typeof(user->who) != TYPE_PLAYER)
			if ((Going(user->who)) &&
			    (God(Owner(user->who))))
				do_comdisconnectchannel(user->who, ch->name);
			else if (user->on) {
				buff = unparse_object(player, user->who, 0);
				raw_notify(player, tprintf("%s", buff));
				free_lbuf(buff);
			}
	}
	raw_notify(player, tprintf("-- %s --", ch->name));
}

struct channel *select_channel(channel)
char *channel;
{
	struct channel *cp;

	cp = (struct channel *)hashfind(channel, &mudstate.channel_htab);
	if (!cp)
		return NULL;
	else
		return cp;
}

struct comuser *select_user(ch, player)
struct channel *ch;
dbref player;
{
	int first, last, current;
	int dir;

	if (!ch)
		return NULL;

	first = 0;
	last = ch->num_users - 1;
	dir = 1;
	current = (first + last) / 2;

	while (dir && (first <= last)) {
		current = (first + last) / 2;
		if (ch->users[current] == NULL) {
			last--;
			continue;
		}
		if (ch->users[current]->who == player)
			dir = 0;
		else if (ch->users[current]->who < player) {
			dir = 1;
			first = current + 1;
		} else {
			dir = -1;
			last = current - 1;
		}
	}

	if (!dir)
		return ch->users[current];
	else
		return NULL;
}

void do_addcom(player, cause, key, arg1, arg2)
dbref player, cause;
int key;
char *arg1;
char *arg2;
{
	char channel[MBUF_SIZE];
	char title[MBUF_SIZE];
	char buffer[MBUF_SIZE];
	struct channel *ch;
	char *s, *t;
	int i, j, where;
	struct comsys *c;
	char *na;
	char **nc;

	if (!mudconf.have_comsys) {
		raw_notify(player, "Comsys disabled.");
		return;
	}
	if (!*arg1) {
		raw_notify(player, "You need to specify an alias.");
		return;
	}
	s = arg2;
	if (!*s) {
		raw_notify(player, "You need to specify a channel.");
		return;
	}
	t = channel;
	while (*s && (*s != ',') && ((t - channel) < MBUF_SIZE)) {
		if (*s != ' ')
			*t++ = *s++;
		else
			s++;
	}
	*t = '\0';

	t = title;
	*t = '\0';
	if (*s) {		/*
				 * Read title 
				 */
		s++;
		while (*s && ((t - title) < MBUF_SIZE))
			*t++ = *s++;
		*t = '\0';
	}
	if (!(ch = select_channel(channel))) {
		raw_notify(player, tprintf("Channel %s does not exist yet.", channel));
		return;
	}
	if (!(do_test_access(player, CHANNEL_JOIN, ch))) {
		raw_notify(player, "Sorry, this channel type does not allow you to join.");
		return;
	}
	if (select_user(ch, player)) {
		raw_notify(player, tprintf("Warning: You are already on that channel."));
	}
	c = get_comsys(player);
	for (j = 0; j < c->numchannels && (strcmp(arg1, c->alias + j * 6) > 0); j++) ;
	if (j < c->numchannels && !strcmp(arg1, c->alias + j * 6)) {
		sprintf(buffer, "That alias is already in use for channel %s.",
			c->channels[j]);
		raw_notify(player, buffer);
		return;
	}
	if (c->numchannels >= c->maxchannels) {
		c->maxchannels += 10;

		na = (char *)malloc(6 * c->maxchannels);
		nc = (char **)malloc(sizeof(char *) * c->maxchannels);

		for (i = 0; i < c->numchannels; i++) {
			StringCopy(na + i * 6, c->alias + i * 6);
			nc[i] = c->channels[i];
		}
		free(c->alias);
		free(c->channels);
		c->alias = na;
		c->channels = nc;
	}
	where = c->numchannels++;
	for (i = where; i > j; i--) {
		StringCopy(c->alias + i * 6, c->alias + (i - 1) * 6);
		c->channels[i] = c->channels[i - 1];
	}

	where = j;
	StringCopyTrunc(c->alias + where * 6, arg1, 5);
	*(c->alias + where * 6 + 5) = '\0';
	c->channels[where] = (char *)malloc(strlen(channel) + 1);
	StringCopy(c->channels[where], channel);

	do_joinchannel(player, ch);
	do_setnewtitle(player, ch, title);

	if (title[0])
		raw_notify(player, tprintf(
						  "Channel %s added with alias %s and title %s.", channel, arg1, title));
	else
		raw_notify(player, tprintf(
			 "Channel %s added with alias %s.", channel, arg1));
}

char *do_setnewtitle(player, ch, title)
dbref player;
struct channel *ch;
char *title;
{
	struct comuser *user;
	char *new;
	
	user = select_user(ch, player);

	/* Make sure there can be no embedded newlines from %r */
	
	new = replace_string("\r\n", "", title);
	
	if (ch && user) {
		if (user->title)
			free(user->title);
		if (strlen(new) > 0) {
			user->title = (char *)malloc(strlen(new) + 1);
			StringCopy(user->title, new);
		} else {
			user->title = (char *)malloc(1);
			user->title[0] = 0;
		}
	}
	free_lbuf(new);
}

void do_delcom(player, cause, key, arg1)
dbref player, cause;
int key;
char *arg1;
{
	int i;
	struct comsys *c;

	if (!mudconf.have_comsys) {
		raw_notify(player, "Comsys disabled.");
		return;
	}
	if (!arg1) {
		raw_notify(player, "Need an alias to delete.");
		return;
	}
	c = get_comsys(player);

	for (i = 0; i < c->numchannels; i++) {
		if (!strcmp(arg1, c->alias + i * 6)) {
			do_delcomchannel(player, c->channels[i]);
			raw_notify(player, tprintf("Channel %s deleted.", c->channels[i]));
			free(c->channels[i]);

			c->numchannels--;
			for (; i < c->numchannels; i++) {
				StringCopy(c->alias + i * 6, c->alias + i * 6 + 6);
				c->channels[i] = c->channels[i + 1];
			}
			return;
		}
	}
	raw_notify(player, "Unable to find that alias.");
}

char *do_delcomchannel(player, channel)
dbref player;
char *channel;
{
	struct channel *ch;
	struct comuser *user;
	char temp[1000];
	int i;
	int j;

	if (!(ch = select_channel(channel))) {
		raw_notify(player, tprintf("Unknown channel %s.", channel));
	} else {
		j = 0;
		for (i = 0; i < ch->num_users && !j; i++) {
			user = ch->users[i];
			if (user->who == player) {
				do_comdisconnectchannel(player, channel);
				if (user->on) {
					sprintf(temp, "[%s] %s has left this channel.",
						channel, Name(player));
					do_comsend(ch, temp);
				}
				raw_notify(player, tprintf("You have left channel %s.", channel));

				if (user->title)
					free(user->title);
				free(user);
				j = 1;
			}
		}

		if (j) {
			ch->num_users--;
			for (i--; i < ch->num_users; i++)
				ch->users[i] = ch->users[i + 1];
		}
	}
}

void do_createchannel(player, cause, key, channel)
dbref player, cause;
int key;
char *channel;
{
	struct channel *newchannel;
	struct channel **nc;
	int i;


	if (select_channel(channel)) {
		raw_notify(player, tprintf("Channel %s already exists.", channel));
		return;
	}
	if (!*channel) {
		raw_notify(player, "You must specify a channel to create.");
		return;
	}
	if (!(Comm_All(player))) {
		raw_notify(player, "You do not have permission to do that.");
		return;
	}
	newchannel = (struct channel *)malloc(sizeof(struct channel));

	StringCopyTrunc(newchannel->name, channel, CHAN_NAME_LEN - 1);
	newchannel->name[CHAN_NAME_LEN - 1] = '\0';
	newchannel->type = 127;
	newchannel->temp1 = 0;
	newchannel->temp2 = 0;
	newchannel->charge = 0;
	newchannel->charge_who = player;
	newchannel->amount_col = 0;
	newchannel->num_users = 0;
	newchannel->max_users = 0;
	newchannel->users = NULL;
	newchannel->on_users = NULL;
	newchannel->chan_obj = NOTHING;
	newchannel->num_messages = 0;

	num_channels++;

	hashadd(newchannel->name, (int *)newchannel, &mudstate.channel_htab);

	raw_notify(player, tprintf("Channel %s created.", channel));
}

void do_destroychannel(player, cause, key, channel)
dbref player, cause;
int key;
char *channel;
{
	struct channel *ch;
	int j;

	if (!mudconf.have_comsys) {
		raw_notify(player, "Comsys disabled.");
		return;
	}
	ch = (struct channel *)hashfind(channel, &mudstate.channel_htab);

	if (!ch) {
		raw_notify(player, tprintf("Could not find channel %s.", channel));
		return;
	} else if (!(Comm_All(player)) && (player != ch->charge_who)) {
		raw_notify(player, "You do not have permission to do that. ");
		return;
	}
	num_channels--;
	hashdelete(channel, &mudstate.channel_htab);

	for (j = 0; j < ch->num_users; j++) {
		free(ch->users[j]);
	}
	free(ch->users);
	free(ch);
	raw_notify(player, tprintf("Channel %s destroyed.", channel));
}

char *do_listchannels(player)
dbref player;
{
	struct channel *ch;
	char temp[1000];
	int i, perm;

	if (!(perm = Comm_All(player))) {
		raw_notify(player, "Warning: Only public channels and your channels will be shown.");
	}
	raw_notify(player,
		   "** Channel      --Flags--  Obj   Own   Charge  Balance  Users   Messages");

	for (ch = (struct channel *)hash_firstentry(&mudstate.channel_htab);
	  ch; ch = (struct channel *)hash_nextentry(&mudstate.channel_htab))
		if (perm || (ch->type & CHANNEL_PUBLIC) || ch->charge_who == player) {

			sprintf(temp, "%c%c %-13.13s %c%c%c/%c%c%c %5d %5d %8d %8d %6d %10d",
				(ch->type & (CHANNEL_PUBLIC)) ? 'P' : '-',
				(ch->type & (CHANNEL_LOUD)) ? 'L' : '-',
				ch->name,
				(ch->type & (CHANNEL_PL_MULT * CHANNEL_JOIN)) ? 'J' : '-',
				(ch->type & (CHANNEL_PL_MULT * CHANNEL_TRANSMIT)) ? 'X' : '-',
				(ch->type & (CHANNEL_PL_MULT * CHANNEL_RECIEVE)) ? 'R' : '-',
				(ch->type & (CHANNEL_OBJ_MULT * CHANNEL_JOIN)) ? 'j' : '-',
				(ch->type & (CHANNEL_OBJ_MULT * CHANNEL_TRANSMIT)) ? 'x' : '-',
				(ch->type & (CHANNEL_OBJ_MULT * CHANNEL_RECIEVE)) ? 'r' : '-',
			      (ch->chan_obj != NOTHING) ? ch->chan_obj : -1,
				ch->charge_who, ch->charge, ch->amount_col, ch->num_users, ch->num_messages);
			raw_notify(player, temp);
		}
	raw_notify(player, "-- End of list of Channels --");
}

void do_comtitle(player, cause, key, arg1, arg2)
dbref player, cause;
int key;
char *arg1;
char *arg2;
{
	struct channel *ch;
	char channel[50];

	if (!mudconf.have_comsys) {
		raw_notify(player, "Comsys disabled.");
		return;
	}
	if (!*arg1) {
		raw_notify(player, "Need an alias to do comtitle.");
		return;
	}
	StringCopy(channel, get_channel_from_alias(player, arg1));

	if (strlen(channel) == 0) {
		raw_notify(player, "Unknown alias");
		return;
	}
	if ((ch = select_channel(channel)) && select_user(ch, player)) {
		raw_notify(player, tprintf("Title set to '%s' on channel %s.", arg2, channel));
		do_setnewtitle(player, ch, arg2);
	}
	if (!ch) {
		raw_notify(player, "Illegal comsys alias, please delete.");
		return;
	}
}

void do_comlist(player, cause, key)
dbref player, cause;
int key;
{
	struct comuser *user;
	struct comsys *c;
	int i;
	char temp[200];

	if (!mudconf.have_comsys) {
		raw_notify(player, "Comsys disabled.");
		return;
	}
	c = get_comsys(player);

	raw_notify(player,
		   "Alias     Channel             Title                                   Status");

	for (i = 0; i < c->numchannels; i++) {
		if ((user = select_user(select_channel(c->channels[i]), player))) {
			sprintf(temp, "%-9.9s %-19.19s %-39.39s %s", c->alias + i * 6,
				c->channels[i], user->title, (user->on ? "on" : "off"));
			raw_notify(player, temp);
		} else {
			raw_notify(player,
			     tprintf("Bad Comsys Alias: %s for Channel: %s",
				     c->alias + i * 6, c->channels[i]));
		}
	}
	raw_notify(player, "-- End of comlist --");
}

char *do_channelnuke(player)
dbref player;
{
	struct channel *ch;
	int i, j;

	for (ch = (struct channel *)hash_firstentry(&mudstate.channel_htab);
	ch; ch = (struct channel *)hash_nextentry(&mudstate.channel_htab)) {
		if (ch->charge_who == player) {
			num_channels--;
			hashdelete(ch->name, &mudstate.channel_htab);

			for (j = 0; j < ch->num_users; j++)
				free(ch->users[j]);
			free(ch->users);
			free(ch);
		}
	}
}

void do_clearcom(player, cause, key)
dbref player, cause;
int key;
{
	int i;
	struct comsys *c;

	if (!mudconf.have_comsys) {
		raw_notify(player, "Comsys disabled.");
		return;
	}
	c = get_comsys(player);

	for (i = (c->numchannels) - 1; i > -1; --i) {
		do_delcom(player, player, 0, c->alias + i * 6);
	}
}

void do_allcom(player, cause, key, arg1)
dbref player, cause;
int key;
char *arg1;
{
	int i;
	struct comsys *c;
	char temparg[50];;

	if (!mudconf.have_comsys) {
		raw_notify(player, "Comsys disabled.");
		return;
	}
	c = get_comsys(player);

	if ((strcmp(arg1, "who") != 0) &&
	    (strcmp(arg1, "on") != 0) &&
	    (strcmp(arg1, "off") != 0)) {
		raw_notify(player, "Only options available are: on, off and who.");
		return;
	}
	for (i = 0; i < c->numchannels; i++) {
		do_processcom(player, c->channels[i], arg1);
		if (strcmp(arg1, "who") == 0)
			raw_notify(player, "");
	}

}

char *sort_users(ch)
struct channel *ch;
{
	int i;
	int nu;
	int done;
	struct comuser *user;

	nu = ch->num_users;
	done = 0;
	while (!done) {
		done = 1;
		for (i = 0; i < (nu - 1); i++) {
			if (ch->users[i]->who > ch->users[i + 1]->who) {
				user = ch->users[i];
				ch->users[i] = ch->users[i + 1];
				ch->users[i + 1] = user;
				done = 0;
			}
		}
	}
}

void do_channelwho(player, cause, key, arg1)
dbref player, cause;
int key;
char *arg1;
{
	struct channel *ch;
	struct comuser *user;
	char channel[100];
	int flag;
	char *s;
	char *t;
	char *buff;
	char temp[100];
	int i;

	if (!mudconf.have_comsys) {
		raw_notify(player, "Comsys disabled.");
		return;
	}
	s = arg1;
	t = channel;
	while (*s && *s != '/')
		*t++ = *s++;
	*t = 0;

	flag = 0;
	if (*s && *(s + 1))
		flag = (*(s + 1) == 'a');

	if (!(ch = select_channel(channel))) {
		raw_notify(player, tprintf("Unknown channel %s.", channel));
		return;
	}
	if (!((Comm_All(player)) || (player == ch->charge_who))) {
		raw_notify(player,
			   "You do not have permission to do that. (Not owner or admin.)");
		return;
	}
	raw_notify(player, tprintf("-- %s --", ch->name));
	raw_notify(player, tprintf("%-29.29s %-6.6s %-6.6s",
				   "Name", "Status", "Player"));
	for (i = 0; i < ch->num_users; i++) {
		user = ch->users[i];
		if ((flag || UNDEAD(user->who)) && (!Dark(user->who) || Wizard_Who(player))) {
			buff = unparse_object(player, user->who, 0);
			sprintf(temp, "%-29.29s %-6.6s %-6.6s",
			     strip_ansi(buff), ((user->on) ? "on " : "off"),
			(Typeof(user->who) == TYPE_PLAYER) ? "yes" : "no ");
			raw_notify(player, temp);
			free_lbuf(buff);
		}
	}
	raw_notify(player, tprintf("-- %s --", ch->name));
}

char *do_comdisconnectraw_notify(player, chan)
dbref player;
char *chan;
{
	struct channel *ch;
	struct comuser *cu;
	char *buff;

	if (!(ch = select_channel(chan)))
		return;
	if (!(cu = select_user(ch, player)))
		return;

	if ((ch->type & CHANNEL_LOUD) && (cu->on) && (!Dark(player))) {
		buff = alloc_lbuf("do_comconnect");
		sprintf(buff, "[%s] %s has disconnected.", ch->name, Name(player));
		do_comsend(ch, buff);
		free_lbuf(buff);
	}
}

char *do_comconnectraw_notify(player, chan)
dbref player;
char *chan;
{
	struct channel *ch;
	struct comuser *cu;
	char *buff;

	if (!(ch = select_channel(chan)))
		return;
	if (!(cu = select_user(ch, player)))
		return;

	if ((ch->type & CHANNEL_LOUD) && (cu->on) && (!Dark(player))) {
		buff = alloc_lbuf("do_comconnect");
		sprintf(buff, "[%s] %s has connected.", ch->name, Name(player));
		do_comsend(ch, buff);
		free_lbuf(buff);
	}
}

char *do_comconnectchannel(player, channel, alias, i)
dbref player;
char *channel;
char *alias;
int i;
{
	struct channel *ch;
	struct comuser *user;

	if ((ch = select_channel(channel))) {
		for (user = ch->on_users;
		     user && user->who != player;
		     user = user->on_next) ;

		if (!user)
			if ((user = select_user(ch, player))) {
				user->on_next = ch->on_users;
				ch->on_users = user;
			} else
				raw_notify(player,
					   tprintf("Bad Comsys Alias: %s for Channel: %s",
						   alias + i * 6, channel));
	} else
		raw_notify(player, tprintf("Bad Comsys Alias: %s for Channel: %s",
					   alias + i * 6, channel));
}

char *do_comdisconnect(player)
dbref player;
{
	int i;
	struct comsys *c;

	c = get_comsys(player);

	for (i = 0; i < c->numchannels; i++) {
		do_comdisconnectchannel(player, c->channels[i]);
#ifdef CHANNEL_LOUD
		do_comdisconnectraw_notify(player, c->channels[i]);
#endif
	}
}

char *do_comconnect(player)
dbref player;
{
	struct comsys *c;
	int i;

	c = get_comsys(player);

	for (i = 0; i < c->numchannels; i++) {
		do_comconnectchannel(player, c->channels[i], c->alias, i);
		do_comconnectraw_notify(player, c->channels[i]);
	}
}


char *do_comdisconnectchannel(player, channel)
dbref player;
char *channel;
{
	struct comuser *user, *prevuser;
	struct channel *ch;

	prevuser = NULL;
	if (!(ch = select_channel(channel)))
		return;
	for (user = ch->on_users; user;) {
		if (user->who == player) {
			if (prevuser)
				prevuser->on_next = user->on_next;
			else
				ch->on_users = user->on_next;
			return;
		} else {
			prevuser = user;
			user = user->on_next;
		}
	}
}

void do_editchannel(player, cause, flag, arg1, arg2)
dbref player, cause;
int flag;
char *arg1;
char *arg2;
{
	char *s;
	struct channel *ch;
	int add_remove = 1;

	if (!mudconf.have_comsys) {
		raw_notify(player, "Comsys disabled.");
		return;
	}
	if (!(ch = select_channel(arg1))) {
		raw_notify(player, tprintf("Unknown channel %s.", arg1));
		return;
	}
	if (!((Comm_All(player)) || (player == ch->charge_who))) {
		raw_notify(player,
			   "You do not have permission to do that. (Not owner or Admin.)");
		return;
	}
	s = arg2;
	if (*s == '!') {
		add_remove = 0;
		s++;
	}
	switch (flag) {
	case 0:
		if (lookup_player(player, arg2, 1) != NOTHING) {
			ch->charge_who = lookup_player(player, arg2, 1);
			raw_notify(player, "Set.");
			return;
		} else {
			raw_notify(player, "Invalid player.");
			return;
		}
	case 1:
		ch->charge = atoi(arg2);
		raw_notify(player, "Set.");
		return;
	case 3:
		if (strcmp(s, "join") == 0) {
			add_remove ? (ch->type |= (CHANNEL_PL_MULT * CHANNEL_JOIN)) :
				(ch->type &= ~(CHANNEL_PL_MULT * CHANNEL_JOIN));
			raw_notify(player, (add_remove) ? "@cpflags: Set." : "@cpflags: Cleared.");
			return;
		}
		if (strcmp(s, "receive") == 0) {
			add_remove ? (ch->type |= (CHANNEL_PL_MULT * CHANNEL_RECIEVE)) :
				(ch->type &= ~(CHANNEL_PL_MULT * CHANNEL_RECIEVE));
			raw_notify(player, (add_remove) ? "@cpflags: Set." : "@cpflags: Cleared.");
			return;
		}
		if (strcmp(s, "transmit") == 0) {
			add_remove ? (ch->type |= (CHANNEL_PL_MULT * CHANNEL_TRANSMIT)) :
				(ch->type &= ~(CHANNEL_PL_MULT * CHANNEL_TRANSMIT));
			raw_notify(player, (add_remove) ? "@cpflags: Set." : "@cpflags: Cleared.");
			return;
		}
		raw_notify(player, "@cpflags: Unknown Flag.");
		break;
	case 4:
		if (strcmp(s, "join") == 0) {
			add_remove ? (ch->type |= (CHANNEL_OBJ_MULT * CHANNEL_JOIN)) :
				(ch->type &= ~(CHANNEL_OBJ_MULT * CHANNEL_JOIN));
			raw_notify(player, (add_remove) ? "@coflags: Set." : "@coflags: Cleared.");
			return;
		}
		if (strcmp(s, "receive") == 0) {
			add_remove ? (ch->type |= (CHANNEL_OBJ_MULT * CHANNEL_RECIEVE)) :
				(ch->type &= ~(CHANNEL_OBJ_MULT * CHANNEL_RECIEVE));
			raw_notify(player, (add_remove) ? "@coflags: Set." : "@coflags: Cleared.");
			return;
		}
		if (strcmp(s, "transmit") == 0) {
			add_remove ? (ch->type |= (CHANNEL_OBJ_MULT * CHANNEL_TRANSMIT)) :
				(ch->type &= ~(CHANNEL_OBJ_MULT * CHANNEL_TRANSMIT));
			raw_notify(player, (add_remove) ? "@coflags: Set." : "@coflags: Cleared.");
			return;
		}
		raw_notify(player, "@coflags: Unknown Flag.");
		break;
	}
	return;
}

int do_test_access(player, access, chan)
dbref player;
long access;
struct channel *chan;
{
	long flag_value = access;

	if (Comm_All(player))
		return (1);
	if ((flag_value & CHANNEL_JOIN) && Comm_All(player))
		return (1);

	/*
	 * Channel objects allow custom locks for channels.  The normal lock
	 * * * * is used to see if they can join that channel.  The enterlock
	 * * is * * checked to see if they can receive messages on it. The *
	 * Uselock is * * checked to see if they can transmit on it. Note: *
	 * These checks do * * not supercede the normal channel flags. If a *
	 * channel is set JOIN *  * for players, ALL players can join the *
	 * channel, whether or not they  * * pass the lock.  Same for all *
	 * channel  object locks. 
	 */

	if ((flag_value & CHANNEL_JOIN) && !((chan->chan_obj == NOTHING) ||
					     (chan->chan_obj == 0))) {
		if (could_doit(player, chan->chan_obj, A_LOCK))
			return (1);
	}
	if ((flag_value & CHANNEL_TRANSMIT) && !((chan->chan_obj == NOTHING) ||
						 (chan->chan_obj == 0))) {
		if (could_doit(player, chan->chan_obj, A_LUSE))
			return (1);
	}
	if ((flag_value & CHANNEL_RECIEVE) && !((chan->chan_obj == NOTHING) ||
						(chan->chan_obj == 0))) {
		if (could_doit(player, chan->chan_obj, A_LENTER))
			return (1);
	}
	if (Typeof(player) == TYPE_PLAYER)
		flag_value *= CHANNEL_PL_MULT;
	else
		flag_value *= CHANNEL_OBJ_MULT;
	flag_value &= 0xFF;	/*
				 * Mask out CHANNEL_PUBLIC and CHANNEL_LOUD * 
				 * 
				 * *  * *  * *  * *  * * just to be paranoid. 
				 * ^_^ 
				 */

	return (((long)chan->type & flag_value));
}

int do_comsystem(who, cmd)	/*
				 * * 1 means continue, 0 means stop  
				 */
dbref who;
char *cmd;
{
	char *t;
	char *ch;
	char *alias;
	char *s;


	alias = alloc_lbuf("do_comsystem");
	s = alias;
	for (t = cmd; *t && *t != ' '; *s++ = *t++) ;

	*s = '\0';

	if (*t)
		t++;

	ch = get_channel_from_alias(who, alias);
	if (strlen(ch) > 0) {
		do_processcom(who, ch, t);
		free_lbuf(alias);
		return 0;
	} else
		free_lbuf(alias);

	return 1;

}

char *do_chclose(player, chan)
dbref player;
char *chan;
{
	struct channel *ch;

	if (!(ch = select_channel(chan))) {
		raw_notify(player, tprintf("@cset: Channel %s does not exist.", chan));
		return;
	}
	if ((player != ch->charge_who) && (!Comm_All(player))) {
		raw_notify(player, "@cset: Permission denied.");
		return;
	}
	ch->type &= (~(CHANNEL_PUBLIC));
	raw_notify(player, tprintf("@cset: Channel %s taken off the public listings."
				   ,chan));
	return;
}

void do_cemit(player, cause, key, chan, text)
dbref player, cause;
int key;
char *chan, *text;
{
	struct channel *ch;


	if (!mudconf.have_comsys) {
		raw_notify(player, "Comsys disabled.");
		return;
	}
	if (!(ch = select_channel(chan))) {
		raw_notify(player, tprintf("Channel %s does not exist.",
					   chan));
		return;
	}
	if ((player != ch->charge_who) && (!Comm_All(player))) {
		raw_notify(player, "Permission denied.");
		return;
	}
	if (key == CEMIT_NOHEADER)
		do_comsend(ch, text);
	else
		do_comsend(ch, tprintf("[%s] %s", chan, text));
}

void do_chopen(player, cause, key, chan, object)
dbref player, cause;
int key;
char *chan, *object;
{
	struct channel *ch;

	if (!mudconf.have_comsys) {
		raw_notify(player, "Comsys disabled.");
		return;
	}
	switch (key) {
	case CSET_PRIVATE:
		do_chclose(player, chan);
		return;
	case CSET_LOUD:
		do_chloud(player, chan);
		return;
	case CSET_QUIET:
		do_chsquelch(player, chan);
		return;
	case CSET_LIST:
		do_chanlist(player, NOTHING, 1);
		return;
	case CSET_OBJECT:
		do_chanobj(player, chan, object);
		return;
	}

	if (!(ch = select_channel(chan))) {
		raw_notify(player, tprintf("@cset: Channel %s does not exist.", chan));
		return;
	}
	if ((player != ch->charge_who) && (!Comm_All(player))) {
		raw_notify(player, "@cset: Permission denied.");
		return;
	}
	ch->type |= (CHANNEL_PUBLIC);
	raw_notify(player, tprintf("@cset: Channel %s placed on the public listings."
				   ,chan));
	return;
}

char *do_chloud(player, chan)
dbref player;
char *chan;
{
	struct channel *ch;

	if (!(ch = select_channel(chan))) {
		raw_notify(player, tprintf("@cset: Channel %s does not exist.", chan));
		return;
	}
	if ((player != ch->charge_who) && (!Comm_All(player))) {
		raw_notify(player, "@cset: Permission denied.");
		return;
	}
	ch->type |= (CHANNEL_LOUD);
	raw_notify(player, tprintf("@cset: Channel %s now sends connect/disconnect msgs."
				   ,chan));
	return;
}

char *do_chsquelch(player, chan)
dbref player;
char *chan;
{
	struct channel *ch;

	if (!(ch = select_channel(chan))) {
		raw_notify(player, tprintf("@cset: Channel %s does not exist.", chan));
		return;
	}
	if ((player != ch->charge_who) && (!Comm_All(player))) {
		raw_notify(player, "@cset: Permission denied.");
		return;
	}
	ch->type &= ~(CHANNEL_LOUD);
	raw_notify(player, tprintf("@cset: Channel %s connect/disconnect msgs muted."
				   ,chan));
	return;
}


void do_chboot(player, cause, key, channel, victim)
dbref player, cause;
int key;
char *channel;
char *victim;
{
	struct comuser *user;
	struct channel *ch;
	struct comuser *vu;
	dbref thing;
	char buff[200];
	char buf2[LBUF_SIZE];
				/*
				 * * I sure hope it's not going to be that *
				 * *  * *  * *  * * long.  
				 */

	if (!mudconf.have_comsys) {
		raw_notify(player, "Comsys disabled.");
		return;
	}
	if (!(ch = select_channel(channel))) {
		raw_notify(player, "@cboot: Unknown channel.");
		return;
	}
	if (!(user = select_user(ch, player))) {
		raw_notify(player, "@cboot: You are not on that channel.");
		return;
	}
	if (!(ch->charge_who == player) && !Comm_All(player)) {
		raw_notify(player, "@cboot:  You can't do that!");
		return;
	}
	thing = match_thing(player, victim);

	if (thing == NOTHING) {
		return;
	}
	if (!(vu = select_user(ch, thing))) {
		raw_notify(player, tprintf("@cboot: %s in not on the channel.", Name(thing)));
		return;
	}
	/*
	 * We should be in the clear now. :) 
	 */

	strcpy(buf2, Name(player));
	sprintf(buff, "[%s] %s boots %s off the channel.", ch->name, buf2, Name(thing));
	do_comsend(ch, buff);
	do_delcomchannel(thing, channel);

}

char *do_chanobj(player, channel, object)
dbref player;
char *channel;
char *object;
{
	struct channel *ch;
	dbref thing;
	char *buff;

	init_match(player, object, NOTYPE);
	match_everything(0);
	thing = match_result();

	if (!(ch = select_channel(channel))) {
		raw_notify(player, "That channel does not exist.");
		return;
	}
	if (!(ch->charge_who == player) && !Comm_All(player)) {
		raw_notify(player, "Permission denied.");
		return;
	}
	if (thing == NOTHING) {
		ch->chan_obj = NOTHING;
		raw_notify(player, "Set.");
		return;
	}
	ch->chan_obj = thing;
	buff = unparse_object(player, thing, 0);
	raw_notify(player, tprintf("Channel %s is now using %s as channel object.",
				   ch->name, buff));
	free_lbuf(buff);
}

void do_chanlist(player, cause, key)
dbref player, cause;
int key;
{
	dbref owner;
	struct channel *ch;
	int i, flags;
	char *temp;
	char *buf;
	char *atrstr;


	if (!mudconf.have_comsys) {
		raw_notify(player, "Comsys disabled.");
		return;
	}
	flags = (int)NULL;

	if (key & CLIST_FULL) {
		do_listchannels(player);
		return;
	}
	temp = alloc_mbuf("do_chanlist_temp");
	buf = alloc_mbuf("do_chanlist_buf");

	raw_notify(player, "** Channel       Owner           Description");

	for (ch = (struct channel *)hash_firstentry(&mudstate.channel_htab);
	ch; ch = (struct channel *)hash_nextentry(&mudstate.channel_htab)) {
		if (Comm_All(player) || (ch->type & CHANNEL_PUBLIC) ||
		    ch->charge_who == player) {

			atrstr = atr_pget(ch->chan_obj, A_DESC, &owner, &flags);
			if ((ch->chan_obj == NOTHING) ||
			    !*atrstr)
				strcpy(buf, "No description.");
			else
				sprintf(buf, "%-54.54s", atrstr);

			free_lbuf(atrstr);
			sprintf(temp, "%c%c %-13.13s %-15.15s %-45.45s",
				(ch->type & (CHANNEL_PUBLIC)) ? 'P' : '-',
				(ch->type & (CHANNEL_LOUD)) ? 'L' : '-',
				ch->name, Name(ch->charge_who), buf);

			raw_notify(player, temp);
		}
	}
	free_mbuf(temp);
	free_mbuf(buf);
	raw_notify(player, "-- End of list of Channels --");
}
