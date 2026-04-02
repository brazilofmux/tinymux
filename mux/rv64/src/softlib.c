/*! \file softlib.c
 * \brief Tier 2 softcode library — RISC-V implementations.
 *
 * Compiled with riscv64-unknown-elf-gcc to produce a standalone
 * RV64 binary blob.  No libc, no system calls — just byte-level
 * operations on memory passed in through registers.
 *
 * Calling convention (matches compiler's emit_call layout):
 *   a0 = pointer to output buffer (guest address)
 *   a1 = pointer to fargs array (guest address, array of char*)
 *   a2 = number of arguments
 *   Return: a0 = pointer to output buffer (start)
 *
 * The fargs array contains pointers to NUL-terminated strings
 * in guest memory.  The function writes its result to the output
 * buffer and returns the output buffer's start address.
 */

typedef unsigned long uint64_t;
typedef unsigned long size_t;
typedef long          int64_t;

#include <math.h>

/* Forward declarations for intrinsics (defined below). */
int   rv64_slen(const char *s);
char *rv64_scopy(char *dst, const char *src);
void *memcpy(void *dst, const void *src, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
void *memset(void *dst, int c, size_t n);
void  memswap(void *a, void *b, size_t n);

/* Forward declarations for co_* functions (in color_ops.o). */
size_t co_first(unsigned char *out, const unsigned char *p, size_t len,
                unsigned char delim);
size_t co_rest(unsigned char *out, const unsigned char *p, size_t len,
               unsigned char delim);
size_t co_last(unsigned char *out, const unsigned char *p, size_t len,
               unsigned char delim);
size_t co_extract(unsigned char *out, const unsigned char *p, size_t len,
                  size_t iFirst, size_t nWords, unsigned char delim,
                  unsigned char osep);
size_t co_words_count(const unsigned char *p, size_t len,
                      unsigned char delim);
size_t co_member(const unsigned char *target, size_t tlen,
                 const unsigned char *list, size_t llen,
                 unsigned char delim);
size_t co_trim(unsigned char *out, const unsigned char *p, size_t len,
               unsigned char trim_char, int trim_flags);
size_t co_repeat(unsigned char *out, const unsigned char *p, size_t len,
                 size_t count);
size_t co_mid(unsigned char *out, const unsigned char *p, size_t len,
              size_t iStart, size_t nCount);
size_t co_pos(const unsigned char *haystack, size_t hlen,
              const unsigned char *needle, size_t nlen);
size_t co_sort_words(unsigned char *out, const unsigned char *list,
                     size_t llen, unsigned char delim, unsigned char osep,
                     char sort_type);
size_t co_setunion(unsigned char *out, const unsigned char *a, size_t alen,
                   const unsigned char *b, size_t blen,
                   unsigned char delim, unsigned char osep, char sort_type);
size_t co_setdiff(unsigned char *out, const unsigned char *a, size_t alen,
                  const unsigned char *b, size_t blen,
                  unsigned char delim, unsigned char osep, char sort_type);
size_t co_setinter(unsigned char *out, const unsigned char *a, size_t alen,
                   const unsigned char *b, size_t blen,
                   unsigned char delim, unsigned char osep, char sort_type);
size_t co_delete(unsigned char *out, const unsigned char *list, size_t llen,
                 size_t pos, unsigned char delim, unsigned char osep);
size_t co_splice(unsigned char *out, const unsigned char *list, size_t llen,
                 size_t pos, size_t count, const unsigned char *word,
                 size_t wlen, unsigned char delim, unsigned char osep);
size_t co_insert_word(unsigned char *out, const unsigned char *list,
                      size_t llen, size_t pos, const unsigned char *word,
                      size_t wlen, unsigned char delim, unsigned char osep);

/* New co_* functions for Tier 2 wrappers below. */
size_t co_cluster_count(const unsigned char *data, size_t len);
size_t co_tolower(unsigned char *out, const unsigned char *p, size_t len);
size_t co_toupper(unsigned char *out, const unsigned char *p, size_t len);
size_t co_reverse(unsigned char *out, const unsigned char *p, size_t len);
size_t co_escape(unsigned char *out, const unsigned char *data, size_t len);
size_t co_left(unsigned char *out, const unsigned char *p, size_t len,
               size_t n);
size_t co_right(unsigned char *out, const unsigned char *p, size_t len,
                size_t n);
size_t co_compress(unsigned char *out, const unsigned char *p, size_t len,
                   unsigned char compress_char);
size_t co_transform(unsigned char *out,
                    const unsigned char *str, size_t slen,
                    const unsigned char *from_set, size_t flen,
                    const unsigned char *to_set, size_t tlen);
size_t co_lpos(unsigned char *out, const unsigned char *haystack, size_t hlen,
               unsigned char pattern);

/* Stubs for color_ops symbols referenced by new functions but not
 * needed in the Tier 2 blob.  gc-sections would remove them if they
 * weren't transitively pulled in by co_render_ansi256/co_dfa_ascii.
 */
unsigned char co_nearest_xterm16(unsigned long rgb) { (void)rgb; return 0; }
unsigned char co_nearest_xterm256(unsigned long rgb) { (void)rgb; return 0; }
const unsigned char tr_ascii_itt[256] = {0};
const unsigned short tr_ascii_sot[99] = {0};
const unsigned char tr_ascii_sbt[3431] = {0};

/* ---------------------------------------------------------------
 * Intrinsic helpers — global and noinline so the DBT can intercept
 * JAL calls to these and emit native x86-64 instead of translating
 * the RV64 byte loops.  On native RISC-V hosts, these are normal
 * function calls with fallback implementations.
 *
 * Calling convention (standard RISC-V):
 *   slen:  a0 = string pointer, returns length in a0
 *   scopy: a0 = dst, a1 = src, returns pointer AT NUL in a0
 * --------------------------------------------------------------- */

__attribute__((noinline))
int rv64_slen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

__attribute__((noinline))
char *rv64_scopy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
    return dst;
}

/* ---------------------------------------------------------------
 * Host-native intrinsics — the DBT intercepts JAL calls to these
 * and emits native x86-64 (rep movsb, rep cmpsb, rep stosb, etc.)
 * instead of translating the RV64 byte loops.  The RV64 fallback
 * implementations exist for correctness on native RISC-V hosts.
 * --------------------------------------------------------------- */

__attribute__((noinline))
void *memcpy(void *dst, const void *src, size_t n) {
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

__attribute__((noinline))
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    while (n--) {
        if (*pa != *pb) return *pa - *pb;
        pa++;
        pb++;
    }
    return 0;
}

__attribute__((noinline))
void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    unsigned char v = (unsigned char)c;
    while (n--) *d++ = v;
    return dst;
}

__attribute__((noinline))
void memswap(void *a, void *b, size_t n) {
    unsigned char *pa = (unsigned char *)a;
    unsigned char *pb = (unsigned char *)b;
    while (n--) {
        unsigned char t = *pa;
        *pa++ = *pb;
        *pb++ = t;
    }
}

/* ---------------------------------------------------------------
 * libc stubs — implementations for functions color_ops.c needs.
 * --------------------------------------------------------------- */

__attribute__((noinline))
long atol(const char *s) {
    long v = 0;
    int neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

/* Minimal snprintf — only supports %zu (used by co_lpos). */
__attribute__((noinline))
int snprintf(char *buf, size_t size, const char *fmt, ...) {
    /* co_lpos uses snprintf(p, remain, "%zu", value) exclusively.
     * We handle that single format string and nothing else. */
    if (size == 0) return 0;

    /* Walk fmt, copying literals and handling %zu. */
    char *out = buf;
    char *end = buf + size - 1;
    const unsigned char *f = (const unsigned char *)fmt;

    /* Extract the va_list argument (first vararg = the size_t value). */
    /* On RV64, varargs are passed in a3, a4, ... registers.
     * We use __builtin_va_list for portability. */
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);

    while (*f && out < end) {
        if (*f == '%' && f[1] == 'z' && f[2] == 'u') {
            size_t val = __builtin_va_arg(ap, size_t);
            /* Convert to decimal. */
            char tmp[20];
            int pos = 0;
            if (val == 0) {
                tmp[pos++] = '0';
            } else {
                while (val > 0) {
                    tmp[pos++] = '0' + (val % 10);
                    val /= 10;
                }
            }
            for (int i = pos - 1; i >= 0 && out < end; i--) {
                *out++ = tmp[i];
            }
            f += 3;
        } else {
            *out++ = *f++;
        }
    }
    *out = '\0';
    __builtin_va_end(ap);
    return (int)(out - buf);
}

/* qsort — Shellsort using memswap intrinsic.
 * Classic qsort would recurse; Shellsort is iterative and compact. */
__attribute__((noinline))
void qsort(void *base, size_t nmemb, size_t size,
            int (*compar)(const void *, const void *)) {
    unsigned char *b = (unsigned char *)base;
    /* Knuth's gap sequence: 1, 4, 13, 40, 121, ... */
    size_t gap = 1;
    while (gap < nmemb / 3) gap = gap * 3 + 1;

    while (gap >= 1) {
        for (size_t i = gap; i < nmemb; i++) {
            size_t j = i;
            while (j >= gap &&
                   compar(b + j * size, b + (j - gap) * size) < 0) {
                memswap(b + j * size, b + (j - gap) * size, size);
                j -= gap;
            }
        }
        gap /= 3;
    }
}

