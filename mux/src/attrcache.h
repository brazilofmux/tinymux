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

const UTF8 *cache_get(Aname *nam, size_t *pLen, dbref *owner, int *flags);
bool cache_put(Aname *nam, const UTF8 *obj, size_t len, dbref owner, int flags);
int cache_init(const UTF8 *indb);
void cache_close(void);
void cache_tick(void);
bool cache_sync(void);
bool cache_del(Aname *nam);
void cache_preload(dbref obj);
void list_cache_stats(dbref player);

#endif // !ATTRCACHE_H
