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
size_t co_mid_cluster(unsigned char *out, const unsigned char *data,
                      size_t len, size_t iStart, size_t nCount);
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
size_t co_delete_at(unsigned char *out, const unsigned char *list, size_t llen,
                    int *positions, int nPositions,
                    unsigned char delim, unsigned char osep);
size_t co_splice(unsigned char *out,
                 const unsigned char *list1, size_t len1,
                 const unsigned char *list2, size_t len2,
                 const unsigned char *search, size_t slen,
                 unsigned char delim, unsigned char osep);
size_t co_insert_word(unsigned char *out, const unsigned char *list,
                      size_t llen, size_t pos, const unsigned char *word,
                      size_t wlen, unsigned char delim, unsigned char osep);
size_t co_replace_at(unsigned char *out, const unsigned char *list,
                     size_t llen, int *positions, int nPositions,
                     const unsigned char *word, size_t wlen,
                     unsigned char delim, unsigned char osep);
size_t co_insert_at(unsigned char *out, const unsigned char *list,
                    size_t llen, int *positions, int nPositions,
                    const unsigned char *word, size_t wlen,
                    unsigned char delim, unsigned char osep);

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
static int sisspace(char c);
static int sis_integer(const char *s);

/* String<->double intrinsic stubs (defined near the math wrappers;
 * the DBT replaces them with host mux_atof / fval). */
double rv64_strtod(const char *s);
int rv64_fval(char *buf, double val);

/* Per-evaluation guest heap allocator (intrinsic → host_alloc). */
void *rv64_alloc(unsigned long n);

/* Error-compensated list sum (intrinsic → host AddDoubles + NearestPretty). */
double rv64_add_doubles(double *vals, int n);

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
    /* fun_mid negative normalization (mirrors the interpreter and the
     * host-side MID fold): a negative count selects before the start
     * position; a negative start eats into the count (#782). */
    if (count < 0) {
        start += 1 + count;
        count = -count;
    }
    if (start < 0) {
        count += start;
        start = 0;
    }
    if (count <= 0) { out[0] = '\0'; return out; }
    /* co_mid_cluster, not co_mid: fun_mid counts grapheme clusters and
     * excludes color codes trailing the last copied cluster; the
     * code-point-based co_mid included them, a raw-byte divergence. */
    size_t n = co_mid_cluster((unsigned char *)out,
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
    /* co_pos returns 0 for not found, 1-based position otherwise.  The
     * interpreter's fun_pos returns the string "#-1" for not found (via
     * safe_nothing), so match it here — otherwise @if pos(...) and #-1
     * comparisons flip truthiness on the blob/DBT runtime path.  See #770. */
    if (pos == 0) {
        rv64_scopy(out, "#-1");
        return out;
    }
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

/* decode_positions is defined just below; ldelete needs it too. */
static int decode_positions(const char *str, int *ai, int max_n);

/* ldelete(list, positions[, delim][, osep]) — delete word(s) at the given
 * 1-based positions.  Mirrors the interpreter's fun_ldelete (do_itemfuns
 * IF_DELETE): a space-separated position list, not a single position. */
char *co_ldelete_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) {
        if (nfargs >= 1) rv64_scopy(out, fargs[0]);
        else out[0] = '\0';
        return out;
    }
    unsigned char delim = get_delim(fargs, nfargs, 2);
    unsigned char osep = get_osep(fargs, nfargs, 3, delim);

    int positions[LBUF_SIZE / 2];
    int npos = decode_positions(fargs[1], positions, LBUF_SIZE / 2);

    size_t n = co_delete_at((unsigned char *)out,
                            (const unsigned char *)fargs[0],
                            rv64_slen(fargs[0]),
                            positions, npos,
                            delim, osep);
    out[n] = '\0';
    return out;
}

/* Helper: parse a space-separated list of integers from a string.
 * Matches DecodeListOfIntegers in the interpreter: split on spaces,
 * mux_atol() each token.  mux_atol stops at the first non-digit, so a
 * token's trailing garbage is ignored ("3-1" is ONE position, 3, not
 * [3,-1]), and a non-numeric token contributes 0 rather than being
 * skipped (#782). */
static int decode_positions(const char *str, int *ai, int max_n) {
    int n = 0;
    const char *p = str;
    while (*p == ' ') p++;
    while (*p && n < max_n) {
        /* One token: mux_atol semantics. */
        const char *q = p;
        while (sisspace(*q)) q++;
        int sign = 1;
        if (*q == '+') { q++; }
        else if (*q == '-') { sign = -1; q++; }
        int val = 0;
        while (*q >= '0' && *q <= '9') {
            val = val * 10 + (*q - '0');
            q++;
        }
        ai[n++] = val * sign;
        /* Skip the rest of the token, then the separator run. */
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }
    return n;
}

/* replace(list, positions, word[, delim][, osep]) */
char *co_replace_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) {
        if (nfargs >= 1) rv64_scopy(out, fargs[0]);
        else out[0] = '\0';
        return out;
    }
    unsigned char delim = get_delim(fargs, nfargs, 3);
    unsigned char osep = get_osep(fargs, nfargs, 4, delim);

    int positions[LBUF_SIZE / 2];
    int npos = decode_positions(fargs[1], positions, LBUF_SIZE / 2);

    size_t n = co_replace_at((unsigned char *)out,
                             (const unsigned char *)fargs[0],
                             rv64_slen(fargs[0]),
                             positions, npos,
                             (const unsigned char *)fargs[2],
                             rv64_slen(fargs[2]),
                             delim, osep);
    out[n] = '\0';
    return out;
}

