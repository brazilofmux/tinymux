// svdhash.h -- CHashPage, CHashFile, CHashTable modules.
//
// $Id: svdhash.h,v 1.7 2003-07-30 05:13:43 sdennis Exp $
//
// MUX 2.3
// Copyright (C) 1998 through 2003 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.  
//
#ifndef SVDHASH_H
#define SVDHASH_H

//#define HP_PROTECTION

extern UINT32 CRC32_ProcessBuffer
(
    UINT32         ulCrc,
    const void    *pBuffer,
    unsigned int   nBuffer
);

extern UINT32 CRC32_ProcessInteger(UINT32 nInteger);
extern UINT32 CRC32_ProcessInteger2
(
    UINT32 nInteger1,
    UINT32 nInteger2
);

extern UINT32 HASH_ProcessBuffer
(
    UINT32       ulHash,
    const void  *arg_pBuffer,
    size_t       nBuffer
);

#ifdef _SGI_SOURCE
#define EXPAND_TO_BOUNDARY(x) (((x) + 3) & (~3))
typedef unsigned int HP_HEAPOFFSET, *HP_PHEAPOFFSET;
typedef unsigned int HP_HEAPLENGTH, *HP_PHEAPLENGTH;
typedef unsigned int HP_DIRINDEX, *HP_PDIRINDEX;
#else //_SGI_SOURCE
#define EXPAND_TO_BOUNDARY(x) (((x) + 1) & (~1))
typedef unsigned short HP_HEAPOFFSET, *HP_PHEAPOFFSET;
typedef unsigned short HP_HEAPLENGTH, *HP_PHEAPLENGTH;
typedef unsigned short HP_DIRINDEX, *HP_PDIRINDEX;
#endif // _SGI_SOURCE

#define HP_SIZEOF_HEAPOFFSET sizeof(HP_HEAPOFFSET)
#define HP_SIZEOF_HEAPLENGTH sizeof(HP_HEAPLENGTH)
#define HP_SIZEOF_DIRINDEX sizeof(HP_DIRINDEX);

#pragma pack(1)
typedef struct tagHPHeader
{
    UINT32         m_nTotalInsert;
    UINT32         m_nDirEmptyLeft;
    UINT32         m_nHashGroup;
    HP_DIRINDEX    m_nDirSize;
    HP_DIRINDEX    m_Primes[16];
    HP_HEAPOFFSET  m_oFreeList;
    HP_DIRINDEX    m_nDepth;
} HP_HEADER, *HP_PHEADER;

#define HP_NIL_OFFSET 0xFFFFU

// Possible special values for m_pDirectory[i]
//
#define HP_DIR_EMPTY   0xFFFFU
#define HP_DIR_DELETED 0xFFFEU

typedef struct tagHPTrailer
{
    UINT32 m_checksum;
} HP_TRAILER, *HP_PTRAILER;

typedef struct tagHPHeapNode
{
    HP_HEAPLENGTH nBlockSize;
    union
    {
        HP_HEAPOFFSET oNext;
        struct
        {
            HP_HEAPLENGTH nRecordSize;
            UINT32        nHash;
        } s;
    } u;
} HP_HEAPNODE, *HP_PHEAPNODE;
#define HP_SIZEOF_HEAPNODE sizeof(HP_HEAPNODE)
#pragma pack()

#define HP_MIN_HEAP_ALLOC HP_SIZEOF_HEAPNODE

typedef unsigned long HF_FILEOFFSET, *HF_PFILEOFFSET;
#define HF_SIZEOF_FILEOFFSET sizeof(HF_FILEOFFSET)

class CHashPage
{
private:
    unsigned char  *m_pPage;
    unsigned int    m_nPageSize;
    HP_PHEADER      m_pHeader;
    HP_PHEAPOFFSET  m_pDirectory;
    unsigned char  *m_pHeapStart;
    unsigned char  *m_pHeapEnd;
    HP_PTRAILER     m_pTrailer;

