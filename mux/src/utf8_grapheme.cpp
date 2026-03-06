/*! \file utf8_grapheme.cpp
 * \brief Extended Grapheme Cluster segmentation per UAX #29.
 *
 * Implements Unicode Extended Grapheme Cluster boundary detection using
 * DFA state machines for Grapheme_Cluster_Break (GCB) property lookups
 * and Extended_Pictographic classification.
 *
 * All operations are locale-independent per Unicode default algorithms.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

// GCB property values (must match gen_gcb.pl mapping).
//
enum GCB_Value
{
    GCB_Other              = 0,
    GCB_CR                 = 1,
    GCB_LF                 = 2,
    GCB_Control            = 3,
    GCB_Extend             = 4,
    GCB_ZWJ                = 5,
    GCB_Regional_Indicator = 6,
    GCB_Prepend            = 7,
    GCB_SpacingMark        = 8,
    GCB_L                  = 9,
    GCB_V                  = 10,
    GCB_T                  = 11,
    GCB_LV                 = 12,
    GCB_LVT                = 13,
};

// ---------------------------------------------------------------------------
// DFA wrapper: get GCB property for a single UTF-8 encoded code point.
// ---------------------------------------------------------------------------

template<typename T>
static int RunIntegerDFA_GCB(
    const unsigned char *itt,
    const unsigned short *sot,
    const T *sbt,
    int start_state,
    int accepting_start,
    int default_val,
    const UTF8 *p,
    const UTF8 *pEnd)
{
    int iState = start_state;
    while (p < pEnd)
    {
        unsigned char ch = *p++;
        int iColumn = itt[ch];
        int iOffset = sot[iState];

        for (;;)
        {
            int y = static_cast<signed char>(sbt[iOffset]);
            if (0 < y)
            {
                // RUN phrase.
                if (iColumn < y)
                {
                    iState = sbt[iOffset + 1];
                    break;
                }
                iColumn -= y;
                iOffset += 2;
            }
            else
            {
                // COPY phrase.
                y = -y;
                if (iColumn < y)
                {
                    iState = sbt[iOffset + iColumn + 1];
                    break;
                }
                iColumn -= y;
                iOffset += y + 1;
            }
        }
    }

    if (iState >= accepting_start)
    {
        return iState - accepting_start;
    }
    return default_val;
}

static int GetGCB(const UTF8 *p, const UTF8 *pEnd)
{
    return RunIntegerDFA_GCB(
        tr_gcb_itt, tr_gcb_sot, tr_gcb_sbt,
        TR_GCB_START_STATE, TR_GCB_ACCEPTING_STATES_START,
        GCB_Other, p, pEnd);
}

// ---------------------------------------------------------------------------
// DFA wrapper: check Extended_Pictographic property.
// ---------------------------------------------------------------------------

static bool IsExtPict(const UTF8 *p, const UTF8 *pEnd)
{
    unsigned short iState = CL_EXTPICT_START_STATE;
    while (p < pEnd)
    {
        unsigned char ch = *p++;
        int iColumn = cl_extpict_itt[ch];
        int iOffset = cl_extpict_sot[iState];

        for (;;)
        {
            int y = static_cast<signed char>(cl_extpict_sbt[iOffset]);
            if (0 < y)
            {
                if (iColumn < y)
                {
                    iState = cl_extpict_sbt[iOffset + 1];
                    break;
                }
                iColumn -= y;
                iOffset += 2;
            }
            else
            {
                y = -y;
                if (iColumn < y)
                {
                    iState = cl_extpict_sbt[iOffset + iColumn + 1];
                    break;
                }
                iColumn -= y;
                iOffset += y + 1;
            }
        }
    }

    if (iState >= CL_EXTPICT_ACCEPTING_STATES_START)
    {
        return (1 == iState - CL_EXTPICT_ACCEPTING_STATES_START);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Advance one UTF-8 code point.  Returns pointer past the code point.
// ---------------------------------------------------------------------------

static const UTF8 *utf8_advance(const UTF8 *p, const UTF8 *pEnd)
{
    if (p >= pEnd)
    {
        return p;
    }
    int n = utf8_FirstByte[*p];
    if (n < 1 || n >= UTF8_CONTINUE)
    {
        return p + 1;
    }
    for (int i = 1; i < n; i++)
    {
        if (  p + i >= pEnd
           || UTF8_CONTINUE != utf8_FirstByte[p[i]])
        {
            return p + 1;
        }
    }
    UTF32 cp = utf8_decode_raw(p, n);
    if (!utf8_is_valid_scalar(cp, n))
    {
        return p + 1;
    }
    const UTF8 *pNext = p + n;
    return (pNext <= pEnd) ? pNext : pEnd;
}

// ---------------------------------------------------------------------------
// utf8_next_grapheme: Advance past one Extended Grapheme Cluster.
//
// Given a pointer into a UTF-8 string and its end, returns a mux_cursor
// with the byte count and code point count consumed by the next grapheme
// cluster.  Implements GB rules from UAX #29 (Unicode 16.0).
//
// Rules implemented:
//   GB3:    CR x LF
//   GB4:    (Control|CR|LF) ÷
//   GB5:    ÷ (Control|CR|LF)
//   GB6:    L x (L|V|LV|LVT)
//   GB7:    (LV|V) x (V|T)
//   GB8:    (LVT|T) x T
//   GB9:    x (Extend|ZWJ)
//   GB9a:   x SpacingMark
//   GB9b:   Prepend x
//   GB11:   ExtPict Extend* ZWJ x ExtPict
//   GB12/13: RI x RI (pairs only)
//   GB999:  ÷ (otherwise)
// ---------------------------------------------------------------------------

mux_cursor utf8_next_grapheme(const UTF8 *src, size_t nSrc)
{
    const UTF8 *p = src;
    const UTF8 *pEnd = src + nSrc;

    if (p >= pEnd)
    {
        return mux_cursor(0, 0);
    }

    // Consume first code point.
    //
    const UTF8 *pFirstEnd = utf8_advance(p, pEnd);
    int prevGCB = GetGCB(p, pFirstEnd);
    bool bPrevExtPict = IsExtPict(p, pFirstEnd);
    const UTF8 *pCur = pFirstEnd;
    int nPoints = 1;

    // GB4: (Control|CR|LF) ÷  (break after, except GB3 CR x LF)
    //
    if (GCB_Control == prevGCB || GCB_LF == prevGCB)
    {
        return mux_cursor(pCur - src, nPoints);
    }

    // Track state for GB11 and GB12/13.
    //
    bool bSeenExtPictExtendZWJ = bPrevExtPict;  // For GB11
    int nRI = (GCB_Regional_Indicator == prevGCB) ? 1 : 0;  // For GB12/13

    while (pCur < pEnd)
    {
        const UTF8 *pNextEnd = utf8_advance(pCur, pEnd);
        int curGCB = GetGCB(pCur, pNextEnd);
        bool bCurExtPict = IsExtPict(pCur, pNextEnd);

        // GB3: CR x LF
        //
        if (GCB_CR == prevGCB && GCB_LF == curGCB)
        {
            pCur = pNextEnd;
            nPoints++;
            return mux_cursor(pCur - src, nPoints);
        }

        // GB4: (Control|CR|LF) ÷  — already handled for first cp.
        // GB5: ÷ (Control|CR|LF)
        //
        if (GCB_Control == curGCB || GCB_CR == curGCB || GCB_LF == curGCB)
        {
            break;
        }

        // GB6: L x (L|V|LV|LVT)
        //
        if (  GCB_L == prevGCB
           && (  GCB_L   == curGCB
              || GCB_V   == curGCB
              || GCB_LV  == curGCB
              || GCB_LVT == curGCB))
        {
            goto extend;
        }

        // GB7: (LV|V) x (V|T)
        //
        if (  (GCB_LV == prevGCB || GCB_V == prevGCB)
           && (GCB_V  == curGCB  || GCB_T == curGCB))
        {
            goto extend;
        }

        // GB8: (LVT|T) x T
        //
        if (  (GCB_LVT == prevGCB || GCB_T == prevGCB)
           && GCB_T == curGCB)
        {
            goto extend;
        }

        // GB9: x (Extend|ZWJ)
        //
        if (GCB_Extend == curGCB || GCB_ZWJ == curGCB)
        {
            // GB11: ExtPict Extend* ZWJ x ExtPict
            // Track whether we've seen ExtPict followed by Extend*/ZWJ.
            //
            if (GCB_ZWJ == curGCB)
            {
                // ZWJ after ExtPict Extend* — the flag stays set.
                // It will be checked when we see the next code point.
            }
            else  // Extend
            {
                // Extend does not reset the ExtPict tracking.
            }
            goto extend;
        }

        // GB9a: x SpacingMark
        //
        if (GCB_SpacingMark == curGCB)
        {
            goto extend;
        }

        // GB9b: Prepend x
        //
        if (GCB_Prepend == prevGCB)
        {
            goto extend;
        }

        // GB11: ExtPict Extend* ZWJ x ExtPict
        //
        if (  bSeenExtPictExtendZWJ
           && GCB_ZWJ == prevGCB
           && bCurExtPict)
        {
            goto extend;
        }

        // GB12/13: Regional_Indicator x Regional_Indicator (pairs only)
        //
        if (  GCB_Regional_Indicator == curGCB
           && 1 == (nRI % 2))
        {
            goto extend;
        }

        // GB999: ÷ (otherwise break)
        //
        break;

    extend:
        // Continue the current grapheme cluster.
        //
        if (GCB_Regional_Indicator == curGCB)
        {
            nRI++;
        }

        // Update GB11 tracking.
        //
        if (bCurExtPict)
        {
            bSeenExtPictExtendZWJ = true;
        }
        else if (  !bSeenExtPictExtendZWJ
                || (  GCB_Extend != curGCB
                   && GCB_ZWJ != curGCB))
        {
            bSeenExtPictExtendZWJ = false;
        }

        prevGCB = curGCB;
        pCur = pNextEnd;
        nPoints++;
    }

    return mux_cursor(pCur - src, nPoints);
}

