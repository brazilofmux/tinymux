/*
 * commac.c 
 */
/*
 * $Id: commac.c,v 1.1.1.1 1997/04/16 03:36:52 dpassmor Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"
#include <ctype.h>
#include <sys/types.h>

#include "db.h"
#include "interface.h"
#include "match.h"
#include "config.h"
#include "externs.h"

#include "commac.h"

char *load_comsys_and_macros(filename)
char *filename;
{
	FILE *fp;
	int i;
	char buffer[200];

	for (i = 0; i < NUM_COMMAC; i++)
		commac_table[i] = NULL;

	if (!(fp = fopen(filename, "r"))) {
		fprintf(stderr, "Error: Couldn't find %s.\n", filename);
	} else {
		fprintf(stderr, "LOADING: %s\n", filename);
		if (fscanf(fp, "*** Begin %s ***\n", buffer) == 1 &&
		    !strcmp(buffer, "COMMAC")) {
			load_commac(fp);
		} else {
			fprintf(stderr, "Error: Couldn't find Begin COMMAC in %s.", filename);
			return;
		}

		if (fscanf(fp, "*** Begin %s ***\n", buffer) == 1 &&
		    !strcmp(buffer, "COMSYS")) {
			load_comsystem(fp);
		} else {
			fprintf(stderr, "Error: Couldn't find Begin COMSYS in %s.", filename);
			return;
		}

		if (fscanf(fp, "*** Begin %s ***\n", buffer) == 1 &&
		    !strcmp(buffer, "MACRO")) {
			load_macros(fp);
		} else {
			fprintf(stderr, "Error: Couldn't find Begin MACRO in %s.", filename);
			return;
		}

		fclose(fp);
		fprintf(stderr, "LOADING: %s (done)\n", filename);
	}
}

char *save_comsys_and_macros(filename)
char *filename;
{
	FILE *fp;
	char buffer[500];

	sprintf(buffer, "%s.#", filename);
	if (!(fp = fopen(buffer, "w"))) {
		fprintf(stderr, "Unable to open %s for writing.\n", buffer);
		return;
	}
	fprintf(fp, "*** Begin COMMAC ***\n");
	save_commac(fp);

	fprintf(fp, "*** Begin COMSYS ***\n");
	save_comsystem(fp);

	fprintf(fp, "*** Begin MACRO ***\n");
	save_macros(fp);

	fclose(fp);
	rename(buffer, filename);
}

char *load_commac(fp)
FILE *fp;
{
	int i, j;
	char buffer[100];
	int np;
	struct commac *c;
	char *t;
	char in;

	fscanf(fp, "%d\n", &np);
	for (i = 0; i < np; i++) {
		c = create_new_commac();
		fscanf(fp, "%d %d %d %d %d %d %d %d\n",
		       &(c->who), &(c->numchannels), &(c->macros[0]), &(c->macros[1]),
		       &c->macros[2], &(c->macros[3]), &(c->macros[4]), &(c->curmac));
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
			add_commac(c);
		purge_commac();
	}
}

char *purge_commac()
{
	struct commac *c;
	struct commac *d;
	int i;

#ifdef ABORT_PURGE_COMSYS
	return;
#endif /*
        * * ABORT_PURGE_COMSYS  
        */

	for (i = 0; i < NUM_COMMAC; i++) {
		c = commac_table[i];
		while (c) {
			d = c;
			c = c->next;
			if (d->numchannels == 0 && d->curmac == -1 && d->macros[1] == -1 &&
			    d->macros[2] == -1 && d->macros[3] == -1 && d->macros[4] == -1 &&
			    d->macros[0] == -1) {
				del_commac(d->who);
				continue;
			}
/*
 * if ((Typeof(d->who) != TYPE_PLAYER) && (God(Owner(d->who))) &&
 * * (Going(d->who))) 
 */
			if (Typeof(d->who) == TYPE_PLAYER)
				continue;
			if (God(Owner(d->who)) && Going(d->who)) {
				del_commac(d->who);
				continue;
			}
		}
	}
}

char *save_commac(fp)
FILE *fp;
{
	int np;
	struct commac *c;
	int i, j;

	purge_commac();
	np = 0;
	for (i = 0; i < NUM_COMMAC; i++) {
		c = commac_table[i];
		while (c) {
			np++;
			c = c->next;
		}
	}

	fprintf(fp, "%d\n", np);
	for (i = 0; i < NUM_COMMAC; i++) {
		c = commac_table[i];
		while (c) {
			fprintf(fp, "%d %d %d %d %d %d %d %d\n", c->who, c->numchannels,
				c->macros[0], c->macros[1], c->macros[2], c->macros[3],
				c->macros[4], c->curmac);
			for (j = 0; j < c->numchannels; j++) {
				fprintf(fp, "%s %s\n", c->alias + j * 6, c->channels[j]);
			}
			c = c->next;
		}
	}
}

struct commac *create_new_commac()
{
	struct commac *c;
	int i;

	c = (struct commac *)malloc(sizeof(struct commac));

	c->who = -1;
	c->numchannels = 0;
	c->maxchannels = 0;
	c->alias = NULL;
	c->channels = NULL;

	c->curmac = -1;
	for (i = 0; i < 5; i++)
		c->macros[i] = -1;

	c->next = NULL;
	return c;
}

struct commac *get_commac(which)
dbref which;
{
	struct commac *c;

	if (which < 0)
		return NULL;

	c = commac_table[which % NUM_COMMAC];

	while (c && (c->who != which))
		c = c->next;

	if (!c) {
		c = create_new_commac();
		c->who = which;
		add_commac(c);
	}
	return c;
}

char *add_commac(c)
struct commac *c;
{
	if (c->who < 0)
		return;

	c->next = commac_table[c->who % NUM_COMMAC];
	commac_table[c->who % NUM_COMMAC] = c;
}

char *del_commac(who)
dbref who;
{
	struct commac *c;
	struct commac *last;

	if (who < 0)
		return;

	c = commac_table[who % NUM_COMMAC];

	if (c == NULL)
		return;

	if (c->who == who) {
		commac_table[who % NUM_COMMAC] = c->next;
		destroy_commac(c);
		return;
	}
	last = c;
	c = c->next;
	while (c) {
		if (c->who == who) {
			last->next = c->next;
			destroy_commac(c);
			return;
		}
		last = c;
		c = c->next;
	}
}

char *destroy_commac(c)
struct commac *c;
{
	int i;

	free(c->alias);
	for (i = 0; i < c->numchannels; i++)
		free(c->channels[i]);
	free(c->channels);
	free(c);
}

char *sort_com_aliases(c)
struct commac *c;
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
