/*! \file utf8_normalize.cpp
 * \brief NFC normalization for UTF-8 strings.
 *
 * Implements Unicode NFC normalization (Canonical Decomposition followed by
 * Canonical Composition) using DFA state machines for all Unicode property
 * lookups.  Hangul composition and decomposition are handled algorithmically.
 *
 * All operations are locale-independent per Unicode default algorithms.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

// Hangul constants (Unicode 3.0+ algorithmic composition/decomposition).
//
#define HANGUL_SBASE  0xAC00
#define HANGUL_LBASE  0x1100
#define HANGUL_VBASE  0x1161
#define HANGUL_TBASE  0x11A7
#define HANGUL_LCOUNT 19
#define HANGUL_VCOUNT 21
#define HANGUL_TCOUNT 28
#define HANGUL_NCOUNT (HANGUL_VCOUNT * HANGUL_TCOUNT)  // 588
#define HANGUL_SCOUNT (HANGUL_LCOUNT * HANGUL_NCOUNT)  // 11172

// ---------------------------------------------------------------------------
// UTF-8 <-> UTF-32 helpers.
// ---------------------------------------------------------------------------

// Decode a single UTF-32 code point from a UTF-8 byte sequence.
// Advances *pp past the code point.  Returns UNI_EOF on error.
//
static UTF32 utf8_Decode(const UTF8 **pp, const UTF8 *pEnd)
{
    const UTF8 *p = *pp;
    if (p >= pEnd)
    {
        return UNI_EOF;
    }

    int n = utf8_FirstByte[*p];
    if (n <= 0 || n >= UTF8_CONTINUE)
    {
        // Continuation byte or invalid — skip one byte.
        (*pp)++;
        return UNI_EOF;
    }
    if (p + n > pEnd)
    {
        // Truncated sequence at end of buffer.
        *pp = pEnd;
        return UNI_EOF;
    }
    for (int i = 1; i < n; i++)
    {
        if (UTF8_CONTINUE != utf8_FirstByte[p[i]])
        {
            // Invalid continuation byte — skip one byte.
            (*pp)++;
            return UNI_EOF;
        }
    }

    UTF32 cp;
    switch (n)
    {
    case 1:
        cp = p[0];
        break;
    case 2:
        cp = ((p[0] & 0x1F) << 6)
           |  (p[1] & 0x3F);
        break;
    case 3:
        cp = ((p[0] & 0x0F) << 12)
           | ((p[1] & 0x3F) << 6)
           |  (p[2] & 0x3F);
        break;
    case 4:
        cp = ((p[0] & 0x07) << 18)
           | ((p[1] & 0x3F) << 12)
           | ((p[2] & 0x3F) << 6)
           |  (p[3] & 0x3F);
        break;
    default:
        cp = UNI_EOF;
        break;
    }
    *pp = p + n;
    return cp;
}

// Encode a UTF-32 code point to UTF-8.
// Returns the number of bytes written (1-4), or 0 on error.
//
static int utf8_Encode(UTF32 cp, UTF8 *buf)
{
    if (cp < 0x80)
    {
        buf[0] = static_cast<UTF8>(cp);
        return 1;
    }
    else if (cp < 0x800)
    {
        buf[0] = static_cast<UTF8>(0xC0 | (cp >> 6));
        buf[1] = static_cast<UTF8>(0x80 | (cp & 0x3F));
        return 2;
    }
    else if (cp < 0x10000)
    {
        buf[0] = static_cast<UTF8>(0xE0 | (cp >> 12));
        buf[1] = static_cast<UTF8>(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = static_cast<UTF8>(0x80 | (cp & 0x3F));
        return 3;
    }
    else if (cp <= 0x10FFFF)
    {
        buf[0] = static_cast<UTF8>(0xF0 | (cp >> 18));
        buf[1] = static_cast<UTF8>(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = static_cast<UTF8>(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = static_cast<UTF8>(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// DFA traversal helpers.
// ---------------------------------------------------------------------------

// Run the integer-valued DFA (CCC, NFC_QC) on a single code point's bytes.
// Returns the accepting state offset (the integer value), or the default.
// Templated on SBT type because the generator uses unsigned char for small
// tables and unsigned short for larger ones.
//
template<typename T>
static int RunIntegerDFA(
    const unsigned char *itt,
    const unsigned short *sot,
    const T *sbt,
    int nStartState,
    int nAcceptStart,
    int nDefault,
    const UTF8 *pStart,
    const UTF8 *pEnd)
{
    int iState = nStartState;
    const UTF8 *p = pStart;
    while (p < pEnd && iState < nAcceptStart)
    {
        unsigned char ch = *p++;
        unsigned char iColumn = itt[ch];
        unsigned short iOffset = sot[iState];
        for (;;)
        {
            int y = sbt[iOffset];
            if (y < 128)
            {
                if (iColumn < y)
                {
                    iState = sbt[iOffset + 1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                y = 256 - y;
                if (iColumn < y)
                {
                    iState = sbt[iOffset + iColumn + 1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned short>(iOffset + y + 1);
                }
            }
        }
    }

    if (iState < nAcceptStart)
    {
        return nDefault;
    }
    return iState - nAcceptStart;
}

// Get Canonical Combining Class for a code point.
//
static int GetCCC(const UTF8 *pStart, const UTF8 *pEnd)
{
    return RunIntegerDFA(
        tr_ccc_itt, tr_ccc_sot, tr_ccc_sbt,
        TR_CCC_START_STATE, TR_CCC_ACCEPTING_STATES_START,
        0,
        pStart, pEnd);
}

// Get NFC_QC property: 0=Yes, 1=No, 2=Maybe.
//
static int GetNFCQC(const UTF8 *pStart, const UTF8 *pEnd)
{
    return RunIntegerDFA(
        tr_nfcqc_itt, tr_nfcqc_sot, tr_nfcqc_sbt,
        TR_NFCQC_START_STATE, TR_NFCQC_ACCEPTING_STATES_START,
        0,
        pStart, pEnd);
}

// Get NFD decomposition for a code point.
// Returns the string_desc* or nullptr if no decomposition.
//
static const string_desc *GetNFD(const UTF8 *p, bool &bXor)
{
    unsigned short iState = TR_NFD_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_nfd_itt[ch];
        unsigned short iOffset = tr_nfd_sot[iState];
        for (;;)
        {
            int y = tr_nfd_sbt[iOffset];
            if (y < 128)
            {
                if (iColumn < y)
                {
                    iState = tr_nfd_sbt[iOffset + 1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                y = 256 - y;
                if (iColumn < y)
                {
                    iState = tr_nfd_sbt[iOffset + iColumn + 1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned short>(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_NFD_ACCEPTING_STATES_START);

    int idx = iState - TR_NFD_ACCEPTING_STATES_START;
    if (TR_NFD_DEFAULT == idx)
    {
        bXor = false;
        return nullptr;
    }
    bXor = (TR_NFD_XOR_START <= idx);
    return tr_nfd_ott + idx - 1;
}

// Look up NFC composition pair via the two-code-point DFA.
// Returns the composed code point, or 0 if no composition.
//
static UTF32 ComposeViaTable(const UTF8 *pStarter, int nStarterBytes,
                              const UTF8 *pCombining, int nCombiningBytes)
{
    int iState = TR_NFC_COMPOSE_START_STATE;

    // Feed starter bytes.
    //
    for (int i = 0; i < nStarterBytes && iState < TR_NFC_COMPOSE_ACCEPTING_STATES_START; i++)
    {
        unsigned char ch = pStarter[i];
        unsigned char iColumn = tr_nfc_compose_itt[ch];
        unsigned short iOffset = tr_nfc_compose_sot[iState];
        for (;;)
        {
            int y = tr_nfc_compose_sbt[iOffset];
            if (y < 128)
            {
                if (iColumn < y)
                {
                    iState = tr_nfc_compose_sbt[iOffset + 1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                y = 256 - y;
                if (iColumn < y)
                {
                    iState = tr_nfc_compose_sbt[iOffset + iColumn + 1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned short>(iOffset + y + 1);
                }
            }
        }
    }

    // Feed combining bytes.
    //
    for (int i = 0; i < nCombiningBytes && iState < TR_NFC_COMPOSE_ACCEPTING_STATES_START; i++)
    {
        unsigned char ch = pCombining[i];
        unsigned char iColumn = tr_nfc_compose_itt[ch];
        unsigned short iOffset = tr_nfc_compose_sot[iState];
        for (;;)
        {
            int y = tr_nfc_compose_sbt[iOffset];
            if (y < 128)
            {
                if (iColumn < y)
                {
                    iState = tr_nfc_compose_sbt[iOffset + 1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                y = 256 - y;
                if (iColumn < y)
                {
                    iState = tr_nfc_compose_sbt[iOffset + iColumn + 1];
                    break;
                }
                else
                {
                    iColumn = static_cast<unsigned char>(iColumn - y);
                    iOffset = static_cast<unsigned short>(iOffset + y + 1);
                }
            }
        }
    }

    if (iState < TR_NFC_COMPOSE_ACCEPTING_STATES_START)
    {
        return 0;  // No composition.
    }

    int idx = iState - TR_NFC_COMPOSE_ACCEPTING_STATES_START;
    if (0 == idx)
    {
        return 0;  // Default state = no composition.
    }
    return tr_nfc_compose_nfc_compose_result[idx];
}

// Compose two code points.  Handles Hangul algorithmically, then
// falls back to the DFA table.
//
static UTF32 Compose(UTF32 cp1, UTF32 cp2)
{
    // Hangul L + V -> LV
    //
    if (  HANGUL_LBASE <= cp1
       && cp1 < HANGUL_LBASE + HANGUL_LCOUNT
       && HANGUL_VBASE <= cp2
       && cp2 < HANGUL_VBASE + HANGUL_VCOUNT)
    {
        return HANGUL_SBASE
             + (cp1 - HANGUL_LBASE) * HANGUL_NCOUNT
             + (cp2 - HANGUL_VBASE) * HANGUL_TCOUNT;
    }

    // Hangul LV + T -> LVT
    //
    if (  HANGUL_SBASE <= cp1
       && cp1 < HANGUL_SBASE + HANGUL_SCOUNT
       && 0 == ((cp1 - HANGUL_SBASE) % HANGUL_TCOUNT)
       && HANGUL_TBASE < cp2
       && cp2 < HANGUL_TBASE + HANGUL_TCOUNT)
    {
        return cp1 + (cp2 - HANGUL_TBASE);
    }

    // Table lookup via DFA.
    //
    UTF8 buf1[4], buf2[4];
    int n1 = utf8_Encode(cp1, buf1);
    int n2 = utf8_Encode(cp2, buf2);
    if (n1 <= 0 || n2 <= 0)
    {
        return 0;
    }
    return ComposeViaTable(buf1, n1, buf2, n2);
}

// ---------------------------------------------------------------------------
// NFC Quick Check: utf8_is_nfc()
// ---------------------------------------------------------------------------

bool utf8_is_nfc(const UTF8 *src, size_t nSrc)
{
    const UTF8 *p = src;
    const UTF8 *pEnd = src + nSrc;
    int lastCCC = 0;

    while (p < pEnd)
    {
        const UTF8 *pStart = p;
        int n = utf8_FirstByte[*p];
        if (n <= 0 || n >= UTF8_CONTINUE)
        {
            p++;
            continue;
        }
        if (p + n > pEnd)
        {
            return false;
        }

        // Check NFC_QC property.
        //
        int qc = GetNFCQC(pStart, pStart + n);
        if (1 == qc)  // No
        {
            return false;
        }

        // Check CCC ordering.
        //
        int ccc = GetCCC(pStart, pStart + n);
        if (ccc != 0 && lastCCC > ccc)
        {
            // Combining marks out of canonical order.
            return false;
        }

        if (2 == qc)  // Maybe
        {
            // Could be NFC or not — need full normalization to be sure.
            return false;
        }

        lastCCC = ccc;
        p = pStart + n;
    }
    return true;
}

// ---------------------------------------------------------------------------
// NFC Normalization: utf8_normalize_nfc()
//
// Algorithm (UAX #15):
//   1. Decompose: Expand each code point to its NFD form (canonical
//      decomposition, recursive). Hangul syllables decomposed algorithmically.
//   2. Reorder: Sort combining marks by Canonical Combining Class (stable).
//   3. Compose: Combine starter + combining mark pairs back into precomposed
//      forms where possible. Hangul composed algorithmically.
// ---------------------------------------------------------------------------

// Maximum code points we can handle in a single normalization buffer.
// A LBUF_SIZE string is at most LBUF_SIZE code points (ASCII case).
// After decomposition, each code point can expand to at most ~4 code points.
// In practice, decomposition rarely exceeds 2:1 expansion.
//
#define NFC_MAX_CODEPOINTS (LBUF_SIZE * 2)

struct NFCCodePoint
{
    UTF32 cp;
    int   ccc;
};

// Decompose a single code point into the buffer.  Handles Hangul
// algorithmically and uses the NFD DFA table for everything else.
//
static void DecomposeOne(UTF32 cp, NFCCodePoint *buf, int &n, int maxN)
{
    // Hangul syllable decomposition.
    //
    if (HANGUL_SBASE <= cp && cp < HANGUL_SBASE + HANGUL_SCOUNT)
    {
        int sIndex = cp - HANGUL_SBASE;
        UTF32 l = HANGUL_LBASE + sIndex / HANGUL_NCOUNT;
        UTF32 v = HANGUL_VBASE + (sIndex % HANGUL_NCOUNT) / HANGUL_TCOUNT;
        UTF32 t = HANGUL_TBASE + sIndex % HANGUL_TCOUNT;

        if (n < maxN) { buf[n].cp = l; buf[n].ccc = 0; n++; }
        if (n < maxN) { buf[n].cp = v; buf[n].ccc = 0; n++; }
        if (t != HANGUL_TBASE && n < maxN)
        {
            buf[n].cp = t; buf[n].ccc = 0; n++;
        }
        return;
    }

    // Table lookup.
    //
    UTF8 encoded[4];
    int nBytes = utf8_Encode(cp, encoded);
    if (nBytes <= 0)
    {
        return;
    }

    bool bXor;
    const string_desc *sd = GetNFD(encoded, bXor);
    if (nullptr == sd)
    {
        // No decomposition — emit the code point as-is.
        //
        if (n < maxN)
        {
            UTF8 enc2[4];
            int nb2 = utf8_Encode(cp, enc2);
            buf[n].cp = cp;
            buf[n].ccc = GetCCC(enc2, enc2 + nb2);
            n++;
        }
        return;
    }

    // The string_desc contains the decomposed UTF-8 bytes.
    // If bXor, we need to XOR the original bytes with the pattern.
    //
    UTF8 decomposed[32];
    size_t nDecomp;

    if (bXor)
    {
        // XOR decomposition: XOR each byte of the original encoding
        // with the pattern bytes.
        //
        nDecomp = sd->n_bytes;
        if (nDecomp > sizeof(decomposed)) nDecomp = sizeof(decomposed);
        for (size_t i = 0; i < nDecomp; i++)
        {
            decomposed[i] = encoded[i] ^ sd->p[i];
        }
    }
    else
    {
        // Literal decomposition.
        //
        nDecomp = sd->n_bytes;
        if (nDecomp > sizeof(decomposed)) nDecomp = sizeof(decomposed);
        memcpy(decomposed, sd->p, nDecomp);
    }

    // Parse the decomposed UTF-8 into code points and recursively decompose.
    // (The table already contains fully recursive decompositions, but we
    //  look up CCC for each resulting code point.)
    //
    const UTF8 *dp = decomposed;
    const UTF8 *dpEnd = decomposed + nDecomp;
    while (dp < dpEnd && n < maxN)
    {
        const UTF8 *dpStart = dp;
        UTF32 dcp = utf8_Decode(&dp, dpEnd);
        if (UNI_EOF == dcp)
        {
            break;
        }

        // The table output is already fully decomposed, so no further
        // recursion needed.  Just look up CCC.
        //
        UTF8 enc3[4];
        int nb3 = utf8_Encode(dcp, enc3);
        buf[n].cp = dcp;
        buf[n].ccc = (nb3 > 0) ? GetCCC(enc3, enc3 + nb3) : 0;
        n++;
    }
}

// Canonical ordering: stable sort combining marks by CCC.
// Starters (CCC=0) are never reordered.
//
static void CanonicalOrder(NFCCodePoint *buf, int n)
{
    // Simple insertion sort — combining mark sequences are short.
    //
    for (int i = 1; i < n; i++)
    {
        if (buf[i].ccc != 0)
        {
            NFCCodePoint tmp = buf[i];
            int j = i;
            while (j > 0 && buf[j-1].ccc > tmp.ccc && buf[j-1].ccc != 0)
            {
                buf[j] = buf[j-1];
                j--;
            }
            buf[j] = tmp;
        }
    }
}

// Canonical composition step.
//
static void CanonicalCompose(NFCCodePoint *buf, int &n)
{
    if (n < 2)
    {
        return;
    }

    // Find the starter (leftmost CCC=0 code point).
    //
    int starterIdx = -1;
    for (int i = 0; i < n; i++)
    {
        if (0 == buf[i].ccc)
        {
            starterIdx = i;
            break;
        }
    }

    if (starterIdx < 0)
    {
        return;
    }

    int lastCCC = -1;
    for (int i = starterIdx + 1; i < n; i++)
    {
        int ccc = buf[i].ccc;

        // A combining mark is "blocked" from the starter if there is
        // an intervening combining mark with CCC >= this one's CCC
        // (and CCC != 0), or if this mark has CCC = 0 (it's a new starter).
        //
        bool blocked = (lastCCC != -1 && lastCCC >= ccc && ccc != 0);

        if (!blocked)
        {
            UTF32 composed = Compose(buf[starterIdx].cp, buf[i].cp);
            if (0 != composed)
            {
                // Replace the starter with the composed character.
                //
                buf[starterIdx].cp = composed;

                // Remove buf[i] by shifting.
                //
                for (int j = i; j < n - 1; j++)
                {
                    buf[j] = buf[j+1];
                }
                n--;
                i--;

                // Reset lastCCC since we modified the sequence.
                //
                lastCCC = -1;
                continue;
            }
        }

        if (0 == ccc)
        {
            // New starter.
            //
            starterIdx = i;
            lastCCC = -1;
        }
        else
        {
            lastCCC = ccc;
        }
    }
}

void utf8_normalize_nfc(const UTF8 *src, size_t nSrc, UTF8 *dst, size_t nDstMax, size_t *pnDst)
{
    *pnDst = 0;

    // Quick check: if already NFC, just copy.
    //
    if (utf8_is_nfc(src, nSrc))
    {
        size_t nCopy = (nSrc < nDstMax) ? nSrc : nDstMax;
        memcpy(dst, src, nCopy);
        *pnDst = nCopy;
        return;
    }

    // Step 1: Decompose all code points to NFD.
    //
    NFCCodePoint cps[NFC_MAX_CODEPOINTS];
    int nCps = 0;

    const UTF8 *p = src;
    const UTF8 *pEnd = src + nSrc;
    while (p < pEnd && nCps < NFC_MAX_CODEPOINTS)
    {
        UTF32 cp = utf8_Decode(&p, pEnd);
        if (UNI_EOF == cp)
        {
            continue;
        }
        DecomposeOne(cp, cps, nCps, NFC_MAX_CODEPOINTS);
    }

    // Step 2: Canonical ordering (sort combining marks by CCC).
    //
    CanonicalOrder(cps, nCps);

    // Step 3: Canonical composition.
    //
    CanonicalCompose(cps, nCps);

    // Step 4: Encode back to UTF-8.
    //
    size_t nOut = 0;
    for (int i = 0; i < nCps; i++)
    {
        UTF8 enc[4];
        int nb = utf8_Encode(cps[i].cp, enc);
        if (nb > 0 && nOut + nb <= nDstMax)
        {
            memcpy(dst + nOut, enc, nb);
            nOut += nb;
        }
    }
    *pnDst = nOut;
}