/* co_console_width — DFA-driven Unicode console width.
 * Returns 0 (combining), 1 (normal), or 2 (fullwidth/CJK). */
extern const unsigned char  tr_widths_itt[256];
extern const unsigned short tr_widths_sot[];
extern const unsigned short tr_widths_sbt[];
#define TR_WIDTHS_START_STATE (0)
#define TR_WIDTHS_ACCEPTING_STATES_START (373)

int co_console_width(const unsigned char *pCodePoint) {
    const unsigned char *p = pCodePoint;
    int iState = TR_WIDTHS_START_STATE;
    do {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_widths_itt[ch];
        unsigned short iOffset = tr_widths_sot[iState];
        for (;;) {
            int y = tr_widths_sbt[iOffset];
            if (y < 128) {
                if (iColumn < y) {
                    iState = tr_widths_sbt[iOffset + 1];
                    break;
                } else {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset += 2;
                }
            } else {
                y = 256 - y;
                if (iColumn < y) {
                    iState = tr_widths_sbt[iOffset + iColumn + 1];
                    break;
                } else {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset = (unsigned short)(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_WIDTHS_ACCEPTING_STATES_START);
    return (iState - TR_WIDTHS_ACCEPTING_STATES_START);
}

/* strlen — needed by freestanding.h inline but also by softlib. */
__attribute__((noinline))
size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Forward declarations for helpers defined below. */
static int sitoa(char *buf, int val);
static int satoi(const char *s);

/* ECALL helpers — invoke host syscall from guest code.
 * a7 = syscall number, a0..a2 = args.  Returns a0.
 */
static long ecall1(long num, long a0) {
    register long x10 __asm__("a0") = a0;
    register long x17 __asm__("a7") = num;
    __asm__ volatile ("ecall" : "+r"(x10) : "r"(x17) : "memory");
    return x10;
}

static long ecall2(long num, long a0, long a1) {
    register long x10 __asm__("a0") = a0;
    register long x11 __asm__("a1") = a1;
    register long x17 __asm__("a7") = num;
    __asm__ volatile ("ecall" : "+r"(x10) : "r"(x11), "r"(x17) : "memory");
    return x10;
}

static long ecall3(long num, long a0, long a1, long a2) {
    register long x10 __asm__("a0") = a0;
    register long x11 __asm__("a1") = a1;
    register long x12 __asm__("a2") = a2;
    register long x17 __asm__("a7") = num;
    __asm__ volatile ("ecall" : "+r"(x10) : "r"(x11), "r"(x12), "r"(x17) : "memory");
    return x10;
}

static long ecall5(long num, long a0, long a1, long a2, long a3, long a4) {
    register long x10 __asm__("a0") = a0;
    register long x11 __asm__("a1") = a1;
    register long x12 __asm__("a2") = a2;
    register long x13 __asm__("a3") = a3;
    register long x14 __asm__("a4") = a4;
    register long x17 __asm__("a7") = num;
    __asm__ volatile ("ecall" : "+r"(x10)
        : "r"(x11), "r"(x12), "r"(x13), "r"(x14), "r"(x17) : "memory");
    return x10;
}

/* ---------------------------------------------------------------
 * Tier 2 wrappers for co_* functions.
 *
 * These unpack the fargs calling convention (a0=out, a1=fargs[],
 * a2=nfargs) and call the co_* Ragel functions which use explicit
 * pointer+length parameters.
 * --------------------------------------------------------------- */

/* Helper: get delimiter from fargs, default space. */
static unsigned char get_delim(const char **fargs, int nfargs, int idx) {
    if (idx < nfargs && fargs[idx][0] != '\0')
        return (unsigned char)fargs[idx][0];
    return ' ';
}

/* Helper: get output separator, default = delimiter. */
static unsigned char get_osep(const char **fargs, int nfargs, int idx,
                               unsigned char delim) {
    if (idx < nfargs && fargs[idx][0] != '\0')
        return (unsigned char)fargs[idx][0];
    return delim;
}

char *co_first_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    unsigned char delim = get_delim(fargs, nfargs, 1);
    size_t n = co_first((unsigned char *)out,
                        (const unsigned char *)fargs[0],
                        rv64_slen(fargs[0]), delim);
    out[n] = '\0';
    return out;
}

/* trim_space_sep parity: for space delimiter, strip leading+trailing
 * spaces from the input string.  Returns pointer into str (which may
 * be mutated by NUL-terminating trailing spaces) and sets *plen to
 * the trimmed length.  For non-space delimiters, returns str unchanged. */
static const char *trim_space_input(const char *str, size_t slen,
                                     unsigned char delim, size_t *plen) {
    if (delim != ' ') { *plen = slen; return str; }
    const char *p = str;
    const char *pe = str + slen;
    while (p < pe && *p == ' ') p++;
    while (pe > p && *(pe - 1) == ' ') pe--;
    *plen = (size_t)(pe - p);
    return p;
}

char *co_rest_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    unsigned char delim = get_delim(fargs, nfargs, 1);
    size_t tlen;
    const char *trimmed = trim_space_input(fargs[0],
                              rv64_slen(fargs[0]), delim, &tlen);
    size_t n = co_rest((unsigned char *)out,
                       (const unsigned char *)trimmed, tlen, delim);
    out[n] = '\0';
    /* For space delimiter, the interpreter's split_token skips
     * consecutive spaces after the first delimiter.  co_rest
     * doesn't, so strip leading spaces from the result. */
    if (delim == ' ' && n > 0) {
        char *p = out;
        while (*p == ' ') p++;
        if (p != out) {
            size_t remain = n - (size_t)(p - out);
            size_t i;
            for (i = 0; i <= remain; i++) out[i] = p[i];
        }
    }
    return out;
}

char *co_last_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    unsigned char delim = get_delim(fargs, nfargs, 1);
    size_t tlen;
    const char *trimmed = trim_space_input(fargs[0],
                              rv64_slen(fargs[0]), delim, &tlen);
    size_t n = co_last((unsigned char *)out,
                       (const unsigned char *)trimmed, tlen, delim);
    out[n] = '\0';
    return out;
}

char *co_words_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') {
        out[0] = '0'; out[1] = '\0'; return out;
    }
    unsigned char delim = get_delim(fargs, nfargs, 1);
    size_t count = co_words_count((const unsigned char *)fargs[0],
                                  rv64_slen(fargs[0]), delim);
    sitoa(out, (int)count);
    return out;
}

char *co_extract_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) { out[0] = '\0'; return out; }
    int pos = satoi(fargs[1]);
    int count = satoi(fargs[2]);
    unsigned char delim = get_delim(fargs, nfargs, 3);
    unsigned char osep = get_osep(fargs, nfargs, 4, delim);
    if (pos < 1 || count < 1) { out[0] = '\0'; return out; }
    size_t n = co_extract((unsigned char *)out,
                          (const unsigned char *)fargs[0],
                          rv64_slen(fargs[0]),
                          (size_t)pos, (size_t)count, delim, osep);
    out[n] = '\0';
    return out;
}

char *co_member_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '0'; out[1] = '\0'; return out; }
    unsigned char delim = get_delim(fargs, nfargs, 2);
    size_t pos = co_member((const unsigned char *)fargs[1],
                           rv64_slen(fargs[1]),
                           (const unsigned char *)fargs[0],
                           rv64_slen(fargs[0]), delim);
    sitoa(out, (int)pos);
    return out;
}

char *co_trim_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    unsigned char tc = ' ';
    if (nfargs >= 3 && fargs[2][0] != '\0')
        tc = (unsigned char)fargs[2][0];
    /* Trim flags: 1=left, 2=right, 3=both */
    int flags = 3;  /* both */
    if (nfargs >= 2 && fargs[1][0] != '\0') {
        char s = fargs[1][0];
        if (s == 'l' || s == 'L') flags = 1;
        else if (s == 'r' || s == 'R') flags = 2;
    }
    size_t n = co_trim((unsigned char *)out,
                       (const unsigned char *)fargs[0],
                       rv64_slen(fargs[0]), tc, flags);
    out[n] = '\0';
    return out;
}

char *co_repeat_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    int count = satoi(fargs[1]);
    if (count <= 0) { out[0] = '\0'; return out; }
    size_t n = co_repeat((unsigned char *)out,
                         (const unsigned char *)fargs[0],
                         rv64_slen(fargs[0]),
                         (size_t)count);
    out[n] = '\0';
    return out;
}

char *co_mid_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) { out[0] = '\0'; return out; }
    int start = satoi(fargs[1]);
    int count = satoi(fargs[2]);
    if (count <= 0) { out[0] = '\0'; return out; }
    size_t n = co_mid((unsigned char *)out,
                      (const unsigned char *)fargs[0],
                      rv64_slen(fargs[0]),
                      (size_t)start, (size_t)count);
    out[n] = '\0';
    return out;
}

char *co_pos_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) {
        out[0] = '#'; out[1] = '-'; out[2] = '1'; out[3] = '\0';
        return out;
    }
    size_t pos = co_pos((const unsigned char *)fargs[1],
                        rv64_slen(fargs[1]),
                        (const unsigned char *)fargs[0],
                        rv64_slen(fargs[0]));
    /* co_pos returns 0 for not found, 1-based position otherwise. */
    /* MUX match() returns #-1 for not found... but strmatch/pos
     * returns a number. Let's return the raw position. */
    sitoa(out, (int)pos);
    return out;
}

