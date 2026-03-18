/*
 * trigger_match.rl — Multi-pattern trigger matching engine.
 *
 * Ragel -G2 generates the pattern parser.  The matching engine is
 * pure C: Aho-Corasick for simultaneous literal matching, plus a
 * backtracking glob matcher for wildcard patterns.
 *
 * Build:  ragel -G2 -C -o trigger_match.c trigger_match.rl
 */

#define _POSIX_C_SOURCE 200809L
#include "trigger_match.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  ASCII case folding                                                 */
/* ------------------------------------------------------------------ */

static inline unsigned char ascii_lower(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32) : c;
}

/* ------------------------------------------------------------------ */
/*  Pattern segment types                                              */
/* ------------------------------------------------------------------ */

enum {
    SEG_LITERAL,       /* Single literal byte */
    SEG_STAR,          /* * — match any sequence */
    SEG_QUESTION,      /* ? or . — match any single byte */
    SEG_BOL,           /* ^ — anchor to start of line */
    SEG_EOL            /* $ — anchor to end of line */
};

typedef struct {
    uint8_t type;
    unsigned char ch;  /* For SEG_LITERAL */
} trig_seg_t;

#define MAX_SEGS 512

/* ------------------------------------------------------------------ */
/*  Ragel pattern parser                                               */
/* ------------------------------------------------------------------ */

%%{
    machine trigger_parse;
    alphtype unsigned char;

    action emit_literal {
        if (nsegs < max_segs) {
            segs[nsegs].type = SEG_LITERAL;
            segs[nsegs].ch = fc;
            nsegs++;
        }
    }

    action emit_star {
        if (nsegs < max_segs) {
            /* Collapse consecutive stars. */
            if (nsegs == 0 || segs[nsegs - 1].type != SEG_STAR) {
                segs[nsegs].type = SEG_STAR;
                nsegs++;
            }
        }
        *is_glob = 1;
    }

    action emit_question {
        if (nsegs < max_segs) {
            segs[nsegs].type = SEG_QUESTION;
            nsegs++;
        }
        *is_glob = 1;
    }

    action emit_dot {
        if (nsegs < max_segs) {
            segs[nsegs].type = SEG_QUESTION;
            nsegs++;
        }
        *is_glob = 1;
    }

    action emit_bol {
        if (nsegs == 0 && nsegs < max_segs) {
            segs[nsegs].type = SEG_BOL;
            nsegs++;
            *is_glob = 1;
        } else {
            *is_regex = 1;
        }
    }

    action emit_eol {
        /* $ at end of pattern = EOL anchor; elsewhere = regex. */
        eol_pending = 1;
        *is_glob = 1;
    }

    action emit_escaped {
        if (nsegs < max_segs) {
            segs[nsegs].type = SEG_LITERAL;
            segs[nsegs].ch = fc;
            nsegs++;
        }
    }

    action mark_regex {
        *is_regex = 1;
    }

    # Escaped character: backslash followed by any byte.
    escape = '\\' any @emit_escaped;

    # Glob wildcards.
    star     = '*' @emit_star;
    question = '?' @emit_question;
    dot      = '.' @emit_dot;

    # Anchors.
    caret  = '^' @emit_bol;
    dollar = '$' @emit_eol;

    # Characters that require std::regex fallback.
    regex_char = [+|(){}\[\]] @mark_regex;

    # Plain literal: anything not special.
    literal = (any - [*?\\+|(){}\[\]^$.]) @emit_literal;

    main := (escape | star | question | dot | caret | dollar
             | regex_char | literal)*;

    write data noerror nofinal;
}%%

/*
 * Parse a trigger pattern into segments.
 * Returns the number of segments, or -1 on error.
 * Sets *is_glob if the pattern has glob wildcards (* ? .).
 * Sets *is_regex if the pattern needs std::regex fallback.
 */
static int trigger_parse_pattern(const char *pattern, size_t plen,
                                 trig_seg_t *segs, int max_segs,
                                 int *is_glob, int *is_regex)
{
    int cs;
    const unsigned char *p  = (const unsigned char *)pattern;
    const unsigned char *pe = p + plen;
    int nsegs = 0;
    int eol_pending = 0;

    *is_glob  = 0;
    *is_regex = 0;

    %% write init;
    (void)trigger_parse_en_main;  /* Suppress unused-variable warning. */
    %% write exec;

    /* If $ was the last character, it's a valid EOL anchor. */
    if (eol_pending) {
        if (nsegs < max_segs) {
            segs[nsegs].type = SEG_EOL;
            nsegs++;
        }
    }

    return nsegs;
}

/* ------------------------------------------------------------------ */
/*  Aho-Corasick multi-pattern automaton                               */
/* ------------------------------------------------------------------ */

