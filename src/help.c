/*
 * help.c -- commands for giving help 
 */
/*
 * $Id: help.c,v 1.2 1997/04/16 06:01:08 dpassmor Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"

#include <fcntl.h>

#include "mudconf.h"
#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"
#include "help.h"
#include "htab.h"
#include "alloc.h"

/*
 * Pointers to this struct is what gets stored in the help_htab's 
 */
struct help_entry {
	int pos;		/*
				 * Position, copied from help_indx 
				 */
	char original;		/*
				 * 1 for the longest name for a topic. 0 for
				 * * * * abbreviations 
				 */
	char *key;		/*
				 * The key this is stored under. 
				 */
};

int helpindex_read(htab, filename)
HASHTAB *htab;
char *filename;
{
	help_indx entry;
	char *p;
	int count;
	FILE *fp;
	struct help_entry *htab_entry;

/*
 * Let's clean out our hash table, before we throw it away. 
 */
	for (htab_entry = (struct help_entry *)hash_firstentry(htab);
	     htab_entry;
	     htab_entry = (struct help_entry *)hash_nextentry(htab)) {
		free(htab_entry->key);
		free(htab_entry);
	}

	hashflush(htab, 0);
	if ((fp = tf_fopen(filename, O_RDONLY)) == NULL) {
		STARTLOG(LOG_PROBLEMS, "HLP", "RINDX")
			p = alloc_lbuf("helpindex_read.LOG");
		sprintf(p, "Can't open %s for reading.", filename);
		log_text(p);
		free_lbuf(p);
		ENDLOG
			return -1;
	}
	count = 0;
	while ((fread((char *)&entry, sizeof(help_indx), 1, fp)) == 1) {

		/*
		 * Lowercase the entry and add all leftmost substrings. * * * 
		 * 
		 * * Substrings already added will be rejected by hashadd. 
		 */
		for (p = entry.topic; *p; p++)
			*p = ToLower(*p);

		htab_entry = (struct help_entry *)malloc(sizeof(struct help_entry));

		htab_entry->pos = entry.pos;
		htab_entry->original = 1;	/*
						 * First is the longest 
						 */
		htab_entry->key = (char *)malloc(strlen(entry.topic) + 1);
		StringCopy(htab_entry->key, entry.topic);
		while (p > entry.topic) {
			p--;
			if (!isspace(*p)) {
				if ((hashadd(entry.topic, (int *)htab_entry, htab)) == 0)
					count++;
				else {
					free(htab_entry->key);
					free(htab_entry);
				}
			} else {
				free(htab_entry->key);
				free(htab_entry);
			}
			*p = '\0';
			htab_entry = (struct help_entry *)malloc(sizeof(struct help_entry));

			htab_entry->pos = entry.pos;
			htab_entry->original = 0;
			htab_entry->key = (char *)malloc(strlen(entry.topic) + 1);
			StringCopy(htab_entry->key, entry.topic);
		}
		free(htab_entry->key);
		free(htab_entry);
	}
	tf_fclose(fp);
	hashreset(htab);
	return count;
}

void helpindex_load(player)
dbref player;
{
	int news, help, whelp;
	int phelp, wnhelp;

	phelp = helpindex_read(&mudstate.plushelp_htab, mudconf.plushelp_indx);
	wnhelp = helpindex_read(&mudstate.wiznews_htab, mudconf.wiznews_indx);
	news = helpindex_read(&mudstate.news_htab, mudconf.news_indx);
	help = helpindex_read(&mudstate.help_htab, mudconf.help_indx);
	whelp = helpindex_read(&mudstate.wizhelp_htab, mudconf.whelp_indx);
	if ((player != NOTHING) && !Quiet(player))
		notify(player,
		       tprintf("Index entries: News...%d  Help...%d  Wizhelp...%d  +Help...%d  Wiznews...%d",
			       news, help, whelp, phelp, wnhelp));
}

void NDECL(helpindex_init)
{
	hashinit(&mudstate.news_htab, 15 * HASH_FACTOR);
	hashinit(&mudstate.help_htab, 30 * HASH_FACTOR);
	hashinit(&mudstate.wizhelp_htab, 30 * HASH_FACTOR);
	hashinit(&mudstate.plushelp_htab, 30 * HASH_FACTOR);
	hashinit(&mudstate.wiznews_htab, 30 * HASH_FACTOR);

	helpindex_load(NOTHING);
}

