// svdocache.cpp -- Attribute caching module
//
// $Id: svdocache.cpp,v 1.2 2000-04-16 07:33:28 sdennis Exp $
//
// MUX 2.0
// Copyright (C) 1998 through 2000 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved. Permission is given to
// use this code for building and hosting text-based game servers.
// Permission is given to use this code for other non-commercial
// purposes. To use this code for commercial purposes other than
// building/hosting text-based game servers, contact the author at
// Stephen Dennis <sdennis@svdltd.com> for another license.
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "mudconf.h"

CHashFile hfAttributeFile;

static int cache_initted = FALSE;
#ifdef STANDALONE
static int cache_redirected = FALSE;
#define N_TEMP_FILES 4
FILE *TempFiles[N_TEMP_FILES];
#endif

CLinearTimeAbsolute cs_ltime;

#pragma pack(1)
typedef struct tagAttrRecord
{
    Aname attrKey;
    char attrText[LBUF_SIZE];
} ATTR_RECORD, *PATTR_RECORD;
#pragma pack()

static ATTR_RECORD TempRecord;

#ifndef STANDALONE
#define DO_CACHEING
#endif

#ifdef DO_CACHEING
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
#endif // DO_CACHEING

int cache_init(const char *game_dir_file, const char *game_pag_file)
{
    if (cache_initted)
    {
        return HF_OPEN_STATUS_ERROR;
    }

    int cc = hfAttributeFile.Open(game_dir_file, game_pag_file);
    if (cc != HF_OPEN_STATUS_ERROR)
    {
        // Mark caching system live
        //
        cache_initted = TRUE;
        cs_ltime.GetUTC();
    }
    return cc;
}

#ifdef STANDALONE
void cache_redirect(void)
{
    for (int i = 0; i < N_TEMP_FILES; i++)
    {
        char TempFileName[20];
        sprintf(TempFileName, "$convtemp.%d", i);
        TempFiles[i] = fopen(TempFileName, "wb+");
        if (TempFiles[i] == NULL)
        {
            Log.printf("Cannot create %s.\n", TempFileName);
            Log.Flush();
            abort();
        }
        setvbuf(TempFiles[i], NULL, _IOFBF, 16384);
    }
    cache_redirected = TRUE;
}

void cache_pass2(void)
{
    cache_redirected = FALSE;
    fprintf(stderr, "2nd Pass:\n");
    for (int i = 0; i < N_TEMP_FILES; i++)
    {
        fprintf(stderr, "File %d: ", i);
        fseek(TempFiles[i], 0, SEEK_SET);
        int cnt = 1000;
        int nSize;
        for (;;)
        {
            int cc = fread(&nSize, 1, sizeof(nSize), TempFiles[i]);
            if (cc != sizeof(nSize))
            {
                break;
            }
            ATTR_RECORD Record;
            fread(&Record, 1, nSize, TempFiles[i]);
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
        fprintf(stderr, "\n");
    }
}
#endif
void cache_close(void)
{
    hfAttributeFile.CloseAll();
    cache_initted = FALSE;
}

void cache_reset(int trim)
{
    hfAttributeFile.Reset();
}

void cache_tick(void)
{
    hfAttributeFile.Tick();
}

#ifdef DO_CACHEING
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
#endif // DO_CACHEING

char *cache_get(Aname *nam, int *pLen)
{
    if (nam == (Aname *) 0 || !cache_initted)
    {
        *pLen = 0;
        return 0;
    }

#ifdef DO_CACHEING
    // Check the cache, first.
    //
    PCENT_HDR pCacheEntry = (PCENT_HDR)hashfindLEN((char *)nam, sizeof(Aname), &mudstate.acache_htab);
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
#endif // DO_CACHEING

    unsigned long nHash = CRC32_ProcessInteger2(nam->object, nam->attrnum);

    HP_DIRINDEX iDir;
    iDir = hfAttributeFile.FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        hfAttributeFile.Copy(iDir, &nRecord, &TempRecord);

        if ((TempRecord.attrKey.attrnum == nam->attrnum) && (TempRecord.attrKey.object == nam->object))
        {
            int nLength = nRecord - sizeof(Aname);
            *pLen = nLength;
#ifdef DO_CACHEING
            // Add this information to the cache.
            //
            pCacheEntry = (PCENT_HDR)MEMALLOC(sizeof(CENT_HDR)+nLength, __FILE__, __LINE__);
            if (pCacheEntry)
            {
                pCacheEntry->attrKey = *nam;
                pCacheEntry->nSize = nLength + sizeof(CENT_HDR);
                CacheSize += pCacheEntry->nSize;
                memcpy((char *)(pCacheEntry+1), TempRecord.attrText, nLength);
                ADD_ENTRY(pCacheEntry);
                hashaddLEN((char *)nam, sizeof(Aname), (int *)pCacheEntry, &mudstate.acache_htab);

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
                    hashdeleteLEN((char *)&(pCacheEntry->attrKey), sizeof(Aname), &mudstate.acache_htab);
                    MEMFREE(pCacheEntry, __FILE__, __LINE__);
                }
            }
#endif // DO_CACHEING
            return TempRecord.attrText;
        }
        iDir = hfAttributeFile.FindNextKey(iDir, nHash);
    }

    // We didn't find that one.
    //
    *pLen = 0;
    return 0;
}


