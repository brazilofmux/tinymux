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
size_t co_lpos(unsigned char *out, const unsigned char *haystack, size_t hlen,
               unsigned char pattern);

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

char *co_rest_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    unsigned char delim = get_delim(fargs, nfargs, 1);
    size_t n = co_rest((unsigned char *)out,
                       (const unsigned char *)fargs[0],
                       rv64_slen(fargs[0]), delim);
    out[n] = '\0';
    return out;
}

char *co_last_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    unsigned char delim = get_delim(fargs, nfargs, 1);
    size_t n = co_last((unsigned char *)out,
                       (const unsigned char *)fargs[0],
                       rv64_slen(fargs[0]), delim);
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

char *co_sort_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1) { out[0] = '\0'; return out; }
    unsigned char delim = get_delim(fargs, nfargs, 1);
    unsigned char osep = get_osep(fargs, nfargs, 2, delim);
    char sort_type = 'a';  /* alphabetic default */
    if (nfargs >= 4 && fargs[3][0] != '\0')
        sort_type = fargs[3][0];
    size_t n = co_sort_words((unsigned char *)out,
                             (const unsigned char *)fargs[0],
                             rv64_slen(fargs[0]),
                             delim, osep, sort_type);
    out[n] = '\0';
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

char *co_replace_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) {
        if (nfargs >= 1) rv64_scopy(out, fargs[0]);
        else out[0] = '\0';
        return out;
    }
    int pos = satoi(fargs[1]);
    unsigned char delim = get_delim(fargs, nfargs, 3);
    unsigned char osep = get_osep(fargs, nfargs, 4, delim);
    if (pos < 1) { rv64_scopy(out, fargs[0]); return out; }
    /* replace = splice with count=1 */
    size_t n = co_splice((unsigned char *)out,
                         (const unsigned char *)fargs[0],
                         rv64_slen(fargs[0]),
                         (size_t)pos, 1,
                         (const unsigned char *)fargs[2],
                         rv64_slen(fargs[2]),
                         delim, osep);
    out[n] = '\0';
    return out;
}

char *co_insert_wrap(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) {
        if (nfargs >= 1) rv64_scopy(out, fargs[0]);
        else out[0] = '\0';
        return out;
    }
    int pos = satoi(fargs[1]);
    unsigned char delim = get_delim(fargs, nfargs, 3);
    unsigned char osep = get_osep(fargs, nfargs, 4, delim);
    if (pos < 1) pos = 1;
    size_t n = co_insert_word((unsigned char *)out,
                              (const unsigned char *)fargs[0],
                              rv64_slen(fargs[0]),
                              (size_t)pos,
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
 * Helper: inline itoa, writes decimal to buf, returns length.
 * --------------------------------------------------------------- */

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

char *rv64_strlen(char *out, const char **fargs, int nfargs) {
    int len = 0;
    if (nfargs >= 1) {
        len = rv64_slen(fargs[0]);
    }
    sitoa(out, len);
    return out;
}

/* ---------------------------------------------------------------
 * strcat(s1, s2, ..., sN) — concatenate without separators.
 *
 * Equivalent to MUX strcat(): join all arguments directly.
 * --------------------------------------------------------------- */

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

/* ---------------------------------------------------------------
 * extract(list, pos, count, delim) — extract elements from list.
 *
 * Equivalent to MUX extract(): 1-based position, returns count
 * elements separated by the delimiter.  If pos or count is out
 * of range, returns empty string.  Delimiter defaults to space
 * if empty.
 * --------------------------------------------------------------- */

char *rv64_extract(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) {
        out[0] = '\0';
        return out;
    }

    const char *list = fargs[0];
    int pos = satoi(fargs[1]);    /* 1-based */
    int count = satoi(fargs[2]);
    char delim = ' ';
    if (nfargs >= 4 && fargs[3][0] != '\0') {
        delim = fargs[3][0];
    }

    if (pos < 1 || count < 1) {
        out[0] = '\0';
        return out;
    }

    /* Skip to element at position pos (1-based). */
    const char *p = list;
    int cur = 1;
    while (cur < pos && *p) {
        if (*p == delim) cur++;
        p++;
    }
    if (cur < pos || *p == '\0') {
        /* If we ran past delimiters but landed exactly at pos,
         * check if we're at a trailing delimiter. */
        if (cur < pos) {
            out[0] = '\0';
            return out;
        }
    }

    /* Copy 'count' elements. */
    char *op = out;
    int copied = 0;
    while (copied < count && *p) {
        if (*p == delim) {
            copied++;
            if (copied < count) {
                *op++ = delim;
            }
        } else {
            *op++ = *p;
        }
        p++;
    }
    *op = '\0';
    return out;
}

