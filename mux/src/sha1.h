#ifndef SHA1_H
#define SHA1_H

typedef struct
{
    UINT64   nTotal;
    UINT32   H[5];
    UINT8    block[64];
    size_t   nblock;
} SHA1_CONTEXT;

void SHA1_Init(SHA1_CONTEXT *p);
void SHA1_Compute(SHA1_CONTEXT *p, size_t n, const char *buf);
void SHA1_Final(SHA1_CONTEXT *p);

#endif // SHA1_H