/* sort(list[, sort_type[, delim[, osep]]]) via ECALL_SORT (0x164).
 * MUX arg order: fargs[1]=sort_type, fargs[2]=delim, fargs[3]=osep.
 */
char *co_sort_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    char sort_type = '?';  /* auto-detect default */
    if (nfargs >= 2 && fargs[1][0] != '\0')
        sort_type = fargs[1][0];
    unsigned char delim = ' ';
    if (nfargs >= 3 && fargs[2][0] != '\0')
        delim = (unsigned char)fargs[2][0];
    unsigned char osep = delim;
    if (nfargs >= 4 && fargs[3][0] != '\0')
        osep = (unsigned char)fargs[3][0];
    ecall5(0x164, (long)fargs[0], (long)sort_type,
           (long)delim, (long)osep, (long)out);
    return out;
}

char *co_setunion_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    unsigned char delim = get_delim(fargs, nfargs, 2);
    unsigned char osep = get_osep(fargs, nfargs, 3, delim);
    char sort_type = 'a';
    if (nfargs >= 5 && fargs[4][0] != '\0')
        sort_type = fargs[4][0];
    size_t n = co_setunion((unsigned char *)out,
                           (const unsigned char *)fargs[0],
                           rv64_slen(fargs[0]),
                           (const unsigned char *)fargs[1],
                           rv64_slen(fargs[1]),
                           delim, osep, sort_type);
    out[n] = '\0';
    return out;
}

char *co_setdiff_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    unsigned char delim = get_delim(fargs, nfargs, 2);
    unsigned char osep = get_osep(fargs, nfargs, 3, delim);
    char sort_type = 'a';
    if (nfargs >= 5 && fargs[4][0] != '\0')
        sort_type = fargs[4][0];
    size_t n = co_setdiff((unsigned char *)out,
                          (const unsigned char *)fargs[0],
                          rv64_slen(fargs[0]),
                          (const unsigned char *)fargs[1],
                          rv64_slen(fargs[1]),
                          delim, osep, sort_type);
    out[n] = '\0';
    return out;
}

char *co_setinter_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    unsigned char delim = get_delim(fargs, nfargs, 2);
    unsigned char osep = get_osep(fargs, nfargs, 3, delim);
    char sort_type = 'a';
    if (nfargs >= 5 && fargs[4][0] != '\0')
        sort_type = fargs[4][0];
    size_t n = co_setinter((unsigned char *)out,
                           (const unsigned char *)fargs[0],
                           rv64_slen(fargs[0]),
                           (const unsigned char *)fargs[1],
                           rv64_slen(fargs[1]),
                           delim, osep, sort_type);
    out[n] = '\0';
    return out;
}

char *co_ldelete_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) {
        if (nfargs >= 1) rv64_scopy(out, fargs[0]);
        else out[0] = '\0';
        return out;
    }
    int pos = satoi(fargs[1]);
    unsigned char delim = get_delim(fargs, nfargs, 2);
    unsigned char osep = get_osep(fargs, nfargs, 3, delim);
    if (pos < 1) { rv64_scopy(out, fargs[0]); return out; }
    size_t n = co_delete((unsigned char *)out,
                         (const unsigned char *)fargs[0],
                         rv64_slen(fargs[0]),
                         (size_t)pos, delim, osep);
    out[n] = '\0';
    return out;
}

/* replace(list, positions, word[, delim][, osep])
 *
 * Replace word(s) at given position(s) in list.  Matches do_itemfuns
 * IF_REPLACE semantics: 1-based positions, negative wraps from end,
 * out-of-range positions are ignored, duplicates are collapsed.
 *
 * For the Tier 2 path we handle the single-position case here.
 * Multi-position calls fall through to ECALL via the nargs/delimiter
 * guards in hir_lower.cpp.
 */
char *co_replace_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) {
        if (nfargs >= 1) rv64_scopy(out, fargs[0]);
        else out[0] = '\0';
        return out;
    }
    int raw_pos = satoi(fargs[1]);
    unsigned char delim = get_delim(fargs, nfargs, 3);
    unsigned char osep = get_osep(fargs, nfargs, 4, delim);

    /* For space delimiter, trim leading/trailing spaces. */
    const unsigned char *data = (const unsigned char *)fargs[0];
    size_t dlen = rv64_slen(fargs[0]);
    if (delim == ' ') {
        while (dlen > 0 && *data == ' ') { data++; dlen--; }
        while (dlen > 0 && data[dlen - 1] == ' ') { dlen--; }
    }

    /* Count words. */
    size_t nWords = co_words_count(data, dlen, delim);

    /* Convert position: negative wraps from end, 1-based → 0-based. */
    int pos;
    if (raw_pos < 0) {
        pos = raw_pos + (int)nWords;
    } else {
        pos = raw_pos - 1;
    }
    if (pos < 0 || (size_t)pos >= nWords) {
        /* Out of range: return original list reassembled with osep. */
        size_t n = co_extract((unsigned char *)out, data, dlen,
                              1, nWords, delim, osep);
        out[n] = '\0';
        return out;
    }

    /* Reassemble: words before pos, replacement, words after pos. */
    const unsigned char *word = (const unsigned char *)fargs[2];
    size_t wlen = rv64_slen(fargs[2]);
    unsigned char *wp = (unsigned char *)out;
    unsigned char *wp_end = wp + 7999;
    const unsigned char *p = data;
    const unsigned char *pe = data + dlen;
    int cur = 0;
    int emitted = 0;

    while (cur <= (int)nWords && wp < wp_end) {
        if (cur == pos) {
            /* Emit replacement word. */
            if (emitted > 0 && wp < wp_end) *wp++ = osep;
            size_t cb = wlen;
            if (cb > (size_t)(wp_end - wp)) cb = (size_t)(wp_end - wp);
            size_t i; for (i = 0; i < cb; i++) wp[i] = word[i];
            wp += cb;
            emitted++;
            /* Skip the original word. */
            while (p < pe && *p != delim) p++;
            if (p < pe) {
                p++;
                if (delim == ' ') while (p < pe && *p == ' ') p++;
            }
        } else if (p < pe) {
            /* Emit original word. */
            if (emitted > 0 && wp < wp_end) *wp++ = osep;
            while (p < pe && *p != delim && wp < wp_end) *wp++ = *p++;
            emitted++;
            if (p < pe && *p == delim) {
                p++;
                if (delim == ' ') while (p < pe && *p == ' ') p++;
            }
        }
        cur++;
    }
    *wp = '\0';
    return out;
}

/* insert(list, word, positions[, delim][, osep])
 *
 * Note: interpreter arg order is (list, positions, word, delim, osep)
 * but the MUX function table says insert(list, word, pos, delim, osep).
 * Actually checking functions.cpp: insert is (list, word, pos[, sep][, osep]).
 */
char *co_insert_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) {
        if (nfargs >= 1) rv64_scopy(out, fargs[0]);
        else out[0] = '\0';
        return out;
    }
    unsigned char delim = get_delim(fargs, nfargs, 3);
    unsigned char osep = get_osep(fargs, nfargs, 4, delim);

    /* For space delimiter, trim leading/trailing spaces. */
    const unsigned char *data = (const unsigned char *)fargs[0];
    size_t dlen = rv64_slen(fargs[0]);
    if (delim == ' ') {
        while (dlen > 0 && *data == ' ') { data++; dlen--; }
        while (dlen > 0 && data[dlen - 1] == ' ') { dlen--; }
    }

    size_t n = co_insert_word((unsigned char *)out, data, dlen,
                              (const unsigned char *)fargs[2],
                              rv64_slen(fargs[2]),
                              (size_t)satoi(fargs[1]),
                              delim, osep);
    out[n] = '\0';
    return out;
}

/* ---------------------------------------------------------------
 * Batch 2 wrappers: strlen, case conversion, reverse, escape,
 * left/right, compress, lpos.
 * --------------------------------------------------------------- */

char *co_strlen_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') {
        out[0] = '0'; out[1] = '\0'; return out;
    }
    size_t count = co_cluster_count((const unsigned char *)fargs[0],
                                     rv64_slen(fargs[0]));
    sitoa(out, (int)count);
    return out;
}

char *co_lcstr_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    size_t n = co_tolower((unsigned char *)out,
                          (const unsigned char *)fargs[0],
                          rv64_slen(fargs[0]));
    out[n] = '\0';
    return out;
}

char *co_ucstr_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    size_t n = co_toupper((unsigned char *)out,
                          (const unsigned char *)fargs[0],
                          rv64_slen(fargs[0]));
    out[n] = '\0';
    return out;
}

char *co_reverse_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    size_t n = co_reverse((unsigned char *)out,
                          (const unsigned char *)fargs[0],
                          rv64_slen(fargs[0]));
    out[n] = '\0';
    return out;
}

char *co_escape_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    size_t n = co_escape((unsigned char *)out,
                         (const unsigned char *)fargs[0],
                         rv64_slen(fargs[0]));
    out[n] = '\0';
    return out;
}

char *co_left_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    size_t count = (size_t)atol(fargs[1]);
    size_t n = co_left((unsigned char *)out,
                       (const unsigned char *)fargs[0],
                       rv64_slen(fargs[0]), count);
    out[n] = '\0';
    return out;
}

