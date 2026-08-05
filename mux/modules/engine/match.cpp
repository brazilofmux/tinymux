/*! \file match.cpp
 * \brief Routines for parsing arguments that may refer to objects.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

// Player-facing match prose is opt-in M_() (#1444). Softcode/machine
// tokens use S_ so NLS cannot change the softcode ABI (#1419).
//
// Static init stores English msgids with N_() (no gettext). After
// mux_nls_init, mux_nls_refresh_messages() rebinds via M_().
//
const UTF8 *NOMATCH_MESSAGE      = N_("I don’t see that here.");
const UTF8 *AMBIGUOUS_MESSAGE    = N_("I don’t know which one you mean!");
const UTF8 *NOPERM_MESSAGE       = N_("Permission denied.");
const UTF8 *FUNC_FAIL_MESSAGE    = S_("#-1");
const UTF8 *FUNC_NOMATCH_MESSAGE = S_("#-1 NO MATCH");
const UTF8 *OUT_OF_RANGE         = S_("#-1 OUT OF RANGE");
const UTF8 *FUNC_NOT_FOUND       = S_("#-1 NOT FOUND");
const UTF8 *FUNC_AMBIGUOUS       = S_("#-2 AMBIGUOUS");
const UTF8 *FUNC_NOPERM_MESSAGE  = S_("#-1 PERMISSION DENIED");
const UTF8 *OUT_OF_MEMORY        = S_("#-1 OUT OF MEMORY");
const UTF8 *NOT_CONNECTED        = S_("#-1 NOT CONNECTED");

void mux_nls_refresh_messages(void)
{
#if defined(HAVE_NLS)
    // Re-resolve opted-in prose now that textdomain is bound.
    //
    NOMATCH_MESSAGE   = M_("I don’t see that here.");
    AMBIGUOUS_MESSAGE = M_("I don’t know which one you mean!");
    NOPERM_MESSAGE    = M_("Permission denied.");
#endif
}

#define CON_LOCAL       0x01    // Match is near me.
#define CON_TYPE        0x02    // Match is of requested type.
#define CON_LOCK        0x04    // I pass the lock on match.
#define CON_COMPLETE    0x08    // Name given is the full name.
#define CON_TOKEN       0x10    // Name is a special token.
#define CON_DBREF       0x20    // Name is a dbref.

static MSTATE md;

// ---------------------------------------------------------------------------
// Exact-name index (#2058).
//
// match_list() compares the query against every object in a contents list,
// and cannot stop at the first hit because ambiguity has to be detected.  At
// 1000 objects in scope that is ~27-40us per lookup on top of a ~26us dbref
// floor, and name resolution is 66-75% of the attribute phases once storage
// is out of the way.
//
// THE INDEX IS A FILTER, NOT THE DECIDER.  string_compare() folds Unicode
// case through mux_tolower(), whose results are not a byte-for-byte map, and
// reimplementing that to build a hash key would be an approximation that
// silently disagrees with the walk on some name.  So the bucket key is only
// an ASCII normalisation, every candidate it produces is still confirmed with
// the real string_compare(), and any name containing a byte >= 0x80 is not
// bucketed at all -- it goes on a must-scan list.  A wrong key can therefore
// cost a fallback, never a wrong answer.
//
// Skipping the rest of the walk on a hit is sound because an exact match
// scores CON_COMPLETE|CON_LOCAL (0x09 minimum) and a partial match tops out
// at CON_LOCAL|CON_TYPE|CON_LOCK (0x07): exact always outranks partial.
//
// Only used when md.check_keys is false.  With it set, promote_match()
// evaluates could_doit() per candidate -- softcode -- and skipping the walk
// would skip those evaluations.  Exit and movement matching (the only
// check_keys users) therefore keep the existing behaviour exactly.
//
struct NameIndex
{
    std::unordered_map<std::string, std::vector<dbref>> exact;
    std::vector<dbref> unbucketed;   // names with any byte >= 0x80
};
static std::unordered_map<dbref, NameIndex> s_name_index;

// ASCII normalisation mirroring what string_compare() ignores: leading space,
// runs of space, and ASCII case.  Returns false if the string carries any
// byte >= 0x80, in which case the caller must not bucket it.
static bool name_bucket_key(const UTF8 *s, std::string &out)
{
    out.clear();
    if (nullptr == s)
    {
        return false;
    }
    if (g_space_compress)
    {
        while (mux_isspace(*s))
        {
            s++;
        }
    }
    bool bPendingSpace = false;
    for (; *s; s++)
    {
        if (*s >= 0x80)
        {
            return false;
        }
        if (g_space_compress && mux_isspace(*s))
        {
            bPendingSpace = true;
            continue;
        }
        if (bPendingSpace)
        {
            out.push_back(' ');
            bPendingSpace = false;
        }
        out.push_back(static_cast<char>(mux_tolower_ascii[*s]));
    }
    // A trailing space run is dropped, matching string_compare()'s tail
    // handling of one side running out while the other holds only spaces.
    return true;
}

void name_index_invalidate(dbref container)
{
    if (  !s_name_index.empty()
       && Good_obj(container))
    {
        s_name_index.erase(container);
    }
}

void name_index_invalidate_all(void)
{
    s_name_index.clear();
}

static const NameIndex *name_index_get(dbref container, dbref first)
{
    const auto it = s_name_index.find(container);
    if (it != s_name_index.end())
    {
        return &it->second;
    }

    NameIndex idx;
    std::string key;
    dbref t;
    DOLIST(t, first)
    {
        if (name_bucket_key(PureName(t), key))
        {
            idx.exact[key].push_back(t);
        }
        else
        {
            idx.unbucketed.push_back(t);
        }
    }
    return &(s_name_index[container] = std::move(idx));
}

static void promote_match(dbref what, int confidence)
{
#ifdef REALITY_LVLS
    // Check is the object is visible.
    //
    if (  Good_obj(what)
       && (confidence & CON_LOCAL)
       && !IsReal(md.player, what)
       && what != Location(md.player))
    {
        return;
    }
#endif // REALITY_LVLS
    // Check for type and locks, if requested.
    //
    if (md.pref_type != NOTYPE)
    {
        if (  Good_obj(what)
           && Typeof(what) == md.pref_type)
        {
            confidence |= CON_TYPE;
        }
    }
    if (md.check_keys)
    {
        MSTATE save_md;

        save_match_state(&save_md);
        if (  Good_obj(what)
           && could_doit(md.player, what, A_LOCK))
        {
            confidence |= CON_LOCK;
        }
        restore_match_state(&save_md);
    }

    // If nothing matched, take it.
    //
    if (md.count == 0)
    {
        md.match = what;
        md.confidence = confidence;
        md.count = 1;
        return;
    }

    // If confidence is lower, ignore.
    //
    if (confidence < md.confidence)
    {
        return;
    }

    // If confidence is higher, replace.
    //
    if (confidence > md.confidence)
    {
        md.match = what;
        md.confidence = confidence;
        md.count = 1;
        return;
    }

    // Equal confidence, pick randomly.
    //
    md.count++;
    if (RandomINT32(1,md.count) == 1)
    {
        md.match = what;
    }
    return;
}
/*
 * ---------------------------------------------------------------------------
 * * This function removes repeated spaces from the template to which object
 * * names are being matched.  It also removes inital and terminal spaces.
 */