    int             m_iDir;
    int             m_nProbesLeft;
    UINT32          m_nDirEmptyTrigger;

#ifdef HP_PROTECTION
    bool ValidateAllocatedBlock(UINT32 iDir);
    bool ValidateFreeBlock(HP_HEAPOFFSET oBlock);
    bool ValidateFreeList(void);
#endif // HP_PROTECTION
    bool HeapAlloc(HP_DIRINDEX iDir, HP_HEAPLENGTH nRecord, UINT32 nHash, void *pRecord);
    void SetVariablePointers(void);
    void SetFixedPointers(void);
    void GetStats(HP_HEAPLENGTH nExtra, int *pnRecords, HP_HEAPLENGTH *pnAllocatedSize, int *pnGoodDirSize);

public:
    CHashPage(void);
    bool Allocate(unsigned int nPageSize);
    ~CHashPage(void);
    void Empty(HP_DIRINDEX arg_nDepth, UINT32 arg_nHashGroup, HP_DIRINDEX arg_nDirSize);
#ifdef HP_PROTECTION
    void Protection(void);
    bool Validate(void);
#endif // HP_PROTECTION

#define HP_INSERT_SUCCESS_DEFRAG 0
#define HP_INSERT_SUCCESS        1
#define HP_INSERT_ERROR_FULL     2
#define HP_INSERT_ERROR_ILLEGAL  3
#define IS_HP_SUCCESS(x) ((x) <= HP_INSERT_SUCCESS)
    int Insert(HP_HEAPLENGTH nRecord, UINT32 nHash, void *pRecord);
    HP_DIRINDEX FindFirstKey(UINT32 nHash, unsigned int *numchecks);
    HP_DIRINDEX FindNextKey(HP_DIRINDEX i, UINT32 nHash, unsigned int *numchecks);
    HP_DIRINDEX FindFirst(HP_PHEAPLENGTH pnRecord, void *pRecord);
    HP_DIRINDEX FindNext(HP_PHEAPLENGTH pnRecord, void *pRecord);
    void HeapCopy(HP_DIRINDEX iDir, HP_PHEAPLENGTH pnRecord, void *pRecord);
    void HeapFree(HP_DIRINDEX iDir);
    void HeapUpdate(HP_DIRINDEX iDir, HP_HEAPLENGTH nRecord, void *pRecord);

    bool WritePage(HANDLE hFile, HF_FILEOFFSET oWhere);
    bool ReadPage(HANDLE hFile, HF_FILEOFFSET oWhere);

    HP_DIRINDEX GetDepth(void);
    bool Split(CHashPage &hp0, CHashPage &hp1);

    bool Defrag(HP_HEAPLENGTH nExtra);
    void GetRange(UINT32 arg_nDirDepth, UINT32 &nStart, UINT32 &nEnd);
};


#define HF_FIND_FIRST  HP_DIR_EMPTY
#define HF_FIND_END    HP_DIR_EMPTY

#define HF_CACHE_EMPTY       0
#define HF_CACHE_CLEAN       1
#define HF_CACHE_UNPROTECTED 2
#define HF_CACHE_UNWRITTEN   3
#define HF_CACHE_NUM_STATES  4

typedef struct tagHashFileCache
{
    CHashPage     m_hp;
    HF_FILEOFFSET m_o;
    int           m_iState;
    int           m_Age;
} HF_CACHE;

class CHashFile
{
private:
    HANDLE          m_hDirFile;
    HANDLE          m_hPageFile;
    int             iCache;
    int             m_iLastEmpty;
    int             m_iLastFlushed;
    int             *m_hpCacheLookup;
    int             m_nhpCacheLookup;
    int             m_iAgeNext;
    HF_FILEOFFSET   oEndOfFile;
    unsigned int    m_nDir;
    unsigned int    m_nDirDepth;
    HF_CACHE        *m_Cache;
    int             m_nCache;
    HF_PFILEOFFSET  m_pDir;
    bool DoubleDirectory(void);

