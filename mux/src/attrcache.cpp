// svdocache.cpp -- Attribute caching module.
//
// $Id: attrcache.cpp,v 1.8 2003-07-23 03:23:38 sdennis Exp $
//
// MUX 2.3
// Copyright (C) 1998 through 2003 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

CHashFile hfAttributeFile;
static bool cache_initted = false;

static bool cache_redirected = false;
#define N_TEMP_FILES 4
FILE *TempFiles[N_TEMP_FILES];

CLinearTimeAbsolute cs_ltime;

#pragma pack(1)
typedef struct tagAttrRecord
{
    Aname attrKey;
    char attrText[LBUF_SIZE];
} ATTR_RECORD, *PATTR_RECORD;
#pragma pack()

static ATTR_RECORD TempRecord;

typedef struct tagCacheEntryHeader
{
    struct tagCacheEntryHeader *pPrevEntry;
    struct tagCacheEntryHeader *pNextEntry;
    Aname attrKey;
    unsigned int nSize;
} CENT_HDR, *PCENT_HDR;

PCENT_HDR pCacheHead = 0;
PCENT_HDR pCacheTail = 0;
unsigned int CacheSize = 0;

int cache_init(const char *game_dir_file, const char *game_pag_file,
    int nCachePages)
{
    if (cache_initted)
    {
        return HF_OPEN_STATUS_ERROR;
    }

    int cc = hfAttributeFile.Open(game_dir_file, game_pag_file, nCachePages);
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
    for (int i = 0; i < N_TEMP_FILES; i++)
    {
        char TempFileName[20];
        sprintf(TempFileName, "$convtemp.%d", i);
        TempFiles[i] = fopen(TempFileName, "wb+");
        mux_assert(TempFiles[i]);
        setvbuf(TempFiles[i], NULL, _IOFBF, 16384);
    }
    cache_redirected = true;
}

