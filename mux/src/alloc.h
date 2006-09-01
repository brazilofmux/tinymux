// alloc.h -- External definitions for memory allocation subsystem.
//
// $Id: alloc.h,v 1.5 2006/01/07 07:25:27 sdennis Exp $
//

#ifndef M_ALLOC_H
#define M_ALLOC_H

#define POOL_LBUF   0
#define POOL_SBUF   1
#define POOL_MBUF   2
#define POOL_BOOL   3
#define POOL_DESC   4
#define POOL_QENTRY 5
#define POOL_PCACHE 6
#define NUM_POOLS   7

#ifdef FIRANMUX
#define LBUF_SIZE   16000   // Large
#else
#define LBUF_SIZE   8000    // Large
#endif
#define GBUF_SIZE   1024    // Generic
#define MBUF_SIZE   400     // Medium
#define PBUF_SIZE   128     // Pathname
#define SBUF_SIZE   64      // Small

extern void pool_init(int, int);
extern char *pool_alloc(int, const char *, const char *, int);
extern char *pool_alloc_lbuf(const char *, const char *, int);
extern void pool_free(int, char *, const char *, int);
extern void pool_free_lbuf(char *, const char *, int);
extern void list_bufstats(dbref);
extern void list_buftrace(dbref);
extern void pool_reset(void);

#define alloc_lbuf(s)   pool_alloc_lbuf(s, __FILE__, __LINE__)
#define free_lbuf(b)    pool_free_lbuf((char *)(b), __FILE__, __LINE__)
#define alloc_mbuf(s)   pool_alloc(POOL_MBUF,s, __FILE__, __LINE__)
#define free_mbuf(b)    pool_free(POOL_MBUF,(char *)(b), __FILE__, __LINE__)
#define alloc_sbuf(s)   pool_alloc(POOL_SBUF,s, __FILE__, __LINE__)
#define free_sbuf(b)    pool_free(POOL_SBUF,(char *)(b), __FILE__, __LINE__)
#define alloc_bool(s)   (struct boolexp *)pool_alloc(POOL_BOOL,s, __FILE__, __LINE__)
#define free_bool(b)    pool_free(POOL_BOOL,(char *)(b), __FILE__, __LINE__)
#define alloc_qentry(s) (BQUE *)pool_alloc(POOL_QENTRY,s, __FILE__, __LINE__)
#define free_qentry(b)  pool_free(POOL_QENTRY,(char *)(b), __FILE__, __LINE__)
#define alloc_pcache(s) (PCACHE *)pool_alloc(POOL_PCACHE,s, __FILE__, __LINE__)
#define free_pcache(b)  pool_free(POOL_PCACHE,(char *)(b), __FILE__, __LINE__)

#define safe_copy_chr(src, buff, bufp, nSizeOfBuffer) \
{ \
    if ((*bufp - buff) < nSizeOfBuffer) \
    { \
        **bufp = src; \
        (*bufp)++; \
    } \
}

#define safe_str(s,b,p)     safe_copy_str_lbuf(s,b,p)
#define safe_chr(c,b,p)     safe_copy_chr((unsigned char)(c),b,p,(LBUF_SIZE-1))
#define safe_bool(c,b,p)    safe_chr(((c) ? '1' : '0'),b,p)
#define safe_sb_str(s,b,p)  safe_copy_str(s,b,p,(SBUF_SIZE-1))
#define safe_sb_chr(c,b,p)  safe_copy_chr(c,b,p,(SBUF_SIZE-1))
#define safe_mb_str(s,b,p)  safe_copy_str(s,b,p,(MBUF_SIZE-1))
#define safe_mb_chr(c,b,p)  safe_copy_chr(c,b,p,(MBUF_SIZE-1))

#endif // M_ALLOC_H