void help_write(player, topic, htab, filename, eval)
dbref player;
char *topic, *filename;
HASHTAB *htab;
int eval;
{
	FILE *fp;
	char *p, *line, *bp, *str, *result;
	int offset;
	struct help_entry *htab_entry;
	char matched;
	char *topic_list, *buffp, *buff;

	if (*topic == '\0')
		topic = (char *)"help";
	else
		for (p = topic; *p; p++)
			*p = ToLower(*p);
	htab_entry = (struct help_entry *)hashfind(topic, htab);
	if (htab_entry)
		offset = htab_entry->pos;
	else {
		matched = 0;
		for (htab_entry = (struct help_entry *)hash_firstentry(htab);
		     htab_entry != NULL;
		   htab_entry = (struct help_entry *)hash_nextentry(htab)) {
			if (htab_entry->original &&
			    quick_wild(topic, htab_entry->key)) {
				if (matched == 0) {
					matched = 1;
					topic_list = alloc_lbuf("help_write");
					buffp = topic_list;
				}
				safe_str(htab_entry->key, topic_list, &buffp);
				safe_str((char *)"  ", topic_list, &buffp);
			}
		}
		if (matched == 0)
			notify(player, tprintf("No entry for '%s'.", topic));
		else {
			notify(player, tprintf("Here are the entries which match '%s':", topic));
			*buffp = '\0';
			notify(player, topic_list);
			free_lbuf(topic_list);
		}
		return;
	}
	if ((fp = tf_fopen(filename, O_RDONLY)) == NULL) {
		notify(player,
		       "Sorry, that function is temporarily unavailable.");
		STARTLOG(LOG_PROBLEMS, "HLP", "OPEN")
			line = alloc_lbuf("help_write.LOG.open");
		sprintf(line, "Can't open %s for reading.", filename);
		log_text(line);
		free_lbuf(line);
		ENDLOG
			return;
	}
	if (fseek(fp, offset, 0) < 0L) {
		notify(player,
		       "Sorry, that function is temporarily unavailable.");
		STARTLOG(LOG_PROBLEMS, "HLP", "SEEK")
			line = alloc_lbuf("help_write.LOG.seek");
		sprintf(line, "Seek error in file %s.", filename);
		log_text(line);
		free_lbuf(line);
		ENDLOG
			tf_fclose(fp);
		return;
	}
	line = alloc_lbuf("help_write");
	result = alloc_lbuf("help_write.2");
	for (;;) {
		if (fgets(line, LBUF_SIZE - 1, fp) == NULL)
			break;
		if (line[0] == '&')
			break;
		for (p = line; *p != '\0'; p++)
			if (*p == '\n')
				*p = '\0';
		if (eval) {
			str = line;
			bp = result;
			exec(result, &bp, 0, player, player,
			     EV_NO_COMPRESS | EV_FIGNORE | EV_EVAL, &str,
			     (char **)NULL, 0);
			*bp = '\0';
			notify(player, result);
		} else
			notify(player, line);
	}
	tf_fclose(fp);
	free_lbuf(line);
	free_lbuf(result);
}

/*
 * ---------------------------------------------------------------------------
 * * do_help: display information from new-format news and help files
 */

void do_help(player, cause, key, message)
dbref player, cause;
int key;
char *message;
{
	char *buf;

	switch (key) {
	case HELP_HELP:
		help_write(player, message, &mudstate.help_htab,
			   mudconf.help_file, 0);
		break;
	case HELP_NEWS:
		help_write(player, message, &mudstate.news_htab,
			   mudconf.news_file, 1);
		break;
	case HELP_WIZHELP:
		help_write(player, message, &mudstate.wizhelp_htab,
			   mudconf.whelp_file, 0);
		break;
	case HELP_PLUSHELP:
		help_write(player, message, &mudstate.plushelp_htab,
			   mudconf.plushelp_file, 1);
		break;
	case HELP_WIZNEWS:
		help_write(player, message, &mudstate.wiznews_htab,
			   mudconf.wiznews_file, 0);
		break;
	default:
		STARTLOG(LOG_BUGS, "BUG", "HELP")
			buf = alloc_mbuf("do_help.LOG");
		sprintf(buf, "Unknown help file number: %d", key);
		log_text(buf);
		free_mbuf(buf);
		ENDLOG
	}
}
