/*! \file svdhash.h
 * \brief CHashPage, CHashFile, CHashTable modules.
 *
 */

#ifndef SVDHASH_H
#define SVDHASH_H

//#define HP_PROTECTION

#define SECTOR_SIZE     512
#define LBUF_BLOCKED   (SECTOR_SIZE*((LBUF_SIZE+SECTOR_SIZE-1)/SECTOR_SIZE))
#define HT_SIZEOF_PAGE (1*LBUF_BLOCKED)
#define HF_SIZEOF_PAGE (3*LBUF_BLOCKED)

UINT32 CRC32_ProcessBuffer
(
    UINT32         ulCrc,
    const void    *pBuffer,
    size_t         nBuffer
);

UINT32 CRC32_ProcessInteger(UINT32 nInteger);
UINT32 CRC32_ProcessInteger2
(
    UINT32 nInteger1,
    UINT32 nInteger2
);

UINT32 CRC32_ProcessPointer(void *p);

UINT32 HASH_ProcessBuffer
(
    UINT32       ulHash,
    const void  *arg_pBuffer,
    size_t       nBuffer
);

#if defined(_SGI_SOURCE) || ((UINT16_MAX_VALUE-2) <= HF_SIZEOF_PAGE)
typedef UINT32 UINT_OFFSET;
#define UINT_OFFSET_MAX_VALUE UINT32_MAX_VALUE
#define EXPAND_TO_BOUNDARY(x) (((x) + 3) & (~3))
#else
typedef UINT16 UINT_OFFSET;
#define UINT_OFFSET_MAX_VALUE UINT16_MAX_VALUE
#define EXPAND_TO_BOUNDARY(x) (static_cast<HP_HEAPLENGTH>(((x) + 1) & (~1)))
#endif

typedef UINT_OFFSET HP_HEAPOFFSET, *HP_PHEAPOFFSET;
typedef UINT_OFFSET HP_HEAPLENGTH, *HP_PHEAPLENGTH;
typedef UINT_OFFSET HP_DIRINDEX, *HP_PDIRINDEX;

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

#define HP_NIL_OFFSET UINT_OFFSET_MAX_VALUE

// Possible special values for m_pDirectory[i]
//
#define HP_DIR_EMPTY   UINT_OFFSET_MAX_VALUE
#define HP_DIR_DELETED (UINT_OFFSET_MAX_VALUE-1)

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
    bool HeapAlloc(UINT32 iDir, HP_HEAPLENGTH nRecord, UINT32 nHash, void *pRecord);
    void SetVariablePointers(void);
    void SetFixedPointers(void);
    void GetStats(HP_HEAPLENGTH nExtra, int *pnRecords, HP_HEAPLENGTH *pnAllocatedSize, UINT32 *pnGoodDirSize);

public:
    CHashPage(void);
    bool Allocate(unsigned int nPageSize);
    ~CHashPage(void);
    void Empty(UINT32 arg_nDepth, UINT32 arg_nHashGroup, UINT32 arg_nDirSize);
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
    UINT32 FindFirstKey(UINT32 nHash, unsigned int *numchecks);
    UINT32 FindNextKey(UINT32 i, UINT32 nHash, unsigned int *numchecks);
    UINT32 FindFirst(HP_PHEAPLENGTH pnRecord, void *pRecord);
    UINT32 FindNext(HP_PHEAPLENGTH pnRecord, void *pRecord);
    void HeapCopy(UINT32 iDir, HP_PHEAPLENGTH pnRecord, void *pRecord);
    void HeapFree(UINT32 iDir);
    void HeapUpdate(UINT32 iDir, HP_HEAPLENGTH nRecord, void *pRecord);

    UINT32 GetDepth(void);
    bool Split(CHashPage &hp0, CHashPage &hp1);

    bool Defrag(HP_HEAPLENGTH nExtra);
    void GetRange(UINT32 arg_nDirDepth, UINT32 &nStart, UINT32 &nEnd);
};

#define HF_FIND_FIRST  HP_DIR_EMPTY
#define HF_FIND_END    HP_DIR_EMPTY

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
    unsigned int GetEntryCount();

    void Reset(void);
    bool Insert(HP_HEAPLENGTH nRecord, UINT32 nHash, void *pRecord);
    UINT32 FindFirstKey(UINT32 nHash);
    UINT32 FindNextKey(UINT32 iDir, UINT32 nHash);
    UINT32 FindFirst(HP_PHEAPLENGTH pnRecord, void *pRecord);
    UINT32 FindNext(HP_PHEAPLENGTH pnRecord, void *pRecord);
    void Copy(UINT32 iDir, HP_PHEAPLENGTH pnRecord, void *pRecord);
    void Remove(UINT32 iDir);
    void Update(UINT32 iDir, HP_HEAPLENGTH nRecord, void *pRecord);
    ~CHashTable(void);
};

#endif //!SVDHASH_H