// ---------------------------------------------------------------------------
// utf8_clusters_before: Count grapheme clusters in src[0..nBefore-1].
// ---------------------------------------------------------------------------

size_t utf8_clusters_before(const UTF8 *src, size_t nSrc, size_t nBefore)
{
    size_t nClusters = 0;
    size_t nConsumed = 0;

    if (nBefore > nSrc)
    {
        nBefore = nSrc;
    }

    while (nConsumed < nBefore)
    {
        mux_cursor c = utf8_next_grapheme(src + nConsumed, nSrc - nConsumed);
        if (0 == c.m_byte)
        {
            break;
        }
        if (nConsumed + c.m_byte > nBefore)
        {
            // The cluster straddles the boundary — it starts before nBefore.
            break;
        }
        nConsumed += c.m_byte;
        nClusters++;
    }
    return nClusters;
}

// ---------------------------------------------------------------------------
// utf8_cluster_count: Count grapheme clusters in a UTF-8 string.
// ---------------------------------------------------------------------------

size_t utf8_cluster_count(const UTF8 *src, size_t nSrc)
{
    size_t nClusters = 0;
    size_t nConsumed = 0;

    while (nConsumed < nSrc)
    {
        mux_cursor c = utf8_next_grapheme(src + nConsumed, nSrc - nConsumed);
        if (0 == c.m_byte)
        {
            break;
        }
        nConsumed += c.m_byte;
        nClusters++;
    }
    return nClusters;
}
