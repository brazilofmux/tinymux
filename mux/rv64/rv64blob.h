/*! \file rv64blob.h
 * \brief RV64 binary blob format for pre-compiled RISC-V code.
 *
 * A simple, portable container for RV64 code and read-only data.
 * Designed to be loaded into a guest memory region and called via
 * JAL from JIT-compiled code.
 *
 * Layout:
 *   [rv64_blob_header]    fixed 32-byte header
 *   [code section]        RV64 instructions (.text)
 *   [rodata section]      read-only data (.rodata)
 *   [entry table]         array of rv64_blob_entry
 *
 * All offsets are relative to the start of the file.
 * All multi-byte values are little-endian (matching RV64).
 */

#ifndef RV64BLOB_H
#define RV64BLOB_H

#include <stdint.h>

#define RV64_BLOB_MAGIC   0x34365652  /* "RV64" in little-endian */
#define RV64_BLOB_VERSION 1

/* Maximum function name length (including NUL). */
#define RV64_ENTRY_NAME_LEN 32

#pragma pack(push, 1)

typedef struct {
    uint32_t magic;          /* RV64_BLOB_MAGIC */
    uint32_t version;        /* RV64_BLOB_VERSION */
    uint32_t code_offset;    /* file offset of code section */
    uint32_t code_size;      /* size of code section in bytes */
    uint32_t rodata_offset;  /* file offset of rodata section (0 if none) */
    uint32_t rodata_size;    /* size of rodata section in bytes */
    uint32_t entry_offset;   /* file offset of entry table */
    uint32_t entry_count;    /* number of entries */
} rv64_blob_header;

typedef struct {
    char     name[RV64_ENTRY_NAME_LEN];  /* function name, NUL-padded */
    uint32_t code_off;       /* offset within code section */
    uint32_t flags;          /* reserved, must be 0 */
} rv64_blob_entry;

#pragma pack(pop)

#endif /* RV64BLOB_H */
