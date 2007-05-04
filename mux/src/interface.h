// interface.h
//
// $Id: interface.h,v 1.1 2003-01-22 19:58:25 sdennis Exp $
//

#include "copyright.h"

#ifndef __INTERFACE__H
#define __INTERFACE__H

#include <sys/types.h>

#ifndef WIN32
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif // HAVE_SYS_SELECT_H
#endif // !WIN32

/* these symbols must be defined by the interface */

/* Disconnection reason codes */

#define R_QUIT      1   /* User quit */
#define R_TIMEOUT   2   /* Inactivity timeout */
#define R_BOOT      3   /* Victim of @boot, @toad, or @destroy */
#define R_SOCKDIED  4   /* Other end of socked closed it */
#define R_GOING_DOWN    5   /* Game is going down */
#define R_BADLOGIN  6   /* Too many failed login attempts */
#define R_GAMEDOWN  7   /* Not admitting users now */
#define R_LOGOUT    8   /* Logged out w/o disconnecting */
#define R_GAMEFULL  9   /* Too many players logged in */

/* Logged out command table definitions */

#define CMD_QUIT    1
#define CMD_WHO     2
#define CMD_DOING   3
#define CMD_PREFIX  5
#define CMD_SUFFIX  6
#define CMD_LOGOUT  7
#define CMD_SESSION 8
#define CMD_PUEBLOCLIENT 9
#define CMD_INFO    10

#define CMD_MASK    0xff
#define CMD_NOxFIX  0x100

extern NAMETAB logout_cmdtable[];

typedef struct cmd_block CBLK;
typedef struct cmd_block_hdr
{
    struct cmd_block *nxt;
} CBLKHDR;

typedef struct cmd_block
{
    CBLKHDR hdr;
    char    cmd[LBUF_SIZE - sizeof(CBLKHDR)];
} CBLK;

typedef struct text_block TBLOCK;
typedef struct text_block_hdr
{
    struct text_block *nxt;
    char    *start;
    char    *end;
    int nchars;
}   TBLOCKHDR;

typedef struct text_block
{
    TBLOCKHDR hdr;
    char    data[OUTPUT_BLOCK_SIZE - sizeof(TBLOCKHDR)];
} TBLOCK;

typedef struct prog_data PROG;
struct prog_data {
    dbref wait_enactor;
    char *wait_regs[MAX_GLOBAL_REGS];
};

typedef struct descriptor_data DESC;
struct descriptor_data
{
  CLinearTimeAbsolute connected_at;
  CLinearTimeAbsolute last_time;

  SOCKET descriptor;
#ifdef WIN32
  // these are for the Windows NT TCP/IO
#define SIZEOF_OVERLAPPED_BUFFERS 512
  char input_buffer[SIZEOF_OVERLAPPED_BUFFERS];         // buffer for reading
  char output_buffer[SIZEOF_OVERLAPPED_BUFFERS];        // buffer for writing
  OVERLAPPED InboundOverlapped;   // for asynchronous reading
  OVERLAPPED OutboundOverlapped;  // for asynchronous writing
  BOOL bWritePending;             // TRUE if in process of writing
  BOOL bConnectionDropped;        // TRUE if we cannot send to player
  BOOL bConnectionShutdown;       // TRUE if connection has been shutdown
  BOOL bCallProcessOutputLater;   // Does the socket need priming for output.
#endif // WIN32

  int flags;
  int retries_left;
  int command_count;
  int timeout;
  int host_info;
  dbref player;
  char *output_prefix;
  char *output_suffix;
  int output_size;
  int output_tot;
  int output_lost;
  TBLOCK *output_head;
  TBLOCK *output_tail;
  int input_size;
  int input_tot;
  int input_lost;
  CBLK *input_head;
  CBLK *input_tail;
  CBLK *raw_input;
  char *raw_input_at;
  int quota;
  int wait_for_input;       /* Used by @prog */
  dbref wait_enactor;       /* Used by @prog */
  PROG *program_data;
  struct descriptor_data *hashnext;
  struct descriptor_data *next;
  struct descriptor_data **prev;

  struct sockaddr_in address;   /* added 3/6/90 SCG */

