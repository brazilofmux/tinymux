/*! \file interface.h
 * \brief Declarations relating to network connections.
 *
 */

#pragma once
#include "copyright.h"

#ifndef INTERFACE_H
#define INTERFACE_H

/* these symbols must be defined by the interface */

/* Disconnection reason codes */

constexpr int R_MIN       = 0;
constexpr int R_UNKNOWN   = 0;   /* Unknown/unexpected */
constexpr int R_QUIT      = 1;   /* User quit */
constexpr int R_TIMEOUT   = 2;   /* Inactivity timeout */
constexpr int R_BOOT      = 3;   /* Victim of @boot, @toad, or @destroy */
constexpr int R_SOCKDIED  = 4;   /* Other end of socked closed it */
constexpr int R_GOING_DOWN = 5;   /* Game is going down */
constexpr int R_BADLOGIN  = 6;   /* Too many failed login attempts */
constexpr int R_GAMEDOWN  = 7;   /* Not admitting users now */
constexpr int R_LOGOUT    = 8;   /* Logged out w/o disconnecting */
constexpr int R_GAMEFULL  = 9;   /* Too many players logged in */
constexpr int R_RESTART   = 10;  /* Restarting, and this socket cannot be preserved */
constexpr int R_MAX       = 10;

/* Logged out command table definitions */

constexpr int CMD_QUIT    = 1;
constexpr int CMD_WHO     = 2;
constexpr int CMD_DOING   = 3;
constexpr int CMD_PREFIX  = 5;
constexpr int CMD_SUFFIX  = 6;
constexpr int CMD_LOGOUT  = 7;
constexpr int CMD_SESSION = 8;
constexpr int CMD_PUEBLOCLIENT = 9;
constexpr int CMD_INFO    = 10;

constexpr int CMD_MASK    = 0xff;
constexpr int CMD_NOxFIX  = 0x100;

extern NAMETAB logout_cmdtable[];
extern NAMETAB default_charset_nametab[];

typedef struct program_data
{
    dbref    wait_enactor;
    reg_ref *wait_regs[MAX_GLOBAL_REGS];
} program_data;

// Input state
//
constexpr int NVT_IS_NORMAL          = 0;
constexpr int NVT_IS_HAVE_IAC        = 1;
constexpr int NVT_IS_HAVE_IAC_WILL   = 2;
constexpr int NVT_IS_HAVE_IAC_WONT   = 3;
constexpr int NVT_IS_HAVE_IAC_DO     = 4;
constexpr int NVT_IS_HAVE_IAC_DONT   = 5;
constexpr int NVT_IS_HAVE_IAC_SB     = 6;
constexpr int NVT_IS_HAVE_IAC_SB_IAC = 7;

// Character Names
//
constexpr unsigned char NVT_BS   = 0x08;
constexpr unsigned char NVT_DEL  = 0x7F;
constexpr unsigned char NVT_EOR  = 0xEF;
constexpr unsigned char NVT_NOP  = 0xF1;
constexpr unsigned char NVT_GA   = 0xF9;
constexpr unsigned char NVT_WILL = 0xFB;
constexpr unsigned char NVT_WONT = 0xFC;
constexpr unsigned char NVT_DO   = 0xFD;
constexpr unsigned char NVT_DONT = 0xFE;
constexpr unsigned char NVT_IAC  = 0xFF;
constexpr unsigned char NVT_SB   = 0xFA;
constexpr unsigned char NVT_SE   = 0xF0;

// Telnet Options
//
constexpr unsigned char TELNET_BINARY   = 0x00;
constexpr unsigned char TELNET_SGA      = 0x03;
constexpr unsigned char TELNET_EOR      = 0x19;
constexpr unsigned char TELNET_NAWS     = 0x1F;
constexpr unsigned char TELNET_TTYPE    = 0x18;
constexpr unsigned char TELNET_OLDENV   = 0x24;
constexpr unsigned char TELNET_ENV      = 0x27;
constexpr unsigned char TELNET_CHARSET  = 0x2A;

// Telnet Option Negotiation States
//
constexpr int OPTION_NO               = 0;
constexpr int OPTION_YES              = 1;
constexpr int OPTION_WANTNO_EMPTY     = 2;
constexpr int OPTION_WANTNO_OPPOSITE  = 3;
constexpr int OPTION_WANTYES_EMPTY    = 4;
constexpr int OPTION_WANTYES_OPPOSITE = 5;

// Telnet subnegotiation requests
// Multiple meanings, depending on TELOPT
//
constexpr int TELNETSB_IS             = 0;
constexpr int TELNETSB_VAR            = 0;
constexpr int TELNETSB_REQUEST        = 1;
constexpr int TELNETSB_SEND           = 1;
constexpr int TELNETSB_VALUE          = 1;
constexpr int TELNETSB_FOLLOWS        = 1;
constexpr int TELNETSB_INFO           = 2;
constexpr int TELNETSB_ACCEPT         = 2;
constexpr int TELNETSB_REPLY          = 2;
constexpr int TELNETSB_ESC            = 2;
constexpr int TELNETSB_REJECT         = 3;
constexpr int TELNETSB_NAME           = 3;
constexpr int TELNETSB_USERVAR        = 3;

