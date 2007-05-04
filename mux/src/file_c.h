// file_c.h -- File cache header file.
//
// $Id: file_c.h,v 1.1 2002-05-24 06:53:15 sdennis Exp $
//

#include "copyright.h"
#ifndef __FILE_C
#define __FILE_C

#include "db.h"
#include "interface.h"

/* File caches.  These _must_ track the fcache array in file_c.c */

#define FC_CONN     0
#define FC_CONN_SITE    1
#define FC_CONN_DOWN    2
#define FC_CONN_FULL    3
#define FC_CONN_GUEST   4
#define FC_CONN_REG 5
#define FC_CREA_NEW 6
#define FC_CREA_REG 7
#define FC_MOTD     8
#define FC_WIZMOTD  9
#define FC_QUIT     10
#define FC_LAST     10

/* File cache routines */

extern void FDECL(fcache_rawdump, (SOCKET fd, int num));
extern void FDECL(fcache_dump, (DESC *d, int num));
extern void FDECL(fcache_send, (dbref, int));
extern void FDECL(fcache_load, (dbref));
extern void NDECL(fcache_init);

#endif
