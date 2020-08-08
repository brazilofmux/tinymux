/*! \file mail.h
 * \brief In-game \@mail system.
 *
 */

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

typedef unsigned int mail_flag;

struct mail
{
    struct mail *next;
    struct mail *prev;
    dbref        to;
    dbref        from;
    int          number;
    UTF8        *time;
    UTF8        *subject;
    UTF8        *tolist;
    int          read;
};

struct mail_selector
{
    int       low;
    int       high;
    mail_flag flags;
    dbref     player;
    int       days;
    int       day_comp;
};

struct muser
{
    dbref  who;
    UTF8  *fwd;
    UTF8  *vacation;
    dbref *afilter;
    int    status;
};

typedef struct mail_body MAILBODY;
struct mail_body
{
    size_t m_nMessage;
    UTF8  *m_pMessage;
    int    m_nRefs;
};

class MailList
{
private:
    struct mail *m_miHead;
    struct mail *m_mi;
    dbref        m_player;
    bool         m_bRemoved;

public:
    MailList(dbref player);
    struct mail *FirstItem(void);
    struct mail *NextItem(void);
    bool IsEnd(void);
    void RemoveItem(void);
    void RemoveAll(void);
    void AppendItem(struct mail *newp);
};

#endif // !_MAIL_H
