/*! \file dbt_elf64.h
 * \brief Minimal ELF64 loader for RV64IMD test binaries.
 *
 * Loads PT_LOAD segments into a flat memory buffer.  No symbol table,
 * no relocations, no dynamic linking — just enough to load a
 * statically-linked freestanding RV64 executable.
 */

#ifndef DBT_ELF64_H
#define DBT_ELF64_H

#include "dbt_interp.h"
#include <cstdint>

// Loaded binary representation.
//
struct rv64_binary_t {
    uint8_t *memory;
    size_t   memory_size;
    uint64_t entry_point;
    uint64_t stack_top;
};

// Load an ELF64 RISC-V executable into a flat memory buffer.
// Returns 0 on success, -1 on error (prints diagnostics to stderr).
// Caller must call rv64_free_binary() when done.
//
int rv64_load_elf(const char *filename, rv64_binary_t *bin);

// Free resources allocated by rv64_load_elf.
//
void rv64_free_binary(rv64_binary_t *bin);

#endif // DBT_ELF64_H
