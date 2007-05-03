/* interface.h */
/* $Id: interface.h,v 1.2 1997/04/16 06:01:12 dpassmor Exp $ */

#include "copyright.h"

#ifndef __INTERFACE__H
#define __INTERFACE__H

#include "db.h"
#include "externs.h"
#include "htab.h"
#include "alloc.h"
#include "config.h"

#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

/* these symbols must be defined by the interface */

extern int shutdown_flag; /* if non-zero, interface should shut down */

/* Disconnection reason codes */

#define	R_QUIT		1	/* User quit */
#define	R_TIMEOUT	2	/* Inactivity timeout */
#define	R_BOOT		3	/* Victim of @boot, @toad, or @destroy */
#define	R_SOCKDIED	4	/* Other end of socked closed it */
#define	R_GOING_DOWN	5	/* Game is going down */
#define	R_BADLOGIN	6	/* Too many failed login attempts */
#define	R_GAMEDOWN	7	/* Not admitting users now */
#define	R_LOGOUT	8	/* Logged out w/o disconnecting */
#define R_GAMEFULL	9	/* Too many players logged in */

/* Logged out command tabel definitions */

#define CMD_QUIT	1
#define CMD_WHO		2
#define CMD_DOING	3
#define CMD_PREFIX	5
#define CMD_SUFFIX	6
#define CMD_LOGOUT	7
#define CMD_SESSION	8
#define CMD_PUEBLOCLIENT 9

#define CMD_MASK	0xff
#define CMD_NOxFIX	0x100

extern NAMETAB logout_cmdtable[];

typedef struct cmd_block_hdr CBLKHDR;
typedef struct cmd_block CBLK;

struct cmd_block {
	struct cmd_block_hdr {
		struct cmd_block *nxt;
	}	hdr;
	char	cmd[LBUF_SIZE - sizeof(CBLKHDR)];
};

typedef struct text_block_hdr TBLKHDR;
typedef struct text_block TBLOCK;

struct text_block {
	struct text_block_hdr {
		struct text_block *nxt;
		char	*start;
		char	*end;
		int	nchars;
	}	hdr;
	char	data[OUTPUT_BLOCK_SIZE - sizeof(TBLKHDR)];
};

typedef struct prog_data PROG;
struct prog_data {
	dbref wait_cause;
	char *wait_regs[MAX_GLOBAL_REGS];
};

typedef struct descriptor_data DESC;
struct descriptor_data {
  int descriptor;
#ifdef CONCENTRATE
#define C_CCONTROL 1
#define C_REMOTE 2
  int concid;
  int cstatus;
  struct descriptor_data *parent;
#endif
  int flags;
  int retries_left;
  int command_count;
  int timeout;
  int host_info;
  char addr[51];
  char username[11]; 
  char doing[41];
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
  time_t connected_at;
  time_t last_time;
  int quota;
  int wait_for_input;		/* Used by @prog */
  dbref wait_cause;		/* Used by @prog */
  PROG *program_data;
  struct sockaddr_in address;	/* added 3/6/90 SCG */
  struct descriptor_data *hashnext;
  struct descriptor_data *next;
  struct descriptor_data **prev;
};

/* flags in the flag field */
#define	DS_CONNECTED	0x0001		/* player is connected */
#define	DS_AUTODARK	0x0002		/* Wizard was auto set dark. */
#define DS_PUEBLOCLIENT 0x0004          /* Client is Pueblo-enhanced. */

extern DESC *descriptor_list;

/* from the net interface */

extern void	NDECL(emergency_shutdown);
extern void	FDECL(shutdownsock, (DESC *, int));
extern void	FDECL(shovechars, (int));
extern void	NDECL(set_signals);

/* from netcommon.c */

extern struct timeval	FDECL(timeval_sub, (struct timeval, struct timeval));
extern int	FDECL(msec_diff, (struct timeval now, struct timeval then));
extern struct timeval	FDECL(msec_add, (struct timeval, int));
extern struct timeval	FDECL(update_quotas, (struct timeval, struct timeval));
extern void	FDECL(handle_http, (DESC *, char *));
extern void	FDECL(raw_notify, (dbref, const char *));
extern void	FDECL(raw_notify_newline, (dbref));
extern void	FDECL(clearstrings, (DESC *));
extern void	FDECL(queue_write, (DESC *, const char *, int));
extern void	FDECL(queue_string, (DESC *, const char *));
extern void	FDECL(freeqs, (DESC *));
extern void	FDECL(welcome_user, (DESC *));
extern void	FDECL(save_command, (DESC *, CBLK *));
extern void	FDECL(announce_disconnect, (dbref, DESC *, const char *));
extern int	FDECL(boot_off, (dbref, char *));
extern int	FDECL(boot_by_port, (int, int, char *));
extern int	FDECL(fetch_idle, (dbref));
extern int	FDECL(fetch_connect, (dbref));
extern void	NDECL(check_idle);
extern void	NDECL(process_commands);
extern int	FDECL(site_check, (struct in_addr, SITE *));
extern void	FDECL(make_ulist, (dbref, char *, char **));
extern dbref	FDECL(find_connected_name, (dbref, char *));

/* From predicates.c */

#define alloc_desc(s) (DESC *)pool_alloc(POOL_DESC,s)
#define free_desc(b) pool_free(POOL_DESC,((char **)&(b)))

#define DESC_ITER_PLAYER(p,d) \
	for (d=(DESC *)nhashfind((int)p,&mudstate.desc_htab);d;d=d->hashnext)
#define DESC_ITER_CONN(d) \
	for (d=descriptor_list;(d);d=(d)->next) \
		if ((d)->flags & DS_CONNECTED)
#define DESC_ITER_ALL(d) \
	for (d=descriptor_list;(d);d=(d)->next)

#define DESC_SAFEITER_PLAYER(p,d,n) \
	for (d=(DESC *)nhashfind((int)p,&mudstate.desc_htab), \
        	n=((d!=NULL) ? d->hashnext : NULL); \
	     d; \
	     d=n,n=((n!=NULL) ? n->hashnext : NULL))
#define DESC_SAFEITER_CONN(d,n) \
	for (d=descriptor_list,n=((d!=NULL) ? d->next : NULL); \
	     d; \
	     d=n,n=((n!=NULL) ? n->next : NULL)) \
		if ((d)->flags & DS_CONNECTED)
#define DESC_SAFEITER_ALL(d,n) \
	for (d=descriptor_list,n=((d!=NULL) ? d->next : NULL); \
	     d; \
	     d=n,n=((n!=NULL) ? n->next : NULL))

#define MALLOC(result, type, number, where) do { \
	if (!((result)=(type *) XMALLOC (((number) * sizeof (type)), where))) \
		panic("Out of memory", 1);				\
	} while (0)

#endif