#define AC_ALPHA 256

typedef struct {
    int  go[AC_ALPHA];   /* Goto: byte → next state (-1 = undefined) */
    int  fail;           /* Failure link */
    int  match_id;       /* Trigger ID ending here (-1 = none) */
    int  dict_link;      /* Nearest match state via failure chain */
} ac_node_t;

typedef struct {
    ac_node_t *nodes;
    int        num;
    int        cap;
} ac_machine_t;

static void ac_init(ac_machine_t *ac)
{
    ac->cap = 64;
    ac->num = 0;
    ac->nodes = (ac_node_t *)calloc((size_t)ac->cap, sizeof(ac_node_t));
}

static int ac_new_state(ac_machine_t *ac)
{
    if (ac->num >= ac->cap) {
        ac->cap *= 2;
        ac->nodes = (ac_node_t *)realloc(ac->nodes,
                                         (size_t)ac->cap * sizeof(ac_node_t));
    }
    int s = ac->num++;
    memset(ac->nodes[s].go, -1, sizeof(ac->nodes[s].go));
    ac->nodes[s].fail      = 0;
    ac->nodes[s].match_id  = -1;
    ac->nodes[s].dict_link = -1;
    return s;
}

static void ac_free(ac_machine_t *ac)
{
    free(ac->nodes);
    ac->nodes = NULL;
    ac->num = ac->cap = 0;
}

/*
 * Insert a literal byte string into the trie.
 * Bytes are pre-folded to lowercase if icase.
 */
static void ac_insert(ac_machine_t *ac,
                      const unsigned char *pat, int patlen,
                      int trigger_id, int icase)
{
    int state = 0;  /* root */
    for (int i = 0; i < patlen; i++) {
        unsigned char c = icase ? ascii_lower(pat[i]) : pat[i];
        if (ac->nodes[state].go[c] < 0) {
            ac->nodes[state].go[c] = ac_new_state(ac);
        }
        state = ac->nodes[state].go[c];
    }
    ac->nodes[state].match_id = trigger_id;
}

/*
 * Build failure links and complete the goto function.
 * After this, go[state][byte] is always >= 0 for any state and byte.
 */
static void ac_build(ac_machine_t *ac)
{
    int *queue = (int *)malloc((size_t)ac->num * sizeof(int));
    int qh = 0, qt = 0;

    /* Root: undefined transitions loop back to root (state 0). */
    for (int c = 0; c < AC_ALPHA; c++) {
        int s = ac->nodes[0].go[c];
        if (s > 0) {
            ac->nodes[s].fail = 0;
            ac->nodes[s].dict_link = -1;
            queue[qt++] = s;
        } else {
            ac->nodes[0].go[c] = 0;
        }
    }

    /* BFS: compute failure links and complete goto. */
    while (qh < qt) {
        int u = queue[qh++];
        for (int c = 0; c < AC_ALPHA; c++) {
            int v = ac->nodes[u].go[c];
            if (v > 0) {
                int f = ac->nodes[ac->nodes[u].fail].go[c];
                ac->nodes[v].fail = f;
                ac->nodes[v].dict_link =
                    (ac->nodes[f].match_id >= 0) ? f
                                                 : ac->nodes[f].dict_link;
                queue[qt++] = v;
            } else {
                ac->nodes[u].go[c] = ac->nodes[ac->nodes[u].fail].go[c];
            }
        }
    }

    free(queue);
}

/*
 * Search text using the compiled AC automaton.
 * Returns number of distinct trigger IDs that matched.
 * Uses a bitmask to avoid duplicate reports.
 */
static int ac_search(const ac_machine_t *ac,
                     const unsigned char *text, size_t tlen,
                     int icase,
                     trigger_match_t *results, int max_results)
{
    int n = 0;
    /* Bitmask for dedup (TRIGGER_MAX <= 256, use 256 bits = 32 bytes). */
    uint8_t seen[TRIGGER_MAX / 8];
    memset(seen, 0, sizeof(seen));

    int state = 0;
    for (size_t i = 0; i < tlen && n < max_results; i++) {
        unsigned char c = icase ? ascii_lower(text[i]) : text[i];
        state = ac->nodes[state].go[c];

        /* Check for matches at this state and via dict_link chain. */
        int tmp = state;
        while (tmp > 0 && n < max_results) {
            if (ac->nodes[tmp].match_id >= 0) {
                int id = ac->nodes[tmp].match_id;
                int idx = id / 8;
                int bit = 1 << (id % 8);
                if (!(seen[idx] & bit)) {
                    seen[idx] |= (uint8_t)bit;
                    results[n].id = id;
                    results[n].offset = i;  /* End of match position. */
                    n++;
                }
            }
            tmp = ac->nodes[tmp].dict_link;
        }
    }

    return n;
}

