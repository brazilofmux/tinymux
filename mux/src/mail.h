// mail.h
//
// $Id: mail.h,v 1.7 2002-07-25 14:34:25 jake Exp $
//

#ifndef _MAIL_H
#define _MAIL_H

#include "copyright.h"

/* Some of this isn't implemented yet, but heralds the future! */
#define M_ISREAD    0x0001
#define M_UNREAD    0x0FFE
#define M_CLEARED   0x0002
#define M_URGENT    0x0004
#define M_MASS      0x0008
#define M_SAFE      0x0010
//#define M_RECEIPT   0x0020
#define M_TAG       0x0040
#define M_FORWARD   0x0080
        /* 0x0100 - 0x0F00 reserved for folder numbers */
#define M_FMASK     0xF0FF
#define M_ALL       0x1000  /* Used in mail_selectors */
#define M_MSUNREAD  0x2000  /* Mail selectors */
        /* 0x4000 - 0x8000 available */
#define M_REPLY     0x4000

#define MAX_FOLDERS 15
#define FOLDER_NAME_LEN MBUF_SIZE
#define FolderBit(f) (256 * (f))

#define Urgent(m)   (m->read & M_URGENT)
#define Mass(m)     (m->read & M_MASS)
#define M_Safe(m)   (m->read & M_SAFE)
//#define Receipt(m)  (m->read & M_RECEIPT)
#define Forward(m)  (m->read & M_FORWARD)
#define Tagged(m)   (m->read & M_TAG)
#define Folder(m)   ((m->read & ~M_FMASK) >> 8)
#define Read(m)     (m->read & M_ISREAD)
#define Cleared(m)  (m->read & M_CLEARED)
#define Unread(m)   (!Read(m))
#define All(ms)     (ms.flags & M_ALL)
#define ExpMail(x)  (Wizard(x))
//#define Reply(m)    (m->read & M_REPLY)

#define MA_INC      2   /* what interval to increase the malias list */

#define DASH_LINE  \
  "---------------------------------------------------------------------------"

#define MAIL_ITER_ALL(mp, thing)    for((thing)=0; (thing)<mudstate.db_top; (thing)++) \
                        for (mp = (struct mail *)hashfindLEN(&thing, sizeof(thing), &mudstate.mail_htab); mp != NULL; mp = mp->next)

/* This macro requires you to put nextp = mp->next at
 * the beginning of the loop.
 */

#define MAIL_ITER_SAFE(mp, thing, nextp)    for((thing)=0; (thing)<mudstate.db_top; (thing)++) \
                            for (mp = (struct mail *)hashfindLEN(&thing, sizeof(thing), &mudstate.mail_htab); mp != NULL; mp = nextp)


typedef unsigned int mail_flag;

struct mail
{
  struct mail *next;
  struct mail *prev;
  dbref to;
  dbref from;
  int number;
  char *time;
  char *subject;
  char *tolist;
  int read;
};

struct mail_selector
{
    int low, high;
    mail_flag flags;
    dbref player;
    int days, day_comp;
};

struct muser
{
    dbref who;
    char *fwd;
    char *vacation;
    dbref *afilter;
    int status;
};

typedef struct mail_entry MENT;
struct mail_entry
{
    char *m_pMessage;
    int   m_nRefs;
};

extern void count_mail(dbref player, int folder, int *rcount, int *ucount, int *ccount);
extern char *MessageFetch(int number);
extern void check_mail_expiration(void);
extern void check_mail(dbref player, int folder, BOOL silent);
extern void malias_cleanup(dbref player);
extern void remove_from_mail(dbref victim);

#endif // !_MAIL_H
