/* min_main.c -- minimal repro driver.
 *
 * MIN_WARM=1 performs the passing call (position +1) before the failing
 * one (position -1); MIN_WARM=0 performs only the failing call.  If the
 * failure needs the warm-up, the bug is carried-over DBT state, not the
 * input.  N_WORDS sets the delimiter count so the cap (16384) can be
 * approached from either side.
 */

typedef unsigned long size_t_;

extern size_t_ co_insert_at(unsigned char *out,
                            const unsigned char *list, size_t_ llen,
                            int *positions, int nPositions,
                            const unsigned char *word, size_t_ wlen,
                            unsigned char delim, unsigned char osep);

static long sys_write(long fd, const void *buf, unsigned long n)
{
    register long a0 __asm__("a0") = fd;
    register long a1 __asm__("a1") = (long)buf;
    register long a2 __asm__("a2") = (long)n;
    register long a7 __asm__("a7") = 64;
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7)
                         : "memory");
    return a0;
}

static void sys_exit(long code)
{
    register long a0 __asm__("a0") = code;
    register long a7 __asm__("a7") = 93;
    for (;;) __asm__ __volatile__("ecall" : : "r"(a0), "r"(a7) : "memory");
}

static char g_obuf[4096];
static unsigned g_olen;
static void o_flush(void) { if (g_olen) { sys_write(1, g_obuf, g_olen); g_olen = 0; } }
static void o_ch(char c) { if (g_olen == sizeof g_obuf) o_flush(); g_obuf[g_olen++] = c; }
static void o_str(const char *s) { while (*s) o_ch(*s++); }
static void o_uint(unsigned long long v)
{
    char t[24]; int n = 0;
    if (!v) { o_ch('0'); return; }
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) o_ch(t[--n]);
}

#define OUTCAP 32768
static unsigned char g_out[OUTCAP];
static unsigned char g_big[OUTCAP];

__asm__(
    ".section .text._entry,\"ax\",@progbits\n"
    ".globl _start\n.type _start,@function\n_start:\n"
    ".option push\n.option norelax\n"
    "  la gp, __global_pointer$\n"
    ".option pop\n"
    "  call guest_main\n"
    ".size _start,.-_start\n");

void guest_main(void)
{
    unsigned long i, n = N_WORDS;
    int pos[2];
    unsigned long r;

    for (i = 0; i < n; i++) g_big[i] = '|';

    /* WARM_MODE: 0 none, 1 same list pos +1, 2 same list pos -1,
     *            3 SHORT list pos +1 (never reaches the cap),
     *            4 SHORT list pos -1. */
#if WARM_MODE == 1 || WARM_MODE == 2
    pos[0] = (WARM_MODE == 1) ? 1 : -1;
    r = co_insert_at(g_out, g_big, n, pos, 1,
                     (const unsigned char *)"Q", 1, '|', '|');
    o_str("warm ret="); o_uint(r); o_str("\n");
#elif WARM_MODE == 3 || WARM_MODE == 4
    pos[0] = (WARM_MODE == 3) ? 1 : -1;
    r = co_insert_at(g_out, g_big, 8, pos, 1,
                     (const unsigned char *)"Q", 1, '|', '|');
    o_str("warm ret="); o_uint(r); o_str("\n");
#endif

    pos[0] = -1;
    r = co_insert_at(g_out, g_big, n, pos, 1,
                     (const unsigned char *)"Q", 1, '|', '|');
    o_str("test ret="); o_uint(r); o_str("\n");

    o_flush();
    sys_exit(0);
}
