/*
 * mail.c 
 * This code was taken from Kalkin's DarkZone code, which was
 * originally taken from PennMUSH 1.50 p10, and has been heavily modified
 * since being included in MUX.
 * 
 * $Id: mail.c,v 1.2.2.1 1997/06/21 08:11:03 dpassmor Exp $
 * -------------------------------------------------------------------
 */

#include "copyright.h"
#include "autoconf.h"
#include <sys/time.h>
#include <sys/types.h>
#include "config.h"
#include "db.h"
#include "interface.h"
#include "attrs.h"
#include "externs.h"
#include "mail.h"
#include "alloc.h"
#include "htab.h"

/*
 * Buffers used for RADIX_COMPRESSION 
 */

char msgbuff[LBUF_SIZE + (LBUF_SIZE >> 1) + 1];
char subbuff[LBUF_SIZE + (LBUF_SIZE >> 1) + 1];
char timebuff[LBUF_SIZE + (LBUF_SIZE >> 1) + 1];

extern char *FDECL(getstring_noalloc, (FILE *, int));
static int FDECL(sign, (int));
static void FDECL(do_mail_flags, (dbref, char *, mail_flag, int));
static void FDECL(send_mail, (dbref, dbref, const char *, const char *, \
			      int, mail_flag, int));
static int FDECL(player_folder, (dbref));
static int FDECL(parse_msglist, (char *, struct mail_selector *, dbref));
static int FDECL(mail_match, (struct mail *, struct mail_selector, int));
static int FDECL(parse_folder, (dbref, char *));
static char *FDECL(status_chars, (struct mail *));
static char *FDECL(status_string, (struct mail *));
void FDECL(add_folder_name, (dbref, int, char *));
static int FDECL(get_folder_number, (dbref, char *));
void FDECL(check_mail, (dbref, int, int));
static char *FDECL(get_folder_name, (dbref, int));
static char *FDECL(mail_list_time, (const char *));
static char *FDECL(make_numlist, (dbref, char *));
static char *FDECL(make_namelist, (dbref, char *));
static void FDECL(mail_to_list, (dbref, char *, char *, char *, int, int));
static void FDECL(do_edit_msg, (dbref, char *, char *));
static void FDECL(do_mail_proof, (dbref));
void FDECL(do_mail_cc, (dbref, char *));
void FDECL(do_expmail_abort, (dbref));

#define MALIAS_LEN 100

struct malias {
	int owner;
	char *name;
	char *desc;
	int numrecep;
	dbref list[MALIAS_LEN];
};

int ma_size = 0;
int ma_top = 0;

struct malias **malias;

#ifdef RADIX_COMPRESSION
char *strndup(const char *str, int len)
{
	char *s;

	s = (char *)malloc(len);
	bcopy(str, s, len);
	return s;
}
#endif /*
        * RADIX_COMPRESSION 
        */

/*
 * Handling functions for the database of mail messages. 
 */

/*
 * mail_db_grow - We keep a database of mail text, so if we send a message to
 * * more than one player, we won't have to duplicate the text.
 */

static void mail_db_grow(newtop)
int newtop;
{
	int newsize, i;
	MENT *newdb;

	if (newtop <= mudstate.mail_db_top) {
		return;
	}
	if (newtop <= mudstate.mail_db_size) {
		for (i = mudstate.mail_db_top; i < newtop; i++) {
			mudstate.mail_list[i].count = 0;
			mudstate.mail_list[i].message = NULL;
		}
		mudstate.mail_db_top = newtop;
		return;
	}
	if (newtop <= mudstate.mail_db_size + 100) {
		newsize = mudstate.mail_db_size + 100;
	} else {
		newsize = newtop;
	}

	newdb = (MENT *) malloc((newsize + 1) * sizeof(MENT));

	if (!newdb)
		abort();

	if (mudstate.mail_list) {
		mudstate.mail_list -= 1;
		bcopy((char *)mudstate.mail_list, (char *)newdb,
		      (mudstate.mail_db_top + 1) * sizeof(MENT));
		free(mudstate.mail_list);
	}
	mudstate.mail_list = newdb + 1;
	newdb = NULL;

	for (i = mudstate.mail_db_top; i < newtop; i++) {
		mudstate.mail_list[i].count = 0;
		mudstate.mail_list[i].message = NULL;
	}
	mudstate.mail_db_top = newtop;
	mudstate.mail_db_size = newsize;
}

/*
 * make_mail_freelist - move the freelist to the first empty slot in the mail
 * * database.
 */

static void make_mail_freelist()
{
	int i;

	for (i = 0; i < mudstate.mail_db_top; i++) {
		if (mudstate.mail_list[i].message == NULL) {
			mudstate.mail_freelist = i;
			return;
		}
	}

	mail_db_grow(i + 1);
	mudstate.mail_freelist = i;
}

/*
 * add_mail_message - adds a new text message to the mail database, and returns
 * * a unique number for that message.
 */

static int add_mail_message(player, message)
dbref player;
char *message;
{
	int number, len;
	char *atrstr, *execstr, *msg, *bp, *str;
	int aflags;
	dbref aowner;

	if (!strcasecmp(message, "clear")) {
		notify(player, "MAIL: You probably don't wanna send mail saying 'clear'.");
		return -1;
	}
	if (!mudstate.mail_list) {
		mail_db_grow(1);
	}
	/*
	 * Add an extra bit of protection here 
	 */
	while (mudstate.mail_list[mudstate.mail_freelist].message != NULL) {
		make_mail_freelist();
	}
	number = mudstate.mail_freelist;

	atrstr = atr_get(player, A_SIGNATURE, &aowner, &aflags);
	execstr = bp = alloc_lbuf("add_mail_message");
	str = atrstr;
	exec(execstr, &bp, 0, player, player, EV_STRIP | EV_FCHECK | EV_EVAL,
	     &str, (char **)NULL, 0);
	*bp = '\0';
	msg = bp = alloc_lbuf("add_mail_message.2");
	str = message;
	exec(msg, &bp, 0, player, player, EV_EVAL | EV_FCHECK | EV_NO_COMPRESS, &str,
	     (char **)NULL, 0);
	*bp = '\0';

#ifdef RADIX_COMPRESSION
	len = string_compress(tprintf("%s %s", msg, execstr), msgbuff);
	mudstate.mail_list[number].message = (char *)strndup(msgbuff, len);
#else
	mudstate.mail_list[number].message = (char *)strdup(tprintf("%s %s", msg,
								  execstr));
#endif /*
        * RADIX_COMPRESSION 
        */
	free_lbuf(atrstr);
	free_lbuf(execstr);
	free_lbuf(msg);
	make_mail_freelist();
	return number;
}

/*
 * add_mail_message_nosig - used for reading in old style messages from disk 
 */

static int add_mail_message_nosig(message)
char *message;
{
	int number, len;

	number = mudstate.mail_freelist;
	if (!mudstate.mail_list) {
		mail_db_grow(1);
	}
#ifdef RADIX_COMPRESSION
	len = string_compress(message, msgbuff);
	mudstate.mail_list[number].message = (char *)strndup(msgbuff, len);
#else
	mudstate.mail_list[number].message = (char *)strdup(message);
#endif /*
        * RADIX_COMPRESSION 
        */

	make_mail_freelist();
	return number;
}

/*
 * new_mail_message - used for reading messages in from disk which already have
 * * a number assigned to them.
 */

#ifdef RADIX_COMPRESSION
static void new_mail_message(message, number, len)
char *message;
int number, len;
#else
static void new_mail_message(message, number)
char *message;
int number;
#endif
{
#ifdef RADIX_COMPRESSION
	mudstate.mail_list[number].message = (char *)strndup(message, len);
#else
	mudstate.mail_list[number].message = (char *)strdup(message);
#endif
}

/*
 * add_count - increments the reference count for any particular message 
 */

static INLINE void add_count(number)
int number;
{
	mudstate.mail_list[number].count++;
}

/*
 * delete_mail_message - decrements the reference count for a message, and
 * * deletes the message if the counter reaches 0.
 */

static void delete_mail_message(number)
int number;
{
	mudstate.mail_list[number].count--;

	if (mudstate.mail_list[number].count < 1) {
		free(mudstate.mail_list[number].message);
		mudstate.mail_list[number].message = NULL;
		mudstate.mail_list[number].count = 0;
	}
}

/*
 * get_mail_message - returns the text for a particular number. This text
 * * should NOT be modified.
 */

INLINE char *get_mail_message(number)
int number;
{
	static char buff[LBUF_SIZE];

	if (mudstate.mail_list[number].message != NULL) {
		return mudstate.mail_list[number].message;
	} else {
		delete_mail_message(number);
#ifdef RADIX_COMPRESSION
		string_compress("MAIL: This mail message does not exist in the database. Please alert your admin.", msgbuff);
		StringCopy(buff, msgbuff);
#else		
		StringCopy(buff,
			   "MAIL: This mail message does not exist in the database. Please alert your admin.");
#endif
		return buff;
	}
}

/*-------------------------------------------------------------------------*
 *   User mail functions (these are called from game.c)
 *
 * do_mail - cases 
 * do_mail_read - read messages
 * do_mail_list - list messages
 * do_mail_flags - tagging, untagging, clearing, unclearing of messages
 * do_mail_file - files messages into a new folder
 * do_mail_fwd - forward messages to another player(s)
 * do_mail_count - count messages
 * do_mail_purge - purge cleared messages
 * do_mail_change_folder - change current folder
 *-------------------------------------------------------------------------*/

extern char *FDECL(upcasestr, (char *));

/*
 * Change or rename a folder 
 */
void do_mail_change_folder(player, fld, newname)
dbref player;
char *fld;
char *newname;
{
	int pfld;
	char *p;

	if (!fld || !*fld) {
		/*
		 * Check mail in all folders 
		 */
		for (pfld = MAX_FOLDERS; pfld >= 0; pfld--)
			check_mail(player, pfld, 1);

		pfld = player_folder(player);
		notify(player, tprintf("MAIL: Current folder is %d [%s].",
				       pfld, get_folder_name(player, pfld)));

		return;
	}
	pfld = parse_folder(player, fld);
	if (pfld < 0) {
		notify(player, "MAIL: What folder is that?");
		return;
	}
	if (newname && *newname) {
		/*
		 * We're changing a folder name here 
		 */
		if (strlen(newname) > FOLDER_NAME_LEN) {
			notify(player, "MAIL: Folder name too long");
			return;
		}
		for (p = newname; p && *p; p++) {
			if (!isalnum(*p)) {
				notify(player, "MAIL: Illegal folder name");
				return;
			}
		}

		add_folder_name(player, pfld, newname);
		notify(player, tprintf("MAIL: Folder %d now named '%s'", pfld, newname));
	} else {
		/*
		 * Set a new folder 
		 */
		set_player_folder(player, pfld);
		notify(player, tprintf("MAIL: Current folder set to %d [%s].",
				       pfld, get_folder_name(player, pfld)));
	}
}

void do_mail_tag(player, msglist)
dbref player;
char *msglist;
{
	do_mail_flags(player, msglist, M_TAG, 0);
}

void do_mail_safe(player, msglist)
dbref player;
char *msglist;
{
	do_mail_flags(player, msglist, M_SAFE, 0);
}

void do_mail_clear(player, msglist)
dbref player;
char *msglist;
{
	do_mail_flags(player, msglist, M_CLEARED, 0);
}

void do_mail_untag(player, msglist)
dbref player;
char *msglist;
{
	do_mail_flags(player, msglist, M_TAG, 1);
}

void do_mail_unclear(player, msglist)
dbref player;
char *msglist;
{
	do_mail_flags(player, msglist, M_CLEARED, 1);
}

/*
 * adjust the flags of a set of messages 
 */