void cache_pass2(void)
{
    ATTR_RECORD Record;
    cache_redirected = false;
    fprintf(stderr, "2nd Pass:\n");
    for (int i = 0; i < N_TEMP_FILES; i++)
    {
        fprintf(stderr, "File %d: ", i);
        fseek(TempFiles[i], 0, SEEK_SET);
        int cnt = 1000;
        size_t nSize;
        for (;;)
        {
            size_t cc = fread(&nSize, 1, sizeof(nSize), TempFiles[i]);
            if (cc != sizeof(nSize))
            {
                break;
            }
            cc = fread(&Record, 1, nSize, TempFiles[i]);
            mux_assert(cc == nSize);
            cache_put(&Record.attrKey, Record.attrText, nSize - sizeof(Aname));
            if (cnt-- == 0)
            {
                fputc('.', stderr);
                fflush(stderr);
                cnt = 1000;
            }
        }
        fclose(TempFiles[i]);
        char TempFileName[20];
        sprintf(TempFileName, "$convtemp.%d", i);
        RemoveFile(TempFileName);
        fprintf(stderr, ENDLINE);
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

void REMOVE_ENTRY(PCENT_HDR pEntry)
{
    // How is X positioned?
    //
    if (pEntry == pCacheHead)
    {
        if (pEntry == pCacheTail)
        {
            // HEAD --> X --> 0
            //    0 <--  <-- TAIL
            //
            // ASSERT: pEntry->pNextEntry == 0;
            // ASSERT: pEntry->pPrevEntry == 0;
            //
            pCacheHead = pCacheTail = 0;
        }
        else
        {
            // HEAD  --> X --> Y --> 0
            //    0 <--   <--   <--  TAIL
            //
            // ASSERT: pEntry->pNextEntry != 0;
            // ASSERT: pEntry->pPrevEntry == 0;
            //
            pCacheHead = pEntry->pNextEntry;
            pCacheHead->pPrevEntry = 0;
            pEntry->pNextEntry = 0;
        }
    }
    else if (pEntry == pCacheTail)
    {
        // HEAD  --> Y --> X --> 0
        //    0 <--   <--   <-- TAIL
        //
        // ASSERT: pEntry->pNextEntry == 0;
        // ASSERT: pEntry->pPrevEntry != 0;
        //
        pCacheTail = pEntry->pPrevEntry;
        pCacheTail->pNextEntry = 0;
        pEntry->pPrevEntry = 0;
    }
    else
    {
        // HEAD  --> Y --> X --> Z --> 0
        //    0 <--   <--   <--   <-- TAIL
        //
        // ASSERT: pEntry->pNextEntry != 0;
        // ASSERT: pEntry->pNextEntry != 0;
        //
        pEntry->pNextEntry->pPrevEntry = pEntry->pPrevEntry;
        pEntry->pPrevEntry->pNextEntry = pEntry->pNextEntry;
        pEntry->pNextEntry = 0;
        pEntry->pPrevEntry = 0;
    }
}

void ADD_ENTRY(PCENT_HDR pEntry)
{
    if (pCacheHead)
    {
        pCacheHead->pPrevEntry = pEntry;
    }
    pEntry->pNextEntry = pCacheHead;
    pEntry->pPrevEntry = 0;
    pCacheHead = pEntry;
    if (!pCacheTail)
    {
        pCacheTail = pCacheHead;
    }
}

const char *cache_get(Aname *nam, int *pLen)
{
    if (  nam == (Aname *) 0
       || !cache_initted)
    {
        *pLen = 0;
        return 0;
    }

    PCENT_HDR pCacheEntry = NULL;
    if (!mudstate.bStandAlone)
    {
        // Check the cache, first.
        //
        pCacheEntry = (PCENT_HDR)hashfindLEN(nam, sizeof(Aname),
            &mudstate.acache_htab);
        if (pCacheEntry)
        {
            // It was in the cache, so move this entry to the head of the queue.
            // and return a pointer to it.
            //
            REMOVE_ENTRY(pCacheEntry);
            ADD_ENTRY(pCacheEntry);
            *pLen = pCacheEntry->nSize - sizeof(CENT_HDR);
            return (char *)(pCacheEntry+1);
        }
    }

    UINT32 nHash = CRC32_ProcessInteger2(nam->object, nam->attrnum);

    HP_DIRINDEX iDir;
    iDir = hfAttributeFile.FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        hfAttributeFile.Copy(iDir, &nRecord, &TempRecord);

        if (  TempRecord.attrKey.attrnum == nam->attrnum
           && TempRecord.attrKey.object == nam->object)
        {
            int nLength = nRecord - sizeof(Aname);
            *pLen = nLength;
            if (!mudstate.bStandAlone)
            {
                // Add this information to the cache.
                //
                pCacheEntry = (PCENT_HDR)MEMALLOC(sizeof(CENT_HDR)+nLength);
                if (pCacheEntry)
                {
                    pCacheEntry->attrKey = *nam;
                    pCacheEntry->nSize = nLength + sizeof(CENT_HDR);
                    CacheSize += pCacheEntry->nSize;
                    memcpy((char *)(pCacheEntry+1), TempRecord.attrText, nLength);
                    ADD_ENTRY(pCacheEntry);
                    hashaddLEN(nam, sizeof(Aname), (int *)pCacheEntry,
                        &mudstate.acache_htab);

                    // Check to see if the cache needs to be trimmed.
                    //
                    while (CacheSize > mudconf.max_cache_size)
                    {
                        // Blow something away.
                        //
                        pCacheEntry = pCacheTail;
                        if (!pCacheEntry)
                        {
                            CacheSize = 0;
                            break;
                        }

                        REMOVE_ENTRY(pCacheEntry);
                        CacheSize -= pCacheEntry->nSize;
                        hashdeleteLEN(&(pCacheEntry->attrKey), sizeof(Aname),
                            &mudstate.acache_htab);
                        MEMFREE(pCacheEntry);
                        pCacheEntry = NULL;
                    }
                }
            }
            return TempRecord.attrText;
        }
        iDir = hfAttributeFile.FindNextKey(iDir, nHash);
    }

    // We didn't find that one.
    //
    *pLen = 0;
    return 0;
}


// cache_put no longer frees the pointer.
//
bool cache_put(Aname *nam, const char *value, int len)
{
    if (  !value
       || !nam
       || !cache_initted)
    {
        return false;
    }

    if (len > (int)sizeof(TempRecord.attrText))
    {
        len = sizeof(TempRecord.attrText);
    }

    // Removal from DB.
    //
    UINT32 nHash = CRC32_ProcessInteger2(nam->object, nam->attrnum);

    if (cache_redirected)
    {
        TempRecord.attrKey = *nam;
        memcpy(TempRecord.attrText, value, len);
        TempRecord.attrText[len-1] = '\0';

        int iFile = (N_TEMP_FILES-1) & (nHash >> 30);
        size_t nSize = len+sizeof(Aname);
        fwrite(&nSize, 1, sizeof(nSize), TempFiles[iFile]);
        fwrite(&TempRecord, 1, nSize, TempFiles[iFile]);
        return true;
    }

    HP_DIRINDEX iDir = hfAttributeFile.FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        hfAttributeFile.Copy(iDir, &nRecord, &TempRecord);

        if (  TempRecord.attrKey.attrnum == nam->attrnum
           && TempRecord.attrKey.object  == nam->object)
        {
            hfAttributeFile.Remove(iDir);
        }
        iDir = hfAttributeFile.FindNextKey(iDir, nHash);
    }

    TempRecord.attrKey = *nam;
    memcpy(TempRecord.attrText, value, len);
    TempRecord.attrText[len-1] = '\0';

    // Insertion into DB.
    //
    hfAttributeFile.Insert(len+sizeof(Aname), nHash, &TempRecord);

    if (!mudstate.bStandAlone)
    {
        // Update cache.
        //
        PCENT_HDR pCacheEntry = (PCENT_HDR)hashfindLEN(nam, sizeof(Aname),
            &mudstate.acache_htab);
        if (pCacheEntry)
        {
            // It was in the cache, so delete it.
            //
            REMOVE_ENTRY(pCacheEntry);
            CacheSize -= pCacheEntry->nSize;
            hashdeleteLEN((char *)nam, sizeof(Aname), &mudstate.acache_htab);
            MEMFREE(pCacheEntry);
            pCacheEntry = NULL;
        }

        // Add information about the new entry back into the cache.
        //
        pCacheEntry = (PCENT_HDR)MEMALLOC(sizeof(CENT_HDR)+len);
        if (pCacheEntry)
        {
            pCacheEntry->attrKey = *nam;
            pCacheEntry->nSize = len + sizeof(CENT_HDR);
            CacheSize += pCacheEntry->nSize;
            memcpy((char *)(pCacheEntry+1), TempRecord.attrText, len);
            ADD_ENTRY(pCacheEntry);
            hashaddLEN(nam, sizeof(Aname), (int *)pCacheEntry,
                &mudstate.acache_htab);

            // Check to see if the cache needs to be trimmed.
            //
            while (CacheSize > mudconf.max_cache_size)
            {
                // Blow something away.
                //
                pCacheEntry = pCacheTail;
                if (!pCacheEntry)
                {
                    CacheSize = 0;
                    break;
                }

                REMOVE_ENTRY(pCacheEntry);
                CacheSize -= pCacheEntry->nSize;
                hashdeleteLEN(&(pCacheEntry->attrKey), sizeof(Aname),
                    &mudstate.acache_htab);
                MEMFREE(pCacheEntry);
                pCacheEntry = NULL;
            }
        }
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
void cache_del(Aname *nam)
{
    if (  !nam
       || !cache_initted)
    {
        return;
    }

    UINT32 nHash = CRC32_ProcessInteger2(nam->object, nam->attrnum);

    HP_DIRINDEX iDir = hfAttributeFile.FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        hfAttributeFile.Copy(iDir, &nRecord, &TempRecord);

        if (  TempRecord.attrKey.attrnum == nam->attrnum
           && TempRecord.attrKey.object == nam->object)
        {
            hfAttributeFile.Remove(iDir);
        }
        iDir = hfAttributeFile.FindNextKey(iDir, nHash);
    }

    if (!mudstate.bStandAlone)
    {
        // Update cache.
        //
        PCENT_HDR pCacheEntry = (PCENT_HDR)hashfindLEN(nam, sizeof(Aname),
            &mudstate.acache_htab);
        if (pCacheEntry)
        {
            // It was in the cache, so delete it.
            //
            REMOVE_ENTRY(pCacheEntry);
            CacheSize -= pCacheEntry->nSize;;
            hashdeleteLEN((char *)nam, sizeof(Aname), &mudstate.acache_htab);
            MEMFREE(pCacheEntry);
            pCacheEntry = NULL;
        }
    }
}
