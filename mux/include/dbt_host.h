/*! \file dbt_host.h
 * \brief Compile-time host platform selection for the DBT backend.
 *
 * Selects the correct code emitter header based on the host architecture
 * and operating system.  The DBT_HOST_* macros identify the active
 * backend for conditional compilation.
 *
 * When TINYMUX_JIT is not defined, this header is a no-op — the DBT
 * is not compiled and no platform detection is needed.
 *
 * See docs/DBT-PORTABILITY.md for the full multi-platform design.
 */

#ifndef DBT_HOST_H
#define DBT_HOST_H

#ifdef TINYMUX_JIT

#if defined(__riscv) && __riscv_xlen == 64
  // RV64 native: guest code IS native code.  No translation needed.
  #define DBT_HOST_RV64_NATIVE 1

#elif defined(__aarch64__) || defined(_M_ARM64)
  // AArch64 host — select sub-variant by OS.
  #include "dbt_emit_a64.h"
  #define DBT_HOST_AARCH64 1
  #if defined(__APPLE__)
    #define DBT_HOST_AARCH64_APPLE 1
  #else
    #define DBT_HOST_AARCH64_SYSV 1
  #endif

#elif defined(__x86_64__) || defined(_M_X64)
  // x86-64 host — select sub-variant by OS.
  #include "dbt_emit_x64.h"
  #define DBT_HOST_X64 1
  #if defined(_WIN32)
    #define DBT_HOST_X64_WIN64 1
  #else
    #define DBT_HOST_X64_SYSV 1
  #endif

#else
  #error "Unsupported host platform for DBT — disable JIT or add a backend"
#endif

#endif // TINYMUX_JIT

#endif // DBT_HOST_H