static void do_mail_flags(player, msglist, flag, negate)
dbref player;
char *msglist;
mail_flag flag;
int negate;			/*

				 * if 1, clear the flag 
				 */
{
	struct mail *mp;
	struct mail_selector ms;
	int i = 0, j = 0, folder;

	if (!parse_msglist(msglist, &ms, player)) {
		return;
	}
	folder = player_folder(player);
	for (mp = (struct mail *)nhashfind((int)player, &mudstate.mail_htab);
	     mp; mp = mp->next) {
		if (All(ms) || (Folder(mp) == folder)) {
			i++;
			if (mail_match(mp, ms, i)) {
				j++;
				if (negate) {
					mp->read &= ~flag;
				} else {
					mp->read |= flag;
				}

				switch (flag) {
				case M_TAG:
					notify(player, tprintf("MAIL: Msg #%d %s.", i,
					   negate ? "untagged" : "tagged"));
					break;
				case M_CLEARED:
					if (Unread(mp) && !negate) {
						notify(player,
						       tprintf("MAIL: Unread Msg #%d cleared! Use @mail/unclear %d to recover.", i, i));
					} else {
						notify(player, tprintf("MAIL: Msg #%d %s.", i,
								       negate ? "uncleared" : "cleared"));
					}
					break;
				case M_SAFE:
					notify(player, tprintf("MAIL: Msg #%d marked safe.", i));
					break;
				}
			}
		}
	}

	if (!j) {
		/*
		 * ran off the end of the list without finding anything 
		 */
		notify(player, "MAIL: You don't have any matching messages!");
	}
}

/*
 * Change a message's folder 
 */

void do_mail_file(player, msglist, folder)
dbref player;
char *msglist;
char *folder;
{
	struct mail *mp;
	struct mail_selector ms;
	int foldernum, origfold;
	int i = 0, j = 0;

	if (!parse_msglist(msglist, &ms, player)) {
		return;
	}
	if ((foldernum = parse_folder(player, folder)) == -1) {
		notify(player, "MAIL: Invalid folder specification");
		return;
	}
	origfold = player_folder(player);
	for (mp = (struct mail *)nhashfind((int)player, &mudstate.mail_htab);
	     mp; mp = mp->next) {
		if (All(ms) || (Folder(mp) == origfold)) {
			i++;
			if (mail_match(mp, ms, i)) {
				j++;
				mp->read &= M_FMASK;	/*
							 * Clear the folder 
							 */
				mp->read |= FolderBit(foldernum);
				notify(player, tprintf("MAIL: Msg %d filed in folder %d", i, foldernum));
			}
		}
	}

	if (!j) {
		/*
		 * ran off the end of the list without finding anything 
		 */
		notify(player, "MAIL: You don't have any matching messages!");
	}
}

/*
 * print a mail message(s) 
 */

void do_mail_read(player, msglist)
dbref player;
char *msglist;
{
	struct mail *mp;
	char *tbuf1, *buff, *status, *names;
	struct mail_selector ms;
	int i = 0, j = 0, folder;

	tbuf1 = alloc_lbuf("do_mail_read");

	if (!parse_msglist(msglist, &ms, player)) {
		free_lbuf(tbuf1);
		return;
	}
	folder = player_folder(player);
	for (mp = (struct mail *)nhashfind((int)player, &mudstate.mail_htab);
	     mp; mp = mp->next) {
		if (Folder(mp) == folder) {
			i++;
			if (mail_match(mp, ms, i)) {
				/*
				 * Read it 
				 */
				j++;
				buff = alloc_lbuf("do_mail_read");
#ifdef RADIX_COMPRESSION
				(void)string_decompress(get_mail_message(mp->number), buff);
#else
				StringCopy(buff, get_mail_message(mp->number));
#endif /*
        * RADIX_COMPRESSION 
        */
				notify(player, DASH_LINE);
				status = status_string(mp);
				names = make_namelist(player, (char *)mp->tolist);
#ifdef RADIX_COMPRESSION
				string_decompress(mp->subject, subbuff);
				string_decompress(mp->time, timebuff);
				notify(player, tprintf("%-3d         From:  %-*s  At: %-25s  %s\r\nFldr   : %-2d Status: %s\r\nTo     : %-65s\r\nSubject: %-65s",
				   i, PLAYER_NAME_LIMIT - 6, Name(mp->from),
						       timebuff,
						     (Connected(mp->from) &&
				  (!Hidden(mp->from) || Hasprivs(player))) ?
					       " (Conn)" : "      ", folder,
						       status,
						       names,
						       subbuff));
#else
				notify(player, tprintf("%-3d         From:  %-*s  At: %-25s  %s\r\nFldr   : %-2d Status: %s\r\nTo     : %-65s\r\nSubject: %-65s",
				   i, PLAYER_NAME_LIMIT - 6, Name(mp->from),
						       mp->time,
						     (Connected(mp->from) &&
				  (!Hidden(mp->from) || Hasprivs(player))) ?
					       " (Conn)" : "      ", folder,
						       status,
						       names,
						       mp->subject));
#endif /*
        * RADIX_COMPRESSION 
        */
				free_lbuf(names);
				free_lbuf(status);
				notify(player, DASH_LINE);
				StringCopy(tbuf1, buff);
				notify(player, tbuf1);
				notify(player, DASH_LINE);
				free_lbuf(buff);
				if (Unread(mp))
					mp->read |= M_ISREAD;	/*
								 * mark * * * 
								 * message as 
								 * 
								 * *  * *  *
								 * * * read 
								 */
			}
		}
	}

	if (!j) {
		/*
		 * ran off the end of the list without finding anything 
		 */
		notify(player, "MAIL: You don't have that many matching messages!");
	}
	free_lbuf(tbuf1);
}

void do_mail_retract(player, name, msglist)
dbref player;
char *name, *msglist;
{
	dbref target;
	struct mail *mp, *nextp;
	struct mail_selector ms;
	int i = 0, j = 0;

	target = lookup_player(player, name, 1);
	if (target == NOTHING) {
		notify(player, "MAIL: No such player.");
		return;
	}
	if (!parse_msglist(msglist, &ms, target)) {
		return;
	}
	for (mp = (struct mail *)nhashfind((int)target, &mudstate.mail_htab);
	     mp; mp = nextp) {
		if (mp->from == player) {
			i++;
			if (mail_match(mp, ms, i)) {
				j++;
				if (Unread(mp)) {
					if (mp->prev == NULL)
						nhashrepl((int)target,
							  (int *)mp->next,
						       &mudstate.mail_htab);
					else if (mp->next == NULL)
						mp->prev->next = NULL;

					if (mp->prev != NULL)
						mp->prev->next = mp->next;
					if (mp->next != NULL)
						mp->next->prev = mp->prev;

					nextp = mp->next;
					free((char *)mp->subject);
					delete_mail_message(mp->number);
					free((char *)mp->time);
					free((char *)mp->tolist);
					free(mp);
					notify(player, "MAIL: Mail retracted.");
				} else {
					notify(player, "MAIL: That message has been read.");
					nextp = mp->next;
				}
			} else {
				nextp = mp->next;
			}
		} else {
			nextp = mp->next;
		}
	}

	if (!j) {
		/*
		 * ran off the end of the list without finding anything 
		 */
		notify(player, "MAIL: No matching messages.");
	}
}


void do_mail_review(player, name, msglist)
dbref player;
char *name;
char *msglist;
{
	struct mail *mp;
	struct mail_selector ms;
	int i = 0, j = 0;
	dbref target;
	char *tbuf1, *msg, *status, *bp, *str;


	target = lookup_player(player, name, 1);
	if (target == NOTHING) {
		notify(player, "MAIL: No such player.");
		return;
	}
	if (!msglist || !*msglist) {
		notify(player, tprintf(
					      "--------------------   MAIL: %-25s   ------------------", Name(target)));
		for (mp = (struct mail *)nhashfind((int)target, &mudstate.mail_htab);
		     mp; mp = mp->next) {
			if (mp->from == player) {
				i++;
				/*
				 * list it 
				 */
#ifdef RADIX_COMPRESSION
				string_decompress(get_mail_message(mp->number), msgbuff);
				string_decompress(mp->time, timebuff);
				string_decompress(mp->subject, subbuff);
				notify(player, tprintf("[%s] %-3d (%4d) From: %-*s Sub: %.25s",
						       status_chars(mp),
						       i, strlen(msgbuff),
				      PLAYER_NAME_LIMIT - 6, Name(mp->from),
						       subbuff));
#else
				notify(player, tprintf("[%s] %-3d (%4d) From: %-*s Sub: %.25s",
						       status_chars(mp),
				    i, strlen(get_mail_message(mp->number)),
				      PLAYER_NAME_LIMIT - 6, Name(mp->from),
						       mp->subject));
#endif /*
        * RADIX_COMPRESSION 
        */
			}
		}
		notify(player, DASH_LINE);
	} else {
		tbuf1 = alloc_lbuf("do_mail_read");

		if (!parse_msglist(msglist, &ms, target)) {
			free_lbuf(tbuf1);
			return;
		}
		for (mp = (struct mail *)nhashfind((int)target, &mudstate.mail_htab);
		     mp; mp = mp->next) {
			if (mp->from == player) {
				i++;
				if (mail_match(mp, ms, i)) {
					/*
					 * Read it 
					 */
					j++;
					status = status_string(mp);
#ifdef RADIX_COMPRESSION
					string_decompress(get_mail_message(mp->number), msgbuff);
					string_decompress(mp->time, timebuff);
					string_decompress(mp->subject, subbuff);

					msg = bp = alloc_lbuf("do_mail_review");
					str = msgbuff;
					exec(msg, &bp, 0, player, player, EV_EVAL | EV_FCHECK | EV_NO_COMPRESS, &str,
					     (char **)NULL, 0);
					*bp = '\0';
					notify(player, DASH_LINE);
					notify(player, tprintf("%-3d         From:  %-*s  At: %-25s  %s\r\nFldr   : %-2d Status: %s\r\nSubject: %-65s",
							       i, PLAYER_NAME_LIMIT - 6, Name(mp->from),
							       timebuff,
						     (Connected(mp->from) &&
						      (!Hidden(mp->from) || Hasprivs(player))) ?
						    " (Conn)" : "      ", 0,
							  status, subbuff));
#else
					strcpy(msgbuff, get_mail_message(mp->number));
					msg = bp = alloc_lbuf("do_mail_review");
					str = msgbuff;
					exec(msg, &bp, 0, player, player, EV_EVAL | EV_FCHECK | EV_NO_COMPRESS, &str,
					     (char **)NULL, 0);
					*bp = '\0';
					notify(player, DASH_LINE);
					notify(player, tprintf("%-3d         From:  %-*s  At: %-25s  %s\r\nFldr   : %-2d Status: %s\r\nSubject: %-65s",
							       i, PLAYER_NAME_LIMIT - 6, Name(mp->from),
							       mp->time,
						     (Connected(mp->from) &&
						      (!Hidden(mp->from) || Hasprivs(player))) ?
						    " (Conn)" : "      ", 0,
						      status, mp->subject));
#endif /*
        * RADIX_COMPRESSION 
        */
					free_lbuf(status);
					notify(player, DASH_LINE);
					StringCopy(tbuf1, msg);
					notify(player, tbuf1);
					notify(player, DASH_LINE);
					free_lbuf(msg);
				}
			}
		}

		if (!j) {
			/*
			 * ran off the end of the list without finding * * *
			 * anything 
			 */
			notify(player, "MAIL: You don't have that many matching messages!");
		}
		free_lbuf(tbuf1);
	}
}

void do_mail_list(player, msglist, sub)
dbref player;
char *msglist;
int sub;
{
	struct mail *mp;
	struct mail_selector ms;
	int i = 0, folder;
	char *time;

	if (!parse_msglist(msglist, &ms, player)) {
		return;
	}
	folder = player_folder(player);

	notify(player, tprintf(
				      "---------------------------   MAIL: Folder %d   ----------------------------", folder));

	for (mp = (struct mail *)nhashfind((int)player, &mudstate.mail_htab);
	     mp; mp = mp->next) {
		if (Folder(mp) == folder) {
			i++;
			if (mail_match(mp, ms, i)) {
				/*
				 * list it 
				 */

#ifdef RADIX_COMPRESSION
				string_decompress(get_mail_message(mp->number), msgbuff);
				string_decompress(mp->subject, subbuff);
				string_decompress(mp->time, timebuff);
				time = mail_list_time(timebuff);
				if (sub)
					notify(player, tprintf("[%s] %-3d (%4d) From: %-*s Sub: %.25s",
							   status_chars(mp),
							 i, strlen(msgbuff),
							       PLAYER_NAME_LIMIT - 6, Name(mp->from),
							       subbuff));
				else
					notify(player, tprintf("[%s] %-3d (%4d) From: %-*s At: %s %s",
							   status_chars(mp),
							 i, strlen(msgbuff),
							       PLAYER_NAME_LIMIT - 6, Name(mp->from),
							       time,
						    ((Connected(mp->from) &&
						      (!Hidden(mp->from) || Hasprivs(player)))
						     ? "Conn" : " ")));
#else
				time = mail_list_time(mp->time);
				if (sub)
					notify(player, tprintf("[%s] %-3d (%4d) From: %-*s Sub: %.25s",
							   status_chars(mp),
							       i, strlen(get_mail_message(mp->number)),
							       PLAYER_NAME_LIMIT - 6, Name(mp->from),
							       mp->subject));
				else
					notify(player, tprintf("[%s] %-3d (%4d) From: %-*s At: %s %s",
							   status_chars(mp),
							       i, strlen(get_mail_message(mp->number)),
							       PLAYER_NAME_LIMIT - 6, Name(mp->from),
							       time,
						    ((Connected(mp->from) &&
						      (!Hidden(mp->from) || Hasprivs(player)))
						     ? "Conn" : " ")));
#endif /*
        * RADIX_COMPRESSION 
        */
				free_lbuf(time);
			}
		}
	}
	notify(player, DASH_LINE);
}