    int AllocateEmptyPage(int nSafe, int Safe[]);
    int ReadCache(HF_FILEOFFSET oPage, int *pHits);
    bool FlushCache(int iCache);
    void WriteDirectory(void);
    bool EmptyDirectory(void);

    void Init(void);
    void InitCache(int nCachePages);
    void FinalCache(void);

    bool CreateFileSet(const char *szDirFile, const char *szPageFile);
    bool RebuildDirectory(void);
    bool ReadDirectory(void);

public:
    CHashFile(void);
#define HF_OPEN_STATUS_ERROR -1
#define HF_OPEN_STATUS_NEW    0
#define HF_OPEN_STATUS_OLD    1
    int Open(const char *szDirFile, const char *szPageFile, int nCachePages);
    bool Insert(HP_HEAPLENGTH nRecord, UINT32 nHash, void *pRecord);
    HP_DIRINDEX FindFirstKey(UINT32 nHash);
    HP_DIRINDEX FindNextKey(HP_DIRINDEX iDir, UINT32 nHash);
    void Copy(HP_DIRINDEX iDir, HP_PHEAPLENGTH pnRecord, void *pRecord);
    void Remove(HP_DIRINDEX iDir);
    void CloseAll(void);
    void Sync(void);
    void Tick(void);
    ~CHashFile(void);
};

typedef CHashPage *pCHashPage;

class CHashTable
{
private:
    unsigned int    m_nDir;
    unsigned int    m_nDirDepth;
    pCHashPage     *m_pDir;
    CHashPage      *m_hpLast;
    unsigned int    m_iPage;

    unsigned int    m_nPages;
    unsigned int    m_nEntries;
    INT64           m_nDeletions;
    INT64           m_nScans;
    INT64           m_nHits;
    INT64           m_nChecks;
    unsigned int    m_nMaxScan;

    bool DoubleDirectory(void);

    void Init(void);
    void Final(void);

public:
    CHashTable(void);
    void ResetStats(void);
    void GetStats( unsigned int *hashsize, int *entries, INT64 *deletes,
                   INT64 *scans, INT64 *hits, INT64 *checks, int *max_scan);

    void Reset(void);
    bool Insert(HP_HEAPLENGTH nRecord, UINT32 nHash, void *pRecord);
    HP_DIRINDEX FindFirstKey(UINT32 nHash);
    HP_DIRINDEX FindNextKey(HP_DIRINDEX iDir, UINT32 nHash);
    HP_DIRINDEX FindFirst(HP_PHEAPLENGTH pnRecord, void *pRecord);
    HP_DIRINDEX FindNext(HP_PHEAPLENGTH pnRecord, void *pRecord);
    void Copy(HP_DIRINDEX iDir, HP_PHEAPLENGTH pnRecord, void *pRecord);
    void Remove(HP_DIRINDEX iDir);
    void Update(HP_DIRINDEX iDir, HP_HEAPLENGTH nRecord, void *pRecord);
    ~CHashTable(void);
};


#define SIZEOF_LOG_BUFFER 1024
class CLogFile
{
private:
    CLinearTimeAbsolute m_ltaStarted;
#ifdef WIN32
    CRITICAL_SECTION csLog;
#endif // WIN32
    HANDLE m_hFile;
    size_t m_nSize;
    size_t m_nBuffer;
    char m_aBuffer[SIZEOF_LOG_BUFFER];
    bool bEnabled;
    char m_szPrefix[32];
    char m_szFilename[SIZEOF_PATHNAME];

    void CreateLogFile(void);
    void AppendLogFile(void);
    void CloseLogFile(void);
public:
    CLogFile(void);
    ~CLogFile(void);
    void WriteBuffer(int nString, const char *pString);
    void WriteString(const char *pString);
    void WriteInteger(int iNumber);
    void DCL_CDECL tinyprintf(char *pFormatSpec, ...);
    void Flush(void);
    void ChangePrefix(char *p);
    void EnableLogging(void);
};

extern CLogFile Log;

#endif //!SVDHASH_H

