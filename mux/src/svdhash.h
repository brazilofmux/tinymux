/*! \file svdhash.h
 * \brief CHashPage, CHashFile, CHashTable modules.
 *
 */

#ifndef SVDHASH_H
#define SVDHASH_H

//
// These are from 'svdhash.cpp'.
//
extern int cs_writes;       // total writes
extern int cs_reads;        // total reads
extern int cs_dels;         // total deletes
extern int cs_fails;        // attempts to grab nonexistent
extern int cs_syncs;        // total cache syncs
extern int cs_dbreads;      // total read-throughs
extern int cs_dbwrites;     // total write-throughs
extern int cs_rhits;        // total reads filled from cache
extern int cs_whits;        // total writes to dirty cache

//#define HP_PROTECTION

#define SECTOR_SIZE     512
#define LBUF_BLOCKED   (SECTOR_SIZE*((LBUF_SIZE+SECTOR_SIZE-1)/SECTOR_SIZE))
#define HT_SIZEOF_PAGE (1*LBUF_BLOCKED)
#define HF_SIZEOF_PAGE (3*LBUF_BLOCKED)

extern uint32_t CRC32_ProcessBuffer
(
    uint32_t         ulCrc,
    const void    *pBuffer,
    size_t         nBuffer
);

extern uint32_t CRC32_ProcessInteger(uint32_t nInteger);
extern uint32_t CRC32_ProcessInteger2
(
    uint32_t nInteger1,
    uint32_t nInteger2
);

extern uint32_t HASH_ProcessBuffer
(
    uint32_t       ulHash,
    const void  *arg_pBuffer,
    size_t       nBuffer
);

extern uint32_t munge_hash(const UTF8 *pBuffer);

#if defined(_SGI_SOURCE) || ((UINT16_MAX-2) <= HF_SIZEOF_PAGE)
typedef uint32_t UINT_OFFSET;
#define UINT_OFFSET_MAX_VALUE UINT32_MAX
#define EXPAND_TO_BOUNDARY(x) (((x) + 3) & (~3))
#else
typedef uint16_t UINT_OFFSET;
#define UINT_OFFSET_MAX_VALUE UINT16_MAX
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
    uint32_t         m_nTotalInsert;
    uint32_t         m_nDirEmptyLeft;
    uint32_t         m_nHashGroup;
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
    uint32_t m_checksum;
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
            uint32_t        nHash;
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
    uint32_t          m_nDirEmptyTrigger;

#ifdef HP_PROTECTION
    bool ValidateAllocatedBlock(uint32_t iDir);
    bool ValidateFreeBlock(HP_HEAPOFFSET oBlock);
    bool ValidateFreeList(void);
#endif // HP_PROTECTION
    bool HeapAlloc(uint32_t iDir, HP_HEAPLENGTH nRecord, uint32_t nHash, void *pRecord);
    void SetVariablePointers(void);
    void SetFixedPointers(void);
    void GetStats(HP_HEAPLENGTH nExtra, int *pnRecords, HP_HEAPLENGTH *pnAllocatedSize, uint32_t *pnGoodDirSize);

public:
    CHashPage(void);
    bool Allocate(unsigned int nPageSize);
    ~CHashPage(void);
    void Empty(uint32_t arg_nDepth, uint32_t arg_nHashGroup, uint32_t arg_nDirSize);
#ifdef HP_PROTECTION
    void Protection(void);
    bool Validate(void);
#endif // HP_PROTECTION

#define HP_INSERT_SUCCESS_DEFRAG 0
#define HP_INSERT_SUCCESS        1
#define HP_INSERT_ERROR_FULL     2
#define HP_INSERT_ERROR_ILLEGAL  3
#define IS_HP_SUCCESS(x) ((x) <= HP_INSERT_SUCCESS)
    int Insert(HP_HEAPLENGTH nRecord, uint32_t nHash, void *pRecord);
    uint32_t FindFirstKey(uint32_t nHash, unsigned int *numchecks);
    uint32_t FindNextKey(uint32_t i, uint32_t nHash, unsigned int *numchecks);
    uint32_t FindFirst(HP_PHEAPLENGTH pnRecord, void *pRecord);
    uint32_t FindNext(HP_PHEAPLENGTH pnRecord, void *pRecord);
    void HeapCopy(uint32_t iDir, HP_PHEAPLENGTH pnRecord, void *pRecord);
    void HeapFree(uint32_t iDir);
    void HeapUpdate(uint32_t iDir, HP_HEAPLENGTH nRecord, void *pRecord);


    uint32_t GetDepth(void);
    bool Split(CHashPage &hp0, CHashPage &hp1);

    bool Defrag(HP_HEAPLENGTH nExtra);
    void GetRange(uint32_t arg_nDirDepth, uint32_t &nStart, uint32_t &nEnd);
};

#define HF_FIND_FIRST  HP_DIR_EMPTY
#define HF_FIND_END    HP_DIR_EMPTY

// Status codes returned by cache_init() for both CHashFile and SQLite backends.
//
#define HF_OPEN_STATUS_ERROR -1
#define HF_OPEN_STATUS_NEW    0
#define HF_OPEN_STATUS_OLD    1

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
    int64_t           m_nDeletions;
    int64_t           m_nScans;
    int64_t           m_nHits;
    int64_t           m_nChecks;
    unsigned int    m_nMaxScan;

    bool DoubleDirectory(void);

    void Init(void);
    void Final(void);

public:
    CHashTable(void);
    void ResetStats(void);
    void GetStats( unsigned int *hashsize, int *entries, int64_t *deletes,
                   int64_t *scans, int64_t *hits, int64_t *checks, int *max_scan);
    unsigned int GetEntryCount();

    void Reset(void);
    bool Insert(HP_HEAPLENGTH nRecord, uint32_t nHash, void *pRecord);
    uint32_t FindFirstKey(uint32_t nHash);
    uint32_t FindNextKey(uint32_t iDir, uint32_t nHash);
    uint32_t FindFirst(HP_PHEAPLENGTH pnRecord, void *pRecord);
    uint32_t FindNext(HP_PHEAPLENGTH pnRecord, void *pRecord);
    void Copy(uint32_t iDir, HP_PHEAPLENGTH pnRecord, void *pRecord);
    void Remove(uint32_t iDir);
    void Update(uint32_t iDir, HP_HEAPLENGTH nRecord, void *pRecord);
    ~CHashTable(void);
};

#endif //!SVDHASH_H
