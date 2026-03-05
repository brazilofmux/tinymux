/*! \file svdhash.h
 * \brief CRC32, hash, and cache statistics utilities.
 *
 */

#ifndef SVDHASH_H
#define SVDHASH_H

//
// Cache statistics counters (legacy, kept for @list cache display).
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

// Status codes returned by cache_init().
//
#define HF_OPEN_STATUS_ERROR -1
#define HF_OPEN_STATUS_NEW    0
#define HF_OPEN_STATUS_OLD    1

#endif //!SVDHASH_H
