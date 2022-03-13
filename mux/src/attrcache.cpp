/*! \file attrcache.cpp
 * \brief Attribute caching module.
 *
 * The functions here manage the upper-level attribute value cache for
 * disk-based mode. It's not used in memory-based builds. The lower-level
 * cache is managed in svdhash.cpp
 *
 * The upper-level cache is organized by a CHashTable and a linked list. The
 * former allows random access while the linked list helps find the
 * least-recently-used attribute.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
using namespace std;

#if !defined(MEMORY_BASED)

static CHashFile hfAttributeFile;
static bool cache_initted = false;

static bool cache_redirected = false;
#define NUMBER_OF_TEMPORARY_FILES 8
static FILE *temporary_files[NUMBER_OF_TEMPORARY_FILES];

CLinearTimeAbsolute cs_ltime;

#pragma pack(1)
struct attribute_record
{
    Aname attrKey;
    UTF8 attrText[LBUF_SIZE];
};
#pragma pack()

static attribute_record temp_record;
static size_t cache_size = 0;
static INT64 cache_deletes = 0;
static INT64 cache_scans = 0;
static INT64 cache_hits = 0;

void cache_get_stats
(
    int* entries,
    INT64* deletes,
    INT64* scans,
    INT64* hits
)
{
    *entries = static_cast<int>(mudstate.attribute_lru_cache_list.size());
    *deletes = cache_deletes;
    *scans = cache_scans;
    *hits = cache_hits;
}

int cache_init(_In_z_ const UTF8 *game_dir_file, _In_z_ const UTF8 *game_pag_file, int nCachePages)
{
    if (cache_initted)
    {
        return HF_OPEN_STATUS_ERROR;
    }

    const int cc = hfAttributeFile.Open(game_dir_file, game_pag_file, nCachePages);
    if (cc != HF_OPEN_STATUS_ERROR)
    {
        // Mark caching system live
        //
        cache_initted = true;
        cs_ltime.GetUTC();
    }
    return cc;
}

void cache_redirect(void)
{
    for (int i = 0; i < NUMBER_OF_TEMPORARY_FILES; i++)
    {
        UTF8 temporary_file_name[20];
        mux_sprintf(temporary_file_name, sizeof(temporary_file_name), T("convtemp.%d"), i);
        mux_assert(mux_fopen(&temporary_files[i], temporary_file_name, T("wb+")));
        mux_assert(temporary_files[i]);
        setvbuf(temporary_files[i], nullptr, _IOFBF, 16384);
    }
    cache_redirected = true;
}

void cache_pass2(void)
{
    attribute_record record{};
    cache_redirected = false;
    mux_fprintf(stderr, T("2nd Pass:\n"));
    for (int i = 0; i < NUMBER_OF_TEMPORARY_FILES; i++)
    {
        mux_fprintf(stderr, T("File %d: "), i);
        const long int li = fseek(temporary_files[i], 0, SEEK_SET);
        mux_assert(0L == li);

        int cnt = 1000;
        size_t nSize;
        for (;;)
        {
            size_t cc = fread(&nSize, 1, sizeof(nSize), temporary_files[i]);
            if (cc != sizeof(nSize))
            {
                break;
            }
            cc = fread(&record, 1, nSize, temporary_files[i]);
            mux_assert(cc == nSize);
            cache_put(&record.attrKey, record.attrText, nSize - sizeof(Aname));
            if (cnt-- == 0)
            {
                fputc('.', stderr);
                fflush(stderr);
                cnt = 1000;
            }
        }
        fclose(temporary_files[i]);
        UTF8 temporary_file_name[20];
        mux_sprintf(temporary_file_name, sizeof(temporary_file_name), T("convtemp.%d"), i);
        RemoveFile(temporary_file_name);
        mux_fprintf(stderr, T(ENDLINE));
    }
}

void cache_cleanup(void)
{
    for (int i = 0; i < NUMBER_OF_TEMPORARY_FILES; i++)
    {
        fclose(temporary_files[i]);
        UTF8 temporary_file_name[20];
        mux_sprintf(temporary_file_name, sizeof(temporary_file_name), T("convtemp.%d"), i);
        RemoveFile(temporary_file_name);
    }
}

void cache_close(void)
{
    hfAttributeFile.CloseAll();
    cache_initted = false;
}

void cache_tick(void)
{
    hfAttributeFile.Tick();
}

static void trim_attribute_cache(void)
{
    // Check to see if the cache needs to be trimmed.
    //
    while (cache_size > mudconf.max_cache_size)
    {
        if (mudstate.attribute_lru_cache_list.empty())
        {
            cache_size = 0;
            break;
        }

        // Blow the oldest thing away.
        //
        const auto it = mudstate.attribute_lru_cache_map.find(mudstate.attribute_lru_cache_list.front());
        cache_size -= it->second.first.size();
        mudstate.attribute_lru_cache_map.erase(it);
        mudstate.attribute_lru_cache_list.pop_front();
    }
}

const UTF8 *cache_get(Aname *nam, size_t *pLen)
{
    if (  nam == static_cast<Aname*>(nullptr)
       || !cache_initted)
    {
        *pLen = 0;
        return nullptr;
    }

    cache_scans++;
    if (!mudstate.bStandAlone)
    {
        // Check the cache, first.
        //
        const auto it = mudstate.attribute_lru_cache_map.find(*nam);
        if (it != mudstate.attribute_lru_cache_map.end())
        {
            cache_hits++;

            // It was in the cache, so indicate this entry as the newest and return it.
            //
            mudstate.attribute_lru_cache_list.splice(
                mudstate.attribute_lru_cache_list.end(),
                mudstate.attribute_lru_cache_list,
                (*it).second.second
            );
        	*pLen = (*it).second.first.size();
            return (*it).second.first.data();
        }
    }

    const UINT32 nHash = CRC32_ProcessInteger2(nam->object, nam->attrnum);
    UINT32 iDir = hfAttributeFile.FindFirstKey(nHash);

    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        hfAttributeFile.Copy(iDir, &nRecord, &temp_record);

        if (  temp_record.attrKey.attrnum == nam->attrnum
           && temp_record.attrKey.object == nam->object)
        {
	        const int nLength = static_cast<int>(nRecord - sizeof(Aname));
            *pLen = nLength;
            if (!mudstate.bStandAlone)
            {
                // Add this information to the cache.
                //
                std::vector<UTF8> v(temp_record.attrText, temp_record.attrText + nLength);
                auto it = mudstate.attribute_lru_cache_list.insert(mudstate.attribute_lru_cache_list.end(), *nam);
                mudstate.attribute_lru_cache_map.insert(std::make_pair(*nam, std::make_pair(v, it)));
                cache_size += v.size();
                trim_attribute_cache();
            }
            return temp_record.attrText;
        }
        iDir = hfAttributeFile.FindNextKey(iDir, nHash);
    }

    // We didn't find that one.
    //
    if (!mudstate.bStandAlone)
    {
        // Add an empty entry in the cache.
        //
        std::vector<UTF8> v;
        auto it = mudstate.attribute_lru_cache_list.insert(mudstate.attribute_lru_cache_list.end(), *nam);
        mudstate.attribute_lru_cache_map.insert(std::make_pair(*nam, std::make_pair(v, it)));
        cache_size += v.size();
        trim_attribute_cache();
    }

    *pLen = 0;
    return nullptr;
}


// cache_put no longer frees the pointer.
//
bool cache_put(Aname *nam, const UTF8 *value, size_t len)
{
    if (  !value
       || !nam
       || !cache_initted
       || len == 0)
    {
        return false;
    }
#if defined(HAVE_WORKING_FORK)
    if (mudstate.write_protect)
    {
        Log.tinyprintf(T("cache_put((%d,%d), \xE2\x80\x98%s\xE2\x80\x99, %u) while database is write-protected" ENDLINE),
            nam->object, nam->attrnum, value, len);
        return false;
    }
#endif // HAVE_WORKING_FORK

    if (len > sizeof(temp_record.attrText))
    {
        len = sizeof(temp_record.attrText);
    }

    // Removal from DB.
    //
    const UINT32 nHash = CRC32_ProcessInteger2(nam->object, nam->attrnum);

    if (cache_redirected)
    {
        temp_record.attrKey = *nam;
        memcpy(temp_record.attrText, value, len);
        temp_record.attrText[len-1] = '\0';

        const int iFile = (NUMBER_OF_TEMPORARY_FILES-1) & (nHash >> 29);
        const size_t nSize = len + sizeof(Aname);
        fwrite(&nSize, 1, sizeof(nSize), temporary_files[iFile]);
        fwrite(&temp_record, 1, nSize, temporary_files[iFile]);
        return true;
    }

    UINT32 iDir = hfAttributeFile.FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        hfAttributeFile.Copy(iDir, &nRecord, &temp_record);

        if (  temp_record.attrKey.attrnum == nam->attrnum
           && temp_record.attrKey.object  == nam->object)
        {
            hfAttributeFile.Remove(iDir);
        }
        iDir = hfAttributeFile.FindNextKey(iDir, nHash);
    }

    temp_record.attrKey = *nam;
    memcpy(temp_record.attrText, value, len);
    temp_record.attrText[len-1] = '\0';

    // Insertion into DB.
    //
    if (!hfAttributeFile.Insert(static_cast<HP_HEAPLENGTH>(len + sizeof(Aname)), nHash, &temp_record))
    {
        Log.tinyprintf(T("cache_put((%d,%d), \xE2\x80\x98%s\xE2\x80\x99, %u) failed" ENDLINE),
            nam->object, nam->attrnum, value, len);
    }

    if (!mudstate.bStandAlone)
    {
        // Update cache.
        //
        std::vector<UTF8> v(temp_record.attrText, temp_record.attrText + len);
        auto it2 = mudstate.attribute_lru_cache_list.insert(mudstate.attribute_lru_cache_list.end(), *nam);

        const auto it = mudstate.attribute_lru_cache_map.find(*nam);
        if (it != mudstate.attribute_lru_cache_map.end())
        {
            // It was in the cache map, so replace the pair in the mapping and delete the old list entry.
            //
            cache_size += v.size() - it->second.first.size();
            mudstate.attribute_lru_cache_list.erase((*it).second.second);
            it->second = std::make_pair(v, it2);
        }
        else
        {
            // It wasn't in the cache map, so create a mapping.
            //
            mudstate.attribute_lru_cache_map.insert(std::make_pair(*nam, std::make_pair(v, it2)));
            cache_size += v.size();
        }
        trim_attribute_cache();
    }
    return true;
}

bool cache_sync(void)
{
    hfAttributeFile.Sync();
    return true;
}

// Delete this attribute from the database.
//
void cache_del(_In_ Aname *nam)
{
    if (  !nam
       || !cache_initted)
    {
        return;
    }

#if defined(HAVE_WORKING_FORK)
    if (mudstate.write_protect)
    {
        Log.tinyprintf(T("cache_del((%d,%d)) while database is write-protected" ENDLINE),
            nam->object, nam->attrnum);
        return;
    }
#endif // HAVE_WORKING_FORK

    const UINT32 nHash = CRC32_ProcessInteger2(nam->object, nam->attrnum);
    UINT32 iDir = hfAttributeFile.FindFirstKey(nHash);

    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        hfAttributeFile.Copy(iDir, &nRecord, &temp_record);

        if (  temp_record.attrKey.attrnum == nam->attrnum
           && temp_record.attrKey.object == nam->object)
        {
            hfAttributeFile.Remove(iDir);
        }
        iDir = hfAttributeFile.FindNextKey(iDir, nHash);
    }

    if (!mudstate.bStandAlone)
    {
        // Update cache.
        //
        const auto it = mudstate.attribute_lru_cache_map.find(*nam);
        if (it != mudstate.attribute_lru_cache_map.end())
        {
            // It was in the cache, so delete it.
            //
            cache_size -= it->second.first.size();
            mudstate.attribute_lru_cache_list.erase((*it).second.second);
            mudstate.attribute_lru_cache_map.erase(it);
            cache_deletes++;
        }
    }
}

#endif // MEMORY_BASED