static UTF8 *munge_space_for_match(const UTF8 *name, size_t n)
{
    thread_local UTF8 buffer[LBUF_SIZE];

    size_t i = 0;
    UTF8 *q = buffer;

    if (nullptr != name)
    {
        // Remove Initial spaces.
        //
        while (mux_isspace(name[i]))
        {
            i++;
        }

        while (i < n)
        {
            while (  i < n
                  && !mux_isspace(name[i]))
            {
                safe_chr(name[i], buffer, &q);
                i++;
            }

            while (mux_isspace(name[i]))
            {
                i++;
            }

            if (i < n)
            {
                safe_chr(' ', buffer, &q);
            }
        }
    }

    // Remove terminal spaces and terminate string.
    //
    *q = '\0';
    return buffer;
}

void match_player(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
    if (Good_obj(md.absolute_form) && isPlayer(md.absolute_form))
    {
        promote_match(md.absolute_form, CON_DBREF);
        return;
    }
    if (*md.string == LOOKUP_TOKEN)
    {
        UTF8 *p;
        for (p = md.string + 1; mux_isspace(*p); p++)
        {
            ; // Nothing.
        }
        dbref match = lookup_player(NOTHING, p, true);
        if (Good_obj(match))
        {
            promote_match(match, CON_TOKEN);
        }
    }
}

