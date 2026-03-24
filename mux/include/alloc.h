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
constexpr int NUM_POOLS    = 7;

#define LBUF_SIZE   32768   // Large (must remain #define for preprocessor conditionals)
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

// ---------------------------------------------------------------
// RAII wrapper for pool-allocated LBUF buffers.
//
// Replaces stack-allocated UTF8 buf[LBUF_SIZE] with a heap-backed
// pool buffer that is automatically freed when the scope exits.
// This removes LBUF_SIZE from the stack frame, making it safe to
// increase LBUF_SIZE without risking stack overflow in recursive
// evaluation paths.
//
// Usage:
//   LBuf tmp(T("my_func"));
//   UTF8 *bp = tmp;
//   safe_str(text, tmp, &bp);
//   *bp = '\0';
//
// The LBuf_Src macro captures __FILE__/__LINE__ for pool tracking:
//   LBuf_Src tmp("my_func");
//
class LBuf {
    UTF8 *m_buf;
    const UTF8 *m_file;
    int m_line;
public:
    LBuf(const UTF8 *tag, const UTF8 *file, int line)
        : m_buf(pool_alloc_lbuf(tag, file, line)),
          m_file(file), m_line(line) {}

    ~LBuf() { pool_free_lbuf(m_buf, m_file, m_line); }

    LBuf(const LBuf &) = delete;
    LBuf &operator=(const LBuf &) = delete;

    UTF8 *get() { return m_buf; }
    const UTF8 *get() const { return m_buf; }
    operator UTF8 *() { return m_buf; }
    operator const UTF8 *() const { return m_buf; }
    UTF8 &operator[](size_t i) { return m_buf[i]; }
    const UTF8 &operator[](size_t i) const { return m_buf[i]; }
};

#define LBuf_Src(tag) LBuf(T(tag), reinterpret_cast<const UTF8 *>(__FILE__), __LINE__)

#include <memory>

//! \struct RegBuffer
// Shared buffer that packs multiple register values into a single
// LBUF_SIZE block.  Reference-counted via std::shared_ptr.
struct RegBuffer
{
    UTF8   data[LBUF_SIZE];
    size_t used;

    RegBuffer() : used(0) { memset(data, 0, sizeof(data)); }
};

//! \struct reg_ref
// A register value — a view into a shared RegBuffer.
// Allocated with new/delete (not pool-allocated).
// The refcount tracks how many pointers reference this reg_ref.
// The shared_ptr<RegBuffer> automatically manages the underlying buffer.
struct reg_ref
{
    int      refcount;
    std::shared_ptr<RegBuffer> buf;
    size_t   reg_len;
    UTF8    *reg_ptr;

    reg_ref() : refcount(0), reg_len(0), reg_ptr(nullptr) {}
};

#endif //!ALLOC_H
