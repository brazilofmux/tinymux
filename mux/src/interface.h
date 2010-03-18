/*! \file interface.h
 * \brief Declarations relating to network connections.
 *
 * $Id$
 *
 */

#include "copyright.h"

#ifndef __INTERFACE__H
#define __INTERFACE__H

#include <sys/types.h>

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif // HAVE_NETINET_IN_H
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif // HAVE_ARPA_INET_H
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif // HAVE_NETDB_H

#if defined(UNIX_NETWORKING_EPOLL) && defined(HAVE_SYS_EPOLL_H)
#include <sys/epoll.h>
#endif // UNIX_NETWORKING_EPOLL && HAVE_SYS_EPOLL_H

#if defined(UNIX_NETWORKING_SELECT) && defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif // UNIX_NETWORKING_SELECT && HAVE_SYS_SELECT_H

#ifdef UNIX_SSL
#include <openssl/ssl.h>
#endif

/* these symbols must be defined by the interface */

/* Disconnection reason codes */

#define R_MIN       0
#define R_UNKNOWN   0   /* Unknown/unexpected */
#define R_QUIT      1   /* User quit */
#define R_TIMEOUT   2   /* Inactivity timeout */
#define R_BOOT      3   /* Victim of @boot, @toad, or @destroy */
#define R_SOCKDIED  4   /* Other end of socked closed it */
#define R_GOING_DOWN    5   /* Game is going down */
#define R_BADLOGIN  6   /* Too many failed login attempts */
#define R_GAMEDOWN  7   /* Not admitting users now */
#define R_LOGOUT    8   /* Logged out w/o disconnecting */
#define R_GAMEFULL  9   /* Too many players logged in */
#define R_RESTART   10  /* Restarting, and this socket cannot be preserved */
#define R_MAX       10

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
    UTF8    cmd[LBUF_SIZE - sizeof(CBLKHDR)];
} CBLK;

#define TBLK_FLAG_LOCKED    0x01

typedef struct text_block TBLOCK;
typedef struct text_block_hdr
{
    struct text_block *nxt;
    UTF8    *start;
    UTF8    *end;
    size_t   nchars;
    int      flags;
}   TBLOCKHDR;

typedef struct text_block
{
    TBLOCKHDR hdr;
    UTF8    data[OUTPUT_BLOCK_SIZE - sizeof(TBLOCKHDR)];
} TBLOCK;

typedef struct prog_data PROG;
struct prog_data
{
    dbref    wait_enactor;
    reg_ref *wait_regs[MAX_GLOBAL_REGS];
};

// Input state
//
#define NVT_IS_NORMAL          0
#define NVT_IS_HAVE_IAC        1
#define NVT_IS_HAVE_IAC_WILL   2
#define NVT_IS_HAVE_IAC_WONT   3
#define NVT_IS_HAVE_IAC_DO     4
#define NVT_IS_HAVE_IAC_DONT   5
#define NVT_IS_HAVE_IAC_SB     6
#define NVT_IS_HAVE_IAC_SB_IAC 7

// Character Names
//
#define NVT_BS   '\x08'
#define NVT_DEL  '\x7F'
#define NVT_EOR  '\xEF'
#define NVT_NOP  '\xF1'
#define NVT_GA   '\xF9'
#define NVT_WILL '\xFB'
#define NVT_WONT '\xFC'
#define NVT_DO   '\xFD'
#define NVT_DONT '\xFE'
#define NVT_IAC  '\xFF'
#define NVT_SB   '\xFA'
#define NVT_SE   '\xF0'

// Telnet Options
//
#define TELNET_BINARY   '\x00'
#define TELNET_SGA      '\x03'
#define TELNET_EOR      '\x19'
#define TELNET_NAWS     '\x1F'
#define TELNET_TTYPE    '\x18'
#define TELNET_OLDENV   '\x24'
#define TELNET_ENV      '\x27'
#define TELNET_CHARSET  '\x2A'
#define TELNET_STARTTLS '\x2E'

// Telnet Option Negotiation States
//
#define OPTION_NO               0
#define OPTION_YES              1
#define OPTION_WANTNO_EMPTY     2
#define OPTION_WANTNO_OPPOSITE  3
#define OPTION_WANTYES_EMPTY    4
#define OPTION_WANTYES_OPPOSITE 5

// Telnet subnegotiation requests
// Multiple meanings, depending on TELOPT
//
#define TELNETSB_IS             0
#define TELNETSB_VAR            0
#define TELNETSB_REQUEST        1
#define TELNETSB_SEND           1
#define TELNETSB_VALUE          1
#define TELNETSB_FOLLOWS        1
#define TELNETSB_INFO           2
#define TELNETSB_ACCEPT         2
#define TELNETSB_REPLY          2
#define TELNETSB_ESC            2
#define TELNETSB_REJECT         3
#define TELNETSB_NAME           3
#define TELNETSB_USERVAR        3

#define CHARSET_ASCII           0
#define CHARSET_LATIN1          1
#define CHARSET_UTF8            2

typedef struct descriptor_data DESC;
struct descriptor_data
{
  CLinearTimeAbsolute connected_at;
  CLinearTimeAbsolute last_time;

  SOCKET descriptor;

#if defined(WINDOWS_NETWORKING)
  // these are for the Windows NT TCP/IO
#define SIZEOF_OVERLAPPED_BUFFERS 512
  char input_buffer[SIZEOF_OVERLAPPED_BUFFERS];         // buffer for reading
  OVERLAPPED InboundOverlapped;   // for asynchronous reading
  OVERLAPPED OutboundOverlapped;  // for asynchronous writing
  bool bConnectionDropped;        // true if we cannot send to player
  bool bConnectionShutdown;       // true if connection has been shutdown
  bool bCallProcessOutputLater;   // Does the socket need priming for output.
#endif // WINDOWS_NETWORKING