static char *mail_list_time(the_time)
const char *the_time;
{
	char *new;
	char *p, *q;
	int i;

	p = (char *)the_time;
	q = new = alloc_lbuf("mail_list_time");
	if (!p || !*p) {
		*new = '\0';
		return new;
	}
	/*
	 * Format of the_time is: day mon dd hh:mm:ss yyyy 
	 */
	/*
	 * Chop out :ss 
	 */
	for (i = 0; i < 16; i++) {
		if (*p)
			*q++ = *p++;
	}

	for (i = 0; i < 3; i++) {
		if (*p)
			p++;
	}

	for (i = 0; i < 5; i++) {
		if (*p)
			*q++ = *p++;
	}

	*q = '\0';
	return new;
}

void do_mail_purge(player)
dbref player;
{
	struct mail *mp, *nextp;

	/*
	 * Go through player's mail, and remove anything marked cleared 
	 */
	for (mp = (struct mail *)nhashfind((int)player, &mudstate.mail_htab);
	     mp; mp = nextp) {
		if (Cleared(mp)) {
			/*
			 * Delete this one 
			 */
			/*
			 * head and tail of the list are special 
			 */
			if (mp->prev == NULL)
				nhashrepl((int)player, (int *)mp->next, &mudstate.mail_htab);
			else if (mp->next == NULL)
				mp->prev->next = NULL;

			/*
			 * relink the list 
			 */
			if (mp->prev != NULL)
				mp->prev->next = mp->next;
			if (mp->next != NULL)
				mp->next->prev = mp->prev;

			/*
			 * save the pointer 
			 */
			nextp = mp->next;

			/*
			 * then wipe 
			 */
			free((char *)mp->subject);
			delete_mail_message(mp->number);
			free((char *)mp->time);
			free((char *)mp->tolist);
			free(mp);
		} else {
			nextp = mp->next;
		}
	}
	notify(player, "MAIL: Mailbox purged.");
}

void do_mail_fwd(player, msg, tolist)
dbref player;
char *msg;
char *tolist;
{
	struct mail *mp;
	struct mail *temp;
	int num;
	dbref target;

	if (!msg || !*msg) {
		notify(player, "MAIL: No message list.");
		return;
	}
	if (!tolist || !*tolist) {
		notify(player, "MAIL: To whom should I forward?");
		return;
	}
	num = atoi(msg);
	if (!num) {
		notify(player, "MAIL: I don't understand that message number.");
		return;
	}
	mp = mail_fetch(player, num);
	if (!mp) {
		notify(player, "MAIL: You can't forward non-existent messages.");
		return;
	}
#ifdef RADIX_COMPRESSION
	string_decompress(mp->subject, subbuff);
	string_decompress(get_mail_message(mp->number), msgbuff);
	do_expmail_start(player, tolist, tprintf("%s (fwd from %s)", subbuff, Name(mp->from)));
	atr_add_raw(player, A_MAILMSG, msgbuff);
#else
	do_expmail_start(player, tolist, tprintf("%s (fwd from %s)", mp->subject, Name(mp->from)));
	atr_add_raw(player, A_MAILMSG, get_mail_message(mp->number));
#endif
	atr_add_raw(player, A_MAILFLAGS, tprintf("%d", (atoi(atr_get_raw(player, A_MAILFLAGS)) | M_FORWARD)));
}

/*-------------------------------------------------------------------------*
 *   Admin mail functions
 *
 * do_mail_nuke - clear & purge mail for a player, or all mail in db.
 * do_mail_stat - stats on mail for a player, or for all db.
 * do_mail_debug - fix mail with a sledgehammer
 *-------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*
 *   Basic mail functions
 *-------------------------------------------------------------------------*/
struct mail *mail_fetch(player, num)
dbref player;
int num;
{
	struct mail *mp;
	int i = 0;

	for (mp = (struct mail *)nhashfind((int)player, &mudstate.mail_htab);
	     mp; mp = mp->next) {
		if (Folder(mp) == player_folder(player)) {
			i++;
			if (i == num)
				return mp;
		}
	}
	return NULL;
}

/*
 * returns count of read, unread, & cleared messages as rcount, ucount,
 * * ccount 
 */

void count_mail(player, folder, rcount, ucount, ccount)
dbref player;
int folder;
int *rcount;
int *ucount;
int *ccount;
{
	struct mail *mp;
	int rc, uc, cc;

	cc = rc = uc = 0;
	for (mp = (struct mail *)nhashfind((int)player, &mudstate.mail_htab);
	     mp; mp = mp->next) {
		if (Folder(mp) == folder) {
			if (Read(mp))
				rc++;
			else
				uc++;

			if (Cleared(mp))
				cc++;
		}
	}
	*rcount = rc;
	*ucount = uc;
	*ccount = cc;
}

void urgent_mail(player, folder, ucount)
dbref player;
int folder;
int *ucount;
{
	struct mail *mp;
	int uc;

	uc = 0;

	for (mp = (struct mail *)nhashfind((int)player, &mudstate.mail_htab);
	     mp; mp = mp->next) {
		if (Folder(mp) == folder) {
			if (!(Read(mp)) && (Urgent(mp)))
				uc++;
		}
	}
	*ucount = uc;
}

static void send_mail(player, target, tolist, subject, number, flags, silent)
dbref player;
dbref target;
const char *tolist;
const char *subject;
int number;
mail_flag flags;
int silent;
{
	struct mail *newp;
	struct mail *mp;
	time_t tt;
	char tbuf1[30];
	int aflags, len;
	char *atrstr;

	if (Typeof(target) != TYPE_PLAYER) {
		notify(player, "MAIL: You cannot send mail to non-existent people.");
		delete_mail_message(number);
		return;
	}
	tt = time(NULL);
	StringCopy(tbuf1, ctime(&tt));
	tbuf1[strlen(tbuf1) - 1] = '\0';	/*
						 * whack the newline 
						 */

	/*
	 * initialize the appropriate fields 
	 */
	newp = (struct mail *)malloc(sizeof(struct mail));

	newp->to = target;
	newp->from = player;
	newp->tolist = (char *)strdup((char *)tolist);

	newp->number = number;
	add_count(number);

#ifdef RADIX_COMPRESSION
	len = string_compress(tbuf1, timebuff);
	newp->time = (char *)strndup(timebuff, len);
	len = string_compress(subject, subbuff);
	newp->subject = (char *)strndup(subbuff, len);
#else
	newp->time = (char *)strdup(tbuf1);
	newp->subject = (char *)strdup(subject);
#endif /*
        * RADIX_COMPRESSION 
        */
	newp->read = flags & M_FMASK;	/*
					 * Send to folder 0 
					 */

	/*
	 * if this is the first message, it is the head and the tail 
	 */
	if (!nhashfind((int)target, &mudstate.mail_htab)) {
		nhashadd((int)target, (int *)newp, &mudstate.mail_htab);
		newp->next = NULL;
		newp->prev = NULL;
	} else {
		for (mp = (struct mail *)nhashfind((int)target, &mudstate.mail_htab);
		     mp->next; mp = mp->next) ;

		mp->next = newp;
		newp->next = NULL;
		newp->prev = mp;
	}

	/*
	 * notify people 
	 */
	if (!silent)
		notify(player, tprintf("MAIL: You sent your message to %s.", Name(target)));

	notify(target, tprintf("MAIL: You have a new message from %s.",
			       Name(player)));
	did_it(player, target, A_MAIL, NULL, (int)NULL, NULL, A_AMAIL, NULL, NOTHING);
	return;
}

void do_mail_nuke(player)
dbref player;
{
	struct mail *mp, *nextp;
	dbref thing;

	if (!God(player)) {
		notify(player, "The postal service issues a warrant for your arrest.");
		return;
	}
	/*
	 * walk the list 
	 */
	MAIL_ITER_SAFE(mp, thing, nextp) {
		nextp = mp->next;
		delete_mail_message(mp->number);
		free((char *)mp->subject);
		free((char *)mp->tolist);
		free((char *)mp->time);
		free(mp);
	}

	log_text(tprintf("** MAIL PURGE ** done by %s(#%d).",
			 Name(player), (int)player));

	notify(player, "You annihilate the post office. All messages cleared.");
}

void do_mail_debug(player, action, victim)
dbref player;
char *action;
char *victim;
{
	dbref target, thing;
	struct mail *mp, *nextp, *mptr;
	int i;

	if (!Wizard(player)) {
		notify(player, "Go get some bugspray.");
		return;
	}
	if (string_prefix("clear", action)) {
		target = lookup_player(player, victim, 1);
		if (target == NOTHING) {
			init_match(player, victim, NOTYPE);
			match_absolute();
			target = match_result();
		}
		if (target == NOTHING) {
			notify(player, tprintf("%s: no such player.", victim));
			return;
		}
		do_mail_clear(target, NULL);
		do_mail_purge(target);
		notify(player, tprintf("Mail cleared for %s(#%d).", Name(target), target));
		return;
	} else if (string_prefix("sanity", action)) {
		MAIL_ITER_ALL(mp, thing) {
			if (!Good_obj(mp->to))
				notify(player, tprintf("Bad object #%d has mail.", mp->to));
			else if (Typeof(mp->to) != TYPE_PLAYER)
				notify(player, tprintf("%s(#%d) has mail but is not a player.",
						     Name(mp->to), mp->to));
		}
		notify(player, "Mail sanity check completed.");
	} else if (string_prefix("fix", action)) {
		MAIL_ITER_SAFE(mp, thing, nextp) {
			if (!Good_obj(mp->to) || (Typeof(mp->to) != TYPE_PLAYER)) {
				notify(player, tprintf("Fixing mail for #%d.", mp->to));
				/*
				 * Delete this one 
				 */
				/*
				 * head and tail of the list are * special 
				 */
				if (mp->prev == NULL)
					nhashrepl((int)player, (int *)mp->next, &mudstate.mail_htab);
				else if (mp->next == NULL)
					mp->prev->next = NULL;
				/*
				 * relink the list 
				 */
				if (mp->prev != NULL)
					mp->prev->next = mp->next;
				if (mp->next != NULL)
					mp->next->prev = mp->prev;
				/*
				 * save the pointer 
				 */
				nextp = mp->next;
				/*
				 * then wipe 
				 */
				free((char *)mp->subject);
				delete_mail_message(mp->number);
				free((char *)mp->time);
				free((char *)mp->tolist);
				free(mp);
			} else
				nextp = mp->next;
		}
		notify(player, "Mail sanity fix completed.");
	} else {
		notify(player, "That is not a debugging option.");
		return;
	}
}

