/*! \file interface.h
 * \brief Driver-side struct definitions: DESC and port_info.
 *
 * This header provides the full descriptor_data (DESC) struct definition
 * and port_info.  Engine files that only need forward declarations of DESC
 * or program_data, plus telnet/NVT constants, get those from externs.h.
 *
 * Only include this header in files that dereference DESC members or
 * access main_game_ports.
 */

#pragma once
#include "copyright.h"

#ifndef INTERFACE_H
#define INTERFACE_H

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
