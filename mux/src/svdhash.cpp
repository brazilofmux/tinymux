// svdhash.cpp -- CHashPage, CHashFile, CHashTable modules.
//
// $Id: svdhash.cpp,v 1.41 2005-11-12 08:27:50 rmg Exp $
//
// MUX 2.4
// Copyright (C) 1998 through 2004 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#define DO_COMMIT

int cs_writes   = 0;    // total writes
int cs_reads    = 0;    // total reads
int cs_dels     = 0;    // total deletes
int cs_fails    = 0;    // attempts to grab nonexistent
int cs_syncs    = 0;    // total cache syncs
int cs_dbreads  = 0;    // total read-throughs
int cs_dbwrites = 0;    // total write-throughs
int cs_whits    = 0;    // writes into cached pages
int cs_rhits    = 0;    // read from cached pages

static const UINT32 CRC32_Table[256] =
{
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
    0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
    0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
    0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
    0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
    0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
    0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
    0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
    0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
    0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
    0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
    0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
    0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
    0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
    0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
    0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
    0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
    0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
    0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
    0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
    0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
    0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

// Portable CRC-32 routine. These slower routines are less compiler and
// platform dependent and still get the job done.
//
UINT32 CRC32_ProcessBuffer
(
    UINT32         ulCrc,
    const void    *arg_pBuffer,
    unsigned int   nBuffer
)
{
    UINT8 *pBuffer = (UINT8 *)arg_pBuffer;

    ulCrc = ~ulCrc;
    while (nBuffer--)
    {
        ulCrc  = CRC32_Table[((UINT8)*pBuffer++) ^ (UINT8)ulCrc] ^ (ulCrc >> 8);
    }
    return ~ulCrc;
}

UINT32 CRC32_ProcessInteger(UINT32 nInteger)
{
    UINT32 ulCrc;
    ulCrc  = ~nInteger;
    ulCrc  = CRC32_Table[(UINT8)ulCrc] ^ (ulCrc >> 8);
    ulCrc  = CRC32_Table[(UINT8)ulCrc] ^ (ulCrc >> 8);
    ulCrc  = CRC32_Table[(UINT8)ulCrc] ^ (ulCrc >> 8);
    ulCrc  = CRC32_Table[(UINT8)ulCrc] ^ (ulCrc >> 8);
    return ~ulCrc;
}

UINT32 CRC32_ProcessInteger2(UINT32 nInteger1, UINT32 nInteger2)
{
    UINT32 ulCrc;
    ulCrc  = ~nInteger1;
    ulCrc  = CRC32_Table[(UINT8)ulCrc] ^ (ulCrc >> 8);
    ulCrc  = CRC32_Table[(UINT8)ulCrc] ^ (ulCrc >> 8);
    ulCrc  = CRC32_Table[(UINT8)ulCrc] ^ (ulCrc >> 8);
    ulCrc  = CRC32_Table[(UINT8)ulCrc] ^ (ulCrc >> 8);
    ulCrc ^= nInteger2;
    ulCrc  = CRC32_Table[(UINT8)ulCrc] ^ (ulCrc >> 8);
    ulCrc  = CRC32_Table[(UINT8)ulCrc] ^ (ulCrc >> 8);
    ulCrc  = CRC32_Table[(UINT8)ulCrc] ^ (ulCrc >> 8);
    ulCrc  = CRC32_Table[(UINT8)ulCrc] ^ (ulCrc >> 8);
    return ~ulCrc;
}

#define DO1(buf,i)  {s1 += buf[i]; s2 += s1;}
#define DO2(buf,i)  DO1(buf,i); DO1(buf,i+1);
#define DO4(buf,i)  DO2(buf,i); DO2(buf,i+2);
#define DO8(buf,i)  DO4(buf,i); DO4(buf,i+4);
#define DO16(buf)   DO8(buf,0); DO8(buf,8);

UINT32 HASH_ProcessBuffer
(
    UINT32       ulHash,
    const void  *arg_pBuffer,
    size_t       nBuffer
)
{
    UINT8 *pBuffer = (UINT8 *)arg_pBuffer;
    ulHash = ~ulHash;

    if (nBuffer <= 16)
    {
        pBuffer -= 16 - nBuffer;
        switch (nBuffer)
        {
        case 16: ulHash  = CRC32_Table[pBuffer[0] ^ (UINT8)ulHash] ^ (ulHash >> 8);
        case 15: ulHash  = CRC32_Table[pBuffer[1] ^ (UINT8)ulHash] ^ (ulHash >> 8);
        case 14: ulHash  = CRC32_Table[pBuffer[2] ^ (UINT8)ulHash] ^ (ulHash >> 8);
        case 13: ulHash  = CRC32_Table[pBuffer[3] ^ (UINT8)ulHash] ^ (ulHash >> 8);
        case 12: ulHash  = CRC32_Table[pBuffer[4] ^ (UINT8)ulHash] ^ (ulHash >> 8);
        case 11: ulHash  = CRC32_Table[pBuffer[5] ^ (UINT8)ulHash] ^ (ulHash >> 8);
        case 10: ulHash  = CRC32_Table[pBuffer[6] ^ (UINT8)ulHash] ^ (ulHash >> 8);
        case 9:  ulHash  = CRC32_Table[pBuffer[7] ^ (UINT8)ulHash] ^ (ulHash >> 8);
#if defined(UNALIGNED32) && defined(WORDS_LITTLEENDIAN)
        case 8:  ulHash ^= *(UINT32 *)(pBuffer + 8);
                 ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
                 ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
                 ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
                 ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
                 ulHash ^= *(UINT32 *)(pBuffer + 12);
                 ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
                 ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
                 ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
                 ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
                 return ~ulHash;
#else
        case 8:  ulHash  = CRC32_Table[pBuffer[8] ^ (UINT8)ulHash] ^ (ulHash >> 8);
#endif

        case 7:  ulHash  = CRC32_Table[pBuffer[9] ^ (UINT8)ulHash] ^ (ulHash >> 8);
        case 6:  ulHash  = CRC32_Table[pBuffer[10] ^ (UINT8)ulHash] ^ (ulHash >> 8);
        case 5:  ulHash  = CRC32_Table[pBuffer[11] ^ (UINT8)ulHash] ^ (ulHash >> 8);
#if defined(UNALIGNED32) && defined(WORDS_LITTLEENDIAN)
        case 4:  ulHash ^= *(UINT32 *)(pBuffer + 12);
                 ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
                 ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
                 ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
                 ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
                 return ~ulHash;
#else
        case 4:  ulHash  = CRC32_Table[pBuffer[12] ^ (UINT8)ulHash] ^ (ulHash >> 8);
#endif

        case 3:  ulHash  = CRC32_Table[pBuffer[13] ^ (UINT8)ulHash] ^ (ulHash >> 8);
        case 2:  ulHash  = CRC32_Table[pBuffer[14] ^ (UINT8)ulHash] ^ (ulHash >> 8);
        case 1:  ulHash  = CRC32_Table[pBuffer[15] ^ (UINT8)ulHash] ^ (ulHash >> 8);
        case 0:  return ~ulHash;
        }
    }

    size_t nSmall  = nBuffer & 15;
    size_t nMedium = (nBuffer >> 4) & 255;
    size_t nLarge  = nBuffer >> 12;

    UINT32 s1 = ulHash & 0xFFFF;
    UINT32 s2 = (ulHash >> 16) & 0xFFFF;

    while (nLarge--)
    {
        int k = 256;
        while (k)
        {
            DO16(pBuffer);
            pBuffer += 16;
            k--;
        }
        ulHash  = ~s1;
        ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
        ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
        ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
        ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
        ulHash ^= s2;
        ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
        ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
        ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
        ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
        ulHash = ~ulHash;
        s1 = ulHash & 0xFFFF;
        s2 = (ulHash >> 16) & 0xFFFF;
    }

    while (nMedium--)
    {
        DO16(pBuffer);
        pBuffer += 16;
    }

    pBuffer -= 15 - nSmall;
    switch (nSmall)
    {
    case 15: s1 += pBuffer[0];  s2 += s1;
    case 14: s1 += pBuffer[1];  s2 += s1;
    case 13: s1 += pBuffer[2];  s2 += s1;
    case 12: s1 += pBuffer[3];  s2 += s1;
    case 11: s1 += pBuffer[4];  s2 += s1;
    case 10: s1 += pBuffer[5];  s2 += s1;
    case 9:  s1 += pBuffer[6];  s2 += s1;
    case 8:  s1 += pBuffer[7];  s2 += s1;
    case 7:  s1 += pBuffer[8];  s2 += s1;
    case 6:  s1 += pBuffer[9];  s2 += s1;
    case 5:  s1 += pBuffer[10]; s2 += s1;
    case 4:  s1 += pBuffer[11]; s2 += s1;
    case 3:  s1 += pBuffer[12]; s2 += s1;
    case 2:  s1 += pBuffer[13]; s2 += s1;
    case 1:  s1 += pBuffer[14]; s2 += s1;
    case 0:  break;
    }

    ulHash  = ~s1;
    ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
    ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
    ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
    ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
    ulHash ^= s2;
    ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
    ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
    ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
    ulHash  = CRC32_Table[(UINT8)ulHash] ^ (ulHash >> 8);
    return ~ulHash;
}

#define NUMBER_OF_PRIMES 177
const int Primes[NUMBER_OF_PRIMES] =
{

    1, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67,
    71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151,
    157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239,
    241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317, 331, 337,
    347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401, 409, 419, 421, 431, 433,
    439, 443, 449, 457, 461, 463, 467, 479, 487, 491, 499, 503, 509, 521, 523, 541,
    547, 557, 563, 569, 571, 577, 587, 593, 599, 601, 607, 613, 617, 619, 631, 641,
    643, 647, 653, 659, 661, 673, 677, 683, 691, 701, 709, 719, 727, 733, 739, 743,
    751, 757, 761, 769, 773, 787, 797, 809, 811, 821, 823, 827, 829, 839, 853, 857,
    859, 863, 877, 881, 883, 887, 907, 911, 919, 929, 937, 941, 947, 953, 967, 971,
    977, 983, 991, 997, 1009, 1013, 1019, 1021, 1031, 1033, 1039, 0
};

void ChoosePrimes(int TableSize, HP_HEAPOFFSET HashPrimes[16])
{
    int LargestPrime = TableSize/2;
    if (LargestPrime > Primes[NUMBER_OF_PRIMES-2])
    {
        LargestPrime = Primes[NUMBER_OF_PRIMES-2];
    }
    int Spacing = LargestPrime/16;

    // Pick a set primes that are evenly spaced from (0 to LargestPrime)
    // We divide this interval into 16 equal sized zones. We want to find
    // one prime number that best represents that zone.
    //
    int iZone, iPrime;
    for (iZone = 1, iPrime = 0; iPrime < 16; iZone += Spacing)
    {
        // Search for a prime number that is less than the target zone
        // number given by iZone.
        //
        int Lower = Primes[0];
        for (int jPrime = 0; Primes[jPrime] != 0; jPrime++)
        {
            if (jPrime != 0 && TableSize % Primes[jPrime] == 0) continue;
            int Upper = Primes[jPrime];
            if (Lower <= iZone && iZone <= Upper)
            {
                // Choose the closest lower prime number.
                //
                if (iZone - Lower <= Upper - iZone)
                {
                    HashPrimes[iPrime++] = Lower;
                }
                else
                {
                    HashPrimes[iPrime++] = Upper;
                }
                break;
            }
            Lower = Upper;
        }
    }

    // Alternate negative and positive numbers
    //
    for (iPrime = 0; iPrime < 16; iPrime += 2)
    {
        HashPrimes[iPrime] = TableSize-HashPrimes[iPrime];
    }

    // Shuffle the set of primes to reduce correlation with bits in
    // hash key.
    //
    for (iPrime = 0; iPrime < 16-1; iPrime++)
    {
        int Pick = (int)RandomINT32(0, 15-iPrime);
        HP_HEAPOFFSET Temp = HashPrimes[Pick];
        HashPrimes[Pick] = HashPrimes[15-iPrime];
        HashPrimes[15-iPrime] = Temp;
    }
}

static const UINT32 anGroupMask[33] =
{
    0x00000000U,
    0x80000000U, 0xC0000000U, 0xE0000000U, 0xF0000000U,
    0xF8000000U, 0xFC000000U, 0xFE000000U, 0xFF000000U,
    0xFF800000U, 0xFFC00000U, 0xFFE00000U, 0xFFF00000U,
    0xFFF80000U, 0xFFFC0000U, 0xFFFE0000U, 0xFFFF0000U,
    0xFFFF8000U, 0xFFFFC000U, 0xFFFFE000U, 0xFFFFF000U,
    0xFFFFF800U, 0xFFFFFC00U, 0xFFFFFE00U, 0xFFFFFF00U,
    0xFFFFFF80U, 0xFFFFFFC0U, 0xFFFFFFE0U, 0xFFFFFFF0U,
    0xFFFFFFF8U, 0xFFFFFFFCU, 0xFFFFFFFEU, 0xFFFFFFFFU
};

bool CHashPage::Allocate(unsigned int nPageSize)
{
    if (m_nPageSize) return false;

    m_nPageSize = nPageSize;
    m_pPage = new unsigned char[nPageSize];
    if (m_pPage)
    {
        return true;
    }
    return false;
}

CHashPage::CHashPage(void)
{
    m_nPageSize = 0;
    m_pPage = 0;
}

CHashPage::~CHashPage(void)
{
    if (m_pPage)
    {
        delete m_pPage;
        m_pPage = 0;
    }
}

// GetStats
//
// This functions returns the number of records in this hash page and the
// number of bytes that these records would take up in a fresh page.
//
// It also tries to leave room for a record of size nExtra.
//
// This function is useful for reallocating the page
//
void CHashPage::GetStats
(
    HP_HEAPLENGTH nExtra,
    int *pnRecords,
    HP_HEAPLENGTH *pnAllocatedSize,
    int *pnGoodDirSize
)
{
    unsigned nSize = 0;
    unsigned nCount = 0;
    unsigned nGoodDirSize = 100;

    // Count and measure all the records in this page.
    //
    for (UINT32 iDir = 0; iDir < m_pHeader->m_nDirSize; iDir++)
    {
        if (m_pDirectory[iDir] < HP_DIR_DELETED) // ValidateAllocatedBlock(iDir))
        {
            nCount++;
            HP_PHEAPNODE pNode = (HP_PHEAPNODE)(m_pHeapStart + m_pDirectory[iDir]);
            HP_HEAPLENGTH nRequired = EXPAND_TO_BOUNDARY(
                HP_SIZEOF_HEAPNODE + pNode->u.s.nRecordSize);

            if (nRequired < HP_MIN_HEAP_ALLOC)
            {
                nRequired = HP_MIN_HEAP_ALLOC;
            }
            nSize += nRequired;
        }
    }
    *pnRecords = nCount;
    *pnAllocatedSize = nSize;

    // If we have records to talk about, or even if we are trying to reserve
    // space, then do the math.
    //
    if (  nExtra != 0
       || nCount != 0)
    {
        UINT32 nSpace = (UINT32)( ((unsigned char *)m_pTrailer)
                                - ((unsigned char *)m_pDirectory));
        UINT32 nMinDirSize = nCount;
        UINT32 nMaxDirSize = (nSpace - nSize)/sizeof(HP_HEAPOFFSET);

        if (nExtra)
        {
            nExtra += HP_SIZEOF_HEAPNODE;
            if (nExtra < HP_MIN_HEAP_ALLOC)
            {
                nExtra = HP_MIN_HEAP_ALLOC;
            }
            nExtra = EXPAND_TO_BOUNDARY(nExtra);
            nCount++;
            nSize += nExtra;
        }

#define FILL_FACTOR 1
        UINT32 nAverageSize = (nSize + nCount/2)/nCount;
        UINT32 nHeapGoal = (nSpace * nAverageSize)/(nAverageSize + sizeof(HP_HEAPOFFSET) + FILL_FACTOR);
        nGoodDirSize = (UINT32)((nSpace - nHeapGoal + sizeof(HP_HEAPOFFSET)/2)/sizeof(HP_HEAPOFFSET));
        if (nGoodDirSize < nMinDirSize)
        {
            nGoodDirSize = nMinDirSize;
        }
        else if (nGoodDirSize > nMaxDirSize)
        {
            nGoodDirSize = nMaxDirSize;
        }
    }
    *pnGoodDirSize = nGoodDirSize;
}


void CHashPage::SetFixedPointers(void)
{
    m_pHeader = (HP_PHEADER)m_pPage;
    m_pDirectory = (HP_PHEAPOFFSET)(m_pHeader+1);
    m_pTrailer = (HP_PTRAILER)(m_pPage + m_nPageSize - sizeof(HP_TRAILER));
}

void CHashPage::Empty(HP_DIRINDEX arg_nDepth, UINT32 arg_nHashGroup, HP_DIRINDEX arg_nDirSize)
{
    memset(m_pPage, 0, m_nPageSize);

    SetFixedPointers();

    m_pHeader->m_nDepth = arg_nDepth;
    m_pHeader->m_nDirSize = arg_nDirSize;
    m_pHeader->m_nHashGroup = arg_nHashGroup;
    m_pHeader->m_nTotalInsert = 0;
    m_pHeader->m_nDirEmptyLeft = arg_nDirSize;  // Number of entries marked HP_DIR_EMPTY.
    if (arg_nDirSize > 0)
    {
        ChoosePrimes(arg_nDirSize, m_pHeader->m_Primes);
        for (UINT32 iDir = 0; iDir < arg_nDirSize; iDir++)
        {
            m_pDirectory[iDir] = HP_DIR_EMPTY;
        }
    }
    SetVariablePointers();

    // Setup initial free list.
    //
    HP_PHEAPNODE pNode = (HP_PHEAPNODE)m_pHeapStart;
    pNode->nBlockSize = (HP_HEAPLENGTH)(m_pHeapEnd - m_pHeapStart);
    pNode->u.oNext = HP_NIL_OFFSET;
    m_pHeader->m_oFreeList = 0; // This is intentionally zero (i.e., m_pHeapStart - m_pHeapStart).
}

#ifdef HP_PROTECTION
void CHashPage::Protection(void)
{
    UINT32 ul = HASH_ProcessBuffer(0, m_pPage, m_nPageSize-sizeof(HP_TRAILER));
    m_pTrailer->m_checksum = ul;
}

bool CHashPage::Validate(void)
{
    UINT32 ul = HASH_ProcessBuffer(0, m_pPage, m_nPageSize-sizeof(HP_TRAILER));
    if (ul != m_pTrailer->m_checksum)
    {
        return false;
    }
    return true;
}

// ValidateBlock.
//
// This function validates a block associated with a particular
// Dir entry and blows that entry away if it's suspect.
//
bool CHashPage::ValidateAllocatedBlock(UINT32 iDir)
{
    if (iDir >= m_pHeader->m_nDirSize) return false;
    if (m_pDirectory[iDir] >= HP_DIR_DELETED)
        return false;

    // Use directory entry to go find heap node. The record itself follows.
    //
    unsigned char *pBlockStart = m_pHeapStart + m_pDirectory[iDir];
    unsigned char *pBlockEnd = pBlockStart + HP_MIN_HEAP_ALLOC;
    if (pBlockStart < m_pHeapStart || m_pHeapEnd <= pBlockEnd)
    {
        // Wow. We have a problem here. There is no good way of
        // finding this record anymore, so just mark it as
        // deleted. A sweep of the heap will reclaim any lost
        // free space.
        //
        m_pDirectory[iDir] = HP_DIR_DELETED;
    }
    else
    {
        HP_PHEAPNODE pNode = (HP_PHEAPNODE)pBlockStart;
        pBlockEnd = pBlockStart + pNode->nBlockSize;
        if (m_pHeapEnd < pBlockEnd || pNode->u.s.nRecordSize > pNode->nBlockSize)
        {
            // Wow. Record hangs off the end of the heap space, or the record
            // is larger than the block that holds it.
            //
            m_pDirectory[iDir] = HP_DIR_DELETED;
        }
        else
        {
            return true;
        }
    }
    return false;
}

bool CHashPage::ValidateFreeBlock(HP_HEAPOFFSET oBlock)
{
    // If the free list is empty, then this can't be a valid free block.
    //
    if (m_pHeader->m_oFreeList == HP_NIL_OFFSET)
    {
        return false;
    }

    // Go find heap node. The record itself follows.
    //
    unsigned char *pBlockStart = m_pHeapStart + oBlock;
    unsigned char *pBlockEnd = pBlockStart + HP_MIN_HEAP_ALLOC;
    if (pBlockStart < m_pHeapStart || m_pHeapEnd < pBlockEnd)
    {
        // Wow. We have a problem here. There is no good way of
        // finding this record anymore, so just empty the free list
        // and hope to either rehash the page into a new page, or
        // sweep the heap and re-establish the free list.
        //
        m_pHeader->m_oFreeList = HP_NIL_OFFSET;
    }
    else
    {
        HP_PHEAPNODE pNode = (HP_PHEAPNODE)pBlockStart;
        pBlockEnd = pBlockStart + pNode->nBlockSize;
        if (m_pHeapEnd < pBlockEnd)
        {
            // Wow. Record hangs off the end of the heap space.
            //
            m_pHeader->m_oFreeList = HP_NIL_OFFSET;
        }
        else
        {
            return true;
        }
    }
    return false;
}

// ValidateFreeList - Checks the validity of the free list
//
bool CHashPage::ValidateFreeList(void)
{
    HP_HEAPOFFSET oCurrent = m_pHeader->m_oFreeList;
    while (oCurrent != HP_NIL_OFFSET)
    {
        if (ValidateFreeBlock(oCurrent))
        {
            HP_PHEAPNODE pCurrent = (HP_PHEAPNODE)(m_pHeapStart + oCurrent);
            if (oCurrent >= pCurrent->u.oNext)
            {
                Log.WriteString("CHashPage::ValidateFreeList - Free list is corrupt." ENDLINE);
                m_pHeader->m_oFreeList = HP_NIL_OFFSET;
                return false;
            }
            oCurrent = pCurrent->u.oNext;
        }
        else
        {
            Log.WriteString("CHashPage::ValidateFreeList - Free list is corrupt." ENDLINE);
            m_pHeader->m_oFreeList = HP_NIL_OFFSET;
            return false;
        }
    }
    return true;
}
#endif // HP_PROTECTION

// Insert - Inserts a new record if there is room.
//
int CHashPage::Insert(HP_HEAPLENGTH nRecord, UINT32 nHash, void *pRecord)
{
    int ret = HP_INSERT_SUCCESS;
    m_pHeader->m_nTotalInsert++;
    for (int nTries = 0; nTries < 2; nTries++)
    {
#ifdef HP_PROTECTION
        // First, is this page dealing with keys like this at all?
        //
        HP_DIRINDEX nDepth = m_pHeader->m_nDepth;
        if ((nHash & anGroupMask[nDepth]) != m_pHeader->m_nHashGroup)
        {
            Log.WriteString("CHashPage::Insert - Inserting into the wrong page." ENDLINE);
            return HP_INSERT_ERROR_ILLEGAL;
        }
#endif // HP_PROTECTION

        // Where do we begin our first probe?
        //
        int di   = m_pHeader->m_Primes[nHash & 15];
        int iDir = (nHash >> 4) % (m_pHeader->m_nDirSize);
        m_nProbesLeft = m_pHeader->m_nDirSize;
        while (m_nProbesLeft-- && (m_pDirectory[iDir] < HP_DIR_DELETED))
        {
            iDir += di;
            if (iDir >= (m_pHeader->m_nDirSize))
            {
                iDir -= (m_pHeader->m_nDirSize);
            }
        }
        if (m_nProbesLeft >= 0)
        {
            if (m_pHeader->m_nDirEmptyLeft < m_nDirEmptyTrigger)
            {
                if (!Defrag(nRecord))
                {
                    return HP_INSERT_ERROR_FULL;
                }
                ret = HP_INSERT_SUCCESS_DEFRAG;
                continue;
            }
            if (HeapAlloc(iDir, nRecord, nHash, pRecord))
            {
                return ret;
            }
        }
        if (!Defrag(nRecord))
        {
            return HP_INSERT_ERROR_FULL;
        }
        ret = HP_INSERT_SUCCESS_DEFRAG;
    }
    return HP_INSERT_ERROR_FULL;
}

// Find - Finds the first record with the given hash key and returns its
//        directory index or HP_DIR_EMPTY if no hash keys are found.
//
//        Call iDir = FindFirstKey(hash) the first time, and then call
//        iDir = FindNextKey(iDir, hash) every time after than until
//        iDir == HP_DIR_EMPTY to interate through all the records with the
//        desired hash key.
//
HP_DIRINDEX CHashPage::FindFirstKey(UINT32 nHash, unsigned int *numchecks)
{
#ifdef HP_PROTECTION
    // First, is this page dealing with keys like this at all?
    //
    HP_DIRINDEX nDepth = m_pHeader->m_nDepth;
    if ((nHash & anGroupMask[nDepth]) != m_pHeader->m_nHashGroup)
        return HP_DIR_EMPTY;
#endif // HP_PROTECTION

    const int nDirSize = m_pHeader->m_nDirSize;

    // Where do we begin our first probe?
    //
    int iDir = (nHash >> 4) % nDirSize;
    int sOffset = m_pDirectory[iDir];
    if ((unsigned int)sOffset < HP_DIR_DELETED)
    {
        HP_PHEAPNODE pNode = (HP_PHEAPNODE)(m_pHeapStart + sOffset);
        if (pNode->u.s.nHash == nHash)
        {
            m_nProbesLeft = nDirSize - 1;
            *numchecks = 1;
            return iDir;
        }
    }
    else if (HP_DIR_EMPTY == sOffset)
    {
        m_nProbesLeft = nDirSize;
        *numchecks = 0;
        return HP_DIR_EMPTY;
    }

    //    HP_DIR_DELETED == sOffset
    // || pNode->u.s.nHash != nHash

    m_nProbesLeft = nDirSize - 1;
    int di = m_pHeader->m_Primes[nHash & 15];

    iDir += di;
    if (iDir >= nDirSize)
    {
        iDir -= nDirSize;
    }
    sOffset = m_pDirectory[iDir];

    while (sOffset != HP_DIR_EMPTY)
    {
        m_nProbesLeft--;
        if (sOffset != HP_DIR_DELETED)
        {
            HP_PHEAPNODE pNode = (HP_PHEAPNODE)(m_pHeapStart + sOffset);
            if (pNode->u.s.nHash == nHash)
            {
                *numchecks = nDirSize - m_nProbesLeft;
                return iDir;
            }
        }

        if (!m_nProbesLeft) break;

        iDir += di;
        if (iDir >= nDirSize)
        {
            iDir -= nDirSize;
        }
        sOffset = m_pDirectory[iDir];
    }
    *numchecks = nDirSize - m_nProbesLeft;
    return HP_DIR_EMPTY;
}

// Find - Finds the next record with the given hash key and returns its
//        directory index or HP_DIR_EMPTY if no hash keys are found.
//
//
HP_DIRINDEX CHashPage::FindNextKey(HP_DIRINDEX iDir, UINT32 nHash, unsigned int *numchecks)
{
    *numchecks = 0;

#ifdef HP_PROTECTION
    // First, is this page dealing with keys like this at all?
    //
    HP_DIRINDEX nDepth = m_pHeader->m_nDepth;
    if ((nHash & anGroupMask[nDepth]) != m_pHeader->m_nHashGroup)
        return HP_DIR_EMPTY;
#endif // HP_PROTECTION

    int nDirSize = m_pHeader->m_nDirSize;

    // Where do we begin our first probe? If this is the first call, i will be HP_DIR_EMPTY.
    // On calls after that, it will be what we returned on the previous call.
    //
    int di = m_pHeader->m_Primes[nHash & 15];
    iDir += di;
    if (iDir >= nDirSize)
    {
        iDir -= nDirSize;
    }
    while (m_nProbesLeft && (m_pDirectory[iDir] != HP_DIR_EMPTY))
    {
        m_nProbesLeft--;
        (*numchecks)++;
        if (m_pDirectory[iDir] != HP_DIR_DELETED)
        {
            if (m_pDirectory[iDir] < HP_DIR_DELETED) // ValidateAllocatedBlock(iDir))
            {
                HP_PHEAPNODE pNode = (HP_PHEAPNODE)(m_pHeapStart + m_pDirectory[iDir]);
                if (pNode->u.s.nHash == nHash) return iDir;
            }
        }
        iDir += di;
        if (iDir >= nDirSize)
        {
            iDir -= nDirSize;
        }
    }
    return HP_DIR_EMPTY;
}

// HeapAlloc - Return true if there was enough room to copy the record into the heap, otherwise,
//             it returns false.
//
bool CHashPage::HeapAlloc(HP_DIRINDEX iDir, HP_HEAPLENGTH nRecord, UINT32 nHash, void *pRecord)
{
    //ValidateFreeList();
    if (m_pDirectory[iDir] < HP_DIR_DELETED)
    {
        return false;
    }

    // How much space do we need?
    //
    HP_HEAPLENGTH nRequired = EXPAND_TO_BOUNDARY(HP_SIZEOF_HEAPNODE + nRecord);
    if (nRequired < HP_MIN_HEAP_ALLOC)
    {
        nRequired = HP_MIN_HEAP_ALLOC;
    }

    // Search through the free list for something of the right size.
    //
    HP_HEAPOFFSET oNext = m_pHeader->m_oFreeList;
    HP_PHEAPOFFSET poPrev = &(m_pHeader->m_oFreeList);
    while (oNext != HP_NIL_OFFSET)
    {
#if 0
        if (!ValidateFreeBlock(oPrevious))
        {
            ValidateFreeList();
            return false;
        }
#endif // 0
        unsigned char *pBlockStart = m_pHeapStart + oNext;
        HP_PHEAPNODE pNode = (HP_PHEAPNODE)pBlockStart;
        if (pNode->nBlockSize >= nRequired)
        {
            // We found something of the correct size.
            //
            // Do we cut it into two blocks or take the whole thing?
            //
            HP_HEAPLENGTH nNewBlockSize = pNode->nBlockSize - nRequired;
            if (nNewBlockSize >= EXPAND_TO_BOUNDARY(HP_MIN_HEAP_ALLOC+1))
            {
                // There is enough for leftovers, split it.
                //
                HP_PHEAPNODE pNewNode = (HP_PHEAPNODE)(pBlockStart + nRequired);
                pNewNode->nBlockSize = nNewBlockSize;
                pNewNode->u.oNext = pNode->u.oNext;

                // Update current node.
                //
                pNode->nBlockSize = nRequired;
                pNode->u.s.nHash = nHash;
                pNode->u.s.nRecordSize = nRecord;

                // Update Free list pointer.
                //
                *poPrev += nRequired;
            }
            else
            {
                // Take the whole thing.
                //
                *poPrev = pNode->u.oNext;
                pNode->u.s.nHash = nHash;
                pNode->u.s.nRecordSize = nRecord;
            }
            memcpy(pNode+1, pRecord, nRecord);
            if (m_pDirectory[iDir] == HP_DIR_EMPTY)
            {
                m_pHeader->m_nDirEmptyLeft--;
            }
            m_pDirectory[iDir] = (HP_HEAPOFFSET)(pBlockStart - m_pHeapStart);
            return true;
        }
        poPrev = &(pNode->u.oNext);
        oNext = pNode->u.oNext;
    }
    return false;
}

// HeapFree - Returns to the heap the space for the record associated with iDir. It
//            always succeeds even if there wasn't a record there to delete.
//
void CHashPage::HeapFree(HP_DIRINDEX iDir)
{
    //ValidateFreeList();
    if (m_pDirectory[iDir] < HP_DIR_DELETED) // ValidateAllocatedBlock(iDir))
    {
        HP_HEAPOFFSET oBlock = m_pDirectory[iDir];
        HP_PHEAPNODE pNode = (HP_PHEAPNODE)(m_pHeapStart + oBlock);

        // Clear it. The reason for clearing is that it makes debugging easier,
        // and also, if the file is compressed by the file system, a string
        // of zeros will yield a smaller result.
        //
        HP_HEAPLENGTH nBlockSize = pNode->nBlockSize;
        memset(pNode, 0, nBlockSize);
        pNode->nBlockSize = nBlockSize;

        // Push it onto the free list.
        //
        pNode->u.oNext = m_pHeader->m_oFreeList;
        m_pHeader->m_oFreeList = oBlock;
        m_pDirectory[iDir] = HP_DIR_DELETED;
    }
}

void CHashPage::HeapCopy(HP_DIRINDEX iDir, HP_PHEAPLENGTH pnRecord, void *pRecord)
{
    if (pnRecord == 0 || pRecord == 0) return;

    if (m_pDirectory[iDir] < HP_DIR_DELETED) // ValidateAllocatedBlock(iDir))
    {
        HP_PHEAPNODE pNode = (HP_PHEAPNODE)(m_pHeapStart + m_pDirectory[iDir]);

        // Copy the record.
        //
        *pnRecord = pNode->u.s.nRecordSize;
        memcpy(pRecord, pNode+1, pNode->u.s.nRecordSize);
    }
}

void CHashPage::HeapUpdate(HP_DIRINDEX iDir, HP_HEAPLENGTH nRecord, void *pRecord)
{
    if (nRecord == 0 || pRecord == 0) return;

    if (m_pDirectory[iDir] < HP_DIR_DELETED) // ValidateAllocatedBlock(iDir))
    {
        HP_PHEAPNODE pNode = (HP_PHEAPNODE)(m_pHeapStart + m_pDirectory[iDir]);

        if (pNode->u.s.nRecordSize != nRecord) return;
        memcpy(pNode+1, pRecord, nRecord);
    }
}

bool CHashPage::Split(CHashPage &hp0, CHashPage &hp1)
{
    // Figure out what a good directory size is given the actual records in this page.
    //
    int   nRecords;
    HP_HEAPLENGTH nAllocatedSize;
    int   nGoodDirSize;
    GetStats(0, &nRecords, &nAllocatedSize, &nGoodDirSize);
    if (nRecords == 0)
    {
        Log.WriteString("Why are we splitting a page with no records in it?" ENDLINE);
        return false;
    }

    // Initialize that type of HashPage and copy records over.
    //
    int   nNewDepth = m_pHeader->m_nDepth + 1;
    UINT32 nBitMask = 1 << (32-nNewDepth);
    UINT32 nHashGroup0 = m_pHeader->m_nHashGroup & (~nBitMask);
    UINT32 nHashGroup1 = nHashGroup0 | nBitMask;
    hp0.Empty(nNewDepth, nHashGroup0, nGoodDirSize);
    hp1.Empty(nNewDepth, nHashGroup1, nGoodDirSize);
    for (int iDir = 0; iDir < m_pHeader->m_nDirSize; iDir++)
    {
        if (m_pDirectory[iDir] < HP_DIR_DELETED) // ValidateAllocatedBlock(iDir))
        {
            HP_PHEAPNODE pNode = (HP_PHEAPNODE)(m_pHeapStart + m_pDirectory[iDir]);
            UINT32 nHash = pNode->u.s.nHash;
            if ((nHash & anGroupMask[nNewDepth]) == (nHashGroup0 & anGroupMask[nNewDepth]))
            {
                if (!IS_HP_SUCCESS(hp0.Insert(pNode->u.s.nRecordSize, nHash, pNode+1)))
                {
                    Log.WriteString("CHashPage::Split - Ran out of room." ENDLINE);
                    return false;
                }
            }
            else if ((nHash & anGroupMask[nNewDepth]) == (nHashGroup1 & anGroupMask[nNewDepth]))
            {
                if (!IS_HP_SUCCESS(hp1.Insert(pNode->u.s.nRecordSize, nHash, pNode+1)))
                {
                    Log.WriteString("CHashPage::Split - Ran out of room." ENDLINE);
                    return false;
                }
            }
            else
            {
                Log.WriteString("CHashPage::Split - This record fits in neither page...lost." ENDLINE);
                return false;
            }
        }
    }
#if 0
    int nRecords0, nRecords1;
    HP_HEAPLENGTH nAllocatedSize0, nAllocatedSize1;
    int    temp;
    hp0.GetStats(0, &nRecords0, &nAllocatedSize0, &temp);
    hp1.GetStats(0, &nRecords1, &nAllocatedSize1, &temp);
    Log.tinyprintf("Split (%d %d) page into (%d %d) and (%d %d)" ENDLINE,
        nRecords, nAllocatedSize, nRecords0, nAllocatedSize0, nRecords1,
        nAllocatedSize1);
    if (nRecords0 + nRecords1 != nRecords)
    {
        Log.WriteString("Lost something" ENDLINE);
        return false;
    }
#endif // 0
    return true;
}

void CHashPage::GetRange
(
    UINT32 arg_nDirDepth,
    UINT32 &nStart,
    UINT32 &nEnd
)
{
    UINT32 nBase = 0;
    int nShift = 32 - arg_nDirDepth;
    if (arg_nDirDepth > 0)
    {
        nBase = m_pHeader->m_nHashGroup >> nShift;
    }
    UINT32 ulMask = anGroupMask[nShift + m_pHeader->m_nDepth];
    nStart = nBase & ulMask;
    nEnd   = nBase | ~ulMask;
}

#ifdef WIN32
bool CHashPage::WritePage(HANDLE hFile, HF_FILEOFFSET oWhere)
{
    cs_dbwrites++;
    for ( ; ; MuxAlarm.Sleep(time_250ms))
    {
        if (SetFilePointer(hFile, oWhere, 0, FILE_BEGIN) == 0xFFFFFFFFUL)
        {
            Log.tinyprintf("CHashPage::Write - SetFilePointer error %u." ENDLINE, GetLastError());
            continue;
        }
        DWORD nWritten;
        if (!WriteFile(hFile, m_pPage, m_nPageSize, &nWritten, 0) || nWritten != m_nPageSize)
        {
            UINT32 cc = GetLastError();
            if (cc != ERROR_LOCK_VIOLATION)
            {
                Log.tinyprintf("CHashPage::Write - WriteFile error %u." ENDLINE, cc);
            }
            continue;
        }
        return true;
    }
}

bool CHashPage::ReadPage(HANDLE hFile, HF_FILEOFFSET oWhere)
{
    cs_dbreads++;
    SetFixedPointers();
    for ( ; ; MuxAlarm.Sleep(time_250ms))
    {
        if (SetFilePointer(hFile, oWhere, 0, FILE_BEGIN) == 0xFFFFFFFFUL)
        {
            Log.tinyprintf("CHashPage::Read - SetFilePointer error %u." ENDLINE, GetLastError());
            continue;
        }
        DWORD nRead;
        if (!ReadFile(hFile, m_pPage, m_nPageSize, &nRead, 0) || nRead != m_nPageSize)
        {
            UINT32 cc = GetLastError();
            if (cc != ERROR_LOCK_VIOLATION)
            {
                Log.tinyprintf("CHashPage::Read - ReadFile error %u." ENDLINE, cc);
            }
            continue;
        }
        SetVariablePointers();
        return true;
    }
}

#else // WIN32
bool CHashPage::WritePage(HANDLE hFile, HF_FILEOFFSET oWhere)
{
    cs_dbwrites++;
    int cnt = 60;
    for ( ; cnt; MuxAlarm.Sleep(time_1s), cnt--)
    {
#ifdef HAVE_PWRITE
        int cc = pwrite(hFile, m_pPage, m_nPageSize, oWhere);
#else
        if (lseek(hFile, oWhere, SEEK_SET) == (off_t)-1)
        {
            Log.tinyprintf("CHashPage::Write - lseek error %u." ENDLINE, errno);
            continue;
        }
        int cc = write(hFile, m_pPage, m_nPageSize);
#endif // HAVE_PWRITE
        if ((int)m_nPageSize != cc)
        {
            if (cc == -1)
            {
                Log.tinyprintf("CHashPage::Write - write error %u." ENDLINE, errno);
            }
            else
            {
                // Our write request was only partially filled. The disk is
                // probably full.
                //
                Log.tinyprintf("CHashPage::Write - partial write." ENDLINE);
            }
        }
        return true;
    }

    // Don't struggle further.  You'll just make it worse.
    //
    mudstate.shutdown_flag = true;
    return false;
}

bool CHashPage::ReadPage(HANDLE hFile, HF_FILEOFFSET oWhere)
{
    cs_dbreads++;
    SetFixedPointers();
    int cnt = 60;
    for ( ; cnt; MuxAlarm.Sleep(time_1s), cnt--)
    {
#ifdef HAVE_PREAD
        int cc = pread(hFile, m_pPage, m_nPageSize, oWhere);
#else
        if (lseek(hFile, oWhere, SEEK_SET) == (off_t)-1)
        {
            Log.tinyprintf("CHashPage::Read - lseek error %u." ENDLINE, errno);
            continue;
        }
        int cc = read(hFile, m_pPage, m_nPageSize);
#endif // HAVE_PREAD
        if ((int)m_nPageSize != cc)
        {
            if (cc == -1)
            {
                Log.tinyprintf("CHashPage::Read - read error %u." ENDLINE, errno);
            }
            else
            {
                // Our read request was only partially filled. Surrender.
                //
                Log.tinyprintf("CHashPage::Read - partial read." ENDLINE);
            }
            continue;
        }
        SetVariablePointers();
        return true;
    }

    // Don't struggle further.  You'll just make it worse.
    //
    mudstate.shutdown_flag = true;
    return false;
}
#endif // WIN32

HP_DIRINDEX CHashPage::GetDepth(void)
{
    return m_pHeader->m_nDepth;
}

// Defrag
//
// Moves all the records together, and re-establishes a single-element free list at the end.
//
bool CHashPage::Defrag(HP_HEAPLENGTH nExtra)
{
    CHashPage *hpNew = new CHashPage;
    if (!hpNew) return false;
    if (!hpNew->Allocate(m_nPageSize))
    {
        delete hpNew;
        return false;
    }

    // Figure out what a good directory size is given the actual records in this page.
    //
    int   nRecords;
    HP_HEAPLENGTH nAllocatedSize;
    int   nGoodDirSize;
    GetStats(nExtra, &nRecords, &nAllocatedSize, &nGoodDirSize);

    // Initialize that type of HashPage and copy records over.
    //
    hpNew->Empty(m_pHeader->m_nDepth, m_pHeader->m_nHashGroup, nGoodDirSize);
    int errInserted = HP_INSERT_SUCCESS;
    for (int iDir = 0; iDir < m_pHeader->m_nDirSize && IS_HP_SUCCESS(errInserted); iDir++)
    {
        if (m_pDirectory[iDir] < HP_DIR_DELETED) // ValidateAllocatedBlock(iDir))
        {
            HP_PHEAPNODE pNode = (HP_PHEAPNODE)(m_pHeapStart + m_pDirectory[iDir]);
            errInserted = hpNew->Insert(pNode->u.s.nRecordSize, pNode->u.s.nHash, pNode+1);
        }
    }
    if (IS_HP_SUCCESS(errInserted))
    {
        // Swap buffers.
        //
        unsigned char *tmp;
        tmp = hpNew->m_pPage;
        hpNew->m_pPage = m_pPage;
        m_pPage = tmp;

        SetFixedPointers();
        SetVariablePointers();
        delete hpNew;
        return true;
    }
    delete hpNew;
    return false;
}

void CHashPage::SetVariablePointers(void)
{
    m_pHeapStart = (unsigned char *)(m_pDirectory + m_pHeader->m_nDirSize);
    m_pHeapEnd = (unsigned char *)(m_pTrailer);

    // If less than 14.29% of the entries are empty, then do another Defrag.
    //
    m_nDirEmptyTrigger = (m_pHeader->m_nDirSize)/7;
}

HP_DIRINDEX CHashPage::FindFirst(HP_PHEAPLENGTH pnRecord, void *pRecord)
{
    for (m_iDir = 0; m_iDir < m_pHeader->m_nDirSize; m_iDir++)
    {
        if (m_pDirectory[m_iDir] < HP_DIR_DELETED) // ValidateAllocatedBlock(iDir))
        {
            HP_PHEAPNODE pNode = (HP_PHEAPNODE)(m_pHeapStart + m_pDirectory[m_iDir]);
            *pnRecord = pNode->u.s.nRecordSize;
            memcpy(pRecord, pNode+1, pNode->u.s.nRecordSize);
            return m_iDir;
        }
    }
    return HP_DIR_EMPTY;
}

HP_DIRINDEX CHashPage::FindNext(HP_PHEAPLENGTH pnRecord, void *pRecord)
{
    for ( m_iDir++; m_iDir < m_pHeader->m_nDirSize; m_iDir++)
    {
        if (m_pDirectory[m_iDir] < HP_DIR_DELETED) // ValidateAllocatedBlock(iDir))
        {
            HP_PHEAPNODE pNode = (HP_PHEAPNODE)(m_pHeapStart + m_pDirectory[m_iDir]);
            *pnRecord = pNode->u.s.nRecordSize;
            memcpy(pRecord, pNode+1, pNode->u.s.nRecordSize);
            return m_iDir;
        }
    }
    return HP_DIR_EMPTY;
}

CHashFile::CHashFile(void)
{
    SeedRandomNumberGenerator();
    m_Cache = NULL;
    m_nCache = 0;
    Init();
}

void CHashFile::Init(void)
{
    m_hDirFile = INVALID_HANDLE_VALUE;
    m_hPageFile = INVALID_HANDLE_VALUE;
    m_nDir = 0;
    m_nDirDepth = 0;
    m_pDir = NULL;
    m_hpCacheLookup = NULL;
    iCache = 0;
    m_iLastFlushed = 0;
}

#ifdef WIN32
void CHashFile::WriteDirectory(void)
{
    if (m_hDirFile == INVALID_HANDLE_VALUE) return;

    SetFilePointer(m_hDirFile, 0, 0, FILE_BEGIN);
    DWORD nWritten;
    WriteFile(m_hDirFile, m_pDir, sizeof(HF_FILEOFFSET)*m_nDir, &nWritten, 0);
    SetEndOfFile(m_hDirFile);
#ifdef DO_COMMIT
    if (!mudstate.bStandAlone)
    {
        FlushFileBuffers(m_hDirFile);
    }
#endif // DO_COMMIT
}
#else // WIN32
void CHashFile::WriteDirectory(void)
{
    if (m_hDirFile == INVALID_HANDLE_VALUE) return;

#ifdef HAVE_PWRITE
    pwrite(m_hDirFile, m_pDir, sizeof(HF_FILEOFFSET)*m_nDir, 0);
#else
    lseek(m_hDirFile, 0, SEEK_SET);
    write(m_hDirFile, m_pDir, sizeof(HF_FILEOFFSET)*m_nDir);
#endif // HAVE_PWRITE
    //SetEndOfFile(m_hDirFile);
#ifdef DO_COMMIT
    if (!mudstate.bStandAlone)
    {
        fsync(m_hDirFile);
    }
#endif // DO_COMMIT
}
#endif // WIN32

bool CHashFile::InitializeDirectory(unsigned int n)
{
    if (m_pDir)
    {
        MEMFREE(m_pDir);
        m_pDir = NULL;
    }
    if (m_hpCacheLookup)
    {
        MEMFREE(m_hpCacheLookup);
        m_hpCacheLookup = NULL;
    }

    m_nDir = n;
    m_nDirDepth = 0;
    n >>= 1;
    while (n)
    {
        m_nDirDepth++;
        n >>= 1;
    }

    m_pDir = (HF_FILEOFFSET *)MEMALLOC(sizeof(HF_FILEOFFSET)*m_nDir);
    ISOUTOFMEMORY(m_pDir);
    m_hpCacheLookup = new int[m_nDir];
    ISOUTOFMEMORY(m_hpCacheLookup);

    for (unsigned int i = 0; i < m_nDir; i++)
    {
        m_pDir[i] = 0xFFFFFFFFUL;
        m_hpCacheLookup[i] = -1;
    }

    return true;
}

bool CHashFile::CreateFileSet(const char *szDirFile, const char *szPageFile)
{
    CloseAll();
#ifdef WIN32
    m_hPageFile = CreateFile(szPageFile, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, 0, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL + FILE_FLAG_RANDOM_ACCESS, NULL);
#else // WIN32
    m_hPageFile = open(szPageFile, O_RDWR|O_BINARY|O_CREAT|O_TRUNC, 0600);
#endif // WIN32

    if (m_hPageFile == INVALID_HANDLE_VALUE)
    {
        return false;
    }

#ifdef WIN32
    m_hDirFile = CreateFile(szDirFile, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, 0, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL + FILE_FLAG_SEQUENTIAL_SCAN, NULL);
#else // WIN32
    m_hDirFile = open(szDirFile, O_RDWR|O_BINARY|O_CREAT|O_TRUNC, 0600);
#endif // WIN32

    if (m_hPageFile == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    // Create empty structures in memory and write them out.
    //
    if (!InitializeDirectory(2))
    {
        return false;
    }

    iCache = AllocateEmptyPage(0, 0);
    if (iCache < 0)
    {
        return false;
    }
    m_Cache[iCache].m_hp.Empty(0, 0UL, 100);
    m_Cache[iCache].m_o = 0UL;

    m_pDir[0] = m_pDir[1] = m_Cache[iCache].m_o;
    m_hpCacheLookup[0] = m_hpCacheLookup[1] = iCache;

    m_Cache[iCache].m_iState = HF_CACHE_UNPROTECTED;

    oEndOfFile = HF_SIZEOF_PAGE;

#ifdef DO_COMMIT
    FlushCache(iCache);
#endif // DO_COMMIT
    WriteDirectory();
    return true;
}

bool CHashFile::RebuildDirectory(void)
{
    // Initialize in-memory page directory
    //
    if (!InitializeDirectory(2))
    {
        return false;
    }

    // Re-build the directory from CHashPages.
    //
    for (UINT32 oPage = 0; oPage < oEndOfFile; oPage += HF_SIZEOF_PAGE)
    {
        int iCache;
        if ((iCache = AllocateEmptyPage(0, NULL)) < 0)
        {
            Log.WriteString("CHashFile::RebuildDirectory.  AllocateEmptyPage failed. DB DAMAGE." ENDLINE);
            return false;
        }

        if (m_Cache[iCache].m_hp.ReadPage(m_hPageFile, oPage))
        {
            m_Cache[iCache].m_o = oPage;
            m_Cache[iCache].m_iState = HF_CACHE_CLEAN;
            ResetAge(iCache);
        }
        else
        {
            Log.WriteString("CHashFile::RebuildDirectory.  ReadPage failed to get the page. DB DAMAGE." ENDLINE);
        }

        UINT32 nPageDepth = m_Cache[iCache].m_hp.GetDepth();
        while (m_nDirDepth < nPageDepth)
        {
            if (!DoubleDirectory())
            {
                return false;
            }
        }
        UINT32 nStart, nEnd;
        m_Cache[iCache].m_hp.GetRange(m_nDirDepth, nStart, nEnd);
        for ( ; nStart <= nEnd; nStart++)
        {
            if (m_pDir[nStart] != 0xFFFFFFFFUL)
            {
                Log.WriteString("CHashFile::Open - The keyspace of pages in Page File overlap." ENDLINE);
                return false;
            }
            m_pDir[nStart] = oPage;
            m_hpCacheLookup[nStart] = iCache;
        }
    }

    // Validate that the directory does not have holes.
    //
    for (UINT32 iFileDir = 0; iFileDir < m_nDir; iFileDir++)
    {
        if (m_pDir[iFileDir] == 0xFFFFFFFFUL)
        {
            Log.WriteString("CHashFile::Open - Page File is incomplete." ENDLINE);
            return false;
        }
    }
    WriteDirectory();
    return true;
}

bool CHashFile::ReadDirectory(void)
{
#ifdef WIN32
    UINT32 cc = SetFilePointer(m_hDirFile, 0, 0, FILE_END);
#else // WIN32
    UINT32 cc = lseek(m_hDirFile, 0, SEEK_END);
#endif // WIN32
    if (cc == 0xFFFFFFFFUL)
    {
        return false;
    }

    InitializeDirectory(cc / HF_SIZEOF_FILEOFFSET);

#ifdef WIN32
    cc = SetFilePointer(m_hDirFile, 0, 0, FILE_BEGIN);
    DWORD nRead;
    ReadFile(m_hDirFile, m_pDir, sizeof(HF_FILEOFFSET)*m_nDir, &nRead, 0);
#else // WIN32
#ifdef HAVE_PREAD
    pread(m_hDirFile, m_pDir, sizeof(HF_FILEOFFSET)*m_nDir, 0);
#else
    lseek(m_hDirFile, 0, SEEK_SET);
    read(m_hDirFile, m_pDir, sizeof(HF_FILEOFFSET)*m_nDir);
#endif // HAVE_PREAD
#endif // WIN32
    return true;
}

void CHashFile::InitCache(int nCachePages)
{
    if (m_Cache)
    {
        return;
    }

    // Allocate hash page cache.
    //
    m_nCache = nCachePages;
    m_Cache = new HF_CACHE[m_nCache];
    ISOUTOFMEMORY(m_Cache);
    for (int i = 0; i < m_nCache; i++)
    {
        m_Cache[i].m_hp.Allocate(HF_SIZEOF_PAGE);
        m_Cache[i].m_iState = HF_CACHE_EMPTY;
        m_Cache[i].m_o = 0UL;
        m_Cache[i].m_iYounger = i-1;
        m_Cache[i].m_iOlder   = i+1;
    }
    m_Cache[0].m_iYounger = m_nCache-1;
    m_Cache[m_nCache-1].m_iOlder = 0;
    m_iOldest = 0;
}

int CHashFile::Open(const char *szDirFile, const char *szPageFile, int nCachePages)
{
    CloseAll();
    FinalCache();
    InitCache(nCachePages);

    // First let's try to open the page file. This is the more important file.
    //
#ifdef WIN32
    m_hPageFile = CreateFile(szPageFile, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL + FILE_FLAG_RANDOM_ACCESS, NULL);
#else // WIN32
    m_hPageFile = open(szPageFile, O_RDWR|O_BINARY);
#endif // WIN32
    if (m_hPageFile == INVALID_HANDLE_VALUE)
    {
        // The PageFile doesn't exist, so we have'ta create both of them.
        //
        if (!CreateFileSet(szDirFile, szPageFile))
        {
            CloseAll();
            return HF_OPEN_STATUS_ERROR;
        }
        return HF_OPEN_STATUS_NEW;
    }

    // We have a page file open. Let's check how big it is. If it's
    // zero-length, the page file is useless to use, and we need to go through
    // the standard creation process. If the size is not a multiple of
    // HF_SIZEOF_PAGE, we need to fail.
    //
#ifdef WIN32
    oEndOfFile = SetFilePointer(m_hPageFile, 0, 0, FILE_END);
#else // WIN32
    oEndOfFile = lseek(m_hPageFile, 0, SEEK_END);
#endif // WIN32
    if (oEndOfFile == 0xFFFFFFFFUL)
    {
        CloseAll();
        return HF_OPEN_STATUS_ERROR;
    }
    else if (oEndOfFile == 0UL)
    {
        // The PageFile exists, but it's zero-length, so we have'ta create
        // both of them.
        //
        if (!CreateFileSet(szDirFile, szPageFile))
        {
            CloseAll();
            return HF_OPEN_STATUS_ERROR;
        }
        return HF_OPEN_STATUS_NEW;
    }
    else if ((oEndOfFile % HF_SIZEOF_PAGE) != 0)
    {
        // This is not a mulitple of HP_SIZEOF_PAGE. Weird unknown format.
        //
        CloseAll();
        return HF_OPEN_STATUS_ERROR;
    }

    // Now that the page file appears valid so far, let's see if the directory
    // file is there. This file is not strictly necessary, we can rebuild it.
    // However, having it helps us to open faster.
    //
#ifdef WIN32
    m_hDirFile = CreateFile(szDirFile, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL + FILE_FLAG_SEQUENTIAL_SCAN, NULL);
#else // WIN32
    m_hDirFile = open(szDirFile, O_RDWR|O_BINARY);
#endif // WIN32
    if (m_hDirFile == INVALID_HANDLE_VALUE)
    {
        // The Directory doesn't exist, so we create it anew, and rebuild the
        // index.
#ifdef WIN32
        m_hDirFile = CreateFile(szDirFile, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ, 0, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL + FILE_FLAG_SEQUENTIAL_SCAN, NULL);
#else // WIN32
        m_hDirFile = open(szDirFile, O_RDWR|O_BINARY|O_CREAT|O_TRUNC, 0600);
#endif // WIN32

        if (m_hPageFile == INVALID_HANDLE_VALUE)
        {
            CloseAll();
            return HF_OPEN_STATUS_ERROR;
        }

        if (!RebuildDirectory())
        {
            CloseAll();
            return HF_OPEN_STATUS_ERROR;
        }
        return HF_OPEN_STATUS_OLD;
    }

    // Read in the directory.
    //
    if (!ReadDirectory())
    {
        CloseAll();
        return HF_OPEN_STATUS_ERROR;
    }
    return HF_OPEN_STATUS_OLD;
}

void CHashFile::Sync(void)
{
    if (m_hPageFile != INVALID_HANDLE_VALUE)
    {
        cs_syncs++;
        bool bAllFlushed = true;
        for (int i = 0; i < m_nCache; i++)
        {
            if (!FlushCache(i))
            {
                bAllFlushed = false;
            }
        }
        if (!bAllFlushed)
        {
            Log.WriteString("CHashFile::Sync. Could not flush all the pages. DB DAMAGE." ENDLINE);
        }

#ifdef DO_COMMIT
        if (!mudstate.bStandAlone)
        {
#ifdef WIN32
            FlushFileBuffers(m_hPageFile);
#else // WIN32
            fsync(m_hPageFile);
#endif // WIN32
        }
#endif // DO_COMMIT
    }
#ifdef DO_COMMIT
    if (  m_hDirFile != INVALID_HANDLE_VALUE
       && !mudstate.bStandAlone)
    {
#ifdef WIN32
        FlushFileBuffers(m_hDirFile);
#else // WIN32
        fsync(m_hDirFile);
#endif // WIN32
    }
#endif // DO_COMMIT
}

void CHashFile::CloseAll(void)
{
    if (m_hPageFile != INVALID_HANDLE_VALUE)
    {
        Sync();
        if (m_pDir)
        {
            MEMFREE(m_pDir);
            m_pDir = NULL;
        }
        if (m_hpCacheLookup)
        {
            delete [] m_hpCacheLookup;
            m_hpCacheLookup = NULL;
        }

#ifdef WIN32
        CloseHandle(m_hPageFile);
#else // WIN32
        close(m_hPageFile);
#endif // WIN32
    }
    if (m_hDirFile != INVALID_HANDLE_VALUE)
    {
#ifdef WIN32
        CloseHandle(m_hDirFile);
#else // WIN32
        close(m_hDirFile);
#endif // WIN32
    }
    Init();
}

void CHashFile::FinalCache(void)
{
    if (m_Cache)
    {
        delete [] m_Cache;
        m_Cache = NULL;
        m_nCache = 0;
    }
}

CHashFile::~CHashFile(void)
{
    FinalCache();
    CloseAll();
}

bool CHashFile::Insert(HP_HEAPLENGTH nRecord, UINT32 nHash, void *pRecord)
{
    cs_writes++;
    for (;;)
    {
        UINT32 iFileDir = nHash >> (32-m_nDirDepth);
        if (iFileDir >= m_nDir)
        {
            Log.WriteString("CHashFile::Insert - iFileDir out of range." ENDLINE);
            return false;
        }
        iCache = ReadCache(iFileDir, &cs_whits);
        if (iCache < 0)
        {
            Log.WriteString("CHashFile::Insert - Page wasn't valid." ENDLINE);
            return false;
        }

        UINT32 nStart, nEnd;
        m_Cache[iCache].m_hp.GetRange(m_nDirDepth, nStart, nEnd);
        if (iFileDir < nStart || nEnd < iFileDir)
        {
            Log.tinyprintf("CHashFile::Insert - Directory entry (0x%08X) points to the wrong page (0x%08X-0x%08X)." ENDLINE,
                iFileDir, nStart, nEnd);
            return false;
        }
        int errInserted = m_Cache[iCache].m_hp.Insert(nRecord, nHash, pRecord);
        if (IS_HP_SUCCESS(errInserted))
        {
            // The record was inserted successfully, so the page is dirty.
            //
            m_Cache[iCache].m_iState = HF_CACHE_UNPROTECTED;
            break;
        }
        else if (HP_INSERT_ERROR_ILLEGAL == errInserted)
        {
            return false;
        }

#ifndef WIN32
        // First, if we are @dumping, then we have a @forked process
        // that is also reading from the file. We must pause and let
        // this reader process finish.
        //
        if (  !mudstate.bStandAlone
           && mudstate.dumping)
        {
            STARTLOG(LOG_DBSAVES, "DMP", "DUMP");
            log_text("Waiting on previously-forked child before page-splitting... ");
            ENDLOG;
            do
            {
                // We have a forked dump in progress, so we will wait until the
                // child exits.
                //
                MuxAlarm.Sleep(time_1s);
            } while (mudstate.dumping);
        }
#endif // !WIN32

        // If the depth of this page is already as deep as the directory
        // depth,then we must increase depth of the directory, first.
        //
        if (m_nDirDepth == m_Cache[iCache].m_hp.GetDepth())
        {
            if (!DoubleDirectory())
            {
                return false;
            }
        }

        // Split this page into two pages. We become a new one, and we
        // are given a pointer to the other one.
        //
        int Safe[2];
        int nSafe = 0;
        int iEmpty0, iEmpty1;

        Safe[nSafe++] = iCache;
        iEmpty0 = AllocateEmptyPage(nSafe, Safe);
        if (iEmpty0 < 0) return false;

        if (iCache == iEmpty0)
        {
            Log.WriteString("CHashFile::Split - iCache == iEmpty0" ENDLINE);
            return false;
        }

        Safe[nSafe++] = iEmpty0;
        iEmpty1 = AllocateEmptyPage(nSafe, Safe);
        if (iEmpty1 < 0) return false;

        if (iCache == iEmpty1)
        {
            Log.WriteString("CHashFile::Split - iCache == iEmpty1" ENDLINE);
            return false;
        }

        if (iEmpty0 == iEmpty1)
        {
            Log.WriteString("CHashFile::Split - iEmpty0 == iEmpty1" ENDLINE);
            return false;
        }

        if (!m_Cache[iCache].m_hp.Split(m_Cache[iEmpty0].m_hp, m_Cache[iEmpty1].m_hp))
        {
            return false;
        }

        // Tack another page onto the end of the .pag file.
        //
        long oNew = oEndOfFile;
        oEndOfFile += HF_SIZEOF_PAGE;

        // iEmpty0 => iCache. iEmpty1 => end of file
        //
        m_Cache[iCache].m_iState = HF_CACHE_EMPTY;
        m_Cache[iEmpty0].m_o = m_Cache[iCache].m_o;
        m_Cache[iEmpty1].m_o = oNew;
        m_Cache[iEmpty0].m_iState = HF_CACHE_UNPROTECTED;
        m_Cache[iEmpty1].m_iState = HF_CACHE_UNPROTECTED;

        // Update the directory.
        //
        m_Cache[iEmpty0].m_hp.GetRange(m_nDirDepth, nStart, nEnd);
        for ( ; nStart <= nEnd; nStart++)
        {
            m_hpCacheLookup[nStart] = iEmpty0;
        }
        m_Cache[iEmpty1].m_hp.GetRange(m_nDirDepth, nStart, nEnd);
        for ( ; nStart <= nEnd; nStart++)
        {
            m_pDir[nStart] = oNew;
            m_hpCacheLookup[nStart] = iEmpty1;
        }

        // Flush the pages out.
        //
        FlushCache(iEmpty1);
        FlushCache(iEmpty0);

#ifdef DO_COMMIT
        if (!mudstate.bStandAlone)
        {
#ifdef WIN32
            FlushFileBuffers(m_hPageFile);
#else // WIN32
            fsync(m_hPageFile);
#endif // WIN32
        }
#endif // DO_COMMIT

        // Write Directory
        //
#ifdef WIN32
        SetFilePointer(m_hDirFile, 0, 0, FILE_BEGIN);
        DWORD nWritten;
        WriteFile(m_hDirFile, m_pDir, sizeof(HF_FILEOFFSET)*m_nDir, &nWritten, 0);
#else // WIN32
#ifdef HAVE_PWRITE
        pwrite(m_hDirFile, m_pDir, sizeof(HF_FILEOFFSET)*m_nDir, 0);
#else
        lseek(m_hDirFile, 0, SEEK_SET);
        write(m_hDirFile, m_pDir, sizeof(HF_FILEOFFSET)*m_nDir);
#endif // HAVE_PWRITE
#endif // WIN32

#ifdef DO_COMMIT
        if (!mudstate.bStandAlone)
        {
#ifdef WIN32
            FlushFileBuffers(m_hDirFile);
#else // WIN32
            fsync(m_hDirFile);
#endif // WIN32
        }
#endif // DO_COMMIT
    }
    return true;
}

bool CHashFile::DoubleDirectory(void)
{
    unsigned int nNewDir      = 2 * m_nDir;
    HP_DIRINDEX nNewDirDepth = m_nDirDepth + 1;

    HF_PFILEOFFSET pNewDir = (HF_PFILEOFFSET)MEMALLOC(sizeof(HF_FILEOFFSET)*nNewDir);
    ISOUTOFMEMORY(pNewDir);

    int *pNewCacheLookup = new int[nNewDir];
    ISOUTOFMEMORY(pNewCacheLookup);

    unsigned int iNewDir = 0;
    for (unsigned int iDir = 0; iDir < m_nDir; iDir++)
    {
        pNewDir[iNewDir]   = m_pDir[iDir];
        pNewDir[iNewDir+1] = m_pDir[iDir];
        pNewCacheLookup[iNewDir]   = m_hpCacheLookup[iDir];
        pNewCacheLookup[iNewDir+1] = m_hpCacheLookup[iDir];
        iNewDir += 2;
    }

    // Write out the new directory. It's always larger than
    // the previous one.
    //
    WriteDirectory();

    MEMFREE(m_pDir);
    m_pDir = pNewDir;

    MEMFREE(m_hpCacheLookup);
    m_hpCacheLookup = pNewCacheLookup;

    m_nDirDepth = nNewDirDepth;
    m_nDir = nNewDir;
    return true;
}

HP_DIRINDEX CHashFile::FindFirstKey(UINT32 nHash)
{
    cs_reads++;

    UINT32 iFileDir = nHash >> (32-m_nDirDepth);
    if (iFileDir >= m_nDir)
    {
        Log.WriteString("CHashFile::Insert - iFileDir out of range." ENDLINE);
        cs_fails++;
        return HF_FIND_END;
    }
    iCache = ReadCache(iFileDir, &cs_rhits);
    if (iCache < 0)
    {
        cs_fails++;
        return HF_FIND_END;
    }
    UINT32 nStart, nEnd;
    m_Cache[iCache].m_hp.GetRange(m_nDirDepth, nStart, nEnd);
    if (iFileDir < nStart || nEnd < iFileDir)
    {
        Log.tinyprintf("CHashFile::Find - Directory entry (0x%08X) points to the wrong page (0x%08X-0x%08X)." ENDLINE,
            iFileDir, nStart, nEnd);
        return HF_FIND_END;
    }

    unsigned int numchecks;
    HP_DIRINDEX iDir = m_Cache[iCache].m_hp.FindFirstKey(nHash, &numchecks);

    if (iDir == HP_DIR_EMPTY)
    {
        cs_fails++;
        return HF_FIND_END;
    }
    return iDir;
}

HP_DIRINDEX CHashFile::FindNextKey(HP_DIRINDEX iDir, UINT32 nHash)
{
    cs_reads++;

    unsigned int numchecks;

    iDir = m_Cache[iCache].m_hp.FindNextKey(iDir, nHash, &numchecks);

    if (iDir == HP_DIR_EMPTY)
    {
        cs_fails++;
        return HF_FIND_END;
    }
    return iDir;
}

void CHashFile::Copy(HP_DIRINDEX iDir, HP_PHEAPLENGTH pnRecord, void *pRecord)
{
    m_Cache[iCache].m_hp.HeapCopy(iDir, pnRecord, pRecord);
}

void CHashFile::Remove(HP_DIRINDEX iDir)
{
    cs_dels++;
    m_Cache[iCache].m_hp.HeapFree(iDir);
    m_Cache[iCache].m_iState = HF_CACHE_UNPROTECTED;
}

bool CHashFile::FlushCache(int iCache)
{
    switch (m_Cache[iCache].m_iState)
    {
    case HF_CACHE_UNPROTECTED:
#ifdef HP_PROTECTION
        m_Cache[iCache].m_hp.Protection();
#endif // HP_PROTECTION

    case HF_CACHE_UNWRITTEN:
        if (m_Cache[iCache].m_hp.WritePage(m_hPageFile, m_Cache[iCache].m_o))
        {
            m_Cache[iCache].m_iState = HF_CACHE_CLEAN;
        }
        else
        {
            return false;
        }
    }
    return true;
}

int CHashFile::AllocateEmptyPage(int nSafe, int Safe[])
{
    int cnt = m_nCache;
    while (cnt--)
    {
        int i = m_iOldest;

        bool bExclude = false;
        for (int j = 0; j < nSafe; j++)
        {
            if (Safe[j] == i)
            {
                bExclude = true;
                break;
            }
        }

        ResetAge(i);

        if (  !bExclude
           && FlushCache(i))
        {
            if (HF_CACHE_EMPTY != m_Cache[i].m_iState)
            {
                UINT32 nStart, nEnd;
                m_Cache[i].m_hp.GetRange(m_nDirDepth, nStart, nEnd);
                for ( ; nStart <= nEnd; nStart++)
                {
                    m_hpCacheLookup[nStart] = -1;
                }
                m_Cache[i].m_iState = HF_CACHE_EMPTY;
            }
            return i;
        }
    }
    return -1;
}

void CHashFile::ResetAge(int iEntry)
{
    if (iEntry == m_iOldest)
    {
        // Rotate the doubly-linked list to make the oldest entry the
        // youngest.
        //
        m_iOldest = m_Cache[m_iOldest].m_iYounger;
    }
    else if (iEntry == m_Cache[m_iOldest].m_iOlder)
    {
        // This is already the youngest entry.
        //
    }
    else
    {
        // Unlink this entry.
        //
        int iYounger = m_Cache[iEntry].m_iYounger;
        int iOlder   = m_Cache[iEntry].m_iOlder;
        m_Cache[iYounger].m_iOlder = iOlder;
        m_Cache[iOlder].m_iYounger = iYounger;

        // Re-link at the young end of the queue.
        //
        iYounger = m_iOldest;
        iOlder   = m_Cache[iYounger].m_iOlder;
        m_Cache[iEntry].m_iOlder   = iOlder;
        m_Cache[iEntry].m_iYounger = iYounger;
        m_Cache[iYounger].m_iOlder = iEntry;
        m_Cache[iOlder].m_iYounger = iEntry;
    }
}

void CHashFile::Tick(void)
{
    int nCycle = mudconf.check_interval;
    if (mudconf.dump_interval < nCycle)
    {
        nCycle = mudconf.dump_interval;
    }

    CLinearTimeDelta ltdCycle;
    ltdCycle.SetSeconds(nCycle);

    int n = (mudconf.cache_tick_period*m_nCache)/ltdCycle;
    if (n < 1)
    {
        n = 1;
    }

    for (int i = 0; i < n; i++)
    {
        // Go ahead and flush a cache entry...just to keep the sync load
        // down a bit. This gives the cache time to age, and yet, pushes
        // the pages off to the disk eventually and gradually.
        //
        FlushCache(m_iLastFlushed++);
        if (m_iLastFlushed >= m_nCache)
        {
            m_iLastFlushed = 0;
        }
    }
}

int CHashFile::ReadCache(UINT32 iFileDir, int *phits)
{
    int iCache = m_hpCacheLookup[iFileDir];
    HF_FILEOFFSET oPage = m_pDir[iFileDir];

    if (  iCache != -1
       && m_Cache[iCache].m_iState != HF_CACHE_EMPTY
       && m_Cache[iCache].m_o == oPage)
    {
        ResetAge(iCache);
        (*phits)++;
        return iCache;
    }

    if ((iCache = AllocateEmptyPage(0, NULL)) >= 0)
    {
        if (m_Cache[iCache].m_hp.ReadPage(m_hPageFile, oPage))
        {
            //if (m_Cache[i].m_hp.Validate())
            //{
                m_Cache[iCache].m_o = oPage;
                m_Cache[iCache].m_iState = HF_CACHE_CLEAN;
                ResetAge(iCache);

                UINT32 nStart, nEnd;
                m_Cache[iCache].m_hp.GetRange(m_nDirDepth, nStart, nEnd);
                for ( ; nStart <= nEnd; nStart++)
                {
                    m_hpCacheLookup[nStart] = iCache;
                }

                return iCache;
            //}
        }
        else
        {
            Log.WriteString("CHashFile::ReadCache.  ReadPage failed to get the page. DB DAMAGE." ENDLINE);
        }
    }
    return -1;
}

CHashTable::CHashTable(void)
{
    SeedRandomNumberGenerator();
    Init();
}

void CHashTable::Init(void)
{
    m_nDir = 2;
    m_nDirDepth = 1;
    m_pDir = new pCHashPage[m_nDir];
    if (m_pDir)
    {
        m_pDir[1] = m_pDir[0] = new CHashPage;
        if (m_pDir[0])
        {
            if (m_pDir[0]->Allocate(HT_SIZEOF_PAGE))
            {
                m_pDir[0]->Empty(0, 0UL, 100);
            }
            else
            {
                delete m_pDir[0];
                m_pDir[0] = NULL;
            }
        }
    }

    m_nPages = 1;
    m_nEntries = 0;
    m_nDeletions = 0;
    m_nScans = 0;
    m_nHits = 0;
    m_nChecks = 0;
    m_nMaxScan = 0;
}

void CHashTable::ResetStats(void)
{
    m_nScans = 0;
    m_nHits = 0;
    m_nChecks = 0;
}

bool CHashTable::Insert(HP_HEAPLENGTH nRecord, UINT32  nHash, void *pRecord)
{
    for (;;)
    {
        UINT32  iTableDir = nHash >> (32 - m_nDirDepth);
#ifdef HP_PROTECTION
        if (iTableDir >= m_nDir)
        {
            Log.WriteString("CHashTable::Insert - iTableDir out of range." ENDLINE);
            return false;
        }
#endif // HP_PROTECTION
        m_hpLast = m_pDir[iTableDir];
        if (!m_hpLast)
        {
            Log.WriteString("CHashTable::Insert - Page wasn't valid." ENDLINE);
            return false;
        }
        UINT32  nStart, nEnd;
#ifdef HP_PROTECTION
        m_hpLast->GetRange(m_nDirDepth, nStart, nEnd);
        if (iTableDir < nStart || nEnd < iTableDir)
        {
            Log.WriteString("CHashTable::Insert - Directory points to the wrong page." ENDLINE);
            return false;
        }
#endif // HP_PROTECTION
        int errInserted = m_hpLast->Insert(nRecord, nHash, pRecord);
        if (IS_HP_SUCCESS(errInserted))
        {
            if (errInserted == HP_INSERT_SUCCESS_DEFRAG)
            {
                // Otherwise, this value will be over inflated.
                //
                m_nMaxScan = 0;
            }
            break;
        }
        if (errInserted == HP_INSERT_ERROR_ILLEGAL)
        {
            return false;
        }

        // If the depth of this page is already as deep as the directory
        // depth,then we must increase depth of the directory, first.
        //
        if (m_nDirDepth == m_hpLast->GetDepth())
        {
            if (!DoubleDirectory())
            {
                return false;
            }
        }

        // Split this page into two pages. We become a new one, and we
        // are given a pointer to the other one.
        //
        CHashPage *hpEmpty0 = new CHashPage;
        if (!hpEmpty0) return false;
        if (!hpEmpty0->Allocate(HT_SIZEOF_PAGE))
        {
            delete hpEmpty0;
            return false;
        }

        CHashPage *hpEmpty1 = new CHashPage;
        if (!hpEmpty1) return false;
        if (!hpEmpty1->Allocate(HT_SIZEOF_PAGE))
        {
            delete hpEmpty0;
            delete hpEmpty1;
            return false;
        }

        if (!m_hpLast->Split(*hpEmpty0, *hpEmpty1))
        {
            return false;
        }

        // Otherwise, this value will be over inflated.
        //
        m_nMaxScan = 0;

        // Now, update the directory.
        //
        hpEmpty0->GetRange(m_nDirDepth, nStart, nEnd);
        for ( ; nStart <= nEnd; nStart++)
        {
            m_pDir[nStart] = hpEmpty0;
        }

        hpEmpty1->GetRange(m_nDirDepth, nStart, nEnd);
        for ( ; nStart <= nEnd; nStart++)
        {
            m_pDir[nStart] = hpEmpty1;
        }
        m_nPages++;

        delete m_hpLast;
        m_hpLast = 0;
    }
    m_nEntries++;
    return true;
}

bool CHashTable::DoubleDirectory(void)
{
    unsigned int nNewDir      = 2 * m_nDir;
    unsigned int nNewDirDepth = m_nDirDepth + 1;

    pCHashPage *pNewDir = new pCHashPage[nNewDir];
    if (pNewDir)
    {
        unsigned int iNewDir = 0;
        for (unsigned int iDir = 0; iDir < m_nDir; iDir++)
        {
            pNewDir[iNewDir++] = m_pDir[iDir];
            pNewDir[iNewDir++] = m_pDir[iDir];
        }

        delete m_pDir;

        m_pDir = pNewDir;
        m_nDirDepth = nNewDirDepth;
        m_nDir = nNewDir;
        return true;
    }
    return false;
}

HP_DIRINDEX CHashTable::FindFirstKey(UINT32  nHash)
{
    m_nScans++;
    UINT32  iTableDir = nHash >> (32-m_nDirDepth);
#ifdef HP_PROTECTION
    if (iTableDir >= m_nDir)
    {
        Log.WriteString("CHashTable::Insert - iTableDir out of range." ENDLINE);
        return HF_FIND_END;
    }
#endif // HP_PROTECTION
    m_hpLast = m_pDir[iTableDir];
    if (!m_hpLast)
    {
        Log.WriteString("CHashTable::Insert - Page wasn't valid." ENDLINE);
        return HF_FIND_END;
    }
#ifdef HP_PROTECTION
    UINT32  nStart, nEnd;
    m_hpLast->GetRange(m_nDirDepth, nStart, nEnd);
    if (iTableDir < nStart || nEnd < iTableDir)
    {
        Log.WriteString("CHashTable::Find - Directory points to the wrong page." ENDLINE);
        return HF_FIND_END;
    }
#endif // HP_PROTECTION
    unsigned int numchecks;

    HP_DIRINDEX iDir = m_hpLast->FindFirstKey(nHash, &numchecks);

    m_nChecks += numchecks;
    if (numchecks > m_nMaxScan)
    {
        m_nMaxScan = numchecks;
    }
    if (iDir == HP_DIR_EMPTY)
    {
        return HF_FIND_END;
    }
    m_nHits++;
    return iDir;
}

HP_DIRINDEX CHashTable::FindNextKey(HP_DIRINDEX iDir, UINT32  nHash)
{
    m_nScans++;
    unsigned int numchecks;

    iDir = m_hpLast->FindNextKey(iDir, nHash, &numchecks);

    m_nChecks += numchecks;
    if (numchecks > m_nMaxScan)
    {
        m_nMaxScan = numchecks;
    }
    if (iDir == HP_DIR_EMPTY)
    {
        return HF_FIND_END;
    }
    m_nHits++;
    return iDir;
}

void CHashTable::Copy(HP_DIRINDEX iDir, HP_PHEAPLENGTH pnRecord, void *pRecord)
{
    m_hpLast->HeapCopy(iDir, pnRecord, pRecord);
}

void CHashTable::Remove(HP_DIRINDEX iDir)
{
    m_nEntries--;
    m_nDeletions++;
    m_hpLast->HeapFree(iDir);
}

void CHashTable::Update(HP_DIRINDEX iDir, HP_HEAPLENGTH nRecord, void *pRecord)
{
    m_hpLast->HeapUpdate(iDir, nRecord, pRecord);
}

CHashTable::~CHashTable(void)
{
    Final();
}

void CHashTable::Final(void)
{
    if (m_pDir)
    {
        m_hpLast = 0;
        for (unsigned int i = 0; i < m_nDir; i++)
        {
            CHashPage *hp = m_pDir[i];

            if (hp != m_hpLast && hp)
            {
                delete hp;
                m_hpLast = hp;
            }
        }
        delete m_pDir;
        m_pDir = NULL;
    }
}

void CHashTable::Reset(void)
{
    Final();
    Init();
}

HP_DIRINDEX CHashTable::FindFirst(HP_PHEAPLENGTH pnRecord, void *pRecord)
{
    m_hpLast = 0;
    for (m_iPage = 0; m_iPage < m_nDir; m_iPage++)
    {
        if (m_pDir[m_iPage] == m_hpLast) continue;
        m_hpLast = m_pDir[m_iPage];
        if (m_hpLast)
        {
            HP_DIRINDEX iDir = m_hpLast->FindFirst(pnRecord, pRecord);
            if (iDir != HP_DIR_EMPTY)
            {
                return iDir;
            }
        }
    }
    return HF_FIND_END;
}

HP_DIRINDEX CHashTable::FindNext(HP_PHEAPLENGTH pnRecord, void *pRecord)
{
    if (m_hpLast)
    {
        HP_DIRINDEX iDir = m_hpLast->FindNext(pnRecord, pRecord);
        if (iDir != HP_DIR_EMPTY)
        {
            return iDir;
        }
    }

    // Move on to the next page.
    //
    for ( ; m_iPage < m_nDir; m_iPage++)
    {
        // Move on to the next page.
        //
        if (m_pDir[m_iPage] == m_hpLast) continue;
        m_hpLast = m_pDir[m_iPage];
        if (m_hpLast)
        {
            HP_DIRINDEX iDir = m_hpLast->FindFirst(pnRecord, pRecord);
            if (iDir != HP_DIR_EMPTY)
            {
                return iDir;
            }
        }
    }
    return HF_FIND_END;
}

INT64 CHashTable::GetEntryCount()
{
    return m_nEntries;
}

void CHashTable::GetStats
(
    unsigned int *hashsize,
    int *entries,
    INT64 *deletes,
    INT64 *scans,
    INT64 *hits,
    INT64 *checks,
    int *max_scan
)
{
    *hashsize = m_nPages;
    *entries = m_nEntries;
    *deletes = m_nDeletions;
    *scans = m_nScans;
    *hits = m_nHits;
    *checks = m_nChecks;
    *max_scan = m_nMaxScan;
}

CLogFile Log;
void CLogFile::WriteInteger(int iNumber)
{
    char aTempBuffer[20];
    size_t nTempBuffer = mux_ltoa(iNumber, aTempBuffer);
    WriteBuffer(nTempBuffer, aTempBuffer);
}

void CLogFile::WriteBuffer(size_t nString, const char *pString)
{
    if (!bEnabled)
    {
        return;
    }

#ifdef WIN32
    EnterCriticalSection(&csLog);
#endif // WIN32

    while (nString > 0)
    {
        size_t nAvailable = SIZEOF_LOG_BUFFER - m_nBuffer;
        if (nAvailable == 0)
        {
            Flush();
            continue;
        }

        size_t nToMove = nAvailable;
        if (nString < nToMove)
        {
            nToMove = nString;
        }

        // Move nToMove bytes from pString to aBuffer+nBuffer
        //
        memcpy(m_aBuffer+m_nBuffer, pString, nToMove);
        pString += nToMove;
        nString -= nToMove;
        m_nBuffer += nToMove;
    }
    Flush();

#ifdef WIN32
    LeaveCriticalSection(&csLog);
#endif // WIN32
}

void CLogFile::WriteString(const char *pString)
{
    size_t nString = strlen(pString);
    WriteBuffer(nString, pString);
}

void DCL_CDECL CLogFile::tinyprintf(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char aTempBuffer[SIZEOF_LOG_BUFFER];
    int nString = mux_vsnprintf(aTempBuffer, SIZEOF_LOG_BUFFER, fmt, ap);
    va_end(ap);
    WriteBuffer(nString, aTempBuffer);
}

void MakeLogName
(
    const char *pBasename,
    const char *szPrefix,
    CLinearTimeAbsolute lta,
    char *szLogName
)
{
    char szTimeStamp[18];
    lta.ReturnUniqueString(szTimeStamp);
    if (  pBasename
       && pBasename[0] != '\0')
    {
        sprintf(szLogName, "%s/%s-%s.log",
            pBasename,
            szPrefix,
            szTimeStamp);
    }
    else
    {
        sprintf(szLogName, "%s-%s.log",
            szPrefix,
            szTimeStamp);
    }
}

void CLogFile::CreateLogFile(void)
{
    CloseLogFile();

    m_nSize = 0;
#ifdef WIN32
    m_hFile = CreateFile(m_szFilename, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, 0, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL + FILE_FLAG_SEQUENTIAL_SCAN, NULL);
#else // WIN32
    m_hFile = open(m_szFilename, O_RDWR|O_BINARY|O_CREAT|O_TRUNC, 0600);
#endif // WIN32
}

void CLogFile::AppendLogFile(void)
{
    CloseLogFile();

#ifdef WIN32
    m_hFile = CreateFile(m_szFilename, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, 0, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL + FILE_FLAG_SEQUENTIAL_SCAN, NULL);
#else // WIN32
    m_hFile = open(m_szFilename, O_RDWR|O_BINARY, 0600);
#endif // WIN32

    if (m_hFile != INVALID_HANDLE_VALUE)
    {
#ifdef WIN32
        SetFilePointer(m_hFile, 0, 0, FILE_END);
#else // WIN32
        lseek(m_hFile, 0, SEEK_SET);
#endif // WIN32
    }
}

void CLogFile::CloseLogFile(void)
{
    if (m_hFile != INVALID_HANDLE_VALUE)
    {
#ifdef WIN32
        CloseHandle(m_hFile);
#else // WIN32
        close(m_hFile);
#endif // WIN32
        m_hFile = INVALID_HANDLE_VALUE;
    }
}

#define FILE_SIZE_TRIGGER (512*1024UL)

void CLogFile::Flush(void)
{
    if (  m_nBuffer <= 0
       || !bEnabled)
    {
        return;
    }
    if (bUseStderr)
    {
        fwrite(m_aBuffer, m_nBuffer, 1, stderr);
    }
    else
    {
        m_nSize += m_nBuffer;
#ifdef WIN32
        unsigned long nWritten;
        WriteFile(m_hFile, m_aBuffer, (DWORD)m_nBuffer, &nWritten, NULL);
#else // WIN32
        write(m_hFile, m_aBuffer, m_nBuffer);
#endif // WIN32

        if (m_nSize > FILE_SIZE_TRIGGER)
        {
            CloseLogFile();

            m_ltaStarted.GetLocal();
            MakeLogName(m_pBasename, m_szPrefix, m_ltaStarted, m_szFilename);

            CreateLogFile();
        }
    }
    m_nBuffer = 0;
}

void CLogFile::SetPrefix(const char *szPrefix)
{
    if (  !bUseStderr
       && strcmp(szPrefix, m_szPrefix) != 0)
    {
        if (bEnabled)
        {
            CloseLogFile();
        }

        char szNewName[SIZEOF_PATHNAME];
        MakeLogName(m_pBasename, szPrefix, m_ltaStarted, szNewName);
        if (bEnabled)
        {
            ReplaceFile(m_szFilename, szNewName);
        }
        strcpy(m_szPrefix, szPrefix);
        strcpy(m_szFilename, szNewName);

        if (bEnabled)
        {
            AppendLogFile();
        }
    }
}

void CLogFile::SetBasename(const char *pBasename)
{
    if (m_pBasename)
    {
        MEMFREE(m_pBasename);
        m_pBasename = NULL;
    }
    if (  pBasename
       && strcmp(pBasename, "-") == 0)
    {
        bUseStderr = true;
    }
    else
    {
        bUseStderr = false;
        if (pBasename)
        {
            m_pBasename = StringClone(pBasename);
        }
        else
        {
            m_pBasename = StringClone("");
        }
    }
}

CLogFile::CLogFile(void)
{
#ifdef WIN32
    InitializeCriticalSection(&csLog);
#endif // WIN32

    m_ltaStarted.GetLocal();
    m_hFile = INVALID_HANDLE_VALUE;
    m_nSize = 0;
    m_nBuffer = 0;
    bEnabled = false;
    bUseStderr = true;
    m_pBasename = NULL;
    m_szPrefix[0] = '\0';
    m_szFilename[0] = '\0';
}

void CLogFile::StartLogging()
{
    if (!bUseStderr)
    {
        m_ltaStarted.GetLocal();
        MakeLogName(m_pBasename, m_szPrefix, m_ltaStarted, m_szFilename);
        CreateLogFile();
    }
    bEnabled = true;
}

void CLogFile::StopLogging(void)
{
    Flush();
    bEnabled = false;
    if (!bUseStderr)
    {
        CloseLogFile();
        m_szPrefix[0] = '\0';
        m_szFilename[0] = '\0';
        SetBasename(NULL);
    }
}

CLogFile::~CLogFile(void)
{
    StopLogging();
#ifdef WIN32
    DeleteCriticalSection(&csLog);
#endif // WIN32
}

#ifdef MEMORY_ACCOUNTING

#pragma pack(1)
typedef struct
{
    void  *address;
    int    size;
    UINT32 fileline;
} AllocDataRec;

typedef struct
{
    size_t nSize;
    int    line;
    char   filename[1];
} IdentDataRec;
#pragma pack()

bool bMemAccountingInitialized = false;
CHashFile hfAllocData;
CHashFile hfIdentData;
extern long DebugTotalMemory;

UINT32 HashPointer(void *vp)
{
    UINT32 nHash1 = HASH_ProcessBuffer(0, &vp, sizeof(void *));
    UINT32 nHash2 = CRC32_ProcessBuffer(0, &vp, sizeof(void *));
    mux_assert(nHash1 == nHash2);
    return nHash1;
}

unsigned long HashFileLine(const char *fn, int line)
{
    UINT32 nHash1 = CRC32_ProcessInteger(line);
    nHash1 = HASH_ProcessBuffer(nHash1, fn, strlen(fn)+1);
    UINT32 nHash2 = CRC32_ProcessInteger(line);
    nHash2 = CRC32_ProcessBuffer(nHash2, fn, strlen(fn)+1);
    mux_assert(nHash1 == nHash2);
    return nHash1;
}

bool SubtractSpaceFromFileLine
(
    UINT32  nHash,
    int     size,
    size_t *pnSpace,
    int    *pAllocLine,
    char   *file
)
{
    *pnSpace = 0;
    *pAllocLine = -1;
    HP_DIRINDEX iDir = hfIdentData.FindFirstKey(nHash);
    if (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nIdent;
        char Buffer[1024];
        hfIdentData.Copy(iDir, &nIdent, Buffer);
        hfIdentData.Remove(iDir);

        IdentDataRec *idr = (IdentDataRec *)Buffer;
        *pAllocLine = idr->line;
        strcpy(file, idr->filename);
        idr->nSize -= size;
        *pnSpace = idr->nSize;

        hfIdentData.Insert(nIdent, nHash, Buffer);
    }
    return true;
}

unsigned long AddSpaceToFileLine
(
    const char *file,
    int     line,
    size_t  nSpace,
    size_t *pnTotalSpace
)
{
    *pnTotalSpace = 0;

    HP_HEAPLENGTH nIdent;
    char Buffer[1024];
    IdentDataRec *idr = (IdentDataRec *)Buffer;
    UINT32 nHash = HashFileLine(file, line);
    bool bFound = false;
again:
    HP_DIRINDEX iDir = hfIdentData.FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        hfIdentData.Copy(iDir, &nIdent, Buffer);
        if (  line == idr->line
           && strcmp(idr->filename, file) == 0)
        {
            hfIdentData.Remove(iDir);
            nSpace += idr->nSize;
            bFound = true;
            goto again;
        }
        iDir = hfIdentData.FindNextKey(iDir, nHash);
    }
    if (!bFound)
    {
        idr->line = line;
        strcpy(idr->filename, file);

        char *p = idr->filename + strlen(idr->filename) + 1;
        nIdent = p - Buffer;
    }
    idr->nSize = nSpace;
    hfIdentData.Insert(nIdent, nHash, Buffer);
    *pnTotalSpace = nSpace;
    return nHash;
}

void AccountForAllocation(void *pointer, size_t size, const char *file, int line)
{
    DebugTotalMemory += size;

    AllocDataRec adr;
    adr.address = pointer;
    adr.size = size;

    unsigned long nHash = HashPointer(pointer);
again:
    HP_DIRINDEX iDir = hfAllocData.FindFirstKey(nHash);

    while (iDir != HF_FIND_END)
    {
        AllocDataRec adr2;
        HP_HEAPLENGTH nRecord;
        hfAllocData.Copy(iDir, &nRecord, &adr2);
        if (adr2.address == adr.address)
        {
            // Whoa! We've been told this new pointer, but it's not.
            //
            fprintf(stderr, "The heap is giving us unfreed pointers (0x%08X). Weird.\n", adr.address);
            hfAllocData.Remove(iDir);
            goto again;
        }
        iDir = hfAllocData.FindNextKey(iDir, nHash);
    }

    size_t TotalSpace;
    adr.fileline = AddSpaceToFileLine(file, line, size, &TotalSpace);
    hfAllocData.Insert(sizeof(adr), nHash, &adr);
    fprintf(stderr, "malloc %d bytes %s, %d\n  bringing total to %d bytes\n",
        size, file, line, TotalSpace);
}

void AccountForFree(void *pointer, const char *file, int line)
{
    unsigned long nHash = HashPointer(pointer);

    bool bFound = false;
again:
    HP_DIRINDEX iDir = hfAllocData.FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        // We found it.
        //
        HP_HEAPLENGTH nRecord;
        AllocDataRec adr;
        hfAllocData.Copy(iDir, &nRecord, &adr);
        if (adr.address == pointer)
        {
            bFound = true;
            hfAllocData.Remove(iDir);

            DebugTotalMemory -= adr.size;
            size_t nSpace;
            int AllocLine;
            char filename[1024];
            if (SubtractSpaceFromFileLine(adr.fileline, adr.size, &nSpace, &AllocLine, filename))
            {
                fprintf(stderr, "free %d bytes on %s, %d ...\n  allocated on %s, %d...\n  leaving total of %d bytes\n",
                    adr.size, file, line, filename, AllocLine, nSpace);
            }
            else
            {
                fprintf(stderr, "free %d bytes on %s, %d ...\n  allocated on UNKNOWN\n", adr.size, file, line);
            }
            goto again;
        }
        iDir = hfAllocData.FindNextKey(iDir, nHash);
    }

    if (!bFound)
    {
        // Problems.
        //
        fprintf(stderr, "We are freeing unallocated pointers (0x%08X) on %s, %d\n", pointer, file, line);
    }
}

