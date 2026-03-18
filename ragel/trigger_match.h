/*
 * trigger_match.h — Multi-pattern trigger matching engine.
 *
 * Replaces per-trigger std::regex_search with a single-pass Aho-Corasick
 * automaton for literal patterns and a backtracking glob matcher for
 * wildcard patterns.  Complex PCRE-style patterns (alternation, groups,
 * lookahead, backreferences) are flagged for std::regex fallback.
 *
 * Design: same committed-C-artifact pattern as color_ops.rl.
 * The .rl source lives in ragel/, Ragel -G2 generates trigger_match.c,
 * and that generated file is checked in so builds don't need Ragel.
 */

#ifndef TRIGGER_MATCH_H
#define TRIGGER_MATCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum triggers in a single set. */
#define TRIGGER_MAX 256

/* Pattern flags. */
#define TRIGGER_ICASE   0x01   /* Case-insensitive (default for MUD triggers) */

/* Opaque trigger set. */
typedef struct trigger_set trigger_set;

/* Match result for one trigger. */
typedef struct {
    int id;             /* Caller-assigned trigger ID */
    size_t offset;      /* Byte offset in text where match starts */
} trigger_match_t;

/*
 * trigger_set_create — Allocate an empty trigger set.
 */
trigger_set *trigger_set_create(void);

/*
 * trigger_set_free — Destroy a trigger set and all compiled state.
 */
void trigger_set_free(trigger_set *ts);

/*
 * trigger_set_add — Add a trigger pattern.
 *
 * id:      Caller-assigned ID (0..TRIGGER_MAX-1).
 * pattern: Trigger pattern string (glob or literal).
 * flags:   TRIGGER_ICASE etc.
 *
 * Returns 0 on success, -1 on error (bad id, too many, etc.).
 * Invalidates any prior compilation — call trigger_set_compile() again.
 */
int trigger_set_add(trigger_set *ts, int id,
                    const char *pattern, int flags);

/*
 * trigger_set_remove — Remove a trigger by ID.
 *
 * Returns 0 on success, -1 if not found.
 * Invalidates any prior compilation.
 */
int trigger_set_remove(trigger_set *ts, int id);

/*
 * trigger_set_compile — Build the matching automaton.
 *
 * Must be called after add/remove and before search.
 * Returns 0 on success, -1 on error.
 */
int trigger_set_compile(trigger_set *ts);

/*
 * trigger_set_search — Match text against all compiled triggers.
 *
 * text:        Input line (not necessarily NUL-terminated).
 * len:         Length of text in bytes.
 * results:     Output array for matches.
 * max_results: Capacity of results[].
 *
 * Returns number of matches written to results[].
 * Matching is unanchored (substring search) unless the pattern
 * starts with ^ or ends with $.
 */
int trigger_set_search(const trigger_set *ts,
                       const char *text, size_t len,
                       trigger_match_t *results, int max_results);

/*
 * trigger_needs_regex — Check if a pattern needs std::regex fallback.
 *
 * Returns nonzero if the pattern uses features beyond literal/glob
 * (alternation, groups, quantifiers other than *, lookahead, etc.).
 * The caller should route these patterns to std::regex instead.
 */
int trigger_needs_regex(const char *pattern);

#ifdef __cplusplus
}
#endif

#endif /* TRIGGER_MATCH_H */
