/*! \file alloc.h
 * \brief External definitions for memory allocation subsystem.
 *
 */

#ifndef ALLOC_H
#define ALLOC_H

constexpr int POOL_LBUF    = 0;
constexpr int POOL_SBUF    = 1;
constexpr int POOL_MBUF    = 2;
constexpr int POOL_BOOL    = 3;
constexpr int POOL_DESC    = 4;
constexpr int POOL_QENTRY  = 5;
constexpr int POOL_PCACHE  = 6;
constexpr int POOL_LBUFREF = 7;
constexpr int POOL_REGREF  = 8;
constexpr int NUM_POOLS    = 9;

#define LBUF_SIZE   8000    // Large (must remain #define for preprocessor conditionals)
constexpr int GBUF_SIZE   = 1024;    // Generic
constexpr int MBUF_SIZE   = 400;     // Medium
constexpr int PBUF_SIZE   = 128;     // Pathname
constexpr int SBUF_SIZE   = 64;      // Small

extern LIBMUX_API bool g_paranoid_alloc;
LIBMUX_API void pool_init(int, int);
LIBMUX_API UTF8* pool_alloc(int poolnum, const UTF8* tag, const UTF8* file, const int line);
LIBMUX_API UTF8* pool_alloc_lbuf(const UTF8* tag, const UTF8* file, const int line);
LIBMUX_API void pool_free(int poolnum, UTF8* buf, const UTF8* file, const int line);
LIBMUX_API void pool_free_lbuf(UTF8* buf, const UTF8* file, const int line);
// alloc_notify_fn — callback for @list buffers output.
// Engine sets this to a function that calls notify().
//
typedef void (*ALLOC_NOTIFY_FN)(dbref player, const UTF8 *text);
extern LIBMUX_API ALLOC_NOTIFY_FN g_alloc_notify_fn;

LIBMUX_API void list_bufstats(dbref);
LIBMUX_API void list_buftrace(dbref);
LIBMUX_API void pool_reset(void);

#define alloc_lbuf(s)    pool_alloc_lbuf(T(s), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__)
#define free_lbuf(b)     pool_free_lbuf(reinterpret_cast<UTF8 *>(b), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__)
#define alloc_mbuf(s)    pool_alloc(POOL_MBUF, T(s), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__)
#define free_mbuf(b)     pool_free(POOL_MBUF, reinterpret_cast<UTF8 *>(b), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__)
#define alloc_sbuf(s)    pool_alloc(POOL_SBUF, T(s), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__)
#define free_sbuf(b)     pool_free(POOL_SBUF, reinterpret_cast<UTF8 *>(b), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__)
#define alloc_bool(s)    reinterpret_cast<struct boolexp *>(pool_alloc(POOL_BOOL, T(s), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__))
#define free_bool(b)     pool_free(POOL_BOOL, reinterpret_cast<UTF8 *>(b), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__)
#define alloc_qentry(s)  reinterpret_cast<BQUE *>(pool_alloc(POOL_QENTRY, T(s), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__))
#define free_qentry(b)   pool_free(POOL_QENTRY, reinterpret_cast<UTF8 *>(b), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__)
#define alloc_pcache(s)  reinterpret_cast<PCACHE *>(pool_alloc(POOL_PCACHE, T(s), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__))
#define free_pcache(b)   pool_free(POOL_PCACHE, reinterpret_cast<UTF8 *>(b), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__)
#define alloc_lbufref(s) reinterpret_cast<lbuf_ref *>(pool_alloc(POOL_LBUFREF, T(s), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__))
#define free_lbufref(b)  pool_free(POOL_LBUFREF, reinterpret_cast<UTF8 *>(b), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__)
#define alloc_regref(s)  reinterpret_cast<reg_ref *>(pool_alloc(POOL_REGREF, T(s), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__))
#define free_regref(b)   pool_free(POOL_REGREF, reinterpret_cast<UTF8 *>(b), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__)

#define safe_copy_chr_ascii(src, buff, bufp, nSizeOfBuffer) \
{ \
    if (static_cast<size_t>(*bufp - buff) < nSizeOfBuffer) \
    { \
        **bufp = src; \
        (*bufp)++; \
    } \
}

#define safe_str(s,b,p)           safe_copy_str_lbuf(s,b,p)
#define safe_bool(c,b,p)          safe_chr(((c) ? '1' : '0'),b,p)
#define safe_sb_str(s,b,p)        safe_copy_str(s,b,p,(SBUF_SIZE-1))
#define safe_mb_str(s,b,p)        safe_copy_str(s,b,p,(MBUF_SIZE-1))

#define safe_chr_ascii(c,b,p)     safe_copy_chr_ascii(static_cast<UTF8>(c),b,p,(LBUF_SIZE-1))
#define safe_sb_chr_ascii(c,b,p)  safe_copy_chr_ascii(c,b,p,(SBUF_SIZE-1))
#define safe_mb_chr_ascii(c,b,p)  safe_copy_chr_ascii(c,b,p,(MBUF_SIZE-1))

// Slowly transition from safe_chr to safe_chr_ascii and safe_chr_utf8,
// safe_sb_chr to safe_sb_chr_ascii,and safe_mb_chr to safe_mb_chr_ascii.
//
#define safe_sb_chr safe_sb_chr_ascii
#define safe_mb_chr safe_mb_chr_ascii
#define safe_chr safe_chr_ascii

//! \struct lbuf_ref
// Tracks references to an lbuf. See LBUF_SIZE.
struct lbuf_ref
{
    int      refcount;
    UTF8    *lbuf_ptr;
};

//! \struct reg_ref
// Tracks references to a register.
struct reg_ref
{
    int      refcount;
    lbuf_ref *lbuf;
    size_t   reg_len;
    UTF8    *reg_ptr;
};

#endif //!ALLOC_H