int iRecursion = 0;

void *MemAllocate(size_t size, const char *file, int line)
{
    void *vp = malloc(size);
    if (vp)
    {
        if (bMemAccountingInitialized)
        {
            if (iRecursion == 0)
            {
                iRecursion++;
                AccountForAllocation(vp, size, file, line);
                iRecursion--;
            }
        }
        else
        {
            fprintf(stderr, "malloc not intitalized on %s, %d.\n", file, line);
        }
    }
    else
    {
        fprintf(stderr, "malloc ran out of memory on %s, %d.\n", file, line);
    }
    return vp;
}

void MemFree(void *pointer, const char *file, int line)
{
    if (pointer)
    {
        if (bMemAccountingInitialized)
        {
            if (iRecursion == 0)
            {
                iRecursion++;
                AccountForFree(pointer, file, line);
                iRecursion--;
            }
        }
        else
        {
            fprintf(stderr, "free called on %s, %d before initialized with 0x%08X\n", file, line, pointer);
        }
        free(pointer);
    }
    else
    {
        fprintf(stderr, "We tried to free(0) on %s, %d\n", file, line);
    }
}

void *MemRealloc(void *pointer, size_t size, const char *file, int line)
{
    if (pointer)
    {
        AccountForFree(pointer, file, line);
    }
    void * vp = realloc(pointer, size);
    if (vp)
    {
        AccountForAllocation(vp, size, file, line);
    }
    return vp;
}

#endif // MEMORY_ACCOUNTING
