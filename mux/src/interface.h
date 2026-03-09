/*! \file interface.h
 * \brief Driver-side declarations: DESC struct, telnet, and socket types.
 *
 * This header provides the full descriptor_data (DESC) struct definition
 * and related networking constants.  Engine files that only need forward
 * declarations of DESC or program_data get those from externs.h.
 *
 * Only include this header in files that dereference DESC members.
 */

#pragma once
#include "copyright.h"

#ifndef INTERFACE_H
#define INTERFACE_H

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

// Full descriptor_data struct definition.
// Engine files use only the forward declaration from externs.h.
//
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
  bool charset_request_pending;
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

#ifdef UNIX_SSL
extern port_info main_game_ports[MAX_LISTEN_PORTS * 2];
#else
extern port_info main_game_ports[MAX_LISTEN_PORTS];
#endif
extern int      num_main_game_ports;

#endif // !INTERFACE_H
