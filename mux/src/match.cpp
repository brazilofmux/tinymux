// match.cpp -- Routines for parsing arguments.
//
// $Id: match.cpp,v 1.1 2003-01-22 19:58:25 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "powers.h"

const char *NOMATCH_MESSAGE      = "I don't see that here.";
const char *AMBIGUOUS_MESSAGE    = "I don't know which one you mean!";
const char *NOPERM_MESSAGE       = "Permission denied.";
const char *FUNC_FAIL_MESSAGE    = "#-1";
const char *FUNC_NOMATCH_MESSAGE = "#-1 NO MATCH";
const char *OUT_OF_RANGE         = "#-1 OUT OF RANGE";
const char *FUNC_NOT_FOUND       = "#-1 NOT FOUND";
const char *FUNC_AMBIGUOUS       = "#-2 AMBIGUOUS";
const char *FUNC_NOPERM_MESSAGE  = "#-1 PERMISSION DENIED";

#define CON_LOCAL       0x01    // Match is near me.
#define CON_TYPE        0x02    // Match is of requested type.
#define CON_LOCK        0x04    // I pass the lock on match.
#define CON_COMPLETE    0x08    // Name given is the full name.
#define CON_TOKEN       0x10    // Name is a special token.
#define CON_DBREF       0x20    // Name is a dbref.

static MSTATE md;

static void promote_match(dbref what, int confidence)
{
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

static char *munge_space_for_match(char *name)
{
    static char buffer[LBUF_SIZE];

    char *p = name;
    char *q = buffer;

    if (p)
    {
        // Remove Initial spaces.
        //
        while (Tiny_IsSpace[(unsigned char)*p])
        {
            p++;
        }

        while (*p)
        {
            while (  *p
                  && !Tiny_IsSpace[(unsigned char)*p])
            {
                *q++ = *p++;
            }

            while (Tiny_IsSpace[(unsigned char)*p])
            {
                p++;
            }

            if (*p)
            {
                *q++ = ' ';
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
        char *p;
        for (p = md.string + 1; Tiny_IsSpace[(unsigned char)*p]; p++) ;
        dbref match = lookup_player(NOTHING, p, TRUE);
        if (Good_obj(match))
        {
            promote_match(match, CON_TOKEN);
        }
    }
}

/*
 * returns nnn if name = #nnn, else NOTHING 
 */
static dbref absolute_name(BOOL bNeedPound)
{
    char *mname = md.string;
    if (bNeedPound)
    {
        if (*mname != NUMBER_TOKEN)
        {
            return NOTHING;
        }
        mname++;
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

void match_numeric(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
    dbref match = absolute_name(FALSE);
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
    if (!string_compare(md.string, "me"))
    {
        promote_match(md.player, CON_TOKEN | CON_LOCAL);
    }
    return;
}

void match_home(void)
{
    if (md.confidence >= CON_DBREF)
    {
        return;
    }
    if (!string_compare(md.string, "home"))
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
            else if (!string_compare(md.string, "here")) 
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
        const char *namebuf = PureName(first);

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

static BOOL match_exit_internal(dbref loc, dbref baseloc, int local)
{
    if (  !Good_obj(loc)
       || !Has_exits(loc))
    {
        return TRUE;
    }

    dbref exit;
    BOOL result = FALSE;
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
                return TRUE;
            }
        }
        if (matches_exit_from_list(md.string, PureName(exit))) 
        {
            promote_match(exit, CON_COMPLETE | local);
            result = TRUE;
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
       && (  Has_exits(md.player) 
          || isRoom(md.player)))
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
    strcpy(mstate->string, md.string);
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
    strcpy(md.string, mstate->string);
    free_lbuf(mstate->string);
}

void init_match(dbref player, const char *name, int type)
{
    md.confidence = -1;
    md.count = 0;
    md.check_keys = FALSE;
    md.pref_type = type;
    md.match = NOTHING;
    md.player = player;
    md.string = munge_space_for_match((char *)name);
    md.absolute_form = absolute_name(TRUE);
}

void init_match_check_keys(dbref player, const char *name, int type)
{
    init_match(player, name, type);
    md.check_keys = TRUE;
}

dbref match_thing(dbref player, char *name)
{
    init_match(player, name, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    return noisy_match_result();
}

dbref match_thing_quiet(dbref player, char *name)
{
    init_match(player, name, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    return match_result();
}

void safe_match_result(dbref it, char *buff, char **bufc)
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