  char addr[51];
  char username[11];
  char doing[SIZEOF_DOING_STRING];
};

/* flags in the flag field */
#define DS_CONNECTED    0x0001      // player is connected.
#define DS_AUTODARK     0x0002      // Wizard was auto set dark.
#define DS_PUEBLOCLIENT 0x0004      // Client is Pueblo-enhanced.

extern DESC *descriptor_list;

/* from the net interface */

extern void emergency_shutdown(void);
extern void shutdownsock(DESC *, int);
extern void SetupPorts(int *pnPorts, PortInfo aPorts[], IntArray *pia);
#ifdef WIN32
extern void shovechars9x(int nPorts, PortInfo aPorts[]);
extern void shovecharsNT(int nPorts, PortInfo aPorts[]);
void process_output9x(void *, int);
void process_outputNT(void *, int);
extern FTASK *process_output;
#else // WIN32
extern void shovechars(int nPorts, PortInfo aPorts[]);
extern void process_output(void *, int);
extern void dump_restart_db(void);
#endif // WIN32

extern void BuildSignalNamesTable(void);
extern void set_signals(void);

// from netcommon.cpp

extern void make_ulist(dbref, char *, char **, BOOL);
extern void make_port_ulist(dbref, char *, char **);
extern int fetch_session(dbref target);
extern int fetch_idle(dbref target);
extern int fetch_connect(dbref target);
extern const char *time_format_1(int Seconds);
extern const char *time_format_2(int Seconds);
extern CLinearTimeAbsolute update_quotas(const CLinearTimeAbsolute& tLast, const CLinearTimeAbsolute& tCurrent);
extern void raw_notify(dbref, const char *);
extern void raw_notify_newline(dbref);
extern void clearstrings(DESC *);
extern void queue_write(DESC *, const char *, int);
extern void queue_string(DESC *, const char *);
extern void freeqs(DESC *);
extern void welcome_user(DESC *);
extern void save_command(DESC *, CBLK *);
extern void announce_disconnect(dbref, DESC *, const char *);
extern int boot_by_port(SOCKET port, BOOL no_god, const char *message);
extern void find_oldest(dbref target, DESC *dOldest[2]);
extern void check_idle(void);
void Task_ProcessCommand(void *arg_voidptr, int arg_iInteger);
extern int site_check(struct in_addr, SITE *);
extern dbref  find_connected_name(dbref, char *);

/* From predicates.c */

#define alloc_desc(s) (DESC *)pool_alloc(POOL_DESC,s, __FILE__, __LINE__)
#define free_desc(b) pool_free(POOL_DESC,(char *)(b), __FILE__, __LINE__)

// From player.cpp
extern void record_login(dbref, BOOL, char *, char *, char *, char *);
extern dbref connect_player(char *, char *, char *, char *, char *);

#define DESC_ITER_PLAYER(p,d) \
    for (d=(DESC *)hashfindLEN(&(p), sizeof(p), &mudstate.desc_htab); d; d = d->hashnext)
#define DESC_ITER_CONN(d) \
    for (d=descriptor_list;(d);d=(d)->next) \
        if ((d)->flags & DS_CONNECTED)
#define DESC_ITER_ALL(d) \
    for (d=descriptor_list;(d);d=(d)->next)

#define DESC_SAFEITER_PLAYER(p,d,n) \
    for (d=(DESC *)hashfindLEN(&(p), sizeof(p), &mudstate.desc_htab), \
            n=((d!=NULL) ? d->hashnext : NULL); \
         d; \
         d=n,n=((n!=NULL) ? n->hashnext : NULL))
//#define DESC_SAFEITER_CONN(d,n) \
//    for (d=descriptor_list,n=((d!=NULL) ? d->next : NULL); \
//         d; \
//         d=n,n=((n!=NULL) ? n->next : NULL)) \
//        if ((d)->flags & DS_CONNECTED)
#define DESC_SAFEITER_ALL(d,n) \
    for (d=descriptor_list,n=((d!=NULL) ? d->next : NULL); \
         d; \
         d=n,n=((n!=NULL) ? n->next : NULL))
#endif // !__INTERFACE__H