char *co_right_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    size_t count = (size_t)atol(fargs[1]);
    size_t n = co_right((unsigned char *)out,
                        (const unsigned char *)fargs[0],
                        rv64_slen(fargs[0]), count);
    out[n] = '\0';
    return out;
}

char *co_compress_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    unsigned char ch = (nfargs >= 2 && fargs[1][0]) ? fargs[1][0] : ' ';
    size_t n = co_compress((unsigned char *)out,
                           (const unsigned char *)fargs[0],
                           rv64_slen(fargs[0]), ch);
    out[n] = '\0';
    return out;
}

char *co_secure_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    static const unsigned char from_set[] = "$%(),;[\\]{}";
    static const unsigned char to_set[]   = "           ";
    size_t n = co_transform((unsigned char *)out,
                            (const unsigned char *)fargs[0],
                            rv64_slen(fargs[0]),
                            from_set, sizeof(from_set) - 1,
                            to_set, sizeof(to_set) - 1);
    out[n] = '\0';
    return out;
}

char *co_lpos_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2 || fargs[1][0] == '\0') { out[0] = '\0'; return out; }
    size_t n = co_lpos((unsigned char *)out,
                       (const unsigned char *)fargs[0],
                       rv64_slen(fargs[0]),
                       (unsigned char)fargs[1][0]);
    out[n] = '\0';
    return out;
}

/* ---------------------------------------------------------------
 * Batch 3 wrappers: ljust, rjust, center, edit, splice, totitle,
 * strip_color, visible_length.
 * --------------------------------------------------------------- */

/* Forward declarations for co_* functions in color_ops. */
size_t co_ljust(unsigned char *out, const unsigned char *data, size_t len,
                size_t width, const unsigned char *fill, size_t fill_len,
                int bTrunc);
size_t co_rjust(unsigned char *out, const unsigned char *data, size_t len,
                size_t width, const unsigned char *fill, size_t fill_len,
                int bTrunc);
size_t co_center(unsigned char *out, const unsigned char *data, size_t len,
                 size_t width, const unsigned char *fill, size_t fill_len,
                 int bTrunc);
size_t co_edit(unsigned char *out, const unsigned char *str, size_t slen,
               const unsigned char *from, size_t flen,
               const unsigned char *to, size_t tlen);
/* co_splice already declared above — (out, list, llen, pos, count, word, wlen, delim, osep) */
size_t co_totitle(unsigned char *out, const unsigned char *data, size_t len);
size_t co_strip_color(unsigned char *out, const unsigned char *data,
                      size_t len);
size_t co_visible_length(const unsigned char *data, size_t len);

/* ljust(string, width[, fill]) */
char *co_ljust_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    size_t len = rv64_slen(fargs[0]);
    int width = satoi(fargs[1]);
    if (width < 0) width = 0;
    const unsigned char *fill = (const unsigned char *)" ";
    size_t fill_len = 1;
    if (nfargs >= 3 && fargs[2][0] != '\0') {
        fill = (const unsigned char *)fargs[2];
        fill_len = rv64_slen(fargs[2]);
    }
    size_t n = co_ljust((unsigned char *)out,
                        (const unsigned char *)fargs[0], len,
                        (size_t)width, fill, fill_len, 0);
    out[n] = '\0';
    return out;
}

/* rjust(string, width[, fill]) */
char *co_rjust_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    size_t len = rv64_slen(fargs[0]);
    int width = satoi(fargs[1]);
    if (width < 0) width = 0;
    const unsigned char *fill = (const unsigned char *)" ";
    size_t fill_len = 1;
    if (nfargs >= 3 && fargs[2][0] != '\0') {
        fill = (const unsigned char *)fargs[2];
        fill_len = rv64_slen(fargs[2]);
    }
    size_t n = co_rjust((unsigned char *)out,
                        (const unsigned char *)fargs[0], len,
                        (size_t)width, fill, fill_len, 0);
    out[n] = '\0';
    return out;
}

/* center(string, width[, fill]) */
char *co_center_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    size_t len = rv64_slen(fargs[0]);
    int width = satoi(fargs[1]);
    if (width < 0) width = 0;
    const unsigned char *fill = (const unsigned char *)" ";
    size_t fill_len = 1;
    if (nfargs >= 3 && fargs[2][0] != '\0') {
        fill = (const unsigned char *)fargs[2];
        fill_len = rv64_slen(fargs[2]);
    }
    size_t n = co_center((unsigned char *)out,
                         (const unsigned char *)fargs[0], len,
                         (size_t)width, fill, fill_len, 0);
    out[n] = '\0';
    return out;
}

/* edit(string, from, to) */
char *co_edit_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) { out[0] = '\0'; return out; }
    size_t n = co_edit((unsigned char *)out,
                       (const unsigned char *)fargs[0], rv64_slen(fargs[0]),
                       (const unsigned char *)fargs[1], rv64_slen(fargs[1]),
                       (const unsigned char *)fargs[2], rv64_slen(fargs[2]));
    out[n] = '\0';
    return out;
}

/* splice(list, pos, word[, delim][, osep]) */
char *co_splice_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) { out[0] = '\0'; return out; }
    int pos = satoi(fargs[1]);
    if (pos < 1) pos = 1;
    unsigned char delim = ' ';
    unsigned char osep = ' ';
    if (nfargs >= 4 && fargs[3][0] != '\0') delim = (unsigned char)fargs[3][0];
    if (nfargs >= 5 && fargs[4][0] != '\0') osep = (unsigned char)fargs[4][0];
    size_t n = co_splice((unsigned char *)out,
                         (const unsigned char *)fargs[0], rv64_slen(fargs[0]),
                         (size_t)pos, 1,
                         (const unsigned char *)fargs[2], rv64_slen(fargs[2]),
                         delim, osep);
    out[n] = '\0';
    return out;
}

/* totitle(string) — title case */
char *co_totitle_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') { out[0] = '\0'; return out; }
    size_t n = co_totitle((unsigned char *)out,
                          (const unsigned char *)fargs[0],
                          rv64_slen(fargs[0]));
    out[n] = '\0';
    return out;
}

/* stripansi(string) — remove ANSI/PUA color */
char *co_stripansi_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') { out[0] = '\0'; return out; }
    size_t n = co_strip_color((unsigned char *)out,
                              (const unsigned char *)fargs[0],
                              rv64_slen(fargs[0]));
    out[n] = '\0';
    return out;
}

/* vislen(string) — visible character count */
char *co_vislen_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') {
        out[0] = '0'; out[1] = '\0'; return out;
    }
    size_t count = co_visible_length((const unsigned char *)fargs[0],
                                      rv64_slen(fargs[0]));
    sitoa(out, (int)count);
    return out;
}

/* ---------------------------------------------------------------
 * Batch 4 wrappers: space, secure, squish, delete, elements.
 * --------------------------------------------------------------- */

/* space(n) — generate N spaces */
char *rv64_space(char *out, const char **fargs, int nfargs) {
    int n = 1;
    if (nfargs >= 1 && fargs[0][0] != '\0') {
        n = satoi(fargs[0]);
    }
    if (n < 0) n = 0;
    if (n > 8000) n = 8000;
    memset(out, ' ', (size_t)n);
    out[n] = '\0';
    return out;
}

/* rv64_secure, rv64_squish removed — superseded by co_secure_wrap
 * and co_compress_wrap routing in s_tier2_map[].
 */

/* delete(string, first, len) — delete characters by position */
char *rv64_delete(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) {
        if (nfargs >= 1) rv64_scopy(out, fargs[0]);
        else out[0] = '\0';
        return out;
    }
    int first = satoi(fargs[1]);
    int dlen  = satoi(fargs[2]);
    size_t slen = rv64_slen(fargs[0]);
    if (first < 0) first = 0;
    unsigned char *op = (unsigned char *)out;
    const unsigned char *sp = (const unsigned char *)fargs[0];
    /* Copy before deletion point. */
    size_t before = ((size_t)first < slen) ? (size_t)first : slen;
    memcpy(op, sp, before);
    op += before;
    /* Skip deleted range. */
    size_t skip = (size_t)first + (size_t)(dlen > 0 ? dlen : 0);
    if (skip < slen) {
        size_t after = slen - skip;
        memcpy(op, sp + skip, after);
        op += after;
    }
    *op = '\0';
    return out;
}