/* insert(list, positions, word[, delim][, osep]) */
char *co_insert_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) {
        if (nfargs >= 1) rv64_scopy(out, fargs[0]);
        else out[0] = '\0';
        return out;
    }
    unsigned char delim = get_delim(fargs, nfargs, 3);
    unsigned char osep = get_osep(fargs, nfargs, 4, delim);

    int positions[LBUF_SIZE / 2];
    int npos = decode_positions(fargs[1], positions, LBUF_SIZE / 2);

    size_t n = co_insert_at((unsigned char *)out,
                            (const unsigned char *)fargs[0],
                            rv64_slen(fargs[0]),
                            positions, npos,
                            (const unsigned char *)fargs[2],
                            rv64_slen(fargs[2]),
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
/* co_splice declared above — (out, list1, len1, list2, len2, search, slen, delim, osep) */
size_t co_totitle(unsigned char *out, const unsigned char *data, size_t len);
size_t co_strip_color(unsigned char *out, const unsigned char *data,
                      size_t len);
size_t co_visible_length(const unsigned char *data, size_t len);
size_t co_split_words(const unsigned char *data, size_t len,
                      const unsigned char *sep, size_t sep_len,
                      size_t *word_starts, size_t *word_ends,
                      size_t max_words);

/* centerjustcombo width parity (#782): "" for a non-integer or zero
 * width; the value is parsed with mux_atol semantics and narrowed
 * through LBUF_OFFSET (uint16), so negative widths wrap; widths at or
 * past LBUF_SIZE are a range error.  Returns -1 for "emit nothing",
 * -2 for "emit #-1 OUT OF RANGE", else the width. */
static long parse_justify_width(const char *s) {
    if (!sis_integer(s)) return -1;
    unsigned short w = (unsigned short)satoi(s);
    if (w == 0) return -1;
    if (w >= LBUF_SIZE) return -2;
    return (long)w;
}

/* ljust(string, width[, fill]) */
char *co_ljust_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    size_t len = rv64_slen(fargs[0]);
    long width = parse_justify_width(fargs[1]);
    if (width == -1) { out[0] = '\0'; return out; }
    if (width == -2) { rv64_scopy(out, "#-1 OUT OF RANGE"); return out; }
    const unsigned char *fill = (const unsigned char *)" ";
    size_t fill_len = 1;
    if (nfargs >= 3 && fargs[2][0] != '\0') {
        fill = (const unsigned char *)fargs[2];
        fill_len = rv64_slen(fargs[2]);
    }
    /* bTrunc=1: fun_ljust truncates when width < content (centerjustcombo
     * passes bTrunc=true).  See #772. */
    size_t n = co_ljust((unsigned char *)out,
                        (const unsigned char *)fargs[0], len,
                        (size_t)width, fill, fill_len, 1);
    out[n] = '\0';
    return out;
}

/* rjust(string, width[, fill]) */
char *co_rjust_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    size_t len = rv64_slen(fargs[0]);
    long width = parse_justify_width(fargs[1]);
    if (width == -1) { out[0] = '\0'; return out; }
    if (width == -2) { rv64_scopy(out, "#-1 OUT OF RANGE"); return out; }
    const unsigned char *fill = (const unsigned char *)" ";
    size_t fill_len = 1;
    if (nfargs >= 3 && fargs[2][0] != '\0') {
        fill = (const unsigned char *)fargs[2];
        fill_len = rv64_slen(fargs[2]);
    }
    /* bTrunc=1: see co_ljust_wrap above (#772). */
    size_t n = co_rjust((unsigned char *)out,
                        (const unsigned char *)fargs[0], len,
                        (size_t)width, fill, fill_len, 1);
    out[n] = '\0';
    return out;
}

/* center(string, width[, fill]) */
char *co_center_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    size_t len = rv64_slen(fargs[0]);
    long width = parse_justify_width(fargs[1]);
    if (width == -1) { out[0] = '\0'; return out; }
    if (width == -2) { rv64_scopy(out, "#-1 OUT OF RANGE"); return out; }
    const unsigned char *fill = (const unsigned char *)" ";
    size_t fill_len = 1;
    if (nfargs >= 3 && fargs[2][0] != '\0') {
        fill = (const unsigned char *)fargs[2];
        fill_len = rv64_slen(fargs[2]);
    }
    /* bTrunc=1: see co_ljust_wrap above (#772). */
    size_t n = co_center((unsigned char *)out,
                         (const unsigned char *)fargs[0], len,
                         (size_t)width, fill, fill_len, 1);
    out[n] = '\0';
    return out;
}

/* edit(string, from, to) */
/* edit(string, from, to[, from2, to2, ...])
 *
 * Supports anchor semantics matching the interpreter:
 *   ^ as from  → prepend 'to' to string
 *   $ as from  → append 'to' to string
 *   \^ or %^   → literal ^ substitution
 *   \$ or %$   → literal $ substitution
 * Multiple from/to pairs applied sequentially.
 */