// Check for a matching named reference.
//
static dbref absolute_named_reference(UTF8 *name)
{
    if (  nullptr == name
       || '\0' == name[0])
    {
        return NOTHING;
    }

    LBuf pReferenceName = LBuf_Src("absolute_named_reference");
    // mux_snprintf returns length written (0..count-1), not "would have".
    //
    size_t nReferenceName;
    if ('_' == name[0])
    {
        nReferenceName = mux_snprintf(pReferenceName.get(), LBUF_SIZE,
            T("%s"), name);
    }
    else
    {
        nReferenceName = mux_snprintf(pReferenceName.get(), LBUF_SIZE,
            T("%s.%lld"), name, static_cast<long long>(md.player));
    }

    auto it = mudstate.reference_htab.find(std::vector<UTF8>(pReferenceName.get(), pReferenceName.get() + nReferenceName));
    struct reference_entry *result = (it != mudstate.reference_htab.end()) ? static_cast<struct reference_entry *>(it->second) : nullptr;

    if (  nullptr != result
       && Good_obj(result->target))
    {
        return result->target;
    }
    else
    {
        return NOTHING;
    }
}

/*
 * returns nnn if name = #nnn, else NOTHING
 */
static dbref absolute_name(bool bNeedPound)
{
    UTF8 *mname = md.string;
    if (bNeedPound)
    {
        if (*mname != NUMBER_TOKEN)
        {
            return NOTHING;
        }
        else
        {
            mname++;
        }

        if (  '_'  == mname[0]
           && '\0' != mname[1])
        {
            return absolute_named_reference(mname + 1);
        }
    }
    if (*mname)
    {
        dbref match = parse_dbref(mname);
        if (Good_obj(match))
        {
            return match;
        }
    }
    return NOTHING;
}

void match_absolute(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
    if (Good_obj(md.absolute_form))
    {
        promote_match(md.absolute_form, CON_DBREF);
    }
}

static void match_numeric(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
    dbref match = absolute_name(false);
    if (Good_obj(match))
    {
        promote_match(match, CON_DBREF);
    }
}

void match_me(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
    if (  Good_obj(md.absolute_form)
       && md.absolute_form == md.player)
    {
        promote_match(md.player, CON_DBREF | CON_LOCAL);
        return;
    }
    if (!string_compare(md.string, T("me")))
    {
        promote_match(md.player, CON_TOKEN | CON_LOCAL);
    }
    return;
}

static void match_home(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
    if (!string_compare(md.string, T("home")))
    {
        promote_match(HOME, CON_TOKEN);
    }
    return;
}

void match_here(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
    if (  Good_obj(md.player)
       && Has_location(md.player))
    {
        dbref loc = Location(md.player);
        if (Good_obj(loc))
        {
            if (loc == md.absolute_form)
            {
                promote_match(loc, CON_DBREF | CON_LOCAL);
            }
            else if (!string_compare(md.string, T("here")))
            {
                promote_match(loc, CON_TOKEN | CON_LOCAL);
            }
            else if (!string_compare(md.string, PureName(loc)))
            {
                promote_match(loc, CON_COMPLETE | CON_LOCAL);
            }
        }
    }
}