/* elements(list, positions[, delim][, osep]) — extract by position list */
char *rv64_elements(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    unsigned char delim = ' ';
    if (nfargs >= 3 && fargs[2][0] != '\0') delim = (unsigned char)fargs[2][0];
    /* Output separator defaults to input delimiter (interpreter parity).
     * An explicit empty 4th arg means null separator (join with nothing). */
    unsigned char osep = delim;
    int osep_null = 0;
    if (nfargs >= 4) {
        if (fargs[3][0] != '\0')
            osep = (unsigned char)fargs[3][0];
        else
            osep_null = 1;
    }

    /* For space delimiter, trim leading/trailing spaces to match
     * the interpreter's trim_space_sep behavior. */
    const unsigned char *data = (const unsigned char *)fargs[0];
    size_t dlen = rv64_slen(fargs[0]);
    if (delim == ' ') {
        while (dlen > 0 && *data == ' ') { data++; dlen--; }
        while (dlen > 0 && data[dlen - 1] == ' ') { dlen--; }
    }

    /* Parse position list. */
    const unsigned char *pos_list = (const unsigned char *)fargs[1];
    unsigned char *op = (unsigned char *)out;
    unsigned char *end = op + 7999;
    int first_output = 1;
    while (*pos_list) {
        /* Parse next number.  Skip non-numeric tokens to match
         * the interpreter's mux_atol which consumes and ignores
         * malformed entries like negative numbers or letters. */
        while (*pos_list == ' ') pos_list++;
        if (*pos_list == '\0') break;
        /* Parse signed integer to match mux_atol: optional +/- then digits. */
        int sign = 1;
        if (*pos_list == '+') { pos_list++; }
        else if (*pos_list == '-') { sign = -1; pos_list++; }
        int pos = 0;
        int advanced = 0;
        while (*pos_list >= '0' && *pos_list <= '9') {
            pos = pos * 10 + (*pos_list - '0');
            pos_list++;
            advanced = 1;
        }
        pos *= sign;
        if (!advanced) {
            /* Non-numeric token: skip to next space or end. */
            while (*pos_list && *pos_list != ' ') pos_list++;
        }
        while (*pos_list == ' ') pos_list++;
        /* Extract element at position. */
        if (pos < 1) continue;
        const unsigned char *p = data;
        const unsigned char *pe = data + dlen;
        int cur = 1;
        /* Find element #pos.  For space delimiter, skip consecutive
         * spaces (same as split_token in the interpreter). */
        while (cur < pos && p < pe) {
            while (p < pe && *p != delim) p++;
            if (p < pe && *p == delim) {
                p++;
                if (delim == ' ') { while (p < pe && *p == ' ') p++; }
                cur++;
            }
        }
        if (cur == pos && p < pe) {
            if (!first_output && !osep_null && op < end) *op++ = osep;
            first_output = 0;
            while (p < pe && *p != delim && op < end) *op++ = *p++;
        }
    }
    *op = '\0';
    return out;
}

/* translate(string, type) — control-char conversion via ECALL_TRANSLATE (0x162)
 * type 's'/0 = control chars to spaces, 'p'/1 = percent substitutions.
 */
char *rv64_translate(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) {
        if (nfargs >= 1) rv64_scopy(out, fargs[0]);
        else out[0] = '\0';
        return out;
    }
    int type = (fargs[1][0] == 'p' || fargs[1][0] == '1') ? 1 : 0;
    ecall3(0x162, (long)fargs[0], (long)type, (long)out);
    return out;
}

/* ---------------------------------------------------------------
 * Batch 5: wildcard matching — strmatch, match, grab, graball.
 * Uses ECALL_QUICK_WILD (0x163) for Unicode-aware case-insensitive
 * matching via the host's quick_wild() (mux_strlwr + wildcard engine).
 * --------------------------------------------------------------- */

/* strmatch(string, pattern) — returns 1 or 0 */
char *rv64_strmatch(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '0'; out[1] = '\0'; return out; }
    long result = ecall2(0x163, (long)fargs[1], (long)fargs[0]);
    out[0] = result ? '1' : '0';
    out[1] = '\0';
    return out;
}

/* match(list, pattern[, delim]) — 1-based position of first match, 0 if none */
char *rv64_match(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '0'; out[1] = '\0'; return out; }
    unsigned char delim = ' ';
    if (nfargs >= 3 && fargs[2][0] != '\0') delim = (unsigned char)fargs[2][0];
    const unsigned char *p = (const unsigned char *)fargs[0];
    unsigned char elem[8192];
    int pos = 1;
    while (*p) {
        if (delim == ' ') while (*p == ' ') p++;
        if (*p == '\0') break;
        unsigned char *ep = elem;
        while (*p && *p != delim && ep < elem + sizeof(elem) - 1)
            *ep++ = *p++;
        *ep = '\0';
        if (ecall2(0x163, (long)fargs[1], (long)elem)) {
            sitoa(out, pos);
            return out;
        }
        if (*p == delim) p++;
        pos++;
    }
    out[0] = '0'; out[1] = '\0';
    return out;
}

/* grab(list, pattern[, delim]) — first matching element */
char *rv64_grab(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    unsigned char delim = ' ';
    if (nfargs >= 3 && fargs[2][0] != '\0') delim = (unsigned char)fargs[2][0];
    const unsigned char *p = (const unsigned char *)fargs[0];
    while (*p) {
        if (delim == ' ') while (*p == ' ') p++;
        if (*p == '\0') break;
        const unsigned char *start = p;
        while (*p && *p != delim) p++;
        size_t elen = (size_t)(p - start);
        unsigned char elem[8192];
        if (elen >= sizeof(elem)) elen = sizeof(elem) - 1;
        memcpy(elem, start, elen);
        elem[elen] = '\0';
        if (ecall2(0x163, (long)fargs[1], (long)elem)) {
            memcpy(out, start, elen);
            out[elen] = '\0';
            return out;
        }
        if (*p == delim) p++;
    }
    out[0] = '\0';
    return out;
}

/* graball(list, pattern[, delim][, osep]) — all matching elements */
char *rv64_graball(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    unsigned char delim = ' ';
    unsigned char osep = ' ';
    if (nfargs >= 3 && fargs[2][0] != '\0') delim = (unsigned char)fargs[2][0];
    if (nfargs >= 4 && fargs[3][0] != '\0') osep = (unsigned char)fargs[3][0];
    const unsigned char *p = (const unsigned char *)fargs[0];
    unsigned char *op = (unsigned char *)out;
    unsigned char *end = op + 7999;
    int first = 1;
    while (*p) {
        if (delim == ' ') while (*p == ' ') p++;
        if (*p == '\0') break;
        const unsigned char *start = p;
        while (*p && *p != delim) p++;
        size_t elen = (size_t)(p - start);
        unsigned char elem[8192];
        if (elen >= sizeof(elem)) elen = sizeof(elem) - 1;
        memcpy(elem, start, elen);
        elem[elen] = '\0';
        if (ecall2(0x163, (long)fargs[1], (long)elem)) {
            if (!first && op < end) *op++ = osep;
            first = 0;
            size_t copy = elen;
            if (op + copy > end) copy = (size_t)(end - op);
            memcpy(op, start, copy);
            op += copy;
        }
        if (*p == delim) p++;
    }
    *op = '\0';
    return out;
}

/* ---------------------------------------------------------------
 * Helper: inline itoa, writes decimal to buf, returns length.
 * --------------------------------------------------------------- */

/* ---------------------------------------------------------------
 * Batch 6: lnum, isnum, isint, chr, ord, dec2hex, hex2dec, baseconv.
 * --------------------------------------------------------------- */

/* lnum(count) or lnum(start, end[, osep[, step]]) — generate number list */
char *rv64_lnum(char *out, const char **fargs, int nfargs) {
    int start = 0, end_val = 0, step = 1;
    unsigned char osep = ' ';
    if (nfargs == 1) {
        end_val = satoi(fargs[0]) - 1;
    } else if (nfargs >= 2) {
        start = satoi(fargs[0]);
        end_val = satoi(fargs[1]);
    }
    if (nfargs >= 3 && fargs[2][0] != '\0') osep = (unsigned char)fargs[2][0];
    if (nfargs >= 4) {
        step = satoi(fargs[3]);
        if (step == 0) step = 1;
    }
    if (start > end_val && step > 0) step = -step;
    char *op = out;
    char *end = out + 7999;
    int first = 1;
    int i = start;
    for (;;) {
        if (step > 0 && i > end_val) break;
        if (step < 0 && i < end_val) break;
        if (!first && op < end) *op++ = (char)osep;
        first = 0;
        int n = sitoa(op, i);
        op += n;
        if (op >= end) break;
        i += step;
    }
    *op = '\0';
    return out;
}

/* isnum(string) — is it a valid number? */
char *rv64_isnum(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') { out[0] = '0'; out[1] = '\0'; return out; }
    const char *p = fargs[0];
    if (*p == '-' || *p == '+') p++;
    if (*p == '\0') { out[0] = '0'; out[1] = '\0'; return out; }
    int has_digit = 0, has_dot = 0;
    while (*p) {
        if (*p >= '0' && *p <= '9') { has_digit = 1; p++; }
        else if (*p == '.' && !has_dot) { has_dot = 1; p++; }
        else { out[0] = '0'; out[1] = '\0'; return out; }
    }
    out[0] = has_digit ? '1' : '0';
    out[1] = '\0';
    return out;
}

/* isint(string) — is it a valid integer? */
char *rv64_isint(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') { out[0] = '0'; out[1] = '\0'; return out; }
    const char *p = fargs[0];
    if (*p == '-' || *p == '+') p++;
    if (*p == '\0') { out[0] = '0'; out[1] = '\0'; return out; }
    while (*p) {
        if (*p < '0' || *p > '9') { out[0] = '0'; out[1] = '\0'; return out; }
        p++;
    }
    out[0] = '1'; out[1] = '\0';
    return out;
}

/* chr(codepoints) — Unicode codepoints to UTF-8 via ECALL_CHR (0x160) */
char *rv64_chr(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    long rc = ecall2(0x160, (long)fargs[0], (long)out);
    if (rc != 0) {
        /* Error message already in out from the ECALL handler. */
    }
    return out;
}

/* ord(string) — first grapheme cluster to codepoints via ECALL_ORD (0x161) */
char *rv64_ord(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') {
        rv64_scopy(out, "#-1 FUNCTION EXPECTS ONE CHARACTER");
        return out;
    }
    long rc = ecall2(0x161, (long)fargs[0], (long)out);
    if (rc != 0) {
        /* Error message already in out from the ECALL handler. */
    }
    return out;
}

