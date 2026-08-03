/*! \file word_scratch.h
 * \brief Shared scratch for co_split_words() callers.
 *
 * co_split_words() reports one (start, end) pair per word, so its output
 * arrays must be dimensioned LBUF_SIZE.  At LBUF_SIZE 32768 that is 512 KB,
 * which is far too much to place on the stack of every function that splits a
 * list -- and the functions that do it are ordinary list functions called on
 * every evaluation, not rare paths.
 *
 * The storage is shared rather than per-function because no two users are ever
 * live at the same time.  Every user is a leaf with respect to softcode: none
 * of them evaluate (no ast_exec/mux_exec/jit_eval) and none call each other, so
 * a second activation cannot come into existence while a first one holds the
 * scratch.
 *
 * That invariant is not merely documented.  Two live users would interleave
 * their word tables and produce silently wrong output -- a worse failure than
 * the stack exhaustion this replaces -- so a second claim is served from its
 * own private tables instead of the shared ones.  If a future change gives one
 * of these functions a way to evaluate softcode, it gets correct output rather
 * than either corruption or a dead game.
 *
 * Callers must take their bound from capacity() rather than LBUF_SIZE: when a
 * nested claim cannot allocate, capacity() is 0, and the caller reports an
 * empty or "too long" result the way it already does for an over-long list.
 */

#ifndef WORD_SCRATCH_H
#define WORD_SCRATCH_H

// Defined once in functions.cpp.  Declared extern rather than defined here so
// that every translation unit shares one copy; a static definition in a header
// would give each its own, which is exactly what this is avoiding.
//
extern thread_local size_t g_word_starts[LBUF_SIZE];
extern thread_local size_t g_word_ends[LBUF_SIZE];
extern thread_local bool   g_word_scratch_busy;

class CWordScratch
{
public:
    CWordScratch(void)
    {
        if (!g_word_scratch_busy)
        {
            g_word_scratch_busy = true;
            m_bShared   = true;
            m_pStarts   = g_word_starts;
            m_pEnds     = g_word_ends;
            m_nCapacity = LBUF_SIZE;
            return;
        }

        // Already claimed.  Hand out private tables: sharing them would
        // interleave two word lists and quietly corrupt both.
        //
        m_bShared = false;
        m_pStarts = static_cast<size_t *>(MEMALLOC(sizeof(size_t) * LBUF_SIZE));
        m_pEnds   = static_cast<size_t *>(MEMALLOC(sizeof(size_t) * LBUF_SIZE));
        if (  nullptr != m_pStarts
           && nullptr != m_pEnds)
        {
            m_nCapacity = LBUF_SIZE;
        }
        else
        {
            // No memory for a private copy.  Report room for no words rather
            // than writing into someone else's table; co_split_words() returns
            // 0 for a zero bound, so the caller yields an empty result.
            //
            MEMFREE(m_pStarts);
            MEMFREE(m_pEnds);
            m_pStarts   = nullptr;
            m_pEnds     = nullptr;
            m_nCapacity = 0;
        }
    }

    ~CWordScratch()
    {
        if (m_bShared)
        {
            g_word_scratch_busy = false;
        }
        else
        {
            MEMFREE(m_pStarts);
            MEMFREE(m_pEnds);
        }
    }

    size_t *starts(void) const   { return m_pStarts; }
    size_t *ends(void) const     { return m_pEnds; }
    size_t  capacity(void) const { return m_nCapacity; }

private:
    CWordScratch(const CWordScratch &);
    CWordScratch &operator=(const CWordScratch &);

    bool    m_bShared;
    size_t *m_pStarts;
    size_t *m_pEnds;
    size_t  m_nCapacity;
};

#endif // WORD_SCRATCH_H