static void match_list(dbref container, dbref first, int local)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }

    // Exact-name index (#2058).  Only when check_keys is off -- see the note
    // on s_name_index -- and only when nothing in this container has a name
    // the bucket key cannot represent, since a Unicode fold could bring such
    // a name into equality with an ASCII query.
    if (  !md.check_keys
       && Good_obj(container))
    {
        std::string key;
        if (name_bucket_key(md.string, key))
        {
            const NameIndex *pIdx = name_index_get(container, first);
            if (pIdx->unbucketed.empty())
            {
                const auto hit = pIdx->exact.find(key);
                if (hit != pIdx->exact.end())
                {
                    // Confirm with the real comparison before promoting: the
                    // bucket is a filter, not the decision.
                    bool bAny = false;
                    for (const dbref cand : hit->second)
                    {
                        if (cand == md.absolute_form)
                        {
                            promote_match(cand, CON_DBREF | local);
                            return;
                        }
                        if (!string_compare(PureName(cand), md.string))
                        {
                            promote_match(cand, CON_COMPLETE | local);
                            bAny = true;
                        }
                    }
                    // An exact match outranks every partial one, so the rest
                    // of the list cannot change the answer.
                    if (bAny)
                    {
                        return;
                    }
                }
            }
        }
    }

    const bool ascii_query = (*md.string != '\0' && *md.string < 0x80);
    const UTF8 query0_lower = ascii_query
        ? mux_tolower_ascii[*md.string]
        : 0;

    DOLIST(first, first)
    {
        if (first == md.absolute_form)
        {
            promote_match(first, CON_DBREF | local);
            return;
        }
        /*
         * Warning: make sure there are no other calls to Name() in
         * promote_match or its called subroutines; they
         * would overwrite Name()'s static buffer which is
         * needed by string_match().
         */
        const UTF8 *namebuf = PureName(first);
        bool exact_first_char_mismatch = false;
        if (ascii_query)
        {
            const UTF8 *name0 = namebuf;
            if (g_space_compress)
            {
                while (mux_isspace(*name0))
                {
                    name0++;
                }
            }

            exact_first_char_mismatch =
                (*name0 < 0x80
                 && mux_tolower_ascii[*name0] != query0_lower);
        }

        if (!exact_first_char_mismatch
            && !string_compare(namebuf, md.string))
        {
            promote_match(first, CON_COMPLETE | local);
        }
        else if (string_match(namebuf, md.string))
        {
            promote_match(first, local);
        }
    }
}

void match_possession(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
    if (Good_obj(md.player) && Has_contents(md.player))
    {
        match_list(md.player, Contents(md.player), CON_LOCAL);
    }
}

void match_neighbor(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
    if (  Good_obj(md.player)
       && Has_location(md.player))
    {
        dbref loc = Location(md.player);
        if (Good_obj(loc))
        {
            match_list(loc, Contents(loc), CON_LOCAL);
        }
    }
}

static bool match_exit_internal(dbref loc, dbref baseloc, int local)
{
    if (  !Good_obj(loc)
       || !Has_exits(loc))
    {
        return true;
    }

    dbref exit;
    bool result = false;
    int key;

    DOLIST(exit, Exits(loc))
    {
        if (exit == md.absolute_form)
        {
            key = 0;
            if (Examinable(md.player, loc))
            {
                key |= VE_LOC_XAM;
            }
            if (Dark(loc))
            {
                key |= VE_LOC_DARK;
            }
            if (Dark(baseloc))
            {
                key |= VE_BASE_DARK;
            }
            if (exit_visible(exit, md.player, key))
            {
                promote_match(exit, CON_DBREF | local);
                return true;
            }
        }
        if (matches_exit_from_list(md.string, PureName(exit)))
        {
            promote_match(exit, CON_COMPLETE | local);
            result = true;
        }
    }
    return result;
}

void match_exit(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }

    dbref loc = Location(md.player);
    if (  Good_obj(md.player)
       && Has_location(md.player))
    {
        (void)match_exit_internal(loc, loc, CON_LOCAL);
    }
}

void match_exit_with_parents(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
    if (  Good_obj(md.player)
       && Has_location(md.player))
    {
        dbref parent;
        int lev;
        dbref loc = Location(md.player);
        ITER_PARENTS(loc, parent, lev)
        {
            if (match_exit_internal(parent, loc, CON_LOCAL))
            {
                break;
            }
        }
    }
}

void match_carried_exit(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
    if (  Good_obj(md.player)
       && Has_exits(md.player))
    {
        (void)match_exit_internal(md.player, md.player, CON_LOCAL);
    }
}

void match_carried_exit_with_parents(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
    if (  Good_obj(md.player)
       && Has_exits(md.player))
    {
        dbref parent;
        int lev;
        ITER_PARENTS(md.player, parent, lev)
        {
            if (match_exit_internal(parent, md.player, CON_LOCAL))
            {
                break;
            }
        }
    }
}

void match_master_exit(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
    if (  Good_obj(md.player)
       && Has_exits(md.player))
    {
        (void)match_exit_internal(mudconf.master_room, mudconf.master_room, 0);
    }
}

