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
 * That invariant is enforced, not merely documented.  Two live users would
 * interleave their word tables and produce silently wrong output -- a worse
 * failure than the stack exhaustion this replaces -- so CWordScratch asserts on
 * a double claim.  If a future change gives one of these functions a way to
 * evaluate softcode, the assertion fires during testing instead of the game
 * quietly mangling lists.
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
        mux_assert(!g_word_scratch_busy);
        g_word_scratch_busy = true;
    }

    ~CWordScratch()
    {
        g_word_scratch_busy = false;
    }

    size_t *starts(void) const { return g_word_starts; }
    size_t *ends(void) const   { return g_word_ends; }

private:
    CWordScratch(const CWordScratch &);
    CWordScratch &operator=(const CWordScratch &);
};

#endif // WORD_SCRATCH_H
