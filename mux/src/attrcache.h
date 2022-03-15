/*! \file attrcache.h
 * \brief Attribute caching module.
 *
 */

#ifndef ATTRCACHE_H
#define ATTRCACHE_H

typedef struct Aname
{
    unsigned int    object;
    unsigned int    attrnum;

    bool operator==(const Aname a) const
    {
        return this->object == a.object
            && this->attrnum == a.attrnum;
    }
} Aname;

const UTF8 *cache_get(Aname *nam, size_t *pLen);
bool cache_put(Aname *nam, const UTF8 *obj, size_t len);
int cache_init(_In_z_ const UTF8* game_dir_file, _In_z_ const UTF8* game_pag_file, int nCachePages);
void cache_close(void);
void cache_tick(void);
bool cache_sync(void);
void cache_del(_In_ Aname *nam);

#endif // !ATTRCACHE_H