/* dec2hex(number) — decimal to hexadecimal */
char *rv64_dec2hex(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '0'; out[1] = '\0'; return out; }
    long long val = 0;
    const char *p = fargs[0];
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
    if (neg) val = -val;
    /* Format as hex. */
    if (val == 0) { out[0] = '0'; out[1] = '\0'; return out; }
    char tmp[20];
    int pos = 0;
    unsigned long long uv = (unsigned long long)val;
    if (val < 0) uv = (unsigned long long)(-val);
    while (uv > 0) {
        int d = (int)(uv & 0xF);
        tmp[pos++] = (d < 10) ? ('0' + d) : ('A' + d - 10);
        uv >>= 4;
    }
    char *op = out;
    if (neg) *op++ = '-';
    for (int i = pos - 1; i >= 0; i--) *op++ = tmp[i];
    *op = '\0';
    return out;
}

/* hex2dec(hex_string) — hexadecimal to decimal */
char *rv64_hex2dec(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') { out[0] = '0'; out[1] = '\0'; return out; }
    const char *p = fargs[0];
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    /* Skip 0x prefix. */
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    long long val = 0;
    while (*p) {
        int d;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else break;
        val = val * 16 + d;
        p++;
    }
    if (neg) val = -val;
    /* Format as decimal using sitoa for the magnitude. */
    char *op = out;
    if (val < 0) { *op++ = '-'; val = -val; }
    if (val == 0) { out[0] = '0'; out[1] = '\0'; return out; }
    char tmp[20];
    int pos = 0;
    while (val > 0) { tmp[pos++] = '0' + (int)(val % 10); val /= 10; }
    for (int i = pos - 1; i >= 0; i--) *op++ = tmp[i];
    *op = '\0';
    return out;
}

/* ---------------------------------------------------------------
 * Batch 7: wordpos, remove, table support, misc.
 * --------------------------------------------------------------- */

/* wordpos(string, word[, delim]) — position of exact word in string */
char *rv64_wordpos(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '0'; out[1] = '\0'; return out; }
    unsigned char delim = ' ';
    if (nfargs >= 3 && fargs[2][0] != '\0') delim = (unsigned char)fargs[2][0];
    const unsigned char *word = (const unsigned char *)fargs[1];
    size_t wlen = rv64_slen(fargs[1]);
    const unsigned char *p = (const unsigned char *)fargs[0];
    int pos = 1;
    while (*p) {
        if (delim == ' ') while (*p == ' ') p++;
        if (*p == '\0') break;
        const unsigned char *start = p;
        while (*p && *p != delim) p++;
        size_t elen = (size_t)(p - start);
        if (elen == wlen && memcmp(start, word, wlen) == 0) {
            sitoa(out, pos);
            return out;
        }
        if (*p == delim) p++;
        pos++;
    }
    out[0] = '0'; out[1] = '\0';
    return out;
}

/* remove(list, word[, delim][, osep]) — remove first occurrence of word */
char *rv64_remove(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) {
        if (nfargs >= 1) rv64_scopy(out, fargs[0]);
        else out[0] = '\0';
        return out;
    }
    unsigned char delim = get_delim(fargs, nfargs, 2);
    /* osep defaults to delimiter (interpreter parity). */
    unsigned char osep = delim;
    int osep_null = 0;
    if (nfargs >= 4) {
        if (fargs[3][0] != '\0')
            osep = (unsigned char)fargs[3][0];
        else
            osep_null = 1;
    }

    const unsigned char *word = (const unsigned char *)fargs[1];
    size_t wlen = rv64_slen(fargs[1]);

    /* Check that word doesn't contain the delimiter. */
    {
        const unsigned char *wp = word;
        const unsigned char *we = word + wlen;
        while (wp < we) {
            if (*wp == delim) {
                rv64_scopy(out, "#-1 CAN ONLY REMOVE ONE ELEMENT");
                return out;
            }
            wp++;
        }
    }

    /* Strip color from the target word for comparison. */
    unsigned char wordPlain[8000];
    size_t nWordPlain = co_strip_color(wordPlain, word, wlen);

    /* For space delimiter, trim leading/trailing spaces. */
    const unsigned char *data = (const unsigned char *)fargs[0];
    size_t dlen = rv64_slen(fargs[0]);
    if (delim == ' ') {
        while (dlen > 0 && *data == ' ') { data++; dlen--; }
        while (dlen > 0 && data[dlen - 1] == ' ') { dlen--; }
    }

    const unsigned char *p = data;
    const unsigned char *pe = data + dlen;
    unsigned char *op = (unsigned char *)out;
    unsigned char *end = op + 7999;
    int first = 1;
    int removed = 0;

    while (p < pe) {
        /* For space delimiter, skip consecutive spaces. */
        if (delim == ' ') while (p < pe && *p == ' ') p++;
        if (p >= pe) break;
        const unsigned char *start = p;
        while (p < pe && *p != delim) p++;
        size_t elen = (size_t)(p - start);

        if (!removed) {
            /* Strip color from this word for comparison. */
            unsigned char wPlain[8000];
            size_t nwp = co_strip_color(wPlain, start, elen);
            if (nwp == nWordPlain && memcmp(wPlain, wordPlain, nwp) == 0) {
                removed = 1;
                if (p < pe && *p == delim) {
                    p++;
                    if (delim == ' ') while (p < pe && *p == ' ') p++;
                }
                continue;
            }
        }

        if (!first && !osep_null && op < end) *op++ = osep;
        first = 0;
        size_t copy = elen;
        if (op + copy > end) copy = (size_t)(end - op);
        {
            size_t ci;
            for (ci = 0; ci < copy; ci++) op[ci] = start[ci];
        }
        op += copy;

        if (p < pe && *p == delim) {
            p++;
            if (delim == ' ') while (p < pe && *p == ' ') p++;
        }
    }
    *op = '\0';
    return out;
}

/* iter_delim(list, delim) — count of delimiter-separated items (alias for words) */
/* Already have WORDS. Skip. */

/* null(args...) — evaluate args, return nothing */
char *rv64_null(char *out, const char **fargs, int nfargs) {
    (void)fargs; (void)nfargs;
    out[0] = '\0';
    return out;
}

/* ---------------------------------------------------------------
 * Batch 8: list aggregation, list reversal, type checks.
 * --------------------------------------------------------------- */

/* ladd(list[, delim]) — sum of all numbers in list */
char *rv64_ladd(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '0'; out[1] = '\0'; return out; }
    unsigned char delim = ' ';
    if (nfargs >= 2 && fargs[1][0] != '\0') delim = (unsigned char)fargs[1][0];
    const char *p = fargs[0];
    long long sum = 0;
    while (*p) {
        while (*p == (char)delim) p++;
        if (*p == '\0') break;
        int neg = 0;
        if (*p == '-') { neg = 1; p++; }
        long long val = 0;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (neg) val = -val;
        sum += val;
        while (*p && *p != (char)delim) p++;
    }
    /* Format result. */
    char *op = out;
    if (sum < 0) { *op++ = '-'; sum = -sum; }
    if (sum == 0) { out[0] = '0'; out[1] = '\0'; return out; }
    char tmp[20]; int pos = 0;
    while (sum > 0) { tmp[pos++] = '0' + (int)(sum % 10); sum /= 10; }
    for (int i = pos - 1; i >= 0; i--) *op++ = tmp[i];
    *op = '\0';
    return out;
}

/* lmax(list[, delim]) — maximum number in list */
char *rv64_lmax(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') { out[0] = '0'; out[1] = '\0'; return out; }
    unsigned char delim = ' ';
    if (nfargs >= 2 && fargs[1][0] != '\0') delim = (unsigned char)fargs[1][0];
    const char *p = fargs[0];
    long long best = -9999999999LL;
    int found = 0;
    while (*p) {
        while (*p == (char)delim) p++;
        if (*p == '\0') break;
        long long val = 0; int neg = 0;
        if (*p == '-') { neg = 1; p++; }
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (neg) val = -val;
        if (!found || val > best) { best = val; found = 1; }
        while (*p && *p != (char)delim) p++;
    }
    sitoa(out, (int)best);
    return out;
}

/* lmin(list[, delim]) — minimum number in list */
char *rv64_lmin(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') { out[0] = '0'; out[1] = '\0'; return out; }
    unsigned char delim = ' ';
    if (nfargs >= 2 && fargs[1][0] != '\0') delim = (unsigned char)fargs[1][0];
    const char *p = fargs[0];
    long long best = 9999999999LL;
    int found = 0;
    while (*p) {
        while (*p == (char)delim) p++;
        if (*p == '\0') break;
        long long val = 0; int neg = 0;
        if (*p == '-') { neg = 1; p++; }
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (neg) val = -val;
        if (!found || val < best) { best = val; found = 1; }
        while (*p && *p != (char)delim) p++;
    }
    sitoa(out, (int)best);
    return out;
}

/* land(list[, delim]) — logical AND: 1 if all nonzero, else 0 */
char *rv64_land(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') { out[0] = '0'; out[1] = '\0'; return out; }
    unsigned char delim = ' ';
    if (nfargs >= 2 && fargs[1][0] != '\0') delim = (unsigned char)fargs[1][0];
    const char *p = fargs[0];
    while (*p) {
        while (*p == (char)delim) p++;
        if (*p == '\0') break;
        int val = satoi(p);
        if (val == 0) { out[0] = '0'; out[1] = '\0'; return out; }
        while (*p && *p != (char)delim) p++;
    }
    out[0] = '1'; out[1] = '\0';
    return out;
}

