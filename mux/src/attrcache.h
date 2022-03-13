/*! \file attrcache.h
 * \brief Attribute caching module.
 *
 */

#ifndef _ATTRCACHE_H
#define _ATTRCACHE_H

typedef struct Aname
{
    unsigned int    object;
    unsigned int    attrnum;
} Aname;

struct AnameCompare {
    bool operator()(const Aname lhs, const Aname rhs) const
    {
        if (lhs.object != rhs.object)
            return lhs.object < rhs.object;
        else
            return lhs.attrnum < rhs.attrnum;
    }
};

const UTF8 *cache_get(Aname *nam, size_t *pLen);
bool cache_put(Aname *nam, const UTF8 *obj, size_t len);
int cache_init(_In_z_ const UTF8* game_dir_file, _In_z_ const UTF8* game_pag_file, int nCachePages);
void cache_close(void);
void cache_tick(void);
bool cache_sync(void);
void cache_del(_In_ Aname *nam);
void cache_get_stats
(
    int* entries,
    INT64* deletes,
    INT64* scans,
    INT64* hits
);

#endif // !_ATTRCACHE_H