/* ------------------------------------------------------------------ */
/*  Glob pattern matcher (backtracking)                                */
/* ------------------------------------------------------------------ */

typedef struct {
    int         id;
    int         flags;
    trig_seg_t *segs;
    int         nsegs;
    int         anchored_start;  /* Pattern began with ^ */
} glob_pattern_t;

/*
 * Try matching a glob pattern anchored at text[0..tlen).
 * The match need not consume all of text (unanchored end, unless EOL).
 */
static int glob_match_at(const trig_seg_t *segs, int nsegs,
                         const unsigned char *text, size_t tlen, int icase)
{
    int    si = 0;
    size_t ti = 0;
    int    save_si = -1;
    size_t save_ti = 0;

    for (;;) {
        if (si == nsegs) {
            return 1;  /* All segments consumed — match. */
        }

        switch (segs[si].type) {
        case SEG_STAR:
            save_si = si + 1;
            save_ti = ti;
            si++;
            continue;

        case SEG_EOL:
            if (ti == tlen) { si++; continue; }
            break;   /* Mismatch. */

        case SEG_LITERAL:
            if (ti < tlen) {
                unsigned char pc = icase ? ascii_lower(segs[si].ch) : segs[si].ch;
                unsigned char tc = icase ? ascii_lower(text[ti])    : text[ti];
                if (pc == tc) { si++; ti++; continue; }
            }
            break;   /* Mismatch. */

        case SEG_QUESTION:
            if (ti < tlen) { si++; ti++; continue; }
            break;   /* Mismatch. */

        default:
            break;
        }

        /* Backtrack to last STAR. */
        if (save_si >= 0 && save_ti < tlen) {
            save_ti++;
            si = save_si;
            ti = save_ti;
            continue;
        }

        return 0;  /* No match. */
    }
}

/*
 * Search for a glob pattern anywhere in text.
 * If anchored_start, only try position 0.
 */