// cache_put not longer frees the pointer.
//
BOOL cache_put(Aname *nam, Attr *value, int len)
{
    if (!value || !nam || !cache_initted)
    {
        return FALSE;
    }

    if (len > (int)sizeof(TempRecord.attrText))
    {
        len = sizeof(TempRecord.attrText);
    }

    // Removal from DB.
    //
    unsigned long nHash = CRC32_ProcessInteger2(nam->object, nam->attrnum);

#ifdef STANDALONE
    if (cache_redirected)
    {
        TempRecord.attrKey = *nam;
        memcpy(TempRecord.attrText, value, len);
        TempRecord.attrText[len-1] = '\0';

        int iFile = (N_TEMP_FILES-1) & (nHash >> 30);
        int nSize = len+sizeof(Aname);
        fwrite(&nSize, 1, sizeof(nSize), TempFiles[iFile]);
        fwrite(&TempRecord, 1, nSize, TempFiles[iFile]);
        return TRUE;
    }
#endif

    HP_DIRINDEX iDir = hfAttributeFile.FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        hfAttributeFile.Copy(iDir, &nRecord, &TempRecord);

        if ((TempRecord.attrKey.attrnum == nam->attrnum) && (TempRecord.attrKey.object == nam->object))
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

#ifdef DO_CACHEING

    // Update cache.
    //
    PCENT_HDR pCacheEntry = (PCENT_HDR)hashfindLEN((char *)nam, sizeof(Aname), &mudstate.acache_htab);
    if (pCacheEntry)
    {
        // It was in the cache, so delete it.
        //
        REMOVE_ENTRY(pCacheEntry);
        CacheSize -= pCacheEntry->nSize;
        hashdeleteLEN((char *)nam, sizeof(Aname), &mudstate.acache_htab);
        MEMFREE(pCacheEntry, __FILE__, __LINE__);
    }

    // Add information about the new entry back into the cache.
    //
    pCacheEntry = (PCENT_HDR)MEMALLOC(sizeof(CENT_HDR)+len, __FILE__, __LINE__);
    if (pCacheEntry)
    {
        pCacheEntry->attrKey = *nam;
        pCacheEntry->nSize = len + sizeof(CENT_HDR);
        CacheSize += pCacheEntry->nSize;
        memcpy((char *)(pCacheEntry+1), TempRecord.attrText, len);
        ADD_ENTRY(pCacheEntry);
        hashaddLEN((char *)nam, sizeof(Aname), (int *)pCacheEntry, &mudstate.acache_htab);

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
            hashdeleteLEN((char *)&(pCacheEntry->attrKey), sizeof(Aname), &mudstate.acache_htab);
            MEMFREE(pCacheEntry, __FILE__, __LINE__);
        }
    }
#endif // DO_CACHEING

    return TRUE;
}


BOOL cache_sync(void)
{
    hfAttributeFile.Sync();
    return TRUE;
}

/*
 * Delete this attribute from the database.
 */
void cache_del(Aname *nam)
{
    if (!nam || !cache_initted)
        return;

    unsigned long nHash = CRC32_ProcessInteger2(nam->object, nam->attrnum);

    HP_DIRINDEX iDir = hfAttributeFile.FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        hfAttributeFile.Copy(iDir, &nRecord, &TempRecord);

        if ((TempRecord.attrKey.attrnum == nam->attrnum) && (TempRecord.attrKey.object == nam->object))
        {
            hfAttributeFile.Remove(iDir);
        }
        iDir = hfAttributeFile.FindNextKey(iDir, nHash);
    }

#ifdef DO_CACHEING

    // Update cache.
    //
    PCENT_HDR pCacheEntry = (PCENT_HDR)hashfindLEN((char *)nam, sizeof(Aname), &mudstate.acache_htab);
    if (pCacheEntry)
    {
        // It was in the cache, so delete it.
        //
        REMOVE_ENTRY(pCacheEntry);
        CacheSize -= pCacheEntry->nSize;;
        hashdeleteLEN((char *)nam, sizeof(Aname), &mudstate.acache_htab);
        MEMFREE(pCacheEntry, __FILE__, __LINE__);
    }
#endif // DO_CACHEING
}
