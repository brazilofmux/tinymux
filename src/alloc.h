// alloc.h -- External definitions for memory allocation subsystem.
//
// $Id: alloc.h,v 1.5 2001-11-24 19:19:13 sdennis Exp $
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


#define LBUF_SIZE   8000    // Large
#define GBUF_SIZE   1024    // Generic
#define MBUF_SIZE   400     // Medium
#define PBUF_SIZE   128     // Pathname
#define SBUF_SIZE   64      // Small

/*
#define LBUF_SIZE   4000
#define MBUF_SIZE   200
#define SBUF_SIZE   32
*/

#ifdef STANDALONE

#define alloc_lbuf(s)   (char *)malloc(LBUF_SIZE)
#define free_lbuf(b)    if (b) free(b)
#define alloc_mbuf(s)   (char *)malloc(MBUF_SIZE)
#define free_mbuf(b)    if (b) free(b)
#define alloc_sbuf(s)   (char *)malloc(SBUF_SIZE)
#define free_sbuf(b)    if (b) free(b)
#define alloc_bool(s)   (struct boolexp *)malloc(sizeof(struct boolexp))
#define free_bool(b)    if (b) free(b)
#define alloc_qentry(s) (BQUE *)malloc(sizeof(BQUE))
#define free_qentry(b)  if (b) free(b)
#define alloc_pcache(s) (PCACHE *)malloc(sizeof(PCACHE)
#define free_pcache(b)  if (b) free(b)

#else // STANDALONE

extern void FDECL(pool_init, (int, int));
extern char *pool_alloc(int, const char *);
extern char *pool_alloc_lbuf(const char *);
extern void pool_free(int, char *);
extern void pool_free_lbuf(char *);
extern void FDECL(list_bufstats, (dbref));
extern void FDECL(list_buftrace, (dbref));

#define alloc_lbuf(s)   pool_alloc_lbuf(s)
#define free_lbuf(b)    pool_free_lbuf((char *)(b))
#define alloc_mbuf(s)   pool_alloc(POOL_MBUF,s)
#define free_mbuf(b)    pool_free(POOL_MBUF,(char *)(b))
#define alloc_sbuf(s)   pool_alloc(POOL_SBUF,s)
#define free_sbuf(b)    pool_free(POOL_SBUF,(char *)(b))
#define alloc_bool(s)   (struct boolexp *)pool_alloc(POOL_BOOL,s)
#define free_bool(b)    pool_free(POOL_BOOL,(char *)(b))
#define alloc_qentry(s) (BQUE *)pool_alloc(POOL_QENTRY,s)
#define free_qentry(b)  pool_free(POOL_QENTRY,(char *)(b))
#define alloc_pcache(s) (PCACHE *)pool_alloc(POOL_PCACHE,s)
#define free_pcache(b)  pool_free(POOL_PCACHE,(char *)(b))

#endif // STANDALONE

#define safe_copy_chr(src, buff, bufp, nSizeOfBuffer) \
{ \
    if ((*bufp - buff) < nSizeOfBuffer) \
    { \
        **bufp = src; \
        (*bufp)++; \
    } \
}

#define safe_str(s,b,p)     safe_copy_str_lbuf(s,b,p)
#define safe_chr(c,b,p)     safe_copy_chr(c,b,p,(LBUF_SIZE-1))
#define safe_sb_str(s,b,p)  safe_copy_str(s,b,p,(SBUF_SIZE-1))
#define safe_sb_chr(c,b,p)  safe_copy_chr(c,b,p,(SBUF_SIZE-1))
#define safe_mb_str(s,b,p)  safe_copy_str(s,b,p,(MBUF_SIZE-1))
#define safe_mb_chr(c,b,p)  safe_copy_chr(c,b,p,(MBUF_SIZE-1))

#endif // M_ALLOC_H
