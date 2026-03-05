/*! \file attrcache.cpp
 * \brief Attribute caching module.
 *
 * The functions here manage the upper-level attribute value cache for
 * disk-based mode. It's not used in memory-based builds. The lower-level
 * storage is either CHashFile (.dir/.pag) or SQLite (.db).
 *
 * The upper-level cache is organized by an unordered_map and a linked list.
 * The former allows random access while the linked list helps find the
 * least-recently-used attribute.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
using namespace std;

#if !defined(MEMORY_BASED)

#if defined(SQLITE_STORAGE)
#include "sqlite_backend.h"

CSQLiteBackend *g_pSQLiteBackend = nullptr;
#else
static CHashFile hfAttributeFile;
#endif

static bool cache_initted = false;

#if !defined(SQLITE_STORAGE)
static bool cache_redirected = false;
#define NUMBER_OF_TEMPORARY_FILES 8
static FILE *temporary_files[NUMBER_OF_TEMPORARY_FILES];
#endif

CLinearTimeAbsolute cs_ltime;

#if !defined(SQLITE_STORAGE)
#pragma pack(1)
struct attribute_record
{
    Aname attrKey;
    UTF8 attrText[LBUF_SIZE];
};
#pragma pack()

static attribute_record temp_record;
#else
// SQLite backend uses its own buffer for attribute retrieval.
//
static UTF8 sqlite_attr_buf[LBUF_SIZE];
#endif

static size_t cache_size = 0;

int cache_init(const UTF8 *game_dir_file, const UTF8 *game_pag_file, int nCachePages)
{
    if (cache_initted)
    {
        return HF_OPEN_STATUS_ERROR;
    }

#if defined(SQLITE_STORAGE)
    UNUSED_PARAMETER(game_pag_file);
    UNUSED_PARAMETER(nCachePages);

    g_pSQLiteBackend = new CSQLiteBackend();

    // Derive SQLite database path from the game dir file path.
    // Replace .dir extension with .db, or append .db.
    //
    char szPath[LBUF_SIZE];
    mux_strncpy((UTF8 *)szPath, game_dir_file, sizeof(szPath) - 1);
    szPath[sizeof(szPath) - 1] = '\0';

    // Look for .dir suffix and replace with .sqlite.
    // We use .sqlite instead of .db to avoid colliding with TinyMUX's
    // flatfile convention where .db is the input/output database.
    //
    size_t n = strlen(szPath);
    if (n > 4 && strcmp(szPath + n - 4, ".dir") == 0)
    {
        strcpy(szPath + n - 4, ".sqlite");
    }
    else
    {
        strcat(szPath, ".sqlite");
    }

    // Check if the database file exists before opening.
    // sqlite3_open creates the file if it doesn't exist.
    //
    bool bNewDatabase = (access(szPath, F_OK) != 0);

    if (!g_pSQLiteBackend->Open(szPath))
    {
        delete g_pSQLiteBackend;
        g_pSQLiteBackend = nullptr;
        return HF_OPEN_STATUS_ERROR;
    }

    cache_initted = true;
    cs_ltime.GetUTC();

    return bNewDatabase ? HF_OPEN_STATUS_NEW : HF_OPEN_STATUS_OLD;
#else
    const int cc = hfAttributeFile.Open(game_dir_file, game_pag_file, nCachePages);
    if (cc != HF_OPEN_STATUS_ERROR)
    {
        // Mark caching system live
        //
        cache_initted = true;
        cs_ltime.GetUTC();
    }
    return cc;
#endif
}

#if !defined(SQLITE_STORAGE)
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
#endif // !SQLITE_STORAGE

void cache_close(void)
{
#if defined(SQLITE_STORAGE)
    if (g_pSQLiteBackend)
    {
        g_pSQLiteBackend->Close();
        delete g_pSQLiteBackend;
        g_pSQLiteBackend = nullptr;
    }
#else
    hfAttributeFile.CloseAll();
#endif
    cache_initted = false;
}

void cache_tick(void)
{
#if defined(SQLITE_STORAGE)
    if (g_pSQLiteBackend)
    {
        g_pSQLiteBackend->Tick();
    }
#else
    hfAttributeFile.Tick();
#endif
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

    if (!mudstate.bStandAlone)
    {
        // Check the cache, first.
        //
        const auto it = mudstate.attribute_lru_cache_map.find(*nam);
        if (it != mudstate.attribute_lru_cache_map.end())
        {
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

#if defined(SQLITE_STORAGE)
    size_t nLength = 0;
    if (g_pSQLiteBackend->Get(nam->object, nam->attrnum,
                              sqlite_attr_buf, sizeof(sqlite_attr_buf), &nLength))
    {
        *pLen = nLength;
        if (!mudstate.bStandAlone)
        {
            // Add this information to the cache.
            //
            vector<UTF8> v(sqlite_attr_buf, sqlite_attr_buf + nLength);
            auto it = mudstate.attribute_lru_cache_list.insert(mudstate.attribute_lru_cache_list.end(), *nam);
            mudstate.attribute_lru_cache_map.insert(make_pair(*nam, make_pair(v, it)));
            cache_size += v.size();
            trim_attribute_cache();
        }
        return sqlite_attr_buf;
    }
#else
    const uint32_t nHash = CRC32_ProcessInteger2(nam->object, nam->attrnum);
    uint32_t iDir = hfAttributeFile.FindFirstKey(nHash);

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
                vector<UTF8> v(temp_record.attrText, temp_record.attrText + nLength);
                auto it = mudstate.attribute_lru_cache_list.insert(mudstate.attribute_lru_cache_list.end(), *nam);
                mudstate.attribute_lru_cache_map.insert(make_pair(*nam, make_pair(v, it)));
                cache_size += v.size();
                trim_attribute_cache();
            }
            return temp_record.attrText;
        }
        iDir = hfAttributeFile.FindNextKey(iDir, nHash);
    }
#endif

    // We didn't find that one.
    //
    if (!mudstate.bStandAlone)
    {
        // Add an empty entry in the cache.
        //
        vector<UTF8> v;
        auto it = mudstate.attribute_lru_cache_list.insert(mudstate.attribute_lru_cache_list.end(), *nam);
        mudstate.attribute_lru_cache_map.insert(make_pair(*nam, make_pair(v, it)));
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

#if defined(SQLITE_STORAGE)
    if (len > LBUF_SIZE)
    {
        len = LBUF_SIZE;
    }

    // Write-through: write to SQLite immediately.
    //
    if (!g_pSQLiteBackend->Put(nam->object, nam->attrnum, value, len))
    {
        Log.tinyprintf(T("cache_put((%d,%d), \xE2\x80\x98%s\xE2\x80\x99, %u) failed" ENDLINE),
            nam->object, nam->attrnum, value, len);
    }
#else
    if (len > sizeof(temp_record.attrText))
    {
        len = sizeof(temp_record.attrText);
    }

    // Removal from DB.
    //
    const uint32_t nHash = CRC32_ProcessInteger2(nam->object, nam->attrnum);

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

    uint32_t iDir = hfAttributeFile.FindFirstKey(nHash);
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
#endif

    if (!mudstate.bStandAlone)
    {
        // Update cache.
        //
        vector<UTF8> v(value, value + len);
        auto it2 = mudstate.attribute_lru_cache_list.insert(mudstate.attribute_lru_cache_list.end(), *nam);

        const auto it = mudstate.attribute_lru_cache_map.find(*nam);
        if (it != mudstate.attribute_lru_cache_map.end())
        {
            // It was in the cache map, so replace the pair in the mapping and delete the old list entry.
            //
            cache_size += v.size() - it->second.first.size();
            mudstate.attribute_lru_cache_list.erase((*it).second.second);
            it->second = make_pair(v, it2);
        }
        else
        {
            // It wasn't in the cache map, so create a mapping.
            //
            mudstate.attribute_lru_cache_map.insert(make_pair(*nam, make_pair(v, it2)));
            cache_size += v.size();
        }
        trim_attribute_cache();
    }
    return true;
}

bool cache_sync(void)
{
#if defined(SQLITE_STORAGE)
    if (g_pSQLiteBackend)
    {
        g_pSQLiteBackend->Sync();
    }
#else
    hfAttributeFile.Sync();
#endif
    return true;
}

// Delete this attribute from the database.
//
void cache_del(Aname *nam)
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

#if defined(SQLITE_STORAGE)
    g_pSQLiteBackend->Del(nam->object, nam->attrnum);
#else
    const uint32_t nHash = CRC32_ProcessInteger2(nam->object, nam->attrnum);
    uint32_t iDir = hfAttributeFile.FindFirstKey(nHash);

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
#endif

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
        }
    }
}

#endif // MEMORY_BASED
