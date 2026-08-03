/*! \file list_scratch.h
 * \brief Shared scratch for list2arr() callers.
 *
 * list2arr() stores one pointer per list element, so its output array must be
 * dimensioned LBUF_SIZE/2 -- one element per two bytes is the worst case, since
 * each element needs at least a separator between it and the next.  At
 * LBUF_SIZE 32768 that is 16,384 pointers, or 128 KB per array, and the
 * functions that split lists are ordinary ones called on every evaluation.
 *
 * Two slots because fun_unique needs two tables live at once (the elements and
 * the set of already-seen elements).  Every other user needs one.
 *
 * Shared because no two users are ever live at the same time: all of them are
 * leaves with respect to softcode -- none evaluate (no ast_exec/mux_exec/
 * jit_eval) and none call each other -- so a second activation cannot come into
 * existence while a first holds the scratch.
 *
 * Note this is deliberately NOT used by fun_sortkey, which splits lists the
 * same way but evaluates softcode per element and can therefore re-enter
 * itself.  That one allocates per activation instead; see #1992.
 *
 * CListScratch enforces the invariant rather than documenting it.  Two live
 * users would interleave their element tables and produce silently wrong list
 * output, so claiming the scratch twice trips the assertion instead.
 */

#ifndef LIST_SCRATCH_H
#define LIST_SCRATCH_H

// Defined once in functions.cpp; see word_scratch.h for the same reasoning
// about extern-versus-static in a header.
//
extern thread_local UTF8 *g_list_ptrs_a[LBUF_SIZE / 2];
extern thread_local UTF8 *g_list_ptrs_b[LBUF_SIZE / 2];
extern thread_local bool  g_list_scratch_busy;

class CListScratch
{
public:
    CListScratch(void)
    {
        mux_assert(!g_list_scratch_busy);
        g_list_scratch_busy = true;
    }

    ~CListScratch()
    {
        g_list_scratch_busy = false;
    }

    UTF8 **a(void) const { return g_list_ptrs_a; }
    UTF8 **b(void) const { return g_list_ptrs_b; }

private:
    CListScratch(const CListScratch &);
    CListScratch &operator=(const CListScratch &);
};

#endif // LIST_SCRATCH_H