/* ---------------------------------------------------------------
 * words(list, delim) — count words in a list.
 *
 * Equivalent to MUX words(): count delimiter-separated tokens.
 * Delimiter defaults to space if empty or missing.  Leading
 * delimiters are skipped (matching MUX trim_space_sep behavior).
 * --------------------------------------------------------------- */

char *rv64_words(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') {
        out[0] = '0';
        out[1] = '\0';
        return out;
    }

    char delim = ' ';
    if (nfargs >= 2 && fargs[1][0] != '\0') {
        delim = fargs[1][0];
    }

    const char *p = fargs[0];

    /* Skip leading delimiters (trim_space_sep). */
    while (*p == delim) p++;
    if (*p == '\0') {
        out[0] = '0';
        out[1] = '\0';
        return out;
    }

    int n = 1;
    if (delim == ' ') {
        /* Space delimiter: skip runs of consecutive spaces. */
        while (*p) {
            if (*p == ' ') {
                n++;
                while (p[1] == ' ') p++;
            }
            p++;
        }
    } else {
        /* Non-space delimiter: each delimiter counts. */
        while (*p) {
            if (*p == delim) n++;
            p++;
        }
    }

    sitoa(out, n);
    return out;
}

/* ---------------------------------------------------------------
 * split_token(list, offset_str, delim, cursor_out) — extract one token.
 *
 * Splits the list starting at byte offset (given as decimal string).
 * Writes the extracted token to out.  Writes the new byte offset
 * (past the delimiter) as a decimal string to fargs[3] (cursor_out).
 *
 * For space delimiters, skips runs of consecutive spaces (matching
 * MUX trim_space_sep / next_token behavior).
 *
 * Returns: out (token).
 * --------------------------------------------------------------- */

char *rv64_split_token(char *out, const char **fargs, int nfargs) {
    if (nfargs < 4) {
        out[0] = '\0';
        return out;
    }

    const char *list = fargs[0];
    int offset = satoi(fargs[1]);
    char delim = ' ';
    if (fargs[2][0] != '\0') {
        delim = fargs[2][0];
    }

    const char *p = list + offset;

    /* Skip leading delimiters for space (matching trim_space_sep). */
    if (delim == ' ') {
        while (*p == ' ') p++;
    }

    /* Copy token until delimiter or end of string. */
    char *op = out;
    while (*p && *p != delim) {
        *op++ = *p++;
    }
    *op = '\0';

    /* Advance past delimiter. */
    if (*p == delim) {
        p++;
        /* For space, skip consecutive delimiters. */
        if (delim == ' ') {
            while (*p == ' ') p++;
        }
    }
    int new_offset = (int)(p - list);

    /* Write new cursor offset to fargs[3] (writable output buffer). */
    sitoa((char *)fargs[3], new_offset);
    return out;
}

/* ---------------------------------------------------------------
 * first(list, delim) — return first element of a list.
 *
 * Equivalent to MUX first(): skip leading delimiters (for space),
 * copy characters until the next delimiter or end of string.
 * Delimiter defaults to space.
 * --------------------------------------------------------------- */

char *rv64_first(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') {
        out[0] = '\0';
        return out;
    }

    char delim = ' ';
    if (nfargs >= 2 && fargs[1][0] != '\0') {
        delim = fargs[1][0];
    }

    const char *p = fargs[0];

    /* Skip leading delimiters (trim_space_sep for space). */
    if (delim == ' ') {
        while (*p == ' ') p++;
    }

    /* Copy until delimiter or end. */
    char *op = out;
    while (*p && *p != delim) {
        *op++ = *p++;
    }
    *op = '\0';
    return out;
}

/* ---------------------------------------------------------------
 * rest(list, delim) — return everything after the first element.
 *
 * Equivalent to MUX rest(): skip first token, return remainder.
 * Delimiter defaults to space.
 * --------------------------------------------------------------- */

char *rv64_rest(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') {
        out[0] = '\0';
        return out;
    }

    char delim = ' ';
    if (nfargs >= 2 && fargs[1][0] != '\0') {
        delim = fargs[1][0];
    }

    const char *p = fargs[0];

    /* Skip leading delimiters (trim_space_sep for space). */
    if (delim == ' ') {
        while (*p == ' ') p++;
    }

    /* Skip past first token. */
    while (*p && *p != delim) p++;

    /* Skip the delimiter (and runs of space). */
    if (*p == delim) {
        p++;
        if (delim == ' ') {
            while (*p == ' ') p++;
        }
    }

    /* Copy remainder. */
    rv64_scopy(out, p);
    return out;
}

/* ---------------------------------------------------------------
 * last(list, delim) — return last element of a list.
 *
 * Equivalent to MUX last(): scan to end, then back up to find
 * the start of the last token.  Delimiter defaults to space.
 * --------------------------------------------------------------- */