void match_zone_exit(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
    if (  Good_obj(md.player)
       && Has_exits(md.player))
    {
        (void)match_exit_internal(Zone(md.player), Zone(md.player), 0);
    }
}

void match_everything(int key)
{
    /*
     * Try matching me, then here, then absolute, then player FIRST, since
     * this will hit most cases. STOP if we get something, since those are
     * exact matches.
     */

    match_me();
    match_here();
    match_absolute();
    if (key & MAT_NUMERIC)
    {
        match_numeric();
    }
    if (key & MAT_HOME)
    {
        match_home();
    }
    match_player();
    if (md.confidence >= CON_TOKEN)
    {
        return;
    }

    if (!(key & MAT_NO_EXITS))
    {
        if (key & MAT_EXIT_PARENTS)
        {
            match_carried_exit_with_parents();
            match_exit_with_parents();
        }
        else
        {
            match_carried_exit();
            match_exit();
        }
    }
    match_neighbor();
    match_possession();
}

dbref match_result(void)
{
    switch (md.count)
    {
    case 0:
        return NOTHING;

    case 1:
        return md.match;

    default:
        return AMBIGUOUS;
    }
}

// Use this if you don't care about ambiguity.
//
dbref last_match_result(void)
{
    return md.match;
}

dbref match_status(dbref player, dbref match)
{
    switch (match)
    {
    case NOTHING:
        notify(player, NOMATCH_MESSAGE);
        return NOTHING;

    case AMBIGUOUS:
        notify(player, AMBIGUOUS_MESSAGE);
        return NOTHING;

    case NOPERM:
        notify(player, NOPERM_MESSAGE);
        return NOTHING;
    }
    return match;
}

dbref noisy_match_result(void)
{
    return match_status(md.player, match_result());
}

void save_match_state(MSTATE *mstate)
{
    mstate->confidence = md.confidence;
    mstate->count = md.count;
    mstate->pref_type = md.pref_type;
    mstate->check_keys = md.check_keys;
    mstate->absolute_form = md.absolute_form;
    mstate->match = md.match;
    mstate->player = md.player;
    mstate->string = alloc_lbuf("save_match_state");
    mux_strncpy(mstate->string, md.string, LBUF_SIZE-1);
}

void restore_match_state(MSTATE *mstate)
{
    md.confidence = mstate->confidence;
    md.count = mstate->count;
    md.pref_type = mstate->pref_type;
    md.check_keys = mstate->check_keys;
    md.absolute_form = mstate->absolute_form;
    md.match = mstate->match;
    md.player = mstate->player;
    mux_strncpy(md.string, mstate->string, LBUF_SIZE-1);
    free_lbuf(mstate->string);
}

void init_match(dbref player, const UTF8 *name, int type)
{
    md.confidence = -1;
    md.count = 0;
    md.check_keys = false;
    md.pref_type = type;
    md.match = NOTHING;
    md.player = player;
    md.string = munge_space_for_match(name, strlen(reinterpret_cast<const char *>(name)));
    md.absolute_form = absolute_name(true);
}

void init_match(dbref player, const UTF8 *name, size_t n, int type)
{
    md.confidence = -1;
    md.count = 0;
    md.check_keys = false;
    md.pref_type = type;
    md.match = NOTHING;
    md.player = player;
    md.string = munge_space_for_match(name, n);
    md.absolute_form = absolute_name(true);
}

void init_match_check_keys(dbref player, const UTF8 *name, int type)
{
    init_match(player, name, type);
    md.check_keys = true;
}

void init_match_check_keys(dbref player, const UTF8 *name, size_t n, int type)
{
    init_match(player, name, n, type);
    md.check_keys = true;
}

dbref match_thing(dbref player, const UTF8 *name)
{
    init_match(player, name, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    return noisy_match_result();
}

dbref match_thing_quiet(dbref player, const UTF8 *name)
{
    init_match(player, name, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    return match_result();
}

dbref match_thing_quiet(dbref player, const UTF8 *name, size_t n)
{
    init_match(player, name, n, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    return match_result();
}

void safe_match_result(dbref it, UTF8 *buff, UTF8 **bufc)
{
    if (it == AMBIGUOUS)
    {
        safe_ambiguous(buff, bufc);
    }
    else
    {
        safe_notfound(buff, bufc);
    }
}