/* lor(list[, delim]) — logical OR: 1 if any nonzero, else 0 */
char *rv64_lor(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') { out[0] = '0'; out[1] = '\0'; return out; }
    unsigned char delim = ' ';
    if (nfargs >= 2 && fargs[1][0] != '\0') delim = (unsigned char)fargs[1][0];
    const char *p = fargs[0];
    while (*p) {
        while (*p == (char)delim) p++;
        if (*p == '\0') break;
        int val = satoi(p);
        if (val != 0) { out[0] = '1'; out[1] = '\0'; return out; }
        while (*p && *p != (char)delim) p++;
    }
    out[0] = '0'; out[1] = '\0';
    return out;
}

/* flip/revwords(list[, delim][, osep]) — reverse element order */
char *rv64_revwords(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    unsigned char delim = ' ';
    unsigned char osep = ' ';
    if (nfargs >= 2 && fargs[1][0] != '\0') delim = (unsigned char)fargs[1][0];
    if (nfargs >= 3 && fargs[2][0] != '\0') osep = (unsigned char)fargs[2][0];
    /* Split into element start/len pairs. */
    const unsigned char *p = (const unsigned char *)fargs[0];
    const unsigned char *starts[4096];
    size_t lens[4096];
    int count = 0;
    while (*p && count < 4096) {
        if (delim == ' ') while (*p == ' ') p++;
        if (*p == '\0') break;
        starts[count] = p;
        while (*p && *p != delim) p++;
        lens[count] = (size_t)(p - starts[count]);
        count++;
        if (*p == delim) p++;
    }
    /* Output in reverse. */
    unsigned char *op = (unsigned char *)out;
    unsigned char *end = op + 7999;
    for (int i = count - 1; i >= 0; i--) {
        if (i < count - 1 && op < end) *op++ = osep;
        size_t copy = lens[i];
        if (op + copy > end) copy = (size_t)(end - op);
        memcpy(op, starts[i], copy);
        op += copy;
    }
    *op = '\0';
    return out;
}

/* isdbref(string) — parse #<digits>, validate via ECALL_GOOD_OBJ */
char *rv64_isdbref(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '0'; out[1] = '\0'; return out; }
    const char *p = fargs[0];
    /* Skip leading spaces. */
    while (*p == ' ') p++;
    if (*p != '#') { out[0] = '0'; out[1] = '\0'; return out; }
    p++;
    /* Must start with a digit (no negative dbrefs). */
    if (*p < '0' || *p > '9') { out[0] = '0'; out[1] = '\0'; return out; }
    /* Parse integer. */
    long val = 0;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    /* Skip trailing spaces. */
    while (*p == ' ') p++;
    if (*p != '\0') { out[0] = '0'; out[1] = '\0'; return out; }
    /* ECALL_GOOD_OBJ = 0x150 */
    long ok = ecall1(0x150, val);
    out[0] = ok ? '1' : '0';
    out[1] = '\0';
    return out;
}

static int sitoa(char *buf, int val) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    char tmp[20];
    int pos = 0;
    while (val > 0) {
        tmp[pos++] = '0' + (val % 10);
        val /= 10;
    }
    char *p = buf;
    if (neg) *p++ = '-';
    for (int i = pos - 1; i >= 0; i--) *p++ = tmp[i];
    *p = '\0';
    return (int)(p - buf);
}

/* ---------------------------------------------------------------
 * cat(s1, s2, ..., sN) — concatenate with space separators.
 *
 * Equivalent to MUX cat(): join all arguments with single spaces.
 * --------------------------------------------------------------- */

char *rv64_cat(char *out, const char **fargs, int nfargs) {
    char *p = out;
    for (int i = 0; i < nfargs; i++) {
        if (i > 0) *p++ = ' ';
        p = rv64_scopy(p, fargs[i]);
    }
    if (nfargs == 0) *p = '\0';
    return out;
}

/* ---------------------------------------------------------------
 * strlen(s) — return length as decimal string.
 *
 * Equivalent to MUX strlen(): counts bytes, writes integer result.
 * --------------------------------------------------------------- */

/* rv64_strlen removed — superseded by co_strlen_wrap. */

char *rv64_strcat(char *out, const char **fargs, int nfargs) {
    char *p = out;
    for (int i = 0; i < nfargs; i++) {
        p = rv64_scopy(p, fargs[i]);
    }
    if (nfargs == 0) *p = '\0';
    return out;
}

/* ---------------------------------------------------------------
 * Helper: inline atoi (no libc dependency).
 * --------------------------------------------------------------- */

static int satoi(const char *s) {
    int v = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

/* rv64_extract, rv64_words, rv64_split_token, rv64_first, rv64_rest,
 * rv64_last, rv64_member, rv64_repeat, rv64_trim removed —
 * all superseded by co_*_wrap routing in s_tier2_map[].
 */


/* ---------------------------------------------------------------
 * before(str, pat) — return everything before first occurrence.
 *
 * Equivalent to MUX before(): returns the portion of str before
 * the first occurrence of pat.  Returns empty if pat not found.
 * Pat defaults to space.
 * --------------------------------------------------------------- */

char *rv64_before(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) {
        out[0] = '\0';
        return out;
    }

    const char *str = fargs[0];
    const char *pat = " ";
    if (nfargs >= 2 && fargs[1][0] != '\0') {
        pat = fargs[1];
    }
    int pat_len = rv64_slen(pat);

    /* Search for pat in str. */
    const char *p = str;
    while (*p) {
        int match = 1;
        for (int i = 0; i < pat_len; i++) {
            if (p[i] != pat[i]) { match = 0; break; }
        }
        if (match) {
            /* Copy everything before this point. */
            char *op = out;
            const char *s = str;
            while (s < p) *op++ = *s++;
            *op = '\0';
            return out;
        }
        p++;
    }

    /* Pattern not found — return the entire string (matching MUX). */
    rv64_scopy(out, str);
    return out;
}

/* ---------------------------------------------------------------
 * after(str, pat) — return everything after first occurrence.
 *
 * Equivalent to MUX after(): returns the portion of str after
 * the first occurrence of pat.  Returns empty if pat not found.
 * Pat defaults to space.
 * --------------------------------------------------------------- */

char *rv64_after(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) {
        out[0] = '\0';
        return out;
    }

    const char *str = fargs[0];
    const char *pat = " ";
    if (nfargs >= 2 && fargs[1][0] != '\0') {
        pat = fargs[1];
    }
    int pat_len = rv64_slen(pat);

    /* Search for pat in str. */
    const char *p = str;
    while (*p) {
        int match = 1;
        for (int i = 0; i < pat_len; i++) {
            if (p[i] != pat[i]) { match = 0; break; }
        }
        if (match) {
            /* Copy everything after the match. */
            rv64_scopy(out, p + pat_len);
            return out;
        }
        p++;
    }

    /* Pattern not found — return empty. */
    out[0] = '\0';
    return out;
}

/* rv64_ldelete, rv64_replace, rv64_insert removed —
 * all superseded by co_*_wrap routing in s_tier2_map[].
 */

/* ---------------------------------------------------------------
 * Math function stubs — DBT intrinsic targets.
 *
 * The DBT intercepts JALs to these symbols and replaces them with
 * native host libm calls.  These bodies are never executed; they
 * exist only so the linker emits symbols at known addresses.
 * ---------------------------------------------------------------
 */

double sin(double x)   { return x; }
double cos(double x)   { return x; }
double tan(double x)   { return x; }
double asin(double x)  { return x; }
double acos(double x)  { return x; }
double atan(double x)  { return x; }
double exp(double x)   { return x; }
double log(double x)   { return x; }
double log10(double x) { return x; }
double ceil(double x)  { return x; }
double floor(double x) { return x; }
double fabs(double x)  { return x; }

double pow(double x, double y)   { (void)y; return x; }
double atan2(double y, double x) { (void)x; return y; }
double fmod(double x, double y)  { (void)y; return x; }

/* ---------------------------------------------------------------
 * String↔double conversion — DBT intrinsic targets.
 *
 * rv64_strtod: parse string → double (intrinsic → host strtod)
 * rv64_fval:   format double → string (intrinsic → host fval)
 *
 * These stubs are never executed; the DBT intercepts and calls
 * the host implementations directly.
 * ---------------------------------------------------------------
 */

double rv64_strtod(const char *s) { (void)s; return 0.0; }
int rv64_fval(char *buf, double val) { (void)buf; (void)val; return 0; }
double rv64_nearest_pretty(double val) { return val; }  /* intrinsic → host NearestPretty */

/* rv64_ftoa_round: format double with rounding to frac digits.
 * Intrinsic → host mux_ftoa(val, true, frac) + FP class handling.
 * a0 = output buffer, fa0 = val, a1 = frac.  Returns length in a0.
 */
int rv64_ftoa_round(char *buf, double val, int frac) {
    (void)buf; (void)val; (void)frac; return 0;
}

/* ---------------------------------------------------------------
 * Tier 2 math wrappers — softcode function entry points.
 *
 * Each wrapper: parse fargs[0] string → double, call math fn
 * (intrinsic → native libm), format result → string.
 * All three steps run at native host speed via intrinsics.
 * ---------------------------------------------------------------
 */