constexpr int CHARSET_ASCII           = 0;
constexpr int CHARSET_CP437           = 1;
constexpr int CHARSET_LATIN1          = 2;
constexpr int CHARSET_LATIN2          = 3;
constexpr int CHARSET_UTF8            = 4;

enum class SocketState {
    Initialized = 0,
    Accepted,
#ifdef UNIX_SSL
    SSLAcceptAgain,
    SSLAcceptWantWrite,
    SSLAcceptWantRead,
    SSLReadWantWrite,
    SSLReadWantRead,
    SSLWriteWantWrite,
    SSLWriteWantRead,
#endif
};

typedef struct descriptor_data DESC;
struct descriptor_data
{
  SocketState ss;
  SOCKET socket;

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
  std::deque<std::string> output_queue;
  size_t input_size;
  size_t input_tot;
  size_t input_lost;
  std::deque<std::string> input_queue;
  UTF8 *raw_input_buf;
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
  struct program_data* program_data;

  mux_sockaddr address;

  UTF8 addr[51];
  UTF8 username[11];
  UTF8 doing[SIZEOF_DOING_STRING];
};

int him_state(const DESC *d, unsigned char chOption);
int us_state(const DESC *d, unsigned char chOption);
void enable_him(DESC *d, unsigned char chOption);
void disable_him(DESC *d, unsigned char chOption);
void enable_us(DESC *d, unsigned char chOption);
void disable_us(DESC *d, unsigned char chOption);

/* flags in the flag field */
constexpr int DS_CONNECTED    = 0x0001;      // player is connected.
constexpr int DS_AUTODARK     = 0x0002;      // Wizard was auto set dark.
constexpr int DS_PUEBLOCLIENT = 0x0004;      // Client is Pueblo-enhanced.

typedef struct port_info
{
    bool fMatched{};
    mux_sockaddr msa;
    SOCKET socket{};
#ifdef UNIX_SSL
    bool   fSSL;
#endif
} port_info;

constexpr int MAX_LISTEN_PORTS = 30;
#ifdef UNIX_SSL
extern port_info main_game_ports[MAX_LISTEN_PORTS * 2];
#else
extern port_info main_game_ports[MAX_LISTEN_PORTS];
#endif
extern int      num_main_game_ports;

extern void emergency_shutdown();
extern void shutdownsock(DESC *, int);
void process_output(DESC *, int);
void process_input_helper(DESC *d, char *pBytes, int nBytes);
#if defined(HAVE_WORKING_FORK)
extern void dump_restart_db(void);
#endif // HAVE_WORKING_FORK

extern void build_signal_names_table();
extern void set_signals();

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
extern void save_command(DESC *, const UTF8 *, size_t);
extern void init_desc(DESC *d);
extern void destroy_desc(DESC *d);
extern void announce_disconnect(dbref, DESC *, const UTF8 *);
extern int boot_by_port(SOCKET port, bool bGod, const UTF8 *message);
extern void find_oldest(dbref target, DESC *dOldest[2]);
extern void check_idle();
void Task_ProcessCommand(void *arg_voidptr, int arg_iInteger);
extern dbref  find_connected_name(dbref, const UTF8 *);
extern void do_command(DESC *, UTF8 *);
extern void desc_addhash(DESC *);

// From predicates.cpp
//
#define alloc_desc(s) reinterpret_cast<DESC *>(pool_alloc(POOL_DESC, reinterpret_cast<UTF8 *>(s), reinterpret_cast<UTF8 *>(__FILE__), __LINE__))
#define free_desc(b) pool_free(POOL_DESC,reinterpret_cast<UTF8 *>(b), reinterpret_cast<UTF8 *>(__FILE__), __LINE__)
extern void handle_prog(DESC *d, UTF8 *message);

// From player.cpp
//
void record_login(dbref, bool, const UTF8 *, const UTF8 *, const UTF8 *, const UTF8 *);
extern dbref connect_player(UTF8 *, UTF8 *, UTF8 *, UTF8 *, UTF8 *);

// From bsd.cpp.
//
void close_sockets_emergency(const UTF8* message);
int mux_getaddrinfo(const UTF8 *node, const UTF8 *service, const MUX_ADDRINFO *hints, MUX_ADDRINFO **res);
void mux_freeaddrinfo(MUX_ADDRINFO *res);
int mux_getnameinfo(const mux_sockaddr *msa, UTF8 *host, size_t hostlen, UTF8 *serv, size_t servlen, int flags);
#if defined(HAVE_WORKING_FORK) && defined(STUB_SLAVE)
extern "C" MUX_RESULT DCL_API pipepump(void);
#endif // HAVE_WORKING_FORK && STUB_SLAVE

extern NAMETAB sigactions_nametab[];

extern long DebugTotalSockets;

#endif // !INTERFACE_H