char *co_edit_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) { out[0] = '\0'; return out; }

    /* Use two alternating buffers like the interpreter.  Sized
     * LBUF_SIZE: co_edit() writes up to LBUF_SIZE-1 bytes into the
     * destination, so anything smaller is a stack overflow. */
    unsigned char bufA[LBUF_SIZE], bufB[LBUF_SIZE];
    size_t nLen = rv64_slen(fargs[0]);
    if (nLen > LBUF_SIZE - 1) nLen = LBUF_SIZE - 1;
    { size_t ci; for (ci = 0; ci < nLen; ci++) bufA[ci] = (unsigned char)fargs[0][ci]; }
    bufA[nLen] = '\0';

    unsigned char *pSrc = bufA, *pDst = bufB;
    int i;
    for (i = 1; i + 1 < nfargs; i += 2) {
        const unsigned char *pFrom = (const unsigned char *)fargs[i];
        const unsigned char *pTo = (const unsigned char *)fargs[i + 1];
        size_t fLen = rv64_slen(fargs[i]);
        size_t tLen = rv64_slen(fargs[i + 1]);

        if (fLen == 1 && pFrom[0] == '^') {
            /* Prepend 'to' to string. */
            size_t nTotal = tLen + nLen;
            if (nTotal > LBUF_SIZE - 1) nTotal = LBUF_SIZE - 1;
            size_t nToCopy = (tLen < LBUF_SIZE - 1) ? tLen : LBUF_SIZE - 1;
            { size_t ci; for (ci = 0; ci < nToCopy; ci++) pDst[ci] = pTo[ci]; }
            size_t nRemain = (nTotal > nToCopy) ? nTotal - nToCopy : 0;
            if (nRemain > 0) {
                size_t ci; for (ci = 0; ci < nRemain; ci++) pDst[nToCopy + ci] = pSrc[ci];
            }
            nLen = nTotal;
            pDst[nLen] = '\0';
        } else if (fLen == 1 && pFrom[0] == '$') {
            /* Append 'to' to string. */
            size_t nTotal = nLen + tLen;
            if (nTotal > LBUF_SIZE - 1) nTotal = LBUF_SIZE - 1;
            { size_t ci; for (ci = 0; ci < nLen; ci++) pDst[ci] = pSrc[ci]; }
            size_t nAppend = nTotal - nLen;
            if (nAppend > 0) {
                size_t ci; for (ci = 0; ci < nAppend; ci++) pDst[nLen + ci] = pTo[ci];
            }
            nLen = nTotal;
            pDst[nLen] = '\0';
        } else {
            /* Handle escaped ^ and $ (\^ %^ \$ %$). */
            const unsigned char *pFromActual = pFrom;
            size_t fLenActual = fLen;
            unsigned char fromBuf[2];
            if (fLen == 2
                && (pFrom[0] == '\\' || pFrom[0] == '%')
                && (pFrom[1] == '^' || pFrom[1] == '$')) {
                fromBuf[0] = pFrom[1];
                fromBuf[1] = '\0';
                pFromActual = fromBuf;
                fLenActual = 1;
            }
            nLen = co_edit(pDst, pSrc, nLen,
                           pFromActual, fLenActual, pTo, tLen);
        }
        /* Swap buffers. */
        { unsigned char *tmp = pSrc; pSrc = pDst; pDst = tmp; }
    }
    /* Copy result to output. */
    { size_t ci; for (ci = 0; ci <= nLen; ci++) out[ci] = (char)pSrc[ci]; }
    return out;
}

/* splice(list1, list2, word[, delim][, osep])
 *
 * For each word in list1, if it matches 'word', output the
 * corresponding word from list2 instead.  list1 and list2 must
 * have the same number of words.
 */
