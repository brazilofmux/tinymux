/*! \file match.cpp
 * \brief Routines for parsing arguments that may refer to objects.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "powers.h"
#ifdef REALITY_LVLS
#include "levels.h"
#endif // REALITY_LVLS

const UTF8 *NOMATCH_MESSAGE      = T("I don\xE2\x80\x99t see that here.");
const UTF8 *AMBIGUOUS_MESSAGE    = T("I don\xE2\x80\x99t know which one you mean!");
const UTF8 *NOPERM_MESSAGE       = T("Permission denied.");
const UTF8 *FUNC_FAIL_MESSAGE    = T("#-1");
const UTF8 *FUNC_NOMATCH_MESSAGE = T("#-1 NO MATCH");
const UTF8 *OUT_OF_RANGE         = T("#-1 OUT OF RANGE");
const UTF8 *FUNC_NOT_FOUND       = T("#-1 NOT FOUND");
const UTF8 *FUNC_AMBIGUOUS       = T("#-2 AMBIGUOUS");
const UTF8 *FUNC_NOPERM_MESSAGE  = T("#-1 PERMISSION DENIED");
const UTF8 *OUT_OF_MEMORY        = T("#-1 OUT OF MEMORY");
const UTF8 *NOT_CONNECTED        = T("#-1 NOT CONNECTED");

#define CON_LOCAL       0x01    // Match is near me.
#define CON_TYPE        0x02    // Match is of requested type.
#define CON_LOCK        0x04    // I pass the lock on match.
#define CON_COMPLETE    0x08    // Name given is the full name.
#define CON_TOKEN       0x10    // Name is a special token.
#define CON_DBREF       0x20    // Name is a dbref.

static MSTATE md;

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
    static UTF8 buffer[LBUF_SIZE];

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

    mux_string sRef(name);
    if ('_' != name[0])
    {
        sRef.append(T("."));
        sRef.append(md.player);
    }

    UTF8 *pReferenceName = alloc_lbuf("absolute_named_reference");
    size_t nReferenceName = 0;
    nReferenceName = sRef.export_TextPlain(pReferenceName);

    struct reference_entry *result = (reference_entry *)hashfindLEN(
        pReferenceName, nReferenceName, &mudstate.reference_htab);
    free_lbuf(pReferenceName);

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

static void match_list(dbref first, int local)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
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

        if (!string_compare(namebuf, md.string))
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
        match_list(Contents(md.player), CON_LOCAL);
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
            match_list(Contents(loc), CON_LOCAL);
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
#if defined(FIRANMUX)
        return md.match;
#else
        return AMBIGUOUS;
#endif // FIRANMUX
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
    md.string = munge_space_for_match(name, strlen((const char *)name));
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
