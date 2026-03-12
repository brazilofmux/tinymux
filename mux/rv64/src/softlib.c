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

/* ---------------------------------------------------------------
 * Helper: inline strlen (no libc dependency).
 * Once intrinsics are wired up, this becomes a trampoline call.
 * --------------------------------------------------------------- */

static int slen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* ---------------------------------------------------------------
 * Helper: inline string copy, returns pointer past NUL.
 * --------------------------------------------------------------- */

static char *scopy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
    return dst;
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
        p = scopy(p, fargs[i]);
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
        len = slen(fargs[0]);
    }

    /* itoa: write decimal digits. */
    if (len == 0) {
        out[0] = '0';
        out[1] = '\0';
        return out;
    }

    char buf[20];
    int pos = 0;
    int val = len;
    while (val > 0) {
        buf[pos++] = '0' + (val % 10);
        val /= 10;
    }
    /* Reverse into output. */
    for (int i = 0; i < pos; i++) {
        out[i] = buf[pos - 1 - i];
    }
    out[pos] = '\0';
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
        p = scopy(p, fargs[i]);
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