#define MATH_WRAP_1(name, fn) \
char *rv64_##name(char *out, const char **fargs, int nfargs) { \
    if (nfargs < 1) { out[0] = '0'; out[1] = '\0'; return out; } \
    double val = rv64_strtod(fargs[0]); \
    int n = rv64_fval(out, fn(val)); \
    out[n] = '\0'; \
    return out; \
}

#define MATH_WRAP_2(name, fn) \
char *rv64_##name(char *out, const char **fargs, int nfargs) { \
    if (nfargs < 2) { out[0] = '0'; out[1] = '\0'; return out; } \
    double a = rv64_strtod(fargs[0]); \
    double b = rv64_strtod(fargs[1]); \
    int n = rv64_fval(out, fn(a, b)); \
    out[n] = '\0'; \
    return out; \
}

MATH_WRAP_1(sin,   sin)
MATH_WRAP_1(cos,   cos)
MATH_WRAP_1(tan,   tan)
MATH_WRAP_1(asin,  asin)
MATH_WRAP_1(acos,  acos)
MATH_WRAP_1(atan,  atan)
MATH_WRAP_1(exp,   exp)
MATH_WRAP_1(log,   log)
MATH_WRAP_1(log10, log10)
MATH_WRAP_1(ceil,  ceil)
MATH_WRAP_1(floor, floor)
MATH_WRAP_1(fabs,  fabs)
MATH_WRAP_1(sqrt,  sqrt)

MATH_WRAP_2(power, pow)
MATH_WRAP_2(atan2, atan2)
MATH_WRAP_2(fmod,  fmod)

/* ---------------------------------------------------------------
 * rv64_add / rv64_sub — replicate server's dual integer/float path.
 *
 * Integer fast path: if all args are integers with <= 9 digits
 * and cumulative max won't overflow a 32-bit long, use integer
 * arithmetic and format with itoa.
 *
 * Float fallback: parse all args as doubles, sum, format with fval.
 * For 2-arg sums the server uses AddDoubles (compensated Kahan sum)
 * but the compensation term is zero for 2 values with no rounding
 * error, so simple addition matches.  For N>2 args the sort +
 * compensate can produce different ULP rounding; we accept this
 * minor divergence since N>2 add() with non-integer args is rare.
 * ---------------------------------------------------------------
 */

static const long nMaximums[10] = {
    0, 9, 99, 999, 9999, 99999, 999999, 9999999, 99999999, 999999999
};

/* Check if string is an integer with at most 9 digits.
 * Returns digit count, or -1 if not a small integer.
 * Matches the server's is_integer() + nDigits <= 9 guard.
 */
static int is_int9(const char *s) {
    /* Skip leading spaces. */
    while (*s == ' ') s++;
    /* Optional sign. */
    if (*s == '-' || *s == '+') s++;
    if (*s < '0' || *s > '9') return -1;
    int n = 0;
    while (*s >= '0' && *s <= '9') { s++; n++; }
    /* Trailing spaces. */
    while (*s == ' ') s++;
    if (*s != '\0') return -1;
    return (n <= 9) ? n : -1;
}

/* Simple atol — matches mux_atol for small integers. */
static long rv64_atol(const char *s) {
    while (*s == ' ') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    long v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

/* Simple ltoa into buffer.  Returns length written. */
static int rv64_ltoa(char *buf, long val) {
    char tmp[20];
    int i = 0;
    int neg = 0;
    unsigned long uv;
    if (val < 0) { neg = 1; uv = (unsigned long)(-(val + 1)) + 1; }
    else { uv = (unsigned long)val; }
    if (uv == 0) { tmp[i++] = '0'; }
    else { while (uv) { tmp[i++] = '0' + (char)(uv % 10); uv /= 10; } }
    int len = 0;
    if (neg) buf[len++] = '-';
    while (i > 0) buf[len++] = tmp[--i];
    buf[len] = '\0';
    return len;
}

/* Simple atoi64 — matches mux_atoi64 for integer strings. */
static long long rv64_atoi64(const char *s) {
    while (*s == ' ') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    long long v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

/* Simple i64toa into buffer.  Returns length written. */
static int rv64_i64toa(char *buf, long long val) {
    char tmp[24];
    int i = 0;
    int neg = 0;
    unsigned long long uv;
    if (val < 0) { neg = 1; uv = (unsigned long long)(-(val + 1)) + 1; }
    else { uv = (unsigned long long)val; }
    if (uv == 0) { tmp[i++] = '0'; }
    else { while (uv) { tmp[i++] = '0' + (char)(uv % 10); uv /= 10; } }
    int len = 0;
    if (neg) buf[len++] = '-';
    while (i > 0) buf[len++] = tmp[--i];
    buf[len] = '\0';
    return len;
}

char *rv64_round(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '0'; out[1] = '\0'; return out; }
    double val = rv64_strtod(fargs[0]);
    int frac = (int)rv64_atol(fargs[1]);
    int n = rv64_ftoa_round(out, val, frac);
    out[n] = '\0';
    return out;
}

char *rv64_fdiv(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '0'; out[1] = '\0'; return out; }
    double top = rv64_strtod(fargs[0]);
    double bot = rv64_strtod(fargs[1]);
    /* IEEE 754: top/0.0 produces +Inf/-Inf/NaN — fval handles it. */
    int n = rv64_fval(out, top / bot);
    out[n] = '\0';
    return out;
}

char *rv64_trunc(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '0'; out[1] = '\0'; return out; }
    double val = rv64_strtod(fargs[0]);
    /* Truncate toward zero: use floor/ceil (intrinsics). */
    double ipart = (val >= 0.0) ? floor(val) : ceil(val);
    int n = rv64_fval(out, ipart);
    out[n] = '\0';
    return out;
}

char *rv64_sign(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '0'; out[1] = '\0'; return out; }
    double num = rv64_strtod(fargs[0]);
    if (num < 0.0)      { out[0] = '-'; out[1] = '1'; out[2] = '\0'; }
    else if (num > 0.0)  { out[0] = '1'; out[1] = '\0'; }
    else                 { out[0] = '0'; out[1] = '\0'; }
    return out;
}

char *rv64_min(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '0'; out[1] = '\0'; return out; }
    double minimum = rv64_strtod(fargs[0]);
    for (int i = 1; i < nfargs; i++) {
        double v = rv64_strtod(fargs[i]);
        if (v < minimum) minimum = v;
    }
    int n = rv64_fval(out, minimum);
    out[n] = '\0';
    return out;
}

char *rv64_max(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '0'; out[1] = '\0'; return out; }
    double maximum = rv64_strtod(fargs[0]);
    for (int i = 1; i < nfargs; i++) {
        double v = rv64_strtod(fargs[i]);
        if (v > maximum) maximum = v;
    }
    int n = rv64_fval(out, maximum);
    out[n] = '\0';
    return out;
}

char *rv64_mod(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '0'; out[1] = '\0'; return out; }
    long long bot = rv64_atoi64(fargs[1]);
    if (bot == 0) bot = 1;
    long long top = rv64_atoi64(fargs[0]);
    rv64_i64toa(out, top % bot);
    return out;
}

char *rv64_inc(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '1'; out[1] = '\0'; return out; }
    rv64_i64toa(out, rv64_atoi64(fargs[0]) + 1);
    return out;
}

char *rv64_dec(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '-'; out[1] = '1'; out[2] = '\0'; return out; }
    rv64_i64toa(out, rv64_atoi64(fargs[0]) - 1);
    return out;
}

char *rv64_mul(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '0'; out[1] = '\0'; return out; }
    double prod = 1.0;
    for (int i = 0; i < nfargs; i++) {
        prod *= rv64_strtod(fargs[i]);
    }
    int n = rv64_fval(out, rv64_nearest_pretty(prod));
    out[n] = '\0';
    return out;
}

char *rv64_add(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '0'; out[1] = '\0'; return out; }

    /* Integer fast path: check all args. */
    long nMaxValue = 0;
    int all_int = 1;
    for (int i = 0; i < nfargs; i++) {
        int nd = is_int9(fargs[i]);
        if (nd < 0) { all_int = 0; break; }
        nMaxValue += nMaximums[nd];
        if (nMaxValue > 999999999L) { all_int = 0; break; }
    }

    if (all_int) {
        long sum = 0;
        for (int i = 0; i < nfargs; i++) {
            sum += rv64_atol(fargs[i]);
        }
        rv64_ltoa(out, sum);
    } else {
        double sum = 0.0;
        for (int i = 0; i < nfargs; i++) {
            sum += rv64_strtod(fargs[i]);
        }
        int n = rv64_fval(out, sum);
        out[n] = '\0';
    }
    return out;
}

char *rv64_sub(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '0'; out[1] = '\0'; return out; }

    int nd0 = is_int9(fargs[0]);
    int nd1 = is_int9(fargs[1]);

    if (nd0 >= 0 && nd1 >= 0) {
        /* Integer fast path. */
        long a = rv64_atol(fargs[0]);
        long b = rv64_atol(fargs[1]);
        rv64_ltoa(out, a - b);
    } else {
        double a = rv64_strtod(fargs[0]);
        double b = rv64_strtod(fargs[1]);
        int n = rv64_fval(out, a - b);
        out[n] = '\0';
    }
    return out;
}