  int flags;
  int retries_left;
  int command_count;
  int timeout;
  int host_info;
  dbref player;
  UTF8 *output_prefix;
  UTF8 *output_suffix;
  size_t output_size;
  size_t output_tot;
  size_t output_lost;
  TBLOCK *output_head;
  TBLOCK *output_tail;
  size_t input_size;
  size_t input_tot;
  size_t input_lost;
  CBLK *input_head;
  CBLK *input_tail;
  CBLK *raw_input;
  UTF8 *raw_input_at;
  size_t        nOption;
  unsigned char aOption[SBUF_SIZE];
  int raw_input_state;
  int raw_codepoint_state;
  size_t raw_codepoint_length;
  int nvt_him_state[256];
  int nvt_us_state[256];
  UTF8 *ttype;
  int encoding;
  int negotiated_encoding;
  int width;
  int height;
  int quota;
  PROG *program_data;
  struct descriptor_data *hashnext;
  struct descriptor_data *next;
  struct descriptor_data **prev;

  struct sockaddr_in address;   /* added 3/6/90 SCG */

  UTF8 addr[51];
  UTF8 username[11];
  UTF8 doing[SIZEOF_DOING_STRING];

#ifdef UNIX_SSL
  SSL *ssl_session;
#endif
};

int HimState(DESC *d, unsigned char chOption);
int UsState(DESC *d, unsigned char chOption);
void EnableHim(DESC *d, unsigned char chOption);
void DisableHim(DESC *d, unsigned char chOption);
void EnableUs(DESC *d, unsigned char chOption);
void DisableUs(DESC *d, unsigned char chOption);

/* flags in the flag field */
#define DS_CONNECTED    0x0001      // player is connected.
#define DS_AUTODARK     0x0002      // Wizard was auto set dark.
#define DS_PUEBLOCLIENT 0x0004      // Client is Pueblo-enhanced.

extern DESC *descriptor_list;

/* from the net interface */

extern int mux_socket_write(DESC *d, const char* buffer, int nBytes, int flags);
extern int mux_socket_read(DESC *d, char* buffer, int nBytes, int flags);

extern void emergency_shutdown(void);
extern void shutdownsock(DESC *, int);
extern void SetupPorts(int *pnPorts, PortInfo aPorts[], IntArray *pia, IntArray *piaSSL, const UTF8 *ip_address);
#if defined(WINDOWS_NETWORKING)
extern void shovechars9x(int nPorts, PortInfo aPorts[]);
extern void shovecharsNT(int nPorts, PortInfo aPorts[]);
void process_output_ntio(void *, int);
#elif defined(UNIX_NETWORKING_SELECT)
extern void shovechars_select(int nPorts, PortInfo aPorts[]);
#endif
#if defined(HAVE_WORKING_FORK)
extern void dump_restart_db(void);
#endif // HAVE_WORKING_FORK
extern FTASK *process_output;
void process_output_unix(void *, int);

extern void BuildSignalNamesTable(void);
extern void set_signals(void);

// From netcommon.cpp
//
extern void make_ulist(dbref, UTF8 *, UTF8 **, bool);
extern void make_port_ulist(dbref, UTF8 *, UTF8 **);
extern int fetch_session(dbref target);
extern int fetch_idle(dbref target);
extern int fetch_connect(dbref target);
extern int fetch_height(dbref target);
extern int fetch_width(dbref target);
extern const UTF8 *time_format_1(int Seconds, size_t maxWidth);
extern const UTF8 *time_format_2(int Seconds);
extern void update_quotas(CLinearTimeAbsolute& tLast, const CLinearTimeAbsolute& tCurrent);
extern void raw_notify(dbref, const UTF8 *);
extern void raw_notify(dbref player, const mux_string &sMsg);
extern void raw_notify_newline(dbref);
extern void clearstrings(DESC *);
extern void queue_write_LEN(DESC *, const char *, size_t n);
extern void queue_write(DESC *, const char *);
extern void queue_string(DESC *, const UTF8 *);
extern void queue_string(DESC *d, const mux_string &s);
extern void freeqs(DESC *);
extern void welcome_user(DESC *);
extern void save_command(DESC *, CBLK *);
extern void announce_disconnect(dbref, DESC *, const UTF8 *);
extern int boot_by_port(SOCKET port, bool bGod, const UTF8 *message);
extern void find_oldest(dbref target, DESC *dOldest[2]);
extern void check_idle(void);
void Task_ProcessCommand(void *arg_voidptr, int arg_iInteger);
extern int site_check(struct in_addr, SITE *);
extern dbref  find_connected_name(dbref, UTF8 *);
extern void do_command(DESC *, UTF8 *);
extern void desc_addhash(DESC *);

// From predicates.cpp
//
#define alloc_desc(s) (DESC *)pool_alloc(POOL_DESC, (UTF8 *)s, (UTF8 *)__FILE__, __LINE__)
#define free_desc(b) pool_free(POOL_DESC,(UTF8 *)(b), (UTF8 *)__FILE__, __LINE__)
extern void handle_prog(DESC *d, UTF8 *message);

// From player.cpp
//
void record_login(dbref, bool, UTF8 *, UTF8 *, UTF8 *, UTF8 *);
extern dbref connect_player(UTF8 *, UTF8 *, UTF8 *, UTF8 *, UTF8 *);


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
#define DESC_SAFEITER_ALL(d,n) \
    for (d=descriptor_list,n=((d!=NULL) ? d->next : NULL); \
         d; \
         d=n,n=((n!=NULL) ? n->next : NULL))

#endif // !__INTERFACE__H