char *rv64_last(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') {
        out[0] = '\0';
        return out;
    }

    char delim = ' ';
    if (nfargs >= 2 && fargs[1][0] != '\0') {
        delim = fargs[1][0];
    }

    const char *s = fargs[0];

    /* Skip leading delimiters (trim_space_sep for space). */
    if (delim == ' ') {
        while (*s == ' ') s++;
    }

    /* Find the last delimiter. */
    const char *last_start = s;
    const char *p = s;
    while (*p) {
        if (*p == delim) {
            const char *q = p + 1;
            if (delim == ' ') {
                while (*q == ' ') q++;
            }
            if (*q) {
                last_start = q;
            }
            p = q;
        } else {
            p++;
        }
    }

    rv64_scopy(out, last_start);
    return out;
}

/* ---------------------------------------------------------------
 * Helper: inline strcmp (no libc dependency).
 * Returns 0 if equal, nonzero otherwise.
 * --------------------------------------------------------------- */

static int scmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

/* ---------------------------------------------------------------
 * member(list, word, delim) — find 1-based position of word in list.
 *
 * Equivalent to MUX member(): returns position (1-based) or 0.
 * Delimiter defaults to space.  Comparison is case-sensitive.
 * --------------------------------------------------------------- */

char *rv64_member(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) {
        out[0] = '0';
        out[1] = '\0';
        return out;
    }

    const char *word = fargs[1];
    char delim = ' ';
    if (nfargs >= 3 && fargs[2][0] != '\0') {
        delim = fargs[2][0];
    }

    const char *p = fargs[0];

    /* Skip leading delimiters (trim_space_sep for space). */
    if (delim == ' ') {
        while (*p == ' ') p++;
    }

    if (*p == '\0') {
        out[0] = '0';
        out[1] = '\0';
        return out;
    }

    /* Scan tokens, compare each with word. */
    int pos = 1;
    while (*p) {
        /* Find end of current token. */
        const char *tok = p;
        while (*p && *p != delim) p++;

        /* Compare: check length and content. */
        int tok_len = (int)(p - tok);
        int word_len = rv64_slen(word);
        if (tok_len == word_len) {
            int match = 1;
            for (int i = 0; i < tok_len; i++) {
                if (tok[i] != word[i]) { match = 0; break; }
            }
            if (match) {
                sitoa(out, pos);
                return out;
            }
        }

        /* Advance past delimiter. */
        if (*p == delim) {
            p++;
            if (delim == ' ') {
                while (*p == ' ') p++;
            }
        }
        pos++;
    }

    out[0] = '0';
    out[1] = '\0';
    return out;
}

/* ---------------------------------------------------------------
 * repeat(str, count) — repeat a string N times.
 *
 * Equivalent to MUX repeat(): concatenate str with itself count
 * times.  Returns empty if count <= 0.
 * --------------------------------------------------------------- */

char *rv64_repeat(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) {
        out[0] = '\0';
        return out;
    }

    int count = satoi(fargs[1]);
    if (count <= 0 || fargs[0][0] == '\0') {
        out[0] = '\0';
        return out;
    }

    /* Simple byte-counting guard (LBUF = 8000). */
    int len = rv64_slen(fargs[0]);
    char *p = out;
    for (int i = 0; i < count; i++) {
        if ((int)(p - out) + len >= 7999) break;
        p = rv64_scopy(p, fargs[0]);
    }
    return out;
}

/* ---------------------------------------------------------------
 * trim(str, side, char) — strip leading/trailing characters.
 *
 * Equivalent to MUX trim(): side is 'l'/'L' (left only),
 * 'r'/'R' (right only), or anything else (both).
 * Trim character defaults to space.
 * --------------------------------------------------------------- */

char *rv64_trim(char *out, const char **fargs, int nfargs) {
    if (nfargs < 1 || fargs[0][0] == '\0') {
        out[0] = '\0';
        return out;
    }

    char tc = ' ';
    if (nfargs >= 3 && fargs[2][0] != '\0') {
        tc = fargs[2][0];
    }

    char side = 'b';  /* both */
    if (nfargs >= 2 && fargs[1][0] != '\0') {
        char s = fargs[1][0];
        if (s == 'l' || s == 'L') side = 'l';
        else if (s == 'r' || s == 'R') side = 'r';
    }

    const char *start = fargs[0];
    int len = rv64_slen(start);
    const char *end = start + len;

    /* Trim left. */
    if (side == 'l' || side == 'b') {
        while (*start == tc) start++;
    }

    /* Trim right. */
    if (side == 'r' || side == 'b') {
        while (end > start && end[-1] == tc) end--;
    }

    /* Copy result. */
    char *p = out;
    while (start < end) {
        *p++ = *start++;
    }
    *p = '\0';
    return out;
}

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

