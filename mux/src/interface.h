/*! \file interface.h
 * \brief Declarations relating to network connections.
 *
 */

#include "copyright.h"

#ifndef __INTERFACE__H
#define __INTERFACE__H

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
extern NAMETAB default_charset_nametab[];

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
#define NVT_BS   ((unsigned char)'\x08')
#define NVT_DEL  ((unsigned char)'\x7F')
#define NVT_EOR  ((unsigned char)'\xEF')
#define NVT_NOP  ((unsigned char)'\xF1')
#define NVT_GA   ((unsigned char)'\xF9')
#define NVT_WILL ((unsigned char)'\xFB')
#define NVT_WONT ((unsigned char)'\xFC')
#define NVT_DO   ((unsigned char)'\xFD')
#define NVT_DONT ((unsigned char)'\xFE')
#define NVT_IAC  ((unsigned char)'\xFF')
#define NVT_SB   ((unsigned char)'\xFA')
#define NVT_SE   ((unsigned char)'\xF0')

// Telnet Options
//
#define TELNET_BINARY   ((unsigned char)'\x00')
#define TELNET_SGA      ((unsigned char)'\x03')
#define TELNET_EOR      ((unsigned char)'\x19')
#define TELNET_NAWS     ((unsigned char)'\x1F')
#define TELNET_TTYPE    ((unsigned char)'\x18')
#define TELNET_OLDENV   ((unsigned char)'\x24')
#define TELNET_ENV      ((unsigned char)'\x27')
#define TELNET_CHARSET  ((unsigned char)'\x2A')
#define TELNET_STARTTLS ((unsigned char)'\x2E')

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
#define CHARSET_CP437           1
#define CHARSET_LATIN1          2
#define CHARSET_LATIN2          3
#define CHARSET_UTF8            4

typedef struct descriptor_data DESC;
struct descriptor_data
{
  SOCKET socket;

#ifdef UNIX_SSL
  SSL *ssl_session;
#endif

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

  CLinearTimeAbsolute connected_at;
  CLinearTimeAbsolute last_time;

  int flags;
  int retries_left;
  int command_count;
  int timeout;
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

  mux_sockaddr address;

  UTF8 addr[51];
  UTF8 username[11];
  UTF8 doing[SIZEOF_DOING_STRING];
};

int him_state(DESC *d, unsigned char chOption);
int us_state(DESC *d, unsigned char chOption);
void enable_him(DESC *d, unsigned char chOption);
void disable_him(DESC *d, unsigned char chOption);
void enable_us(DESC *d, unsigned char chOption);
void disable_us(DESC *d, unsigned char chOption);

/* flags in the flag field */
#define DS_CONNECTED    0x0001      // player is connected.
#define DS_AUTODARK     0x0002      // Wizard was auto set dark.
#define DS_PUEBLOCLIENT 0x0004      // Client is Pueblo-enhanced.

extern DESC *descriptor_list;
extern unsigned int ndescriptors;
#if defined(WINDOWS_NETWORKING)
extern CRITICAL_SECTION csDescriptorList;
#endif // WINDOWS_NETWORKING

typedef struct
{
    bool fMatched;
    mux_sockaddr msa;
    SOCKET socket;
#ifdef UNIX_SSL
    bool   fSSL;
#endif
} PortInfo;

#define MAX_LISTEN_PORTS 30
#ifdef UNIX_SSL
extern bool initialize_ssl();
extern void shutdown_ssl();

extern PortInfo main_game_ports[MAX_LISTEN_PORTS * 2];
#else
extern PortInfo main_game_ports[MAX_LISTEN_PORTS];
#endif
extern int      num_main_game_ports;

extern void emergency_shutdown(void);
extern void shutdownsock(DESC *, int);
extern void SetupPorts(int *pnPorts, PortInfo aPorts[], IntArray *pia, IntArray *piaSSL, const UTF8 *ip_address);
extern void shovechars(int nPorts, PortInfo aPorts[]);
void process_output(DESC *, int);
#if defined(HAVE_WORKING_FORK)
extern void dump_restart_db(void);
#endif // HAVE_WORKING_FORK

extern void build_signal_names_table(void);
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
extern void queue_write_LEN(DESC *, const UTF8 *, size_t n);
extern void queue_write(DESC *, const UTF8 *);
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
            n=((d!=nullptr) ? d->hashnext : nullptr); \
         d; \
         d=n,n=((n!=nullptr) ? n->hashnext : nullptr))
#define DESC_SAFEITER_ALL(d,n) \
    for (d=descriptor_list,n=((d!=nullptr) ? d->next : nullptr); \
         d; \
         d=n,n=((n!=nullptr) ? n->next : nullptr))

// From bsd.cpp.
//
void close_sockets(bool emergency, const UTF8 *message);
int mux_getaddrinfo(const UTF8 *node, const UTF8 *service, const MUX_ADDRINFO *hints, MUX_ADDRINFO **res);
void mux_freeaddrinfo(MUX_ADDRINFO *res);
int mux_getnameinfo(const mux_sockaddr *msa, UTF8 *host, size_t hostlen, UTF8 *serv, size_t servlen, int flags);
#if defined(HAVE_WORKING_FORK) || defined(WINDOWS_THREADS)
void boot_slave(dbref executor, dbref caller, dbref enactor, int eval, int key);
#endif
#if defined(WINDOWS_THREADS)
void shutdown_slave();
#endif
#if defined(HAVE_WORKING_FORK)
void CleanUpSlaveSocket(void);
void CleanUpSlaveProcess(void);
#ifdef STUB_SLAVE
void CleanUpStubSlaveSocket(void);
void WaitOnStubSlaveProcess(void);
void boot_stubslave(dbref executor, dbref caller, dbref enactor, int key);
extern "C" MUX_RESULT DCL_API pipepump(void);
#endif // STUB_SLAVE
#endif // HAVE_WORKING_FORK
#ifdef UNIX_SSL
void CleanUpSSLConnections(void);
#endif

extern NAMETAB sigactions_nametab[];

#if defined(UNIX_NETWORKING_SELECT)
extern int maxd;
#endif // UNIX_NETWORKING_SELECT

extern long DebugTotalSockets;

#if defined(WINDOWS_NETWORKING)
extern long DebugTotalThreads;
extern long DebugTotalSemaphores;
extern HANDLE game_process_handle;
typedef int __stdcall FGETNAMEINFO(const SOCKADDR *pSockaddr, socklen_t SockaddrLength, PCHAR pNodeBuffer,
    DWORD NodeBufferSize, PCHAR pServiceBuffer, DWORD ServiceBufferSize, INT Flags);
typedef int __stdcall FGETADDRINFO(PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA *pHints,
    PADDRINFOA *ppResult);
typedef void __stdcall FFREEADDRINFO(PADDRINFOA pAddrInfo);

extern FGETNAMEINFO *fpGetNameInfo;
extern FGETADDRINFO *fpGetAddrInfo;
extern FFREEADDRINFO *fpFreeAddrInfo;
#endif // WINDOWS_NETWORKING

// From timer.cpp
//
#if defined(WINDOWS_NETWORKING)
void Task_FreeDescriptor(void *arg_voidptr, int arg_Integer);
void Task_DeferredClose(void *arg_voidptr, int arg_Integer);
#endif // WINDOWS_NETWORKING

#endif // !__INTERFACE__H