static int glob_search(const glob_pattern_t *gp,
                       const unsigned char *text, size_t tlen)
{
    int icase = (gp->flags & TRIGGER_ICASE) != 0;

    if (gp->anchored_start) {
        return glob_match_at(gp->segs, gp->nsegs, text, tlen, icase);
    }

    for (size_t start = 0; start <= tlen; start++) {
        if (glob_match_at(gp->segs, gp->nsegs,
                          text + start, tlen - start, icase)) {
            return 1;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Stored trigger entry (before compilation)                          */
/* ------------------------------------------------------------------ */

typedef struct {
    int   id;
    char *pattern;
    int   flags;
    int   active;
} trigger_entry_t;

/* ------------------------------------------------------------------ */
/*  trigger_set struct                                                 */
/* ------------------------------------------------------------------ */

struct trigger_set {
    trigger_entry_t entries[TRIGGER_MAX];
    int             num_entries;

    /* Compiled AC machine for literal patterns. */
    ac_machine_t    ac;
    int             ac_icase;    /* Global icase for AC (all same). */
    int             ac_count;    /* Number of patterns in AC. */

    /* Compiled glob patterns for wildcard patterns. */
    glob_pattern_t *globs;
    int             num_globs;
    int             cap_globs;

    int             compiled;
};

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

trigger_set *trigger_set_create(void)
{
    trigger_set *ts = (trigger_set *)calloc(1, sizeof(trigger_set));
    return ts;
}

static void free_globs(trigger_set *ts)
{
    for (int i = 0; i < ts->num_globs; i++) {
        free(ts->globs[i].segs);
    }
    free(ts->globs);
    ts->globs     = NULL;
    ts->num_globs = 0;
    ts->cap_globs = 0;
}

void trigger_set_free(trigger_set *ts)
{
    if (!ts) return;

    for (int i = 0; i < ts->num_entries; i++) {
        free(ts->entries[i].pattern);
    }
    ac_free(&ts->ac);
    free_globs(ts);
    free(ts);
}

int trigger_set_add(trigger_set *ts, int id,
                    const char *pattern, int flags)
{
    if (!ts || id < 0 || id >= TRIGGER_MAX || !pattern) return -1;

    /* Check for existing entry with same ID and replace. */
    for (int i = 0; i < ts->num_entries; i++) {
        if (ts->entries[i].id == id && ts->entries[i].active) {
            free(ts->entries[i].pattern);
            ts->entries[i].pattern = strdup(pattern);
            ts->entries[i].flags   = flags;
            ts->compiled = 0;
            return 0;
        }
    }

    if (ts->num_entries >= TRIGGER_MAX) return -1;

    trigger_entry_t *e = &ts->entries[ts->num_entries++];
    e->id      = id;
    e->pattern = strdup(pattern);
    e->flags   = flags;
    e->active  = 1;
    ts->compiled = 0;
    return 0;
}

int trigger_set_remove(trigger_set *ts, int id)
{
    if (!ts) return -1;
    for (int i = 0; i < ts->num_entries; i++) {
        if (ts->entries[i].id == id && ts->entries[i].active) {
            ts->entries[i].active = 0;
            ts->compiled = 0;
            return 0;
        }
    }
    return -1;
}

int trigger_set_compile(trigger_set *ts)
{
    if (!ts) return -1;

    /* Tear down previous compilation. */
    ac_free(&ts->ac);
    free_globs(ts);
    ts->ac_count = 0;

    /* Initialize AC with root state. */
    ac_init(&ts->ac);
    ac_new_state(&ts->ac);   /* State 0 = root. */

    /* Classify each active entry and route to AC or glob. */
    for (int i = 0; i < ts->num_entries; i++) {
        trigger_entry_t *e = &ts->entries[i];
        if (!e->active) continue;

        trig_seg_t segs[MAX_SEGS];
        int is_glob  = 0;
        int is_regex = 0;
        size_t plen = strlen(e->pattern);

        int nsegs = trigger_parse_pattern(e->pattern, plen,
                                          segs, MAX_SEGS,
                                          &is_glob, &is_regex);
        if (nsegs < 0) continue;

        /* Regex patterns can't be handled here — skip.
         * The caller should check trigger_needs_regex() and use
         * std::regex for those patterns. */
        if (is_regex) continue;

        int icase = (e->flags & TRIGGER_ICASE) != 0;

        if (!is_glob) {
            /* Pure literal: insert into AC automaton. */
            unsigned char lit[MAX_SEGS];
            int llen = 0;
            for (int j = 0; j < nsegs; j++) {
                if (segs[j].type == SEG_LITERAL && llen < MAX_SEGS) {
                    lit[llen++] = segs[j].ch;
                }
            }
            ac_insert(&ts->ac, lit, llen, e->id, icase);
            ts->ac_icase = icase;
            ts->ac_count++;
        } else {
            /* Glob: compile to glob_pattern_t. */
            if (ts->num_globs >= ts->cap_globs) {
                ts->cap_globs = ts->cap_globs ? ts->cap_globs * 2 : 16;
                ts->globs = (glob_pattern_t *)realloc(
                    ts->globs,
                    (size_t)ts->cap_globs * sizeof(glob_pattern_t));
            }
            glob_pattern_t *gp = &ts->globs[ts->num_globs++];
            gp->id    = e->id;
            gp->flags = e->flags;

            /* Check for BOL anchor. */
            int seg_start = 0;
            gp->anchored_start = 0;
            if (nsegs > 0 && segs[0].type == SEG_BOL) {
                gp->anchored_start = 1;
                seg_start = 1;
            }

            int seg_count = nsegs - seg_start;
            gp->segs  = (trig_seg_t *)malloc((size_t)seg_count * sizeof(trig_seg_t));
            gp->nsegs = seg_count;
            memcpy(gp->segs, segs + seg_start,
                   (size_t)seg_count * sizeof(trig_seg_t));
        }
    }

    /* Build AC failure links. */
    if (ts->ac_count > 0) {
        ac_build(&ts->ac);
    }

    ts->compiled = 1;
    return 0;
}

int trigger_set_search(const trigger_set *ts,
                       const char *text, size_t len,
                       trigger_match_t *results, int max_results)
{
    if (!ts || !ts->compiled || !text || max_results <= 0) return 0;

    int n = 0;

    /* AC search for literal patterns. */
    if (ts->ac_count > 0) {
        n = ac_search(&ts->ac,
                      (const unsigned char *)text, len,
                      ts->ac_icase,
                      results, max_results);
    }

    /* Glob search for wildcard patterns. */
    for (int i = 0; i < ts->num_globs && n < max_results; i++) {
        if (glob_search(&ts->globs[i],
                        (const unsigned char *)text, len)) {
            results[n].id     = ts->globs[i].id;
            results[n].offset = 0;   /* Glob doesn't track offset. */
            n++;
        }
    }

    return n;
}

int trigger_needs_regex(const char *pattern)
{
    if (!pattern) return 0;

    trig_seg_t segs[MAX_SEGS];
    int is_glob  = 0;
    int is_regex = 0;

    trigger_parse_pattern(pattern, strlen(pattern),
                          segs, MAX_SEGS,
                          &is_glob, &is_regex);
    return is_regex;
}