/* ---------------------------------------------------------------
 * ldelete(list, pos, delim) — delete element at position from list.
 *
 * Equivalent to MUX ldelete(): 1-based position, single position
 * only (no ranges or negative indices).  Returns the list with
 * the element removed.  Delimiter defaults to space.
 * --------------------------------------------------------------- */

char *rv64_ldelete(char *out, const char **fargs, int nfargs) {
    if (nfargs < 2) {
        if (nfargs >= 1) {
            rv64_scopy(out, fargs[0]);
        } else {
            out[0] = '\0';
        }
        return out;
    }

    int pos = satoi(fargs[1]);
    char delim = ' ';
    if (nfargs >= 3 && fargs[2][0] != '\0') {
        delim = fargs[2][0];
    }

    const char *p = fargs[0];

    /* Skip leading delimiters for space. */
    if (delim == ' ') {
        while (*p == ' ') p++;
    }

    if (pos < 1 || *p == '\0') {
        rv64_scopy(out, p);
        return out;
    }

    /* Walk to the target element. */
    char *op = out;
    int cur = 1;
    int need_sep = 0;

    while (*p) {
        /* Find end of current token. */
        const char *tok = p;
        while (*p && *p != delim) p++;

        if (cur != pos) {
            /* Keep this element. */
            if (need_sep) *op++ = delim;
            while (tok < p) *op++ = *tok++;
            need_sep = 1;
        }
        /* else: skip the element at pos. */

        /* Advance past delimiter. */
        if (*p == delim) {
            p++;
            if (delim == ' ') {
                while (*p == ' ') p++;
            }
        }
        cur++;
    }

    *op = '\0';
    return out;
}

/* ---------------------------------------------------------------
 * replace(list, pos, word, delim) — replace element at position.
 *
 * Equivalent to MUX replace(): 1-based position.  Replaces the
 * element at pos with word.  Delimiter defaults to space.
 * --------------------------------------------------------------- */

char *rv64_replace(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) {
        if (nfargs >= 1) {
            rv64_scopy(out, fargs[0]);
        } else {
            out[0] = '\0';
        }
        return out;
    }

    int pos = satoi(fargs[1]);
    const char *word = fargs[2];
    char delim = ' ';
    if (nfargs >= 4 && fargs[3][0] != '\0') {
        delim = fargs[3][0];
    }

    const char *p = fargs[0];

    /* Skip leading delimiters for space. */
    if (delim == ' ') {
        while (*p == ' ') p++;
    }

    if (pos < 1 || *p == '\0') {
        rv64_scopy(out, p);
        return out;
    }

    char *op = out;
    int cur = 1;
    int need_sep = 0;

    while (*p) {
        const char *tok = p;
        while (*p && *p != delim) p++;

        if (need_sep) *op++ = delim;

        if (cur == pos) {
            /* Replace with new word. */
            op = rv64_scopy(op, word);
        } else {
            /* Copy original token. */
            while (tok < p) *op++ = *tok++;
        }
        need_sep = 1;

        if (*p == delim) {
            p++;
            if (delim == ' ') {
                while (*p == ' ') p++;
            }
        }
        cur++;
    }

    *op = '\0';
    return out;
}

/* ---------------------------------------------------------------
 * insert(list, pos, word, delim) — insert element at position.
 *
 * Equivalent to MUX insert(): 1-based position.  Inserts word
 * before the element at pos.  If pos > words, appends.
 * Delimiter defaults to space.
 * --------------------------------------------------------------- */

char *rv64_insert(char *out, const char **fargs, int nfargs) {
    if (nfargs < 3) {
        if (nfargs >= 1) {
            rv64_scopy(out, fargs[0]);
        } else {
            out[0] = '\0';
        }
        return out;
    }

    int pos = satoi(fargs[1]);
    const char *word = fargs[2];
    char delim = ' ';
    if (nfargs >= 4 && fargs[3][0] != '\0') {
        delim = fargs[3][0];
    }

    const char *p = fargs[0];

    /* Skip leading delimiters for space. */
    if (delim == ' ') {
        while (*p == ' ') p++;
    }

    if (pos < 1) pos = 1;

    char *op = out;
    int cur = 1;
    int need_sep = 0;
    int inserted = 0;

    while (*p) {
        if (cur == pos && !inserted) {
            if (need_sep) *op++ = delim;
            op = rv64_scopy(op, word);
            need_sep = 1;
            inserted = 1;
        }

        const char *tok = p;
        while (*p && *p != delim) p++;

        if (need_sep) *op++ = delim;
        while (tok < p) *op++ = *tok++;
        need_sep = 1;

        if (*p == delim) {
            p++;
            if (delim == ' ') {
                while (*p == ' ') p++;
            }
        }
        cur++;
    }

    /* If pos was past end, append. */
    if (!inserted) {
        if (need_sep) *op++ = delim;
        op = rv64_scopy(op, word);
    }

    *op = '\0';
    return out;
}