char *co_splice_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) { out[0] = '\0'; return out; }
    unsigned char delim = get_delim(fargs, nfargs, 3);
    unsigned char osep = get_osep(fargs, nfargs, 4, delim);

    /* Validate: search word must be single-word (no delimiter). */
    {
        const unsigned char *wp = (const unsigned char *)fargs[2];
        while (*wp) {
            if (*wp == delim) {
                rv64_scopy(out, "#-1 TOO MANY WORDS");
                return out;
            }
            wp++;
        }
    }

    /* Validate: list1 and list2 must have equal word counts. */
    size_t n1 = co_words_count((const unsigned char *)fargs[0],
                                rv64_slen(fargs[0]), delim);
    size_t n2 = co_words_count((const unsigned char *)fargs[1],
                                rv64_slen(fargs[1]), delim);
    if (n1 != n2) {
        rv64_scopy(out, "#-1 NUMBER OF WORDS MUST BE EQUAL");
        return out;
    }

    size_t n = co_splice((unsigned char *)out,
                         (const unsigned char *)fargs[0], rv64_slen(fargs[0]),
                         (const unsigned char *)fargs[1], rv64_slen(fargs[1]),
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

/* space(n) — generate N spaces.
 * fun_space parity: no argument, an empty argument, or a non-integer
 * argument means ONE space; an explicit integer zero (0, 00, ...)
 * means none; negatives clamp to zero. */
char *rv64_space(char *out, const char **fargs, int nfargs) {
    int n = 1;
    if (nfargs >= 1 && fargs[0][0] != '\0') {
        n = satoi(fargs[0]);
        if (n == 0 && !sis_integer(fargs[0])) n = 1;
        if (n < 0) n = 0;
    }
    if (n > LBUF_SIZE - 1) n = LBUF_SIZE - 1;
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
    unsigned char *end = op + LBUF_SIZE - 1;
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
        /* Emit even when p == pe: a trailing delimiter yields a
         * trailing EMPTY word at position cur (#789) — it still
         * participates in osep joining (elements(a||b|,2 4,|) is "|").
         * Positions past the last word leave cur < pos above. */
        if (cur == pos) {
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
    const unsigned char *pe = p + rv64_slen(fargs[0]);
    /* split_token walk (#789): for space, trim and collapse runs; for
     * any other delimiter EVERY occurrence is a boundary, so a trailing
     * delimiter yields a trailing empty word.  The walk always yields
     * at least one (possibly empty) word: match(,) is 1, not 0. */
    if (delim == ' ') {
        while (p < pe && *p == ' ') p++;
        while (pe > p && pe[-1] == ' ') pe--;
    }
    unsigned char elem[LBUF_SIZE];
    int pos = 1;
    for (;;) {
        const unsigned char *start = p;
        while (p < pe && *p != delim) p++;
        size_t elen = (size_t)(p - start);
        if (elen >= sizeof(elem)) elen = sizeof(elem) - 1;
        memcpy(elem, start, elen);
        elem[elen] = '\0';
        if (ecall2(0x163, (long)fargs[1], (long)elem)) {
            sitoa(out, pos);
            return out;
        }
        if (p >= pe) break;
        p++;
        if (delim == ' ') while (p < pe && *p == ' ') p++;
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
    const unsigned char *pe = p + rv64_slen(fargs[0]);
    /* split_token walk: see rv64_match (#789). */
    if (delim == ' ') {
        while (p < pe && *p == ' ') p++;
        while (pe > p && pe[-1] == ' ') pe--;
    }
    for (;;) {
        const unsigned char *start = p;
        while (p < pe && *p != delim) p++;
        size_t elen = (size_t)(p - start);
        unsigned char elem[LBUF_SIZE];
        if (elen >= sizeof(elem)) elen = sizeof(elem) - 1;
        memcpy(elem, start, elen);
        elem[elen] = '\0';
        if (ecall2(0x163, (long)fargs[1], (long)elem)) {
            memcpy(out, start, elen);
            out[elen] = '\0';
            return out;
        }
        if (p >= pe) break;
        p++;
        if (delim == ' ') while (p < pe && *p == ' ') p++;
    }
    out[0] = '\0';
    return out;
}

/* graball(list, pattern[, delim][, osep]) — all matching elements */
char *rv64_graball(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { out[0] = '\0'; return out; }
    unsigned char delim = get_delim(fargs, nfargs, 2);
    /* Absent osep defaults to the delimiter (DELIM_INIT parity, #782). */
    unsigned char osep = get_osep(fargs, nfargs, 3, delim);
    const unsigned char *p = (const unsigned char *)fargs[0];
    const unsigned char *pe = p + rv64_slen(fargs[0]);
    /* split_token walk: see rv64_match (#789). */
    if (delim == ' ') {
        while (p < pe && *p == ' ') p++;
        while (pe > p && pe[-1] == ' ') pe--;
    }
    unsigned char *op = (unsigned char *)out;
    unsigned char *end = op + LBUF_SIZE - 1;
    int first = 1;
    for (;;) {
        const unsigned char *start = p;
        while (p < pe && *p != delim) p++;
        size_t elen = (size_t)(p - start);
        unsigned char elem[LBUF_SIZE];
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
        if (p >= pe) break;
        p++;
        if (delim == ' ') while (p < pe && *p == ' ') p++;
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

/* lnum(count) or lnum(start, end[, osep[, step]]) — generate number list.
 * fun_lnum parity: the one-argument form returns empty for counts
 * below 1 (lnum(0), lnum(-3), lnum(foo)); step is clamped to >= 1 and
 * the direction comes only from start/end.  Multi-char oseps never
 * reach here (hir_lower falls back to ECALL for them). */
char *rv64_lnum(char *out, const char **fargs, int nfargs) {
    int start = 0, end_val = 0, step = 1;
    unsigned char osep = ' ';
    out[0] = '\0';
    if (nfargs < 1) return out;
    if (nfargs == 1) {
        end_val = satoi(fargs[0]) - 1;
        if (end_val < 0) return out;
    } else {
        start = satoi(fargs[0]);
        end_val = satoi(fargs[1]);
        if (nfargs >= 4) {
            step = satoi(fargs[3]);
            if (step < 1) step = 1;
        }
    }
    if (nfargs >= 3 && fargs[2][0] != '\0') osep = (unsigned char)fargs[2][0];
    if (start > end_val) step = -step;
    char *op = out;
    char *end = out + LBUF_SIZE - 1;
    int first = 1;
    int i = start;
    for (;;) {
        if (step > 0 && i > end_val) break;
        if (step < 0 && i < end_val) break;
        if (!first && op < end) *op++ = (char)osep;
        first = 0;
        {
            int n = sitoa(op, i);
            op += n;
        }
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
/* wordpos(<string>, <char position>[, <delim>]) — returns the 1-based
 * number of the word that the given 1-based CHARACTER position falls
 * within.  This mirrors the interpreter's fun_wordpos: color is stripped,
 * the position is bounded against the visible code-point count, and the
 * word index is found by walking word boundaries until one ends past the
 * target byte offset.  Out-of-range positions yield #-1. */
char *rv64_wordpos(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) { rv64_scopy(out, "#-1"); return out; }

    /* Strip color: charpos indexes into the visible (stripped) string,
     * and fun_wordpos uses cp[charpos-1] — a byte index. */
    unsigned char stripped[LBUF_SIZE];
    size_t slen = co_strip_color(stripped,
                                 (const unsigned char *)fargs[0],
                                 rv64_slen(fargs[0]));

    /* Bound charpos against the visible code-point count
     * (fun_wordpos: charpos > 0 && charpos <= ncp). */
    size_t ncp = co_visible_length(stripped, slen);
    int charpos = satoi(fargs[1]);
    if (charpos < 1 || (size_t)charpos > ncp) {
        rv64_scopy(out, "#-1");
        return out;
    }
    size_t tp = (size_t)(charpos - 1);

    unsigned char delim = get_delim(fargs, nfargs, 2);
    unsigned char sep[1];
    sep[0] = delim;

    size_t wstarts[LBUF_SIZE / 2];
    size_t wends[LBUF_SIZE / 2];
    size_t nWords = co_split_words(stripped, slen, sep, 1,
                                   wstarts, wends, LBUF_SIZE / 2);

    /* Return the 1-based word whose end byte-offset passes tp; if tp is
     * past the last word (e.g. in a trailing separator), return nWords+1,
     * matching the fun_wordpos split_token walk. */
    size_t i;
    for (i = 0; i < nWords; i++) {
        if (tp < wends[i]) {
            sitoa(out, (int)(i + 1));
            return out;
        }
    }
    sitoa(out, (int)(nWords + 1));
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
    unsigned char wordPlain[LBUF_SIZE];
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
    unsigned char *end = op + LBUF_SIZE - 1;
    int first = 1;
    int removed = 0;

    /* split_token walk: for a non-space delimiter, EVERY occurrence is
     * a word boundary and empty words are real -- in particular a
     * trailing delimiter yields a trailing empty word, which the old
     * while(p < pe) loop silently dropped (#789).  For space, runs
     * collapse and the input is already trimmed. */
    for (;;) {
        if (delim == ' ' && p >= pe) break;
        const unsigned char *start = p;
        while (p < pe && *p != delim) p++;
        size_t elen = (size_t)(p - start);
        int matched = 0;

        if (!removed) {
            /* Strip color from this word for comparison. */
            unsigned char wPlain[LBUF_SIZE];
            size_t nwp = co_strip_color(wPlain, start, elen);
            if (nwp == nWordPlain && memcmp(wPlain, wordPlain, nwp) == 0) {
                removed = 1;
                matched = 1;
            }
        }

        if (!matched) {
            if (!first && !osep_null && op < end) *op++ = osep;
            first = 0;
            size_t copy = elen;
            if (op + copy > end) copy = (size_t)(end - op);
            {
                size_t ci;
                for (ci = 0; ci < copy; ci++) op[ci] = start[ci];
            }
            op += copy;
        }

        if (p >= pe) break;
        p++;
        if (delim == ' ') while (p < pe && *p == ' ') p++;
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

/* ladd() is NOT implemented here: fun_ladd sums via AddDoubles
 * (|x|-sorted, error-compensated, NearestPretty), which a sequential
 * guest-side sum cannot reproduce — cancellation-heavy lists such as
 * ladd(1e20 1 -1e20) print differently.  LADD is not in the tier2
 * map (jit_compiler.cpp) and always ECALLs the interpreter. */

/* Copy one delimiter-bounded token into a NUL-terminated buffer so
 * number parsing sees exactly the token.  Parsing in place would
 * mis-read tokens when the delimiter is a character a number parser
 * consumes ('.', '-', a digit, 'e').  The caller provides an
 * LBUF_SIZE stack buffer (the blob has no .bss for statics; see
 * softlib.ld and the rv64_remove wPlain precedent). */
static const char *tok_to_buf(char *buf, const unsigned char *start, size_t elen) {
    size_t i;
    if (elen >= (size_t)LBUF_SIZE) elen = (size_t)LBUF_SIZE - 1;
    for (i = 0; i < elen; i++) buf[i] = (char)start[i];
    buf[elen] = '\0';
    return buf;
}

/* satoll: like satoi but 64-bit, for mux_atol parity (long is 64-bit
 * on the host) — land(4294967296) must be true, not a wrapped 0. */
static long long satoll(const char *s) {
    long long v = 0;
    int neg = 0;
    while (sisspace(*s)) s++;
    if (*s == '+') { s++; }
    else if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

/* Tier 2 doubles scratch — fixed guest address mirroring the host's
 * static g_aDoubles[MAX_WORDS].  MUST match rv_compiler::DSCRATCH_BASE in
 * mux/include/dbt_compile.h.  Reused per call (no cumulative limit), so
 * ladd has the same storage semantics as fun_ladd's single static array. */
#define RV64_DSCRATCH_ADDR 0x500000UL

/* Max function arguments (== MAX_ARG in config.h); rv64_add's stack vals[]. */
#define MAX_ARG_BLOB 100

/* fun_ladd caps the word count at MAX_WORDS == LBUF_SIZE (stringutil.h). */
#ifndef MAX_WORDS
#define MAX_WORDS LBUF_SIZE
#endif

/* ladd(list[, delim]) — error-compensated sum of the numbers in list.
 * fun_ladd parity: trim_space_sep + split_token walk (same as rv64_lmax),
 * mux_atof per word capped at MAX_WORDS, then AddDoubles (|x|-sorted,
 * error-compensated, NearestPretty).  The order-sensitive arithmetic runs
 * host-side via rv64_add_doubles, so the result is byte-identical to
 * fun_ladd by construction (a naive sequential sum diverges on
 * cancellation-heavy lists, e.g. ladd(1e20 1 -1e20) is 1, not 0).  See #813. */
char *rv64_ladd(char *out, const char **fargs, int nfargs) {
    double *vals = (double *)RV64_DSCRATCH_ADDR;
    int n = 0;
    if (nfargs >= 1) {
        char tok[LBUF_SIZE];
        unsigned char delim = ' ';
        const unsigned char *p = (const unsigned char *)fargs[0];
        const unsigned char *pe = p + rv64_slen(fargs[0]);
        if (nfargs >= 2 && fargs[1][0] != '\0') delim = (unsigned char)fargs[1][0];
        if (delim == ' ') {
            while (p < pe && *p == ' ') p++;
            while (pe > p && pe[-1] == ' ') pe--;
        }
        for (;;) {
            if (delim == ' ' && p >= pe) break;
            const unsigned char *start = p;
            while (p < pe && *p != delim) p++;
            if (n < MAX_WORDS) {
                vals[n++] = rv64_strtod(tok_to_buf(tok, start, (size_t)(p - start)));
            } else {
                break;  /* fun_ladd stops reading after MAX_WORDS words */
            }
            if (p >= pe) break;
            p++;
            if (delim == ' ') while (p < pe && *p == ' ') p++;
        }
    }
    {
        double result = rv64_add_doubles(vals, n);  /* AddDoubles + NearestPretty */
        int k = rv64_fval(out, result);
        out[k] = '\0';
    }
    return out;
}

/* lmax(list[, delim]) — maximum number in list.
 * Doubles via rv64_strtod/rv64_fval for fun_lmax parity (mux_atof +
 * fval): the old integer parse truncated decimals ("1.5" read as 1)
 * and the (int) output cast wrapped 64-bit values.  split_token walk
 * per rv64_match (#789): for a non-space delimiter, empty words are
 * real and contribute a 0 candidate. */
char *rv64_lmax(char *out, const char **fargs, int nfargs) {
    double best = 0.0;
    if (nfargs >= 1) {
        char tok[LBUF_SIZE];
        unsigned char delim = ' ';
        const unsigned char *p = (const unsigned char *)fargs[0];
        const unsigned char *pe = p + rv64_slen(fargs[0]);
        int found = 0;
        if (nfargs >= 2 && fargs[1][0] != '\0') delim = (unsigned char)fargs[1][0];
        if (delim == ' ') {
            while (p < pe && *p == ' ') p++;
            while (pe > p && pe[-1] == ' ') pe--;
        }
        for (;;) {
            if (delim == ' ' && p >= pe) break;
            const unsigned char *start = p;
            while (p < pe && *p != delim) p++;
            {
                double v = rv64_strtod(tok_to_buf(tok, start, (size_t)(p - start)));
                if (!found || v > best) { best = v; found = 1; }
            }
            if (p >= pe) break;
            p++;
            if (delim == ' ') while (p < pe && *p == ' ') p++;
        }
    }
    {
        int n = rv64_fval(out, best);
        out[n] = '\0';
    }
    return out;
}

/* lmin(list[, delim]) — minimum number in list.  See rv64_lmax. */
char *rv64_lmin(char *out, const char **fargs, int nfargs) {
    double best = 0.0;
    if (nfargs >= 1) {
        char tok[LBUF_SIZE];
        unsigned char delim = ' ';
        const unsigned char *p = (const unsigned char *)fargs[0];
        const unsigned char *pe = p + rv64_slen(fargs[0]);
        int found = 0;
        if (nfargs >= 2 && fargs[1][0] != '\0') delim = (unsigned char)fargs[1][0];
        if (delim == ' ') {
            while (p < pe && *p == ' ') p++;
            while (pe > p && pe[-1] == ' ') pe--;
        }
        for (;;) {
            if (delim == ' ' && p >= pe) break;
            const unsigned char *start = p;
            while (p < pe && *p != delim) p++;
            {
                double v = rv64_strtod(tok_to_buf(tok, start, (size_t)(p - start)));
                if (!found || v < best) { best = v; found = 1; }
            }
            if (p >= pe) break;
            p++;
            if (delim == ' ') while (p < pe && *p == ' ') p++;
        }
    }
    {
        int n = rv64_fval(out, best);
        out[n] = '\0';
    }
    return out;
}

/* land(list[, delim]) — 1 if every word is true, else 0.
 * fun_land parity: isTRUE(mux_atol(word)).  Zero arguments are
 * vacuously true; an empty or all-spaces list is ONE empty word and
 * therefore false; empty words from a non-space delimiter (#789
 * walk) parse as 0 and falsify: land(1||1,|) is 0. */
char *rv64_land(char *out, const char **fargs, int nfargs) {
    int result = 1;
    if (nfargs >= 1) {
        char tok[LBUF_SIZE];
        unsigned char delim = ' ';
        const unsigned char *p = (const unsigned char *)fargs[0];
        const unsigned char *pe = p + rv64_slen(fargs[0]);
        if (nfargs >= 2 && fargs[1][0] != '\0') delim = (unsigned char)fargs[1][0];
        if (delim == ' ') {
            while (p < pe && *p == ' ') p++;
            while (pe > p && pe[-1] == ' ') pe--;
            if (p >= pe) result = 0;   /* one empty word */
        }
        while (result) {
            if (delim == ' ' && p >= pe) break;
            const unsigned char *start = p;
            while (p < pe && *p != delim) p++;
            if (satoll(tok_to_buf(tok, start, (size_t)(p - start))) == 0) result = 0;
            if (p >= pe) break;
            p++;
            if (delim == ' ') while (p < pe && *p == ' ') p++;
        }
    }
    out[0] = result ? '1' : '0'; out[1] = '\0';
    return out;
}

/* lor(list[, delim]) — 1 if any word is true, else 0.
 * fun_lor parity: zero arguments and empty/all-empty-word lists are
 * false; the walk matches rv64_land. */
char *rv64_lor(char *out, const char **fargs, int nfargs) {
    int result = 0;
    if (nfargs >= 1) {
        char tok[LBUF_SIZE];
        unsigned char delim = ' ';
        const unsigned char *p = (const unsigned char *)fargs[0];
        const unsigned char *pe = p + rv64_slen(fargs[0]);
        if (nfargs >= 2 && fargs[1][0] != '\0') delim = (unsigned char)fargs[1][0];
        if (delim == ' ') {
            while (p < pe && *p == ' ') p++;
            while (pe > p && pe[-1] == ' ') pe--;
        }
        while (!result) {
            if (delim == ' ' && p >= pe) break;
            const unsigned char *start = p;
            while (p < pe && *p != delim) p++;
            if (satoll(tok_to_buf(tok, start, (size_t)(p - start))) != 0) result = 1;
            if (p >= pe) break;
            p++;
            if (delim == ' ') while (p < pe && *p == ' ') p++;
        }
    }
    out[0] = result ? '1' : '0'; out[1] = '\0';
    return out;
}

/* flip/revwords(list[, delim][, osep]) — reverse element order */
char *rv64_revwords(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    unsigned char delim = get_delim(fargs, nfargs, 1);
    /* Absent osep defaults to the delimiter (DELIM_INIT parity, #782). */
    unsigned char osep = get_osep(fargs, nfargs, 2, delim);
    /* Split into element start/len pairs.  split_token walk: see
     * rv64_match (#789) — a trailing delimiter yields a trailing
     * empty word, so revwords(a|,|) is |a. */
    const unsigned char *p = (const unsigned char *)fargs[0];
    const unsigned char *pe = p + rv64_slen(fargs[0]);
    if (delim == ' ') {
        while (p < pe && *p == ' ') p++;
        while (pe > p && pe[-1] == ' ') pe--;
    }
    const unsigned char *starts[LBUF_SIZE / 2];
    size_t lens[LBUF_SIZE / 2];
    int count = 0;
    for (;;) {
        if (count >= LBUF_SIZE / 2) break;
        starts[count] = p;
        while (p < pe && *p != delim) p++;
        lens[count] = (size_t)(p - starts[count]);
        count++;
        if (p >= pe) break;
        p++;
        if (delim == ' ') while (p < pe && *p == ' ') p++;
    }
    /* Output in reverse. */
    unsigned char *op = (unsigned char *)out;
    unsigned char *end = op + LBUF_SIZE - 1;
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

/* isdbref() is NOT implemented here: parse_dbref accepts the objid
 * #<dbref>:<timestamp> form, validated against the object's
 * creation_seconds — engine state this blob cannot reach (a compiled
 * isdbref(objid(me)) returned 0 where the interpreter returns 1).
 * ISDBREF is not in the tier2 map (jit_compiler.cpp) and always
 * ECALLs the interpreter. */

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
 *
 * Mirrors mux_atol(): skip leading whitespace, then an optional
 * '+' or '-', then digits.  The interpreter parses every numeric
 * function argument through mux_atol, so accepting "+3" and " 3"
 * here is required for parity (#782).
 * --------------------------------------------------------------- */

static int sisspace(char c) {
    return c == ' ' || c == '\t' || c == '\n'
        || c == '\v' || c == '\f' || c == '\r';
}

static int satoi(const char *s) {
    int v = 0;
    int neg = 0;
    while (sisspace(*s)) s++;
    if (*s == '+') { s++; }
    else if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

/* Mirrors is_integer(): optional leading whitespace, optional sign,
 * at least one digit, optional trailing whitespace, end of string. */
static int sis_integer(const char *s) {
    while (sisspace(*s)) s++;
    if (*s == '-' || *s == '+') {
        s++;
        if (*s == '\0') return 0;
    }
    if (!(*s >= '0' && *s <= '9')) return 0;
    while (*s >= '0' && *s <= '9') s++;
    while (sisspace(*s)) s++;
    return *s == '\0';
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

/* __attribute__((noipa)) on every stub (#785): the bodies are wrong by
 * design (they exist only so the linker emits symbols for the DBT to
 * intercept), so -O2 interprocedural optimization must never inline or
 * constprop-clone them into a caller -- a clone's guest address is not
 * in the intrinsic table, so the wrong body would actually run.  This
 * is the same hazard that broke the FP-conversion stubs in #778; until
 * now these relied on the toolchain merely happening not to inline.
 *
 * The bodies return NaN rather than a plausible value: if interception
 * ever fails (dropped registration, unexpected clone), results read
 * "NaN" instead of being subtly wrong, and the smoke suite fails
 * loudly. */
static double stub_nan(void) {
    union { unsigned long long u; double d; } v;
    v.u = 0x7FF8000000000000ULL;  /* quiet NaN */
    return v.d;
}

__attribute__((noipa)) double sin(double x)   { (void)x; return stub_nan(); }
__attribute__((noipa)) double cos(double x)   { (void)x; return stub_nan(); }
__attribute__((noipa)) double tan(double x)   { (void)x; return stub_nan(); }
__attribute__((noipa)) double asin(double x)  { (void)x; return stub_nan(); }
__attribute__((noipa)) double acos(double x)  { (void)x; return stub_nan(); }
__attribute__((noipa)) double atan(double x)  { (void)x; return stub_nan(); }
__attribute__((noipa)) double exp(double x)   { (void)x; return stub_nan(); }
__attribute__((noipa)) double log(double x)   { (void)x; return stub_nan(); }
__attribute__((noipa)) double log10(double x) { (void)x; return stub_nan(); }
__attribute__((noipa)) double ceil(double x)  { (void)x; return stub_nan(); }
__attribute__((noipa)) double floor(double x) { (void)x; return stub_nan(); }
__attribute__((noipa)) double fabs(double x)  { (void)x; return stub_nan(); }
__attribute__((noipa)) double trunc(double x) { (void)x; return stub_nan(); }

__attribute__((noipa)) double pow(double x, double y)   { (void)x; (void)y; return stub_nan(); }
__attribute__((noipa)) double atan2(double y, double x) { (void)x; (void)y; return stub_nan(); }
__attribute__((noipa)) double fmod(double x, double y)  { (void)x; (void)y; return stub_nan(); }

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

/* These stubs ignore their arguments and return constants, so -O2
 * interprocedural constant propagation would clone them (e.g.
 * rv64_strtod.constprop.0).  A blob-internal caller would then JAL the
 * clone, whose guest address is NOT in the DBT intrinsic table, so the
 * stub body runs and returns 0/empty.  __attribute__((noipa)) keeps the
 * canonical symbol the sole call target so the DBT can intercept it.
 */
__attribute__((noipa)) double rv64_strtod(const char *s) { (void)s; return 0.0; }
__attribute__((noipa)) int rv64_fval(char *buf, double val) { (void)buf; (void)val; return 0; }
__attribute__((noipa)) double rv64_nearest_pretty(double val) { return val; }  /* intrinsic → host NearestPretty */

/* rv64_ftoa_round: format double with rounding to frac digits.
 * Intrinsic → host mux_ftoa(val, true, frac) + FP class handling.
 * a0 = output buffer, fa0 = val, a1 = frac.  Returns length in a0.
 */
__attribute__((noipa)) int rv64_ftoa_round(char *buf, double val, int frac) {
    (void)buf; (void)val; (void)frac; return 0;
}

/* rv64_alloc: bump-allocate `n` bytes from the per-evaluation guest heap
 * arena.  Intrinsic → host_alloc, which returns a guest address (or 0 on
 * exhaustion → NULL).  Never freed; the host resets the arena before each
 * evaluation.  Stub body never runs (DBT intercepts).
 */
__attribute__((noipa)) void *rv64_alloc(unsigned long n) { (void)n; return (void *)0; }

/* rv64_add_doubles: sum n doubles with fun_ladd's error-compensated
 * algorithm.  Intrinsic → host AddDoubles(n, vals) (|x|-sorted qsort,
 * TwoSum chain, NearestPretty).  Stub body never runs (DBT intercepts).
 */
__attribute__((noipa)) double rv64_add_doubles(double *vals, int n) {
    (void)vals; (void)n; return 0.0;
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
/* trunc via libm ::trunc (truncate toward zero), as a single intercepted
 * call — NOT (val>=0)?floor():ceil(), which GCC pattern-matches into an inline
 * fcvt-with-rounding the DBT mistranslates (rounded away from zero, #827). */
MATH_WRAP_1(trunc, trunc)

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

/* ---------------------------------------------------------------
 * rv64_memself — self-test of the guest memory environment.
 *
 * Exercises all three writable-storage capabilities the blob now has:
 *   - .bss   : g_memself_bss (zero-initialized; reset to zero each eval)
 *   - .data  : g_memself_seed (initialized writable; restored each eval)
 *   - heap   : rv64_alloc (per-eval bump arena)
 *
 * Returns a deterministic checksum as a decimal string.  Because the
 * host zero-fills .bss, restores .data, and resets the heap before every
 * evaluation, the checksum is identical on every run — a differing value
 * across runs means a reset path is broken.
 *
 * It also forces .bss/.data to be non-empty in the linked blob, so the
 * softlib.ld + rv64strip writable/bss pipeline is exercised by the build.
 * Follows the standard Tier 2 wrapper ABI but is not mapped to any
 * softcode function name.
 * --------------------------------------------------------------- */
static unsigned char g_memself_bss[1024];          /* .bss  (zero-init) */
static unsigned int  g_memself_seed = 0x1234567u;  /* .data (writable)  */

char *rv64_memself(char *out, const char **fargs, int nfargs) {
    unsigned long long sum = 0;
    int i;
    (void)fargs; (void)nfargs;

    /* .bss must read as zero on entry; then write a pattern and read it. */
    for (i = 0; i < (int)sizeof(g_memself_bss); i++) {
        sum += g_memself_bss[i];                 /* 0 if BSS truly zeroed */
        g_memself_bss[i] = (unsigned char)(i * 7 + 1);
    }
    for (i = 0; i < (int)sizeof(g_memself_bss); i++) {
        sum += g_memself_bss[i];
    }

    /* .data: fold in the seed, then mutate it.  A correct per-eval reset
     * restores g_memself_seed so the next run folds the same value. */
    sum += g_memself_seed;
    g_memself_seed += 1;

    /* heap: two allocations must be valid and distinct; write+read back. */
    unsigned char *a = (unsigned char *)rv64_alloc(512);
    unsigned char *b = (unsigned char *)rv64_alloc(512);
    if (a && b && a != b) {
        for (i = 0; i < 512; i++) { a[i] = (unsigned char)i; b[i] = (unsigned char)(255 - i); }
        for (i = 0; i < 512; i++) { sum += a[i] + b[i]; }
        sum += 1;  /* heap-OK marker */
    }

    rv64_i64toa(out, (long long)sum);
    return out;
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
    long long y = rv64_atoi64(fargs[1]);
    if (y == 0) y = 1;
    long long x = rv64_atoi64(fargs[0]);
    /* Floor-mod, matching fun_mod's i64Mod (result takes the divisor's sign).
     * Plain x%y is truncate-remainder (sign of dividend) — that is remainder(),
     * NOT mod(), and it diverged from the interpreter for negative operands
     * (#828).  Also guards INT64_MIN % -1 (UB that traps on x86). */
    long long r;
    const long long I64MIN = (-9223372036854775807LL - 1);
    if (y < 0) {
        if (x <= 0) {
            r = (x == I64MIN && y == -1) ? 0 : x % y;
        } else {
            r = ((x - 1) % y) + y + 1;
        }
    } else {
        if (x < 0) {
            r = ((x + 1) % y) + y - 1;
        } else {
            r = x % y;
        }
    }
    rv64_i64toa(out, r);
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
        /* Float path: match fun_add — AddDoubles (|x|-sorted, error-compensated,
         * NearestPretty).  A raw sequential `sum +=` diverged from the
         * interpreter on cancellation (add(1e20,1,-1e20)=0 vs 1) AND on ordinary
         * decimals because it skipped NearestPretty (add(0.1,0.2)=
         * 0.30000000000000004 vs 0.3).  #829.  vals is a STACK array, not the
         * shared RV64_DSCRATCH_ADDR — rv64_strtod stages its argument there, so
         * a scratch-backed vals would be clobbered between iterations. */
        double vals[MAX_ARG_BLOB];
        int nv = nfargs > MAX_ARG_BLOB ? MAX_ARG_BLOB : nfargs;
        for (int i = 0; i < nv; i++) {
            vals[i] = rv64_strtod(fargs[i]);
        }
        double sum = rv64_add_doubles(vals, nv);
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
        /* Float path: match fun_sub — AddDoubles([a, -b]) (error-compensated +
         * NearestPretty), not a raw a-b which diverged like add (#829). */
        double vals[2];
        vals[0] = rv64_strtod(fargs[0]);
        vals[1] = -rv64_strtod(fargs[1]);
        double r = rv64_add_doubles(vals, 2);
        int n = rv64_fval(out, r);
        out[n] = '\0';
    }
    return out;
}