void do_mail_stats(player, name, full)
dbref player;
char *name;
int full;
{
	dbref target, thing;
	int fc, fr, fu, tc, tr, tu, fchars, tchars, cchars, count, len;
	char last[50];
	struct mail *mp, *mptr;

	fc = fr = fu = tc = tr = tu = cchars = fchars = tchars = count = 0;

	/*
	 * find player 
	 */

	if ((*name == '\0') || !name) {
		if Wizard
			(player)
				target = AMBIGUOUS;
		else
			target = player;
	} else if (*name == NUMBER_TOKEN) {
		target = atoi(&name[1]);
		if (!Good_obj(target) || (Typeof(target) != TYPE_PLAYER))
			target = NOTHING;
	} else if (!strcasecmp(name, "me")) {
		target = player;
	} else {
		target = lookup_player(player, name, 1);
	}

	if (target == NOTHING) {
		init_match(player, name, NOTYPE);
		match_absolute();
		target = match_result();
	}
	if (target == NOTHING) {
		notify(player, tprintf("%s: No such player.", name));
		return;
	}
	if (!Wizard(player) && (target != player)) {
		notify(player, "The post office protects privacy!");
		return;
	}
	/*
	 * this comand is computationally expensive 
	 */

	if (!payfor(player, mudconf.searchcost)) {
		notify(player, tprintf("Finding mail stats costs %d %s.",
				       mudconf.searchcost,
				       (mudconf.searchcost == 1) ? mudconf.one_coin : mudconf.many_coins));
		return;
	}
	if (target == AMBIGUOUS) {	/*
					 * stats for all 
					 */
		if (full == 0) {
			MAIL_ITER_ALL(mp, thing) {
				count++;
			}
			notify(player,
			       tprintf("There are %d messages in the mail spool.", count));
			return;
		} else if (full == 1) {
			MAIL_ITER_ALL(mp, thing) {
				if (Cleared(mp))
					fc++;
				else if (Read(mp))
					fr++;
				else
					fu++;
			}
			notify(player,
			       tprintf("MAIL: There are %d msgs in the mail spool, %d unread, %d cleared.",
				       fc + fr + fu, fu, fc));
			return;
		} else {
			MAIL_ITER_ALL(mp, thing) {
#ifdef RADIX_COMPRESSION
				if (Cleared(mp)) {
					fc++;
					len = string_decompress(get_mail_message(mp->number), msgbuff);
					cchars += len;
				} else if (Read(mp)) {
					fr++;
					len = string_decompress(get_mail_message(mp->number), msgbuff);
					fchars += len;
				} else {
					fu++;
					len = string_decompress(get_mail_message(mp->number), msgbuff);
					tchars += len;
				}
#else
				if (Cleared(mp)) {
					fc++;
					cchars += strlen(get_mail_message(mp->number));
				} else if (Read(mp)) {
					fr++;
					fchars += strlen(get_mail_message(mp->number));
				} else {
					fu++;
					tchars += strlen(get_mail_message(mp->number));
				}
#endif /*
        * RADIX_COMPRESSION 
        */
			}
			notify(player, tprintf("MAIL: There are %d old msgs in the mail spool, totalling %d characters.", fr, fchars));
			notify(player, tprintf("MAIL: There are %d new msgs in the mail spool, totalling %d characters.", fu, tchars));
			notify(player, tprintf("MAIL: There are %d cleared msgs in the mail spool, totalling %d characters.", fc, cchars));
			return;
		}
	}
	/*
	 * individual stats 
	 */

	if (full == 0) {
		/*
		 * just count number of messages 
		 */
		MAIL_ITER_ALL(mp, thing) {
			if (mp->from == target)
				fr++;
			if (mp->to == target)
				tr++;
		}
		notify(player, tprintf("%s sent %d messages.",
				       Name(target), fr));
		notify(player, tprintf("%s has %d messages.", Name(target), tr));
		return;
	}
	/*
	 * more detailed message count 
	 */
	MAIL_ITER_ALL(mp, thing) {
		if (mp->from == target) {
			if (Cleared(mp))
				fc++;
			else if (Read(mp))
				fr++;
			else
				fu++;
			if (full == 2)
#ifdef RADIX_COMPRESSION
				len = string_compress(get_mail_message(mp->number), msgbuff);
			fchars += len;
#else
				fchars += strlen(get_mail_message(mp->number));
#endif /*
        * RADIX_COMPRESSION 
        */
		}
		if (mp->to == target) {
			if (!tr && !tu) {
#ifdef RADIX_COMPRESSION
				string_decompress(mp->time, timebuff);
				StringCopy(last, timebuff);
#else
				StringCopy(last, mp->time);
#endif /*
        * RADIX_COMPRESSION 
        */
			}
			if (Cleared(mp))
				tc++;
			else if (Read(mp))
				tr++;
			else
				tu++;
			if (full == 2) {
#ifdef RADIX_COMPRESSION
				len = string_decompress(get_mail_message(mp->number), msgbuff);
				tchars += len;
#else
				tchars += strlen(get_mail_message(mp->number));
#endif /*
        * RADIX_COMPRESSION 
        */
			}
		}
	}

	notify(player, tprintf("Mail statistics for %s:", Name(target)));

	if (full == 1) {
		notify(player, tprintf("%d messages sent, %d unread, %d cleared.",
				       fc + fr + fu, fu, fc));
		notify(player, tprintf("%d messages received, %d unread, %d cleared.",
				       tc + tr + tu, tu, tc));
	} else {
		notify(player,
		       tprintf("%d messages sent, %d unread, %d cleared, totalling %d characters.",
			       fc + fr + fu, fu, fc, fchars));
		notify(player,
		       tprintf("%d messages received, %d unread, %d cleared, totalling %d characters.",
			       tc + tr + tu, tu, tc, tchars));
	}

	if (tc + tr + tu > 0)
		notify(player, tprintf("Last is dated %s", last));
	return;
}


/*-------------------------------------------------------------------------*
 *   Main mail routine for @mail w/o a switch
 *-------------------------------------------------------------------------*/

