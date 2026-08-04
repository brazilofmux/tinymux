/* guest_main.c -- RV64 guest side of the color_ops three-way differential.
 *
 * Freestanding: no libc, no startup files.  Talks to the outside world with
 * exactly the two Linux RISC-V syscalls our ELF test harness implements
 * (64 = write, 93 = exit), which are also the real Linux numbers -- so the
 * SAME binary runs under qemu-riscv64-static, under rv64_interp_run, and
 * under the DBT.  That is the whole point: three executions of one
 * instruction stream, with qemu as the external oracle.
 *
 * Build: see build.sh
 */

typedef unsigned long  size_t_;
typedef unsigned long  ulong_;

/* co_* entry points under test.  Declared here rather than via color_ops.h
 * to keep the guest free of LIBMUX_API / config.h expectations. */
extern size_t_ co_insert_at(unsigned char *out,
                            const unsigned char *list, size_t_ llen,
                            int *positions, int nPositions,
                            const unsigned char *word, size_t_ wlen,
                            unsigned char delim, unsigned char osep);
extern size_t_ co_replace_at(unsigned char *out,
                             const unsigned char *list, size_t_ llen,
                             int *positions, int nPositions,
                             const unsigned char *word, size_t_ wlen,
                             unsigned char delim, unsigned char osep);
extern size_t_ co_delete_at(unsigned char *out,
                            const unsigned char *list, size_t_ llen,
                            int *positions, int nPositions,
                            unsigned char delim, unsigned char osep);
extern size_t_ co_insert_word(unsigned char *out,
                              const unsigned char *list, size_t_ llen,
                              const unsigned char *word, size_t_ wlen,
                              size_t_ iPos,
                              unsigned char delim, unsigned char osep);
extern size_t_ co_splice(unsigned char *out,
                         const unsigned char *list1, size_t_ len1,
                         const unsigned char *list2, size_t_ len2,
                         const unsigned char *search, size_t_ slen,
                         unsigned char delim, unsigned char osep);
extern size_t_ co_transform(unsigned char *out,
                            const unsigned char *str, size_t_ slen,
                            const unsigned char *from_set, size_t_ flen,
                            const unsigned char *to_set, size_t_ tlen);
extern size_t_ co_cluster_count(const unsigned char *data, size_t_ len);
extern size_t_ co_mid_cluster(unsigned char *out,
                              const unsigned char *data, size_t_ len,
                              size_t_ iStart, size_t_ nCount);
extern size_t_ co_center(unsigned char *out,
                         const unsigned char *p, size_t_ len,
                         size_t_ width,
                         const unsigned char *fill, size_t_ fill_len,
                         int bTrunc);
extern size_t_ co_ljust(unsigned char *out,
                        const unsigned char *p, size_t_ len,
                        size_t_ width,
                        const unsigned char *fill, size_t_ fill_len,
                        int bTrunc);
extern size_t_ co_rjust(unsigned char *out,
                        const unsigned char *p, size_t_ len,
                        size_t_ width,
                        const unsigned char *fill, size_t_ fill_len,
                        int bTrunc);

/* ---- syscalls ---- */

static long sys_write(long fd, const void *buf, unsigned long n)
{
    register long a0 __asm__("a0") = fd;
    register long a1 __asm__("a1") = (long)buf;
    register long a2 __asm__("a2") = (long)n;
    register long a7 __asm__("a7") = 64;
    __asm__ __volatile__("ecall"
                         : "+r"(a0)
                         : "r"(a1), "r"(a2), "r"(a7)
                         : "memory");
    return a0;
}

static void sys_exit(long code)
{
    register long a0 __asm__("a0") = code;
    register long a7 __asm__("a7") = 93;
    for (;;) {
        __asm__ __volatile__("ecall" : : "r"(a0), "r"(a7) : "memory");
    }
}

/* ---- buffered output ----
 *
 * One write() per flush rather than per token: the DBT route pays a guest
 * exit on every ECALL, and a byte-at-a-time transcript would dominate the
 * run.  Buffer size is arbitrary but must not change between routes -- it
 * cannot affect content, only the number of syscalls.
 */
#define OBUF 8192
static char g_obuf[OBUF];
static unsigned g_olen;

static void o_flush(void)
{
    if (g_olen) {
        sys_write(1, g_obuf, g_olen);
        g_olen = 0;
    }
}

static void o_ch(char c)
{
    if (g_olen == OBUF) o_flush();
    g_obuf[g_olen++] = c;
}

static void o_str(const char *s)
{
    while (*s) o_ch(*s++);
}

static void o_uint(unsigned long long v)
{
    char tmp[24];
    int n = 0;
    if (!v) { o_ch('0'); return; }
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n) o_ch(tmp[--n]);
}

static void o_bytes(const unsigned char *p, unsigned long n)
{
    static const char hex[] = "0123456789abcdef";
    unsigned long i;
    for (i = 0; i < n; i++) {
        o_ch(hex[p[i] >> 4]);
        o_ch(hex[p[i] & 15]);
    }
}

#include "cases.h"

/*
 * _start in asm so gp is established before any C runs.
 *
 * RISC-V addresses small globals (.sdata/.sbss) as gp-relative when the
 * linker relaxes against __global_pointer$, and crt0 normally sets gp.
 * We are -nostdlib, so without this the first .sbss access faults at
 * gp+offset with gp == 0.  Neither qemu nor our loader sets gp for us.
 *
 * (The shipped blob dodges this a different way: softlib.ld never defines
 * __global_pointer$, so the linker cannot relax to gp-relative and none of
 * the blob's code uses gp.  That is an unstated invariant of the blob
 * build, not a property of the compiler.)
 */
__asm__(
    ".section .text._entry,\"ax\",@progbits\n"
    ".globl _start\n"
    ".type _start,@function\n"
    "_start:\n"
    ".option push\n"
    ".option norelax\n"
    "  la gp, __global_pointer$\n"
    ".option pop\n"
    "  call guest_main\n"
    ".size _start,.-_start\n");

void guest_main(void)
{
    run_all(FUZZ_ITERS);
    o_flush();
    sys_exit(0);
}
