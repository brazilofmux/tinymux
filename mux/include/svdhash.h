/*! \file svdhash.h
 * \brief CRC32 and hash utilities.
 *
 */

#ifndef SVDHASH_H
#define SVDHASH_H

extern LIBMUX_API uint32_t CRC32_ProcessBuffer
(
    uint32_t         ulCrc,
    const void    *pBuffer,
    size_t         nBuffer
);

extern LIBMUX_API uint32_t CRC32_ProcessInteger(uint32_t nInteger);
extern LIBMUX_API uint32_t CRC32_ProcessInteger2
(
    uint32_t nInteger1,
    uint32_t nInteger2
);

extern LIBMUX_API uint32_t HASH_ProcessBuffer
(
    uint32_t       ulHash,
    const void  *arg_pBuffer,
    size_t       nBuffer
);

extern LIBMUX_API uint32_t munge_hash(const UTF8 *pBuffer);

// Status codes returned by cache_init().
//
#define HF_OPEN_STATUS_ERROR -1
#define HF_OPEN_STATUS_NEW    0
#define HF_OPEN_STATUS_OLD    1

#endif //!SVDHASH_H