void do_mail_stub(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{

	if (Typeof(player) != TYPE_PLAYER) {
		notify(player, "MAIL: Only players may send and receive mail.");
		return;
	}
	if (!arg1 || !*arg1) {
		if (arg2 && *arg2) {
			notify(player, "MAIL: Invalid mail command.");
			return;
		}
		/*
		 * just the "@mail" command 
		 */
		do_mail_list(player, arg1, 1);
		return;
	}
	/*
	 * purge a player's mailbox 
	 */
	if (!strcasecmp(arg1, "purge")) {
		do_mail_purge(player);
		return;
	}
	/*
	 * clear message 
	 */
	if (!strcasecmp(arg1, "clear")) {
		do_mail_clear(player, arg2);
		return;
	}
	if (!strcasecmp(arg1, "unclear")) {
		do_mail_unclear(player, arg2);
		return;
	}
	if (arg2 && *arg2) {
		/*
		 * Sending mail 
		 */
		do_expmail_start(player, arg1, arg2);
		return;
	} else {
		/*
		 * Must be reading or listing mail - no arg2 
		 */
		if (isdigit(*arg1) && !index(arg1, '-'))
			do_mail_read(player, arg1);
		else
			do_mail_list(player, arg1, 1);
		return;
	}


}


void do_mail(player, cause, key, arg1, arg2)
dbref player, cause;
int key;
char *arg1;
char *arg2;
{
	if (!mudconf.have_mailer) {
		notify(player, "Mailer is disabled.");
		return;
	}
	switch (key) {
	case 0:
		do_mail_stub(player, arg1, arg2);
		break;
	case MAIL_STATS:
		do_mail_stats(player, arg1, 0);
		break;
	case MAIL_DSTATS:
		do_mail_stats(player, arg1, 1);
		break;
	case MAIL_FSTATS:
		do_mail_stats(player, arg1, 2);
		break;
	case MAIL_DEBUG:
		do_mail_debug(player, arg1, arg2);
		break;
	case MAIL_NUKE:
		do_mail_nuke(player);
		break;
	case MAIL_FOLDER:
		do_mail_change_folder(player, arg1, arg2);
		break;
	case MAIL_LIST:
		do_mail_list(player, arg1, 0);
		break;
	case MAIL_READ:
		do_mail_read(player, arg1);
		break;
	case MAIL_CLEAR:
		do_mail_clear(player, arg1);
		break;
	case MAIL_UNCLEAR:
		do_mail_unclear(player, arg1);
		break;
	case MAIL_PURGE:
		do_mail_purge(player);
		break;
	case MAIL_FILE:
		do_mail_file(player, arg1, arg2);
		break;
	case MAIL_TAG:
		do_mail_tag(player, arg1);
		break;
	case MAIL_UNTAG:
		do_mail_untag(player, arg1);
		break;
	case MAIL_FORWARD:
		do_mail_fwd(player, arg1, arg2);
		break;
	case MAIL_SEND:
		do_expmail_stop(player, 0);
		break;
	case MAIL_EDIT:
		do_edit_msg(player, arg1, arg2);
		break;
	case MAIL_URGENT:
		do_expmail_stop(player, M_URGENT);
		break;
	case MAIL_ALIAS:
		do_malias_create(player, arg1, arg2);
		break;
	case MAIL_ALIST:
		do_malias_list_all(player);
		break;
	case MAIL_PROOF:
		do_mail_proof(player);
		break;
	case MAIL_ABORT:
		do_expmail_abort(player);
		break;
	case MAIL_QUICK:
		do_mail_quick(player, arg1, arg2);
		break;
	case MAIL_REVIEW:
		do_mail_review(player, arg1, arg2);
		break;
	case MAIL_RETRACT:
		do_mail_retract(player, arg1, arg2);
		break;
	case MAIL_CC:
		do_mail_cc(player, arg1);
		break;
	case MAIL_SAFE:
		do_mail_safe(player, arg1);
		break;
	}
}

int dump_mail(fp)
FILE *fp;
{
	struct mail *mp, *mptr;
	dbref thing;
	int count = 0, i;

	/*
	 * Write out version number 
	 */
	fprintf(fp, "+V5\n");
	putref(fp, mudstate.mail_db_top);
	DO_WHOLE_DB(thing) {
		if (isPlayer(thing)) {
			mptr = (struct mail *)nhashfind((int)thing, &mudstate.mail_htab);
			if (mptr != NULL)
				for (mp = mptr; mp; mp = mp->next) {
					putref(fp, mp->to);
					putref(fp, mp->from);
					putref(fp, mp->number);
					putstring(fp, mp->tolist);
#ifdef RADIX_COMPRESSION
					string_decompress(mp->time, timebuff);
					string_decompress(mp->subject, subbuff);
					putstring(fp, timebuff);
					putstring(fp, subbuff);
#else
					putstring(fp, mp->time);
					putstring(fp, mp->subject);
#endif /*
        * RADIX_COMPRESSION 
        */
					putref(fp, mp->read);
					count++;
				}
		}
	}

	fprintf(fp, "*** END OF DUMP ***\n");

	/*
	 * Add the db of mail messages 
	 */
	for (i = 0; i < mudstate.mail_db_top; i++) {
		if (mudstate.mail_list[i].count > 0) {
			putref(fp, i);
#ifdef RADIX_COMPRESSION
			string_decompress(get_mail_message(i),msgbuff);
			putstring(fp, msgbuff);
#else
			putstring(fp, get_mail_message(i));
#endif
		}
	}
	fprintf(fp, "+++ END OF DUMP +++\n");

	save_malias(fp);
	fflush(fp);

	return (count);
}

int load_mail(fp)
FILE *fp;
{
	char nbuf1[8];
	int mail_top = 0;
	int new = 0;
	int pennsub = 0;
	int read_tolist = 0;
	int read_newdb = 0;
	int read_new_strings = 0;
	int i = 0;
	int number = 0;
	int len;
	struct mail *mp, *mptr, *prevp;

	/*
	 * Read the version number 
	 */
	fgets(nbuf1, sizeof(nbuf1), fp);

	if (!strncmp(nbuf1, "+V2", 3)) {
		new = 1;
	} else if (!strncmp(nbuf1, "+V3", 3)) {
		new = 1;
		read_tolist = 1;
	} else if (!strncmp(nbuf1, "+V4", 3)) {
		new = 1;
		read_tolist = 1;
		read_newdb = 1;
	} else if (!strncmp(nbuf1, "+V5", 3)) {
		new = 1;
		read_tolist = 1;
		read_newdb = 1;
		read_new_strings = 1;
	} else if (!strncmp(nbuf1, "+1", 2)) {
		pennsub = 1;
	}
	if (pennsub)
		fgets(nbuf1, sizeof(nbuf1), fp);	/*
							 * Toss away the * *
							 * * number of
							 * messages  *  * *  
							 */
	if (read_newdb) {
		mail_top = getref(fp);
		mail_db_grow(mail_top + 1);
	} else {
		mail_db_grow(1);
	}

	fgets(nbuf1, sizeof(nbuf1), fp);

	while (strncmp(nbuf1, "***", 3)) {
		mp = (struct mail *)malloc(sizeof(struct mail));

		mp->to = atoi(nbuf1);

		if (!nhashfind((int)mp->to, &mudstate.mail_htab)) {
			nhashadd((int)mp->to, (int *)mp, &mudstate.mail_htab);
			mp->prev = NULL;
			mp->next = NULL;
		} else {
			for (mptr = (struct mail *)nhashfind((int)mp->to, &mudstate.mail_htab);
			     mptr->next != NULL; mptr = mptr->next) ;
			mptr->next = mp;
			mp->prev = mptr;
			mp->next = NULL;
		}

		mp->from = getref(fp);

		if (read_newdb) {
			mp->number = getref(fp);
			add_count(mp->number);
		}
		if (read_tolist)
			mp->tolist = (char *)strdup(getstring_noalloc(fp, read_new_strings));
		else
			mp->tolist = (char *)strdup(tprintf("%d", mp->to));

#ifdef RADIX_COMPRESSION
		len = string_compress(getstring_noalloc(fp, read_new_strings), timebuff);
		mp->time = (char *)strndup(timebuff, len);
		if (pennsub) {
			len = string_compress(getstring_noalloc(fp, read_new_strings), subbuff);
			mp->subject = (char *)strndup(subbuff, len);
		} else if (!new) {
			len = string_compress("No subject", subbuff);
			mp->subject = (char *)strndup(subbuff, len);
		}
		if (!read_newdb) {
			string_compress(getstring_noalloc(fp, read_new_strings), msgbuff);
			number = add_mail_message_nosig(msgbuff);
			add_count(number);
			mp->number = number;
		}
		if (new) {
			len = string_compress(getstring_noalloc(fp, read_new_strings), subbuff);
			mp->subject = (char *)strndup(subbuff, len);
		} else if (!pennsub) {
			len = string_compress("No subject", subbuff);
			mp->subject = (char *)strndup(subbuff, len);
		}
#else
		mp->time = (char *)strdup(getstring_noalloc(fp, read_new_strings));
		if (pennsub)
			mp->subject = (char *)strdup(getstring_noalloc(fp, read_new_strings));
		else if (!new)
			mp->subject = (char *)strdup("No subject");

		if (!read_newdb) {
			number = add_mail_message_nosig(getstring_noalloc(fp, read_new_strings));
			add_count(number);
			mp->number = number;
		}
		if (new)
			mp->subject = (char *)strdup(getstring_noalloc(fp, read_new_strings));
		else if (!pennsub)
			mp->subject = (char *)strdup("No subject");
#endif /*
        * RADIX_COMPRESSION 
        */
		mp->read = getref(fp);
		fgets(nbuf1, sizeof(nbuf1), fp);
	}

	if (read_newdb) {
		fgets(nbuf1, sizeof(nbuf1), fp);

		while (strncmp(nbuf1, "+++", 3)) {
			number = atoi(nbuf1);
#ifdef RADIX_COMPRESSION
			len = string_compress(getstring_noalloc(fp, read_new_strings), msgbuff);
			new_mail_message(msgbuff, number, len);
#else
			new_mail_message(getstring_noalloc(fp, read_new_strings), number);
#endif
			fgets(nbuf1, sizeof(nbuf1), fp);
		}
	}
	load_malias(fp);
	return (1);
}

static int get_folder_number(player, name)
dbref player;
char *name;
{
	int aflags;
	dbref aowner;
	char *atrstr;
	char *str, *pat, *res, *p, *bp;

	/* Look up a folder name and return the appopriate number */
	
	atrstr = atr_get(player, A_MAILFOLDERS, &aowner, &aflags);
	if (!*atrstr) {
		free_lbuf(atrstr);
		return -1;
	}
	str = alloc_lbuf("get_folder_num_str");
	bp = pat = alloc_lbuf("get_folder_num_pat");

	strcpy(str, atrstr);
	safe_tprintf_str(pat, &bp, ":%s:", upcasestr(name));
	res = (char *)strstr(str, pat);
	if (!res) {
		free_lbuf(str);
		free_lbuf(pat);
		free_lbuf(atrstr);
		return -1;
	}
	res += 2 + strlen(name);
	p = res;
	while (!isspace(*p))
		p++;
	p = '\0';
	free_lbuf(atrstr);
	free_lbuf(str);
	free_lbuf(pat);
	return atoi(res);
}

static char *get_folder_name(player, fld)
dbref player;
int fld;
{
	static char str[LBUF_SIZE];
	char *pat;
	static char *old;
	char *r, *atrstr;
	int flags, len;

	/*
	 * Get the name of the folder, or "nameless" 
	 */
	pat = alloc_lbuf("get_folder_name");
	sprintf(pat, "%d:", fld);
	old = NULL;
	atrstr = atr_get(player, A_MAILFOLDERS, &player, &flags);
	if (!*atrstr) {
		StringCopy(str, "unnamed");
		free_lbuf(pat);
		free_lbuf(atrstr);
		return str;
	}
	StringCopy(str, atrstr);
	old = (char *)string_match(str, pat);
	free_lbuf(atrstr);
	if (old) {
		r = old + strlen(pat);
		while (*r != ':')
			r++;
		*r = '\0';
		len = strlen(pat);
		free_lbuf(pat);
		return old + len;
	} else {
		StringCopy(str, "unnamed");
		free_lbuf(pat);
		return str;
	}
}

void add_folder_name(player, fld, name)
dbref player;
int fld;
char *name;
{
	char *old, *res, *r, *atrstr;
	char *new, *pat, *str, *tbuf;
	int atr, aflags;

	/*
	 * Muck with the player's MAILFOLDERS attrib to add a string of the * 
	 * 
	 * *  * * form: number:name:number to it, replacing any such string
	 * with a  *  * * * matching number. 
	 */

	new = alloc_lbuf("add_folder_name.new");
	pat = alloc_lbuf("add_folder_name.pat");
	str = alloc_lbuf("add_folder_name.str");
	tbuf = alloc_lbuf("add_folder_name.tbuf");

	sprintf(new, "%d:%s:%d ", fld, upcasestr(name), fld);
	sprintf(pat, "%d:", fld);
	/*
	 * get the attrib and the old string, if any 
	 */
	old = NULL;

	atrstr = atr_get(player, A_MAILFOLDERS, &player, &aflags);
	if (*atrstr) {
		StringCopy(str, atrstr);
		old = (char *)string_match(str, pat);
	}
	if (old && *old) {
		StringCopy(tbuf, str);
		r = old;
		while (!isspace(*r))
			r++;
		*r = '\0';
		res = (char *)replace_string(old, new, tbuf);
	} else {
		r = res = alloc_lbuf("mail_folders");
		if (*atrstr)
			safe_str(str, res, &r);
		safe_str(new, res, &r);
		*r = '\0';
	}
	/*
	 * put the attrib back 
	 */
	atr_add(player, A_MAILFOLDERS, res, player,
		AF_MDARK | AF_WIZARD | AF_NOPROG | AF_LOCK);
	free_lbuf(str);
	free_lbuf(pat);
	free_lbuf(new);
	free_lbuf(tbuf);
	free_lbuf(atrstr);
	free_lbuf(res);
}

static int player_folder(player)
dbref player;
{
	/*
	 * Return the player's current folder number. If they don't have one, 
	 * 
	 * *  * *  * * set * it to 0 
	 */
	int flags, number;
	char *atrstr;

	atrstr = atr_pget(player, A_MAILCURF, &player, &flags);
	if (!*atrstr) {
		free_lbuf(atrstr);
		set_player_folder(player, 0);
		return 0;
	}
	number = atoi(atrstr);
	free_lbuf(atrstr);
	return number;
}

void set_player_folder(player, fnum)
dbref player;
int fnum;
{
	/*
	 * Set a player's folder to fnum 
	 */
	ATTR *a, *attr;
	char *tbuf1;
	int atr;

	tbuf1 = alloc_lbuf("set_player_folder");
	sprintf(tbuf1, "%d", fnum);
	a = (ATTR *) atr_num(A_MAILCURF);
	if (a)
		atr_add(player, A_MAILCURF, tbuf1, GOD, a->flags);
	else {			/*
				 * Shouldn't happen, but... 
				 */
		atr_add(player, A_MAILCURF, tbuf1, GOD, AF_ODARK | AF_WIZARD | AF_NOPROG | AF_LOCK);
	}
	free_lbuf(tbuf1);
}

static int parse_folder(player, folder_string)
dbref player;
char *folder_string;
{
	int fnum;

	/*
	 * Given a string, return a folder #, or -1 The string is just a * *
	 * * number, * for now. Later, this will be where named folders are * 
	 * *  * handled 
	 */
	if (!folder_string || !*folder_string)
		return -1;
	if (isdigit(*folder_string)) {
		fnum = atoi(folder_string);
		if ((fnum < 0) || (fnum > MAX_FOLDERS))
			return -1;
		else
			return fnum;
	}
	/*
	 * Handle named folders here 
	 */
	return get_folder_number(player, folder_string);
}

static int mail_match(mp, ms, num)
struct mail *mp;
struct mail_selector ms;
int num;
{
	time_t now, msgtime, diffdays;
	char *msgtimestr;
	struct tm *msgtm;
	mail_flag mpflag;

	/*
	 * Does a piece of mail match the mail_selector? 
	 */
	if (ms.low && num < ms.low)
		return 0;
	if (ms.high && num > ms.high)
		return 0;
	if (ms.player && mp->from != ms.player)
		return 0;
	mpflag = Read(mp) ? mp->read : (mp->read | M_MSUNREAD);
	if (!(ms.flags & M_ALL) && !(ms.flags & mpflag))
		return 0;
	if (ms.days != -1) {
		/*
		 * Get the time now, subtract mp->time, and compare the * * * 
		 * results with * ms.days (in manner of ms.day_comp) 
		 */
		time(&now);
		msgtm = (struct tm *)malloc(sizeof(struct tm));

		if (!msgtm) {
			/*
			 * Ugly malloc failure 
			 */
			log_text("MAIL: Couldn't malloc struct tm!");
			return 0;
		}
		msgtimestr = alloc_lbuf("mail_match");
#ifdef RADIX_COMPRESSION
		string_decompress(mp->time, timebuff);
		StringCopy(msgtimestr, timebuff);
#else
		StringCopy(msgtimestr, mp->time);
#endif /*
        * RADIX_COMPRESSION 
        */
		if (do_convtime(msgtimestr, msgtm)) {
			msgtime = timelocal(msgtm);
			free(msgtm);
			diffdays = (now - msgtime) / 86400;
			free_lbuf(msgtimestr);
			if (sign(diffdays - ms.days) != ms.day_comp) {
				return 0;
			}
		} else {
			free(msgtm);
			free_lbuf(msgtimestr);
			return 0;
		}
	}
	return 1;
}

static int parse_msglist(msglist, ms, player)
char *msglist;
struct mail_selector *ms;
dbref player;
{
	char *p, *q;
	char tbuf1[LBUF_SIZE];

	/*
	 * Take a message list, and return the appropriate mail_selector * *
	 * * setup. For * now, msglists are quite restricted. That'll change
	 * * * * once all this is * working. Returns 0 if couldn't parse, and
	 * * also * * notifys player of why. 
	 */
	/*
	 * Initialize the mail selector - this matches all messages 
	 */
	ms->low = 0;
	ms->high = 0;
	ms->flags = 0x0FFF | M_MSUNREAD;
	ms->player = 0;
	ms->days = -1;
	ms->day_comp = 0;
	/*
	 * Now, parse the message list 
	 */
	if (!msglist || !*msglist) {
		/*
		 * All messages 
		 */
		return 1;
	}
	/*
	 * Don't mess with msglist itself 
	 */
	StringCopyTrunc(tbuf1, msglist, LBUF_SIZE - 1);
	p = tbuf1;
	while (p && *p && isspace(*p))
		p++;
	if (!p || !*p) {
		return 1;	/*
				 * all messages 
				 */
	}
	if (isdigit(*p)) {
		/*
		 * Message or range 
		 */
		q = seek_char(p, '-');
		if (*q) {
			/*
			 * We have a subrange, split it up and test to see if 
			 * 
			 * *  * *  * * it is valid 
			 */
			*q++ = '\0';
			ms->low = atoi(p);
			if (ms->low <= 0) {
				notify(player, "MAIL: Invalid message range");
				return 0;
			}
			if (!q || !*q) {
				/*
				 * Unbounded range 
				 */
				ms->high = 0;
			} else {
				ms->high = atoi(q);
				if ((ms->low > ms->high) || (ms->high <= 0)) {
					notify(player, "MAIL: Invalid message range");
					return 0;
				}
			}
		} else {
			/*
			 * A single message 
			 */
			ms->low = ms->high = atoi(p);
		}
	} else {
		switch (*p) {
		case '-':	/*
				 * Range with no start 
				 */
			p++;
			if (!p || !*p) {
				notify(player, "MAIL: Invalid message range");
				return 0;
			}
			ms->high = atoi(p);
			if (ms->high <= 0) {
				notify(player, "MAIL: Invalid message range");
				return 0;
			}
			break;
		case '~':	/*
				 * exact # of days old 
				 */
			p++;
			if (!p || !*p) {
				notify(player, "MAIL: Invalid age");
				return 0;
			}
			ms->day_comp = 0;
			ms->days = atoi(p);
			if (ms->days < 0) {
				notify(player, "MAIL: Invalid age");
				return 0;
			}
			break;
		case '<':	/*
				 * less than # of days old 
				 */
			p++;
			if (!p || !*p) {
				notify(player, "MAIL: Invalid age");
				return 0;
			}
			ms->day_comp = -1;
			ms->days = atoi(p);
			if (ms->days < 0) {
				notify(player, "MAIL: Invalid age");
				return 0;
			}
			break;
		case '>':	/*
				 * greater than # of days old 
				 */
			p++;
			if (!p || !*p) {
				notify(player, "MAIL: Invalid age");
				return 0;
			}
			ms->day_comp = 1;
			ms->days = atoi(p);
			if (ms->days < 0) {
				notify(player, "MAIL: Invalid age");
				return 0;
			}
			break;
		case '#':	/*
				 * From db# 
				 */
			p++;
			if (!p || !*p) {
				notify(player, "MAIL: Invalid dbref #");
				return 0;
			}
			ms->player = atoi(p);
			if (!Good_obj(ms->player) || !(ms->player)) {
				notify(player, "MAIL: Invalid dbref #");
				return 0;
			}
			break;
		case '*':	/*
				 * From player name 
				 */
			p++;
			if (!p || !*p) {
				notify(player, "MAIL: Invalid player");
				return 0;
			}
			ms->player = lookup_player(player, p, 1);
			if (ms->player == NOTHING) {
				notify(player, "MAIL: Invalid player");
				return 0;
			}
			break;
/*
 * case 'a':
 * * case 'A':            /* all messages, all folders 
 */
			ms->flags = M_ALL;
			break;	/*
				 * /* FIX THIS 
				 */
		case 'u':	/*
				 * Urgent, Unread 
				 */
		case 'U':
			p++;
			if (!p || !*p) {
				notify(player, "MAIL: U is ambiguous (urgent or unread?)");
				return 0;
			}
			switch (*p) {
			case 'r':	/*
					 * Urgent 
					 */
			case 'R':
				ms->flags = M_URGENT;
				break;
			case 'n':	/*
					 * Unread 
					 */
			case 'N':
				ms->flags = M_MSUNREAD;
				break;
			default:	/*
					 * bad 
					 */
				notify(player, "MAIL: Invalid message specification");
				return 0;
				break;
			}
			break;
		case 'r':	/*
				 * read 
				 */
		case 'R':
			ms->flags = M_ISREAD;
			break;
		case 'c':	/*
				 * cleared 
				 */
		case 'C':
			ms->flags = M_CLEARED;
			break;
		case 't':	/*
				 * tagged 
				 */
		case 'T':
			ms->flags = M_TAG;
			break;
		case 'm':
		case 'M':	/*
				 * Mass, me 
				 */
			p++;
			if (!p || !*p) {
				notify(player, "MAIL: M is ambiguous (mass or me?)");
				return 0;
			}
			switch (*p) {
			case 'a':
			case 'A':
				ms->flags = M_MASS;
				break;
			case 'e':
			case 'E':
				ms->player = player;
				break;
			default:
				notify(player, "MAIL: Invalid message specification");
				return 0;
				break;
			}
			break;
		default:	/*
				 * Bad news 
				 */
			notify(player, "MAIL: Invalid message specification");
			return 0;
			break;
		}
	}
	return 1;
}

void check_mail_expiration()
{
	struct mail *mp, *nextp;
	struct tm then_tm;
	time_t then, now = time(0);
	time_t expire_secs = mudconf.mail_expiration * 86400;
	dbref thing;

	/*
	 * negative values for expirations never expire 
	 */
	if (0 > mudconf.mail_expiration)
		return;

	MAIL_ITER_SAFE(mp, thing, nextp) {
#ifdef RADIX_COMPRESSION
		string_decompress(mp->time, timebuff);
		if (do_convtime(timebuff, &then_tm))
#else
		if (do_convtime(mp->time, &then_tm))
#endif
		{
			then = timelocal(&then_tm);
			if (((now - then) > expire_secs) && !(M_Safe(mp))) {
				/*
				 * Delete this one 
				 */
				/*
				 * head and tail of the list are special 
				 */
				if (mp->prev == NULL)
					nhashrepl((int)mp->to, (int *)mp->next, &mudstate.mail_htab);
				else if (mp->next == NULL)
					mp->prev->next = NULL;
				/*
				 * relink the list 
				 */
				if (mp->prev != NULL)
					mp->prev->next = mp->next;
				if (mp->next != NULL)
					mp->next->prev = mp->prev;
				/*
				 * save the pointer 
				 */
				nextp = mp->next;
				/*
				 * then wipe 
				 */
				free((char *)mp->subject);
				delete_mail_message(mp->number);
				free((char *)mp->tolist);
				free((char *)mp->time);
				free(mp);
			} else
				nextp = mp->next;
		} else
			nextp = mp->next;
	}
}

static char *status_chars(mp)
struct mail *mp;
{
	/*
	 * Return a short description of message flags 
	 */
	static char res[10];
	char *p;

	StringCopy(res, "");
	p = res;
	*p++ = Read(mp) ? '-' : 'N';
	*p++ = M_Safe(mp) ? 'S' : '-';
	*p++ = Cleared(mp) ? 'C' : '-';
	*p++ = Urgent(mp) ? 'U' : '-';
	*p++ = Mass(mp) ? 'M' : '-';
	*p++ = Forward(mp) ? 'F' : '-';
	*p++ = Tagged(mp) ? '+' : '-';
	*p = '\0';
	return res;
}

static char *status_string(mp)
struct mail *mp;
{
	/*
	 * Return a longer description of message flags 
	 */
	char *tbuf1;

	tbuf1 = alloc_lbuf("status_string");
	StringCopy(tbuf1, "");
	if (Read(mp))
		strcat(tbuf1, "Read ");
	else
		strcat(tbuf1, "Unread ");
	if (Cleared(mp))
		strcat(tbuf1, "Cleared ");
	if (Urgent(mp))
		strcat(tbuf1, "Urgent ");
	if (Mass(mp))
		strcat(tbuf1, "Mass ");
	if (Forward(mp))
		strcat(tbuf1, "Fwd ");
	if (Tagged(mp))
		strcat(tbuf1, "Tagged ");
	if (M_Safe(mp))
		strcat(tbuf1, "Safe");
	return tbuf1;
}

void check_mail(player, folder, silent)
dbref player;
int folder;
int silent;
{

	/*
	 * Check for new @mail 
	 */
	int rc;			/*

				 * read messages 
				 */
	int uc;			/*

				 * unread messages 
				 */
	int cc;			/*

				 * cleared messages 
				 */
	int gc;			/*

				 * urgent messages 
				 */

	/*
	 * just count messages 
	 */
	count_mail(player, folder, &rc, &uc, &cc);
	urgent_mail(player, folder, &gc);
#ifdef MAIL_ALL_FOLDERS
	notify(player,
	       tprintf("MAIL: %d messages in folder %d [%s] (%d unread, %d cleared).\r\n",
		 rc + uc, folder, get_folder_name(player, folder), uc, cc));
#else
	if (rc + uc > 0)
		notify(player,
		       tprintf("MAIL: %d messages in folder %d [%s] (%d unread, %d cleared).",
		 rc + uc, folder, get_folder_name(player, folder), uc, cc));
	else if (!silent)
		notify(player, tprintf("\r\nMAIL: You have no mail.\r\n"));
	if (gc > 0)
		notify(player, tprintf("URGENT MAIL: You have %d urgent messages in folder %d [%s].",
			      gc, folder, get_folder_name(player, folder)));
#endif
	return;
}

static int sign(x)
int x;
{
	if (x == 0) {
		return 0;
	} else if (x < 0) {
		return -1;
	} else {
		return 1;
	}
}


void do_malias_switch(player, a1, a2)
dbref player;
char *a1;
char *a2;
{
	if ((!a2 || !*a2) && !(!a1 || !*a1))
		do_malias_list(player, a1);
	else if ((!*a1 || !a1) && (!*a2 || !a2))
		do_malias_list_all(player);
	else
		do_malias_create(player, a1, a2);
}

void do_malias(player, cause, key, arg1, arg2)
dbref player, cause;
int key;
char *arg1;
char *arg2;
{
	if (!mudconf.have_mailer) {
		notify(player, "Mailer is disabled.");
		return;
	}
	switch (key) {
	case 0:
		do_malias_switch(player, arg1, arg2);
		break;
	case 1:
		do_malias_desc(player, arg1, arg2);
		break;
	case 2:
		do_malias_chown(player, arg1, arg2);
		break;
	case 3:
		do_malias_add(player, arg1, arg2);
		break;
	case 4:
		do_malias_remove(player, arg1, arg2);
		break;
	case 5:
		do_malias_delete(player, arg1);
		break;
	case 6:
		do_malias_rename(player, arg1, arg2);
		break;
	case 7:
		/*
		 * empty 
		 */
		break;
	case 8:
		do_malias_adminlist(player);
		break;
	case 9:
		do_malias_status(player);
	}
}

void do_malias_send(player, tolist, listto, subject, number, flags, silent)
dbref player;
char *tolist;
char *listto;
char *subject;
mail_flag flags;
int number, silent;
{
	char *list;
	int k;
	dbref vic;
	struct malias *m;

	if (!tolist || !*tolist) {
		notify(player, "MAIL: I can't figure out who you want to mail to.");
		return;
	}
	m = get_malias(player, tolist);

	if (!m) {
		notify(player, tprintf("MAIL: Mail alias %s not found.", tolist));
		return;
	}
	/*
	 * Parse the player list 
	 */

	for (k = 0; k < m->numrecep; k++) {
		vic = m->list[k];

		if (Typeof(vic) == TYPE_PLAYER) {
			send_mail(player, m->list[k], listto, subject, number, flags, silent);
		} else
			send_mail(GOD, GOD, listto, subject,	/*
								 * Complain 
								 * about it 
								 */
				  add_mail_message(player, tprintf("Alias Error: Bad Player %d for %s", (dbref)vic, tolist)),
				  0, silent);
	}
}

struct malias *get_malias(player, alias)
dbref player;
char *alias;
{
	char *mal;
	struct malias *m;
	int i = 0;
	int x = 0;

	if ((*alias == '#') && ExpMail(player)) {
		x = atoi(alias + 1);
		if (x < 0 || x >= ma_top)
			return NULL;
		else
			return malias[x];
	} else {
		if (*alias != '*')
			return NULL;

		mal = alias + 1;

		for (i = 0; i < ma_top; i++) {
			m = malias[i];
			if ((m->owner == player) || (m->owner == GOD)) {
				if (!strcmp(mal, m->name))	/*
								 * Found it! 
								 */
					return m;
			}
		}
	}
	return NULL;
}

void do_malias_create(player, alias, tolist)
dbref player;
char *alias;
char *tolist;
{
	char *head, *tail, spot;
	struct malias *m;
	struct malias **nm;
	char *na, *buff;
	int i = 0;
	dbref target;

	if (Typeof(player) != TYPE_PLAYER) {
		notify(player, "MAIL: Only players may create mail aliases.");
		return;
	}
	if (!alias || !*alias || !tolist || !*tolist) {
		notify(player, "MAIL: What alias do you want to create?.");
		return;
	}
	if (*alias != '*') {
		notify(player, "MAIL: All Mail aliases must begin with '*'.");
		return;
	}
	m = get_malias(player, alias);
	if (m) {
		notify(player,
		   tprintf("MAIL: Mail Alias '%s' already exists.", alias));
		return;
	}
	if (!ma_size) {
		ma_size = MA_INC;
		malias = (struct malias **)malloc(sizeof(struct malias *) * ma_size);
	} else if (ma_top >= ma_size) {
		ma_size += MA_INC;
		nm = (struct malias **)malloc(sizeof(struct malias *) * (ma_size));

		for (i = 0; i < ma_top; i++)
			nm[i] = malias[i];
		free(malias);
		malias = nm;
	}
	malias[ma_top] = (struct malias *)malloc(sizeof(struct malias));

	i = 0;

	/*
	 * Parse the player list 
	 */
	head = (char *)tolist;
	while (head && *head && (i < (MALIAS_LEN - 1))) {
		while (*head == ' ')
			head++;
		tail = head;
		while (*tail && (*tail != ' ')) {
			if (*tail == '"') {
				head++;
				tail++;
				while (*tail && (*tail != '"'))
					tail++;
			}
			if (*tail)
				tail++;
		}
		tail--;
		if (*tail != '"')
			tail++;
		spot = *tail;
		*tail = '\0';
		/*
		 * Now locate a target 
		 */
		if (!strcasecmp(head, "me"))
			target = player;
		else if (*head == '#') {
			target = atoi(head + 1);
			if (!Good_obj(target))
				target = NOTHING;
		} else
			target = lookup_player(player, head, 1);
		if ((target == NOTHING) || (Typeof(target) != TYPE_PLAYER)) {
			notify(player, "MAIL: No such player.");
		} else {
			buff = unparse_object(player, target, 0);
			notify(player,
			tprintf("MAIL: %s added to alias %s", buff, alias));
			malias[ma_top]->list[i] = target;
			i++;
			free_lbuf(buff);
		}
		/*
		 * Get the next recip 
		 */
		*tail = spot;
		head = tail;
		if (*head == '"')
			head++;
	}
	malias[ma_top]->list[i] = NOTHING;

	na = alias + 1;
	malias[ma_top]->name = (char *)malloc(strlen(na) + 1);
	malias[ma_top]->numrecep = i;
	malias[ma_top]->owner = player;
	StringCopy(malias[ma_top]->name, na);
	malias[ma_top]->desc = (char *)malloc(strlen(na) + 1);
	StringCopy(malias[ma_top]->desc, na);	/*
						 * For now do this. 
						 */
	ma_top++;


	notify(player, tprintf("MAIL: Alias set '%s' defined.", alias));
}

void do_malias_list(player, alias)
dbref player;
char *alias;
{
	struct malias *m;
	int i = 0;
	char *buff, *bp;

	m = get_malias(player, alias);

	if (!m) {
		notify(player, tprintf("MAIL: Alias '%s' not found.", alias));
		return;
	}
	if (!ExpMail(player) && (player != m->owner) && !(God(m->owner))) {
		notify(player, "MAIL: Permission denied.");
		return;
	}
	bp = buff = alloc_lbuf("do_malias_list");
	safe_tprintf_str(buff, &bp, "MAIL: Alias *%s: ", m->name);
	for (i = m->numrecep - 1; i > -1; i--) {
		safe_str(Name(m->list[i]), buff, &bp);
		safe_chr(' ', buff, &bp);
	}
	*bp = '\0';
        
	notify(player, buff);
	free_lbuf(buff);
}

void do_malias_list_all(player)
dbref player;
{
	struct malias *m;
	int i = 0;
	int notified = 0;

	for (i = 0; i < ma_top; i++) {
		m = malias[i];
		if ((m->owner == GOD) || (m->owner == player) || God(player)) {
			if (!notified) {
				notify(player,
				       "Name         Description                         Owner");
				notified++;
			}
			notify(player,
			       tprintf("%-12.12s %-35.35s %-15.15s", m->name, m->desc,
				       Name(m->owner)));
		}
	}

	notify(player, "*****  End of Mail Aliases *****");
}

void load_malias(fp)
FILE *fp;
{
	int i;
	char buffer[200];


	(void)getref(fp);
	if (fscanf(fp, "*** Begin %s ***\n", buffer) == 1 &&
	    !strcmp(buffer, "MALIAS")) {
		malias_read(fp);
	} else {
		fprintf(stderr, "ERROR: Couldn't find Begin MALIAS.\n");
		return;

	}
}

void save_malias(fp)
FILE *fp;
{

	fprintf(fp, "*** Begin MALIAS ***\n");
	malias_write(fp);
}

void malias_read(fp)
FILE *fp;
{
	int i, j;
	char c;
	char *t;
	char buffer[1000];
	struct malias *m;

	fscanf(fp, "%d\n", &ma_top);

	ma_size = ma_top;

	if (ma_top > 0)
		malias = (struct malias **)malloc(sizeof(struct malias *) * ma_size);

	else
		malias = NULL;

	for (i = 0; i < ma_top; i++) {
		malias[i] = (struct malias *)malloc(sizeof(struct malias));

		m = (struct malias *)malias[i];

		fscanf(fp, "%d %d\n", &(m->owner), &(m->numrecep));

		fscanf(fp, "%[^\n]\n", buffer);
		m->name = (char *)malloc(strlen(buffer) - 1);
		StringCopy(m->name, buffer + 2);
		fscanf(fp, "%[^\n]\n", buffer);
		m->desc = (char *)malloc(strlen(buffer) - 1);
		StringCopy(m->desc, buffer + 2);

		if (m->numrecep > 0) {

			for (j = 0; j < m->numrecep; j++) {
				m->list[j] = getref(fp);

			}
		} else {
			m->list[0] = 0;
		}
	}
}

void malias_write(fp)
FILE *fp;
{
	int i, j;
	struct malias *m;

	fprintf(fp, "%d\n", ma_top);

	for (i = 0; i < ma_top; i++) {
		m = malias[i];
		fprintf(fp, "%d %d\n", m->owner, m->numrecep);
		fprintf(fp, "N:%s\n", m->name);
		fprintf(fp, "D:%s\n", m->desc);
		for (j = 0; j < m->numrecep; j++)
			fprintf(fp, "%d\n", m->list[j]);
	}
}

void do_expmail_start(player, arg, subject)
dbref player;
char *arg, *subject;
{
	dbref target;
	struct malias *m;
	char *tolist, *names;

	if (!arg || !*arg) {
		notify(player, "MAIL: I do not know whom you want to mail.");
		return;
	}
	if (!subject || !*subject) {
		notify(player, "MAIL: No subject.");
		return;
	}
	if (Flags2(player) & PLAYER_MAILS) {
		notify(player, "MAIL: Mail message already in progress.");
		return;
	}
	if (!(tolist = make_numlist(player, arg))) {
		return;
	}
	atr_add_raw(player, A_MAILTO, tolist);
	atr_add_raw(player, A_MAILSUB, subject);
	atr_add_raw(player, A_MAILFLAGS, "0");
	atr_clr(player, A_MAILMSG);
	Flags2(player) |= PLAYER_MAILS;
	names = make_namelist(player, tolist);
	notify(player, tprintf("MAIL: You are sending mail to '%s'.", names));
	free_lbuf(names);
	free_lbuf(tolist);
}

void do_mail_cc(player, arg)
dbref player;
char *arg;
{
	char *tolist, *fulllist, *bp;
	char *names;

	if (!(Flags2(player) & PLAYER_MAILS)) {
		notify(player, "MAIL: No mail message in progress.");
		return;
	}
	if (!arg || !*arg) {
		notify(player, "MAIL: I do not know whom you want to mail.");
		return;
	}
	if (!(tolist = make_numlist(player, arg))) {
		return;
	}
	fulllist = alloc_lbuf("do_mail_cc");
	bp = fulllist;

	safe_str(tolist, fulllist, &bp);
	if (atr_get_raw(player, A_MAILTO)) {
		safe_chr(' ', fulllist, &bp);
		safe_str(atr_get_raw(player, A_MAILTO), fulllist, &bp);
	}
	*bp = '\0';

	atr_add_raw(player, A_MAILTO, fulllist);
	names = make_namelist(player, fulllist);
	notify(player, tprintf("MAIL: You are sending mail to '%s'.", names));
	free_lbuf(names);
	free_lbuf(tolist);
	free_lbuf(fulllist);
}

static char *make_namelist(player, arg)
dbref player;
char *arg;
{
	char *names, *oldarg;
	char *bp, *p;

	oldarg = alloc_lbuf("make_namelist.oldarg");
	names = alloc_lbuf("make_namelist.names");
	bp = names;

	StringCopy(oldarg, arg);

	for (p = (char *)strtok(oldarg, " ");
	     p != NULL;
	     p = (char *)strtok(NULL, " ")) {
		if (*p == '*') {
			safe_str(p, names, &bp);
			safe_str(", ", names, &bp);
		} else {
			safe_str(Name(atoi(p)), names, &bp);
			safe_str(", ", names, &bp);
		}
	}
	*(bp - 2) = '\0';
	free_lbuf(oldarg);
	return names;
}

static char *make_numlist(player, arg)
dbref player;
char *arg;
{
	char *head, *tail, spot;
	static char *numbuf;
	static char buf[MBUF_SIZE];
	char *numbp;
	struct malias *m;
	struct mail *temp;
	dbref target;
	int num;

	numbuf = alloc_lbuf("make_numlist");
	numbp = numbuf;
	*numbp = '\0';

	head = (char *)arg;
	while (head && *head) {
		while (*head == ' ')
			head++;

		tail = head;
		while (*tail && (*tail != ' ')) {
			if (*tail == '"') {
				head++;
				tail++;
				while (*tail && (*tail != '"'))
					tail++;
			}
			if (*tail)
				tail++;
		}
		tail--;
		if (*tail != '"')
			tail++;
		spot = *tail;
		*tail = 0;

		num = atoi(head);
		if (num) {
			temp = mail_fetch(player, num);
			if (!temp) {
				notify(player, "MAIL: You can't reply to nonexistent mail.");
				free_lbuf(numbuf);
				return NULL;
			}
			sprintf(buf, "%d ", temp->from);
			safe_str(buf, numbuf, &numbp);
		} else if (*head == '*') {
			m = get_malias(player, head);
			if (!m) {
				notify(player, tprintf("MAIL: Alias '%s' does not exist.", head));
				free_lbuf(numbuf);
				return NULL;
			}
			safe_str(head, numbuf, &numbp);
			safe_chr(' ', numbuf, &numbp);
		} else {
			target = lookup_player(player, head, 1);
			if (target != NOTHING) {
				sprintf(buf, "%d ", target);
				safe_str(buf, numbuf, &numbp);
			} else {
				notify(player, tprintf("MAIL: '%s' does not exist.", head));
				free_lbuf(numbuf);
				return NULL;
			}
		}

		/*
		 * Get the next recip 
		 */
		*tail = spot;
		head = tail;
		if (*head == '"')
			head++;
	}

	if (!*numbuf) {
		notify(player, "MAIL: No players specified.");
		free_lbuf(numbuf);
		return NULL;
	} else {
		*(numbp - 1) = '\0';
		return numbuf;
	}
}

void do_mail_quick(player, arg1, arg2)
dbref player;
char *arg1, *arg2;
{
	char *buf, *bp, *p;
	dbref target;

	if (!arg1 || !*arg1) {
		notify(player, "MAIL: I don't know who you want to mail.");
		return;
	}
	if (!arg2 || !*arg2) {
		notify(player, "MAIL: No message.");
		return;
	}
	if (Flags2(player) & PLAYER_MAILS) {
		notify(player, "MAIL: Mail message already in progress.");
		return;
	}
	buf = alloc_lbuf("do_mail_quick");
	bp = buf;
	StringCopy(bp, arg1);

	parse_to(&bp, '/', 1);

	if (!bp) {
		notify(player, "MAIL: No subject.");
		free_lbuf(buf);
		return;
	}
	mail_to_list(player, make_numlist(player, buf), bp, arg2, 0, 0);
	free_lbuf(buf);
}

void mail_to_list(player, list, subject, message, flags, silent)
dbref player;
char *list, *subject, *message;
int flags, silent;
{
	char *head, *tail, spot, *tolist;
	struct malias *m;
	struct mail *temp;
	dbref target;
	int number;

	if (!list) {
		return;
	}
	if (!*list) {
		free_lbuf(list);
		return;
	}
	tolist = alloc_lbuf("mail_to_list");
	StringCopy(tolist, list);

	number = add_mail_message(player, message);

	head = (char *)list;
	while (head && *head) {
		while (*head == ' ')
			head++;

		tail = head;
		while (*tail && (*tail != ' ')) {
			if (*tail == '"') {
				head++;
				tail++;
				while (*tail && (*tail != '"'))
					tail++;
			}
			if (*tail)
				tail++;
		}
		tail--;
		if (*tail != '"')
			tail++;
		spot = *tail;
		*tail = 0;

		if (*head == '*') {
			m = get_malias(player, head);
			if (!m) {
				free_lbuf(list);
				free_lbuf(tolist);
				return;
			}
			do_malias_send(player, head, tolist, subject, number, flags, silent);
		} else {
			target = atoi(head);
			if (target != NOTHING) {
				send_mail(player, target, tolist, subject, number, flags, silent);
			}
		}

		/*
		 * Get the next recip 
		 */
		*tail = spot;
		head = tail;
		if (*head == '"')
			head++;
	}
	free_lbuf(tolist);
	free_lbuf(list);
}

void do_expmail_stop(player, flags)
dbref player;
int flags;
{
	char *p, *target, *tolist, *mailsub, *mailmsg, *mailflags;
	dbref aowner;
	dbref aflags;

	tolist = atr_get(player, A_MAILTO, &aowner, &aflags);
	mailmsg = atr_get(player, A_MAILMSG, &aowner, &aflags);
	mailsub = atr_get(player, A_MAILSUB, &aowner, &aflags);
	mailflags = atr_get(player, A_MAILFLAGS, &aowner, &aflags);

	if (!*tolist || !*mailmsg || !(Flags2(player) & PLAYER_MAILS)) {
		notify(player, "MAIL: No such message to send.");
		free_lbuf(tolist);
	} else {
		mail_to_list(player, tolist, mailsub, mailmsg, flags | atoi(mailflags), 0);
	}
	free_lbuf(mailflags);
	free_lbuf(mailmsg);
	free_lbuf(mailsub);
	Flags2(player) &= ~PLAYER_MAILS;
}

void do_expmail_abort(player)
dbref player;
{
	Flags2(player) &= ~PLAYER_MAILS;
	notify(player, "MAIL: Message aborted.");
}

void do_prepend(player, cause, key, text)
dbref player, cause;
int key;
char *text;
{
	char *oldmsg, *newmsg, *bp, *attr;
	dbref aowner;
	int aflags;

	if (!mudconf.have_mailer) {
		wait_que(player, cause, 0, NOTHING, 0, text, (char **)NULL, 0,
			 (char **)NULL);
		return;
	};

	if (Flags2(player) & PLAYER_MAILS) {
		oldmsg = atr_get(player, A_MAILMSG, &aowner, &aflags);
		if (*oldmsg) {
			bp = newmsg = alloc_lbuf("do_prepend");
			safe_str(text + 1, newmsg, &bp);
			safe_chr(' ', newmsg, &bp);
			safe_str(oldmsg, newmsg, &bp);
			*bp = '\0';
			atr_add_raw(player, A_MAILMSG, newmsg);
			free_lbuf(newmsg);
		} else
			atr_add_raw(player, A_MAILMSG, text + 1);

		free_lbuf(oldmsg);
		attr = atr_get_raw(player, A_MAILMSG);
		notify(player, tprintf("%d/%d characters prepended.", 
			attr ? strlen(attr) : 0, LBUF_SIZE));
	} else {
		notify(player, "MAIL: No message in progress.");
	}

}

void do_postpend(player, cause, key, text)
dbref player, cause;
int key;
char *text;
{
	char *oldmsg, *newmsg, *bp, *attr;
	dbref aowner;
	int aflags;

	if (!mudconf.have_mailer) {
		wait_que(player, cause, 0, NOTHING, 0, text, (char **)NULL, 0,
			 (char **)NULL);
		return;
	};

	if ((*(text + 1) == '-') && !(*(text + 2))) {
		do_expmail_stop(player, 0);
		return;
	}
	if (Flags2(player) & PLAYER_MAILS) {
		oldmsg = atr_get(player, A_MAILMSG, &aowner, &aflags);
		if (*oldmsg) {
			bp = newmsg = alloc_lbuf("do_postpend");
			safe_str(oldmsg, newmsg, &bp);
			safe_chr(' ', newmsg, &bp);
			safe_str(text + 1, newmsg, &bp);
			*bp = '\0';
			atr_add_raw(player, A_MAILMSG, newmsg);
			free_lbuf(newmsg);
		} else
			atr_add_raw(player, A_MAILMSG, text + 1);

		free_lbuf(oldmsg);
		attr = atr_get_raw(player, A_MAILMSG);
		notify(player, tprintf("%d/%d characters added.", 
			attr ? strlen(attr) : 0, LBUF_SIZE));
	} else {
		notify(player, "MAIL: No message in progress.");
	}
}

static void do_edit_msg(player, from, to)
dbref player;
char *from;
char *to;
{
	char *result, *msg;
	dbref aowner;
	int aflags;

	if (Flags2(player) & PLAYER_MAILS) {
		msg = atr_get(player, A_MAILMSG, &aowner, &aflags);
		result = replace_string(from, to, msg);
		atr_add(player, A_MAILMSG, result, aowner, aflags);
		notify(player, "Text edited.");
		free_lbuf(result);
		free_lbuf(msg);
	} else {
		notify(player, "MAIL: No message in progress.");
	}
}

static void do_mail_proof(player)
dbref player;
{
	char *mailto, *names;
	char *mailmsg, *msg, *bp, *str;
	dbref aowner;
	int aflags;

	mailto = atr_get(player, A_MAILTO, &aowner, &aflags);

	if (!atr_get_raw(player, A_MAILMSG)) {
		notify(player, "MAIL: No text.");
		free_lbuf(mailto);
		return;
	} else {
		str = mailmsg = atr_get(player, A_MAILMSG, &aowner, &aflags);
		bp = msg = alloc_lbuf("do_mail_proof");
		exec(msg, &bp, 0, player, player, EV_EVAL | EV_FCHECK, &str,
		     (char **)NULL, 0);
		*bp = '\0';
		free_lbuf(mailmsg);
	}

	if (Flags2(player) & PLAYER_MAILS) {
		names = make_namelist(player, mailto);
		notify(player, DASH_LINE);
		notify(player, tprintf("From:  %-*s  Subject: %-35s\nTo: %s",
				       PLAYER_NAME_LIMIT - 6, Name(player),
				    atr_get_raw(player, A_MAILSUB), names));
		notify(player, DASH_LINE);
		notify(player, msg);
		notify(player, DASH_LINE);
		free_lbuf(names);
	} else {
		notify(player, "MAIL: No message in progress.");
	}
	free_lbuf(mailto);
	free_lbuf(msg);
}

void do_malias_desc(player, alias, desc)
dbref player;
char *alias;
char *desc;
{
	struct malias *m;

	if (!(m = get_malias(player, alias))) {
		notify(player, tprintf("MAIL: Alias %s not found.", alias));
		return;
	} else if ((m->owner != GOD) || ExpMail(player)) {
		free(m->desc);	/*
				 * free it up 
				 */
		m->desc = (char *)malloc(sizeof(char *) * strlen(desc));

		StringCopy(m->desc, desc);
		notify(player, "MAIL: Description changed.");
	} else
		notify(player, "MAIL: Permission denied.");
	return;
}

void do_malias_chown(player, alias, owner)
dbref player;
char *alias;
char *owner;
{
	struct malias *m;
	dbref no = NOTHING;

	if (!(m = get_malias(player, alias))) {
		notify(player, tprintf("MAIL: Alias %s not found.", alias));
		return;
	} else {
		if (!ExpMail(player)) {
			notify(player, "MAIL: You cannot do that!");
			return;
		} else {
			if ((no = lookup_player(player, owner, 1)) == NOTHING) {
				notify(player, "MAIL: I do not see that here.");
				return;
			}
			m->owner = no;
			notify(player, "MAIL: Owner changed for alias.");
		}
	}
}

void do_malias_add(player, alias, person)
dbref player;
char *alias;
char *person;
{
	int i = 0;
	dbref thing;
	struct malias *m;

	thing = NOTHING; 
	
	if (!(m = get_malias(player, alias))) {
		notify(player, tprintf("MAIL: Alias %s not found.", alias));
		return;
	}
	if (*person == '#') {
		thing = parse_dbref(person + 1);
		if (!isPlayer(thing)) {
			notify(player, "MAIL: Only players may be added.");
			return;
		}
	}
	
	if (thing == NOTHING)
		thing = lookup_player(player, person, 1);
	
	if (thing == NOTHING) {
		notify(player, "MAIL: I do not see that person here.");
		return;
	}
	
	if ((m->owner == GOD) && !ExpMail(player)) {
		notify(player, "MAIL: Permission denied.");
		return;
	}
	for (i = 0; i < m->numrecep; i++) {
		if (m->list[i] == thing) {
			notify(player, "MAIL: That person is already on the list.");
			return;
		}
	}

	if (i >= (MALIAS_LEN - 1)) {
        		notify(player, "MAIL: The list is full.");
		return;
	}

	m->list[m->numrecep] = thing;
	m->numrecep = m->numrecep + 1;
	notify(player, tprintf("MAIL: %s added to %s", Name(thing), m->name));
}

void do_malias_remove(player, alias, person)
dbref player;
char *alias;
char *person;
{
	int i, ok = 0;
	dbref thing;
	struct malias *m;

	thing = NOTHING;
	
	if (!(m = get_malias(player, alias))) {
		notify(player, "MAIL: Alias not found.");
		return;
	}
	if ((m->owner == GOD) && !ExpMail(player)) {
		notify(player, "MAIL: Permission denied.");
		return;
	}
	
	if (*person == '#')
		thing = parse_dbref(person + 1);
	
	if (thing == NOTHING)
		thing = lookup_player(player, person, 1);
	
	if (thing == NOTHING) {
		notify(player, "MAIL: I do not see that person here.");
		return;
	}
		
	for (i = 0; i < m->numrecep; i++) {
		if (ok)
			m->list[i] = m->list[i + 1];
		else if ((m->list[i] == thing) && !ok) {
			m->list[i] = m->list[i + 1];
			ok = 1;
		}
	}

	if (ok)
		m->numrecep--;

	notify(player, tprintf("MAIL: %s removed from alias %s.",
			       Name(thing), alias));
}

void do_malias_rename(player, alias, newname)
dbref player;
char *alias;
char *newname;
{
	struct malias *m;

	if (get_malias(player, newname) != NULL) {
		notify(player, "MAIL: That name already exists!");
		return;
	}
	if ((m = get_malias(player, alias)) == NULL) {
		notify(player, "MAIL: I cannot find that alias!");
		return;
	}
	if (*newname != '*') {
		notify(player, "MAIL: Bad alias.");
		return;
	}
	if (!ExpMail(player) && !(m->owner == player)) {
		notify(player, "MAIL: Permission denied.");
		return;
	}
	free(m->name);
	m->name = (char *)malloc(sizeof(char) * strlen(newname));

	StringCopy(m->name, newname + 1);

	notify(player, "MAIL: Mailing Alias renamed.");
}

void do_malias_delete(player, alias)
dbref player;
char *alias;
{
	int i = 0;
	int done = 0;
	struct malias *m;

	m = get_malias(player, alias);
	
	if (!m) {
		notify(player, "MAIL: Not a valid alias. Remember to prefix the alias name with *.");
		return;
	}

	for (i = 0; i < ma_top; i++) {
		if (done)
			malias[i] = malias[i + 1];
		else {
			if ((m->owner == player) || ExpMail(player))
				if (m == malias[i]) {
					done = 1;
					notify(player, "MAIL: Alias Deleted.");
					malias[i] = malias[i + 1];
				}
		}
	}

	if (!done)
		notify(player, "MAIL: Alias not found.");
	else
		ma_top--;
}

void do_malias_adminlist(player)
dbref player;
{
	struct malias *m;
	int i;

	if (!ExpMail(player)) {
		do_malias_list_all(player);
		return;
	}
	notify(player,
	  "Num  Name       Description                              Owner");

	for (i = 0; i < ma_top; i++) {
		m = malias[i];
		notify(player, tprintf("%-4d %-10.10s %-40.40s %-11.11s",
				       i, m->name, m->desc, Name(m->owner)));
	}

	notify(player, "***** End of Mail Aliases *****");
}

void do_malias_status(player)
dbref player;
{
	if (!ExpMail(player))
		notify(player, "MAIL: Permission denied.");
	else {
		notify(player, tprintf("MAIL: Number of mail aliases defined: %d", ma_top));
		notify(player, tprintf("MAIL: Allocated slots %d", ma_size));
	}
}
