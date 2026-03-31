/*! \file dbt_jit_mem.h
 * \brief JIT code buffer allocation and W^X memory management.
 *
 * Provides a platform-neutral interface for allocating executable memory
 * and managing write/execute transitions on platforms that enforce W^X
 * (write XOR execute) memory policies.
 *
 * See docs/DBT-PORTABILITY.md for the full multi-platform design.
 */

#ifndef DBT_JIT_MEM_H
#define DBT_JIT_MEM_H

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#if defined(__APPLE__) && defined(__aarch64__)
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#endif

// Allocate a region of read-write-execute memory for JIT code.
// Returns nullptr on failure.
//
static inline uint8_t *jit_alloc(size_t size) {
#ifdef _WIN32
    void *p = VirtualAlloc(nullptr, size,
                           MEM_COMMIT | MEM_RESERVE,
                           PAGE_EXECUTE_READWRITE);
    return static_cast<uint8_t *>(p);
#elif defined(__APPLE__) && defined(__aarch64__)
    void *p = mmap(nullptr, size,
                   PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    return (p == MAP_FAILED) ? nullptr : static_cast<uint8_t *>(p);
#else
    void *p = mmap(nullptr, size,
                   PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? nullptr : static_cast<uint8_t *>(p);
#endif
}

// Free a JIT code buffer allocated by jit_alloc.
//
static inline void jit_free(uint8_t *ptr, size_t size) {
    if (!ptr) return;
#ifdef _WIN32
    (void)size;
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, size);
#endif
}

// Begin a JIT code write sequence.  On Apple AArch64, toggles the
// current thread from execute to write mode.  No-op on other platforms.
//
static inline void jit_write_begin() {
#if defined(__APPLE__) && defined(__aarch64__)
    pthread_jit_write_protect_np(false);
#endif
}

// End a JIT code write sequence and flush the instruction cache for
// the written region.  On x86-64, the icache is coherent so this is
// a no-op.  On AArch64, the icache must be explicitly invalidated.
//
static inline void jit_write_end(void *addr, size_t len) {
#if defined(__APPLE__) && defined(__aarch64__)
    pthread_jit_write_protect_np(true);
    sys_icache_invalidate(addr, len);
#elif defined(__aarch64__)
    __builtin___clear_cache(static_cast<char *>(addr),
                            static_cast<char *>(addr) + len);
#else
    (void)addr;
    (void)len;
#endif
}

#endif // DBT_JIT_MEM_H
