// attrcache.h
//
// $Id: attrcache.h,v 1.5 2002-08-03 18:50:17 sdennis Exp $
//

#ifndef _ATTRCACHE_H
#define _ATTRCACHE_H

typedef struct Aname
{
    unsigned int    object;
    unsigned int    attrnum;
} Aname;

extern const char *cache_get(Aname *nam, int *pLen);
extern BOOL cache_put(Aname *nam, const char *obj, int len);
extern int  cache_init(const char *game_dir_file, const char *game_pag_file);
extern void cache_close(void);
extern void cache_tick(void);
extern BOOL cache_sync(void);
extern void cache_del(Aname *nam);

#endif // !_ATTRCACHE_H
