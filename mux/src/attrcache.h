// attrcache.h
//
// $Id: attrcache.h,v 1.2 2001-11-19 19:40:09 sdennis Exp $
//

#ifndef _ATTRCACHE_H
#define _ATTRCACHE_H

typedef struct Aname
{
    unsigned int    object;
    unsigned int    attrnum;
} Aname;

extern char *cache_get(Aname *nam, int *pLen);
extern int  cache_put(Aname *nam, char *obj, int len);
extern int  cache_check(void);
extern int  cache_init(const char *game_dir_file, const char *game_pag_file);
extern void cache_close(void);
extern void cache_tick(void);
extern int  cache_sync(void);
extern void cache_del(Aname *nam);

#endif
