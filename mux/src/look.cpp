// look.cpp -- Commands which look at things.
//
// $Id: look.cpp,v 1.33 2005-10-09 19:49:56 sdennis Exp $
//
// MUX 2.4
// Copyright (C) 1998 through 2004 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "ansi.h"
#include "attrs.h"
#include "command.h"
#include "interface.h"
#include "powers.h"

#ifdef REALITY_LVLS
#include "levels.h"
#endif /* REALITY_LVLS */

#if defined(WOD_REALMS) || defined(REALITY_LVLS)
#define NORMAL_REALM  0
#define UMBRA_REALM   1
#define SHROUD_REALM  2
#define MATRIX_REALM  3
#define FAE_REALM     4
#define CHIMERA_REALM 5
#define BLIND_REALM   6
#define STAFF_REALM   7
#define NUMBER_OF_REALMS 8

int RealmActions[NUMBER_OF_REALMS] =
{
    REALM_DO_NORMALLY_SEEN,
    REALM_DO_SHOW_UMBRADESC,
    REALM_DO_SHOW_WRAITHDESC,
    REALM_DO_SHOW_MATRIXDESC,
    REALM_DO_SHOW_FAEDESC,
    REALM_DO_SHOW_FAEDESC,
    REALM_DO_HIDDEN_FROM_YOU,
    REALM_DO_NORMALLY_SEEN
};

// Umbra and Matrix are realms unto themselves, so if you aren't in the same
// realm as what you're looking at, you can't see it.
//
// Normal things can't see shroud things, but shroud things can see normal things.
//
// Only Fae and Chimera can see Chimera.
//
#define MAP_SEEN     0 // Show this to that.
#define MAP_HIDE     1 // Always hide this from that.
#define MAP_NO_ADESC 2 // Don't trigger DESC actions on that.
#define MAP_MEDIUM   4 // Hide this from that unless that is a medium and this is moving or talking.

int RealmHiddenMap[NUMBER_OF_REALMS][NUMBER_OF_REALMS] =
{
    /* NORMAL  LOOKER */ {     MAP_SEEN, MAP_HIDE, MAP_MEDIUM, MAP_HIDE,     MAP_SEEN, MAP_HIDE, MAP_NO_ADESC, MAP_SEEN},
    /* UMBRA   LOOKER */ {     MAP_HIDE, MAP_SEEN, MAP_MEDIUM, MAP_HIDE,     MAP_HIDE, MAP_HIDE,     MAP_HIDE, MAP_SEEN},
    /* SHROUD  LOOKER */ { MAP_NO_ADESC, MAP_HIDE,   MAP_SEEN, MAP_HIDE, MAP_NO_ADESC, MAP_HIDE, MAP_NO_ADESC, MAP_SEEN},
    /* MATRIX  LOOKER */ {     MAP_HIDE, MAP_HIDE, MAP_MEDIUM, MAP_SEEN,     MAP_HIDE, MAP_HIDE,     MAP_HIDE, MAP_SEEN},
    /* FAE     LOOKER */ {     MAP_SEEN, MAP_HIDE, MAP_MEDIUM, MAP_HIDE,     MAP_SEEN, MAP_SEEN, MAP_NO_ADESC, MAP_SEEN},
    /* CHIMERA LOOKER */ { MAP_NO_ADESC, MAP_HIDE, MAP_MEDIUM, MAP_HIDE,     MAP_SEEN, MAP_SEEN, MAP_NO_ADESC, MAP_SEEN},
    /* BLIND   LOOKER */ {     MAP_HIDE, MAP_HIDE,   MAP_HIDE, MAP_HIDE,     MAP_HIDE, MAP_HIDE,     MAP_HIDE, MAP_SEEN},
    /* STAFF   LOOKER */ {     MAP_SEEN, MAP_SEEN,   MAP_SEEN, MAP_SEEN,     MAP_SEEN, MAP_SEEN,     MAP_SEEN, MAP_SEEN}
};

int RealmExitsMap[NUMBER_OF_REALMS][NUMBER_OF_REALMS] =
{
    /* NORMAL  LOOKER */ { MAP_SEEN, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_HIDE},
    /* UMBRA   LOOKER */ { MAP_SEEN, MAP_SEEN, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_HIDE},
    /* SHROUD  LOOKER */ { MAP_SEEN, MAP_HIDE, MAP_SEEN, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_HIDE},
    /* MATRIX  LOOKER */ { MAP_SEEN, MAP_HIDE, MAP_HIDE, MAP_SEEN, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_HIDE},
    /* FAE     LOOKER */ { MAP_SEEN, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_SEEN, MAP_SEEN, MAP_HIDE, MAP_HIDE},
    /* CHIMERA LOOKER */ { MAP_SEEN, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_SEEN, MAP_SEEN, MAP_HIDE, MAP_HIDE},
    /* BLIND   LOOKER */ { MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_HIDE, MAP_HIDE},
    /* STAFF   LOOKER */ { MAP_SEEN, MAP_SEEN, MAP_SEEN, MAP_SEEN, MAP_SEEN, MAP_SEEN, MAP_SEEN, MAP_SEEN}
};
int WhichRealm(dbref what, bool bPeering)
{
    int realm = NORMAL_REALM;
    if (isMatrix(what))       realm = MATRIX_REALM;
    else if (isUmbra(what))   realm = UMBRA_REALM;
    else if (isShroud(what))  realm = SHROUD_REALM;
    else if (isChimera(what)) realm = CHIMERA_REALM;
    else if (isFae(what))     realm = FAE_REALM;

    if (bPeering)
    {
        char *buff;
        dbref owner;
        int flags;
        int iPeeringRealm = get_atr("PEERING_REALM");
        if (0 < iPeeringRealm)
        {
            buff = atr_get(what, iPeeringRealm, &owner, &flags);
            if (*buff)
            {
                if      (mux_stricmp(buff, "FAE") == 0)     realm = FAE_REALM;
                else if (mux_stricmp(buff, "CHIMERA") == 0) realm = CHIMERA_REALM;
                else if (mux_stricmp(buff, "SHROUD") == 0)  realm = SHROUD_REALM;
                else if (mux_stricmp(buff, "UMBRA") == 0)   realm = UMBRA_REALM;
                else if (mux_stricmp(buff, "MATRIX") == 0)  realm = MATRIX_REALM;
                else if (mux_stricmp(buff, "NORMAL") == 0)  realm = NORMAL_REALM;
                else if (mux_stricmp(buff, "BLIND") == 0)   realm = BLIND_REALM;
                else if (mux_stricmp(buff, "STAFF") == 0)   realm = STAFF_REALM;
            }
            free_lbuf(buff);
        }
    }
    return realm;
}
int HandleObfuscation(dbref looker, dbref lookee, int threshhold)
{
    int iReturn = REALM_DO_NORMALLY_SEEN;
    if (isObfuscate(lookee))
    {
        char *buff;
        int iObfuscateLevel = 0;
        dbref owner;
        int flags;
        buff = atr_get(lookee, get_atr("OBF_LEVEL"), &owner, &flags);
        if (*buff)
        {
            iObfuscateLevel = mux_atol(buff);
        }
        free_lbuf(buff);

        // For OBF_LEVELS of 0, 1, and 2, we show the regular description.
        // 3 and above start showing a different OBFDESC.
        //
        if (3 <= iObfuscateLevel)
        {
            iReturn = REALM_DO_SHOW_OBFDESC;
        }
        if (threshhold < iObfuscateLevel)
        {
            int iHeightenSensesLevel = 0;
            if (isHeightenedSenses(looker))
            {
                buff = atr_get(looker, get_atr("HSS_LEVEL"), &owner, &flags);
                if (*buff)
                {
                    iHeightenSensesLevel = mux_atol(buff);
                }
                free_lbuf(buff);
            }

            if (iHeightenSensesLevel < iObfuscateLevel)
            {
                iReturn = REALM_DO_HIDDEN_FROM_YOU;
            }
            else if (iObfuscateLevel == iHeightenSensesLevel)
            {
                if (RandomINT32(0,1))
                {
                    iReturn = REALM_DO_HIDDEN_FROM_YOU;
                }
            }
        }
    }
    return iReturn;
}
int DoThingToThingVisibility(dbref looker, dbref lookee, int action_state)
{
    // If the looker is a room, then there is some contents/recursion stuff
    // that happens in the rest of the game code. We'll be called later for
    // each item in the room, things that are nearby the room, etc.
    //
    if (isRoom(looker))
    {
        return REALM_DO_NORMALLY_SEEN;
    }

    if (Staff(looker))
    {
        if (Connected(looker))
        {
            // Staff players can see everything
            //
            return REALM_DO_NORMALLY_SEEN;
        }

        if (isThing(looker))
        {
            // Wizard things in the master room can see everything.
            //
            if (Location(looker) == mudconf.master_room)
            {
                return REALM_DO_NORMALLY_SEEN;
            }
        }
    }
    int realmLooker = WhichRealm(looker, isPeering(looker));
    int realmLookee = WhichRealm(lookee, false);

    // You can always see yourself.
    //
    if (looker == lookee)
    {
        return RealmActions[realmLooker];
    }

    bool bDisableADESC = false;
    if (isRoom(lookee) || isExit(lookee))
    {
        // All realms see normal rooms and exits, however, if a realm
        // specific description exists, they use that one. If a room or exit
        // is flagged with a specific realm, then -only- players and things of
        // that realm can see it.
        //
        // Fae and Chimera are treated as the same realm for this purpose.
        //
        if (RealmExitsMap[realmLooker][realmLookee] == MAP_HIDE)
        {
            return REALM_DO_HIDDEN_FROM_YOU;
        }
    }
    else
    {
        if (Staff(lookee))
        {
            // Staff players are seen in every realm.
            //
            if (Connected(lookee))
            {
                return REALM_DO_NORMALLY_SEEN;
            }

            if (isThing(lookee))
            {
                // Everyone can use wizard things in the master room.
                //
                // Wizard things that aren't in the master room follow the same
                // realm-rules as everything else.
                //
                if (Location(lookee) == mudconf.master_room)
                {
                    return REALM_DO_NORMALLY_SEEN;
                }
            }
        }

        int iMap = RealmHiddenMap[realmLooker][realmLookee];
        if (iMap & MAP_HIDE)
        {
            return REALM_DO_HIDDEN_FROM_YOU;
        }
        if (iMap & MAP_MEDIUM)
        {
            if (isMedium(looker))
            {
                if (action_state == ACTION_IS_STATIONARY)
                {
                    // Even Mediums can't hear it if the Wraith is just standing still.
                    //
                    return REALM_DO_HIDDEN_FROM_YOU;
                }
            }
            else
            {
                return REALM_DO_HIDDEN_FROM_YOU;
            }
        }
        if (iMap & MAP_NO_ADESC)
        {
            bDisableADESC = true;
        }
    }

    // Do default see rules.
    //
    int iReturn = RealmActions[realmLooker];
    if (iReturn == REALM_DO_HIDDEN_FROM_YOU)
    {
        return iReturn;
    }

    // Do Obfuscate/Heighten Senses rules.
    //
    int threshhold = 0;
    switch (action_state)
    {
    case ACTION_IS_STATIONARY:
        threshhold = 0;
        break;

    case ACTION_IS_MOVING:
        threshhold = 1;
        break;

    case ACTION_IS_TALKING:
        if (bDisableADESC)
        {
            iReturn |= REALM_DISABLE_ADESC;
        }
        return iReturn;
    }
    int iObfReturn = HandleObfuscation(looker, lookee, threshhold);
    switch (iObfReturn)
    {
    case REALM_DO_SHOW_OBFDESC:
    case REALM_DO_HIDDEN_FROM_YOU:
        iReturn = iObfReturn;
        break;
    }

    // Decide whether to disable ADESC or not. If we can look at them,
    // and ADESC isn't -already- disabled via SHROUD looking at NORMAL (see above).
    // then, ADESC may be disabled here if the looker is Obfuscated to the lookee.
    //
    if (iReturn != REALM_DO_HIDDEN_FROM_YOU && !bDisableADESC)
    {
        if (REALM_DO_HIDDEN_FROM_YOU == HandleObfuscation(lookee, looker, 0))
        {
            bDisableADESC = true;
        }
    }
    if (bDisableADESC)
    {
        iReturn |= REALM_DISABLE_ADESC;
    }
    return iReturn;
}

void LetDescriptionsDefault(dbref thing, int *piDESC, int *piADESC, int RealmDirective)
{
    int   iDesc = 0;
    dbref owner;
    int   flags;

    *piDESC = A_DESC;
    *piADESC = A_ADESC;

    if (RealmDirective & REALM_DISABLE_ADESC)
    {
        *piADESC = 0;
    }
    switch (RealmDirective & REALM_DO_MASK)
    {
    case REALM_DO_SHOW_OBFDESC:
        iDesc = get_atr("OBFDESC");
        break;

    case REALM_DO_SHOW_WRAITHDESC:
        iDesc = get_atr("WRAITHDESC");
        *piADESC = 0;
        break;

    case REALM_DO_SHOW_UMBRADESC:
        iDesc = get_atr("UMBRADESC");
        break;

    case REALM_DO_SHOW_MATRIXDESC:
        iDesc = get_atr("MATRIXDESC");
        break;

    case REALM_DO_SHOW_FAEDESC:
        iDesc = get_atr("FAEDESC");
        break;
    }

    if (iDesc > 0)
    {
        char *buff = atr_pget(thing, iDesc, &owner, &flags);
        if (buff)
        {
            if (*buff)
            {
                *piDESC = iDesc;
            }
            free_lbuf(buff);
        }
    }
}
#endif

static void look_exits(dbref player, dbref loc, const char *exit_name)
{
    // Make sure location has exits.
    //
    if (  !Good_obj(loc)
       || !Has_exits(loc))
    {
        return;
    }

    dbref thing, parent;
    char *buff, *e, *buff1, *e1;
    const char *s;

    // make sure there is at least one visible exit.
    //
    bool bFoundAnyDisplayable = false;
    bool bFoundAny = false;
    int key = 0;
    int lev;
#ifdef REALITY_LVLS
    if (Dark(loc) || !IsReal(player, loc))
#else
    if (Dark(loc))
#endif /* REALITY_LVLS */
    {
        key |= VE_BASE_DARK;
    }
    ITER_PARENTS(loc, parent, lev)
    {
        key &= ~VE_LOC_DARK;
        if (Dark(parent))
        {
            key |= VE_LOC_DARK;
        }
        DOLIST(thing, Exits(parent))
        {
            bFoundAny = true;
            if (exit_displayable(thing, player, key))
            {
                bFoundAnyDisplayable = true;
                break;
            }
        }
        if (bFoundAnyDisplayable)
        {
            break;
        }
    }

    if (!bFoundAny)
    {
        return;
    }

    // Retrieve the ExitFormat attribute from the location, evaluate and display
    // the results in lieu of the traditional exits list if it exists.
    //
    dbref aowner;
    int aflags;
    char *ExitFormatBuffer = atr_pget(loc, A_EXITFORMAT, &aowner, &aflags);
    char *ExitFormat = ExitFormatBuffer;

    bool bDisplayExits = bFoundAnyDisplayable;
    if (*ExitFormat)
    {
        char *VisibleObjectList = alloc_lbuf("look_exits.VOL");
        char *tPtr = VisibleObjectList;

        ITL pContext;
        ItemToList_Init(&pContext, VisibleObjectList, &tPtr, '#');

        ITER_PARENTS(loc, parent, lev)
        {
            key &= ~VE_LOC_DARK;
            if (Dark(parent))
            {
                key |= VE_LOC_DARK;
            }

            bool bShortCircuit = false;
            DOLIST(thing, Exits(parent))
            {
                if (  exit_displayable(thing, player, key)
                   && !ItemToList_AddInteger(&pContext, thing))
                {
                    bShortCircuit = true;
                    break;
                }
            }
            if (bShortCircuit) break;
        }
        ItemToList_Final(&pContext);

        char *FormatOutput = alloc_lbuf("look_exits.FO");
        tPtr = FormatOutput;

        char *preserve[MAX_GLOBAL_REGS];
        int preserve_len[MAX_GLOBAL_REGS];
        save_and_clear_global_regs("look_exits_save", preserve, preserve_len);

        mux_exec(FormatOutput, &tPtr, loc, player, player,
                EV_FCHECK | EV_EVAL | EV_TOP,
                &ExitFormat, &VisibleObjectList, 1);

        restore_global_regs("look_exits_restore", preserve, preserve_len);
        notify(player, FormatOutput);

        free_lbuf(FormatOutput);
        free_lbuf(VisibleObjectList);

        bDisplayExits = 0;
    }
    free_lbuf(ExitFormatBuffer);

    if (!bDisplayExits)
    {
        return;
    }

    // Display the list of exit names
    //
    notify(player, exit_name);
    e = buff = alloc_lbuf("look_exits");
    e1 = buff1 = alloc_lbuf("look_exits2");
    ITER_PARENTS(loc, parent, lev)
    {
        key &= ~VE_LOC_DARK;
        if (Dark(parent))
        {
            key |= VE_LOC_DARK;
        }
        if (Transparent(loc))
        {
            DOLIST(thing, Exits(parent))
            {
                if (exit_displayable(thing, player, key))
                {
                    strcpy(buff, Name(thing));
                    for (e = buff; *e && *e != ';'; e++)
                    {
                        ; // Nothing.
                    }
                    *e = '\0';
                    notify(player, tprintf("%s leads to %s.", buff, Name(Location(thing))));
                }
            }
        }
        else
        {
            DOLIST(thing, Exits(parent))
            {
                if (exit_displayable(thing, player, key))
                {
                    e1 = buff1;

                    // Put the exit name in buff1.
                    //
                    // chop off first exit alias to display
                    //
                    if (buff != e)
                    {
                        safe_str("  ", buff, &e);
                    }

                    for (s = Name(thing); *s && (*s != ';'); s++)
                    {
                        safe_chr(*s, buff1, &e1);
                    }

                    *e1 = 0;
                    /* Copy the exit name into 'buff' */
                    if (Html(player))
                    {
                        /* XXX The exit name needs to be HTML escaped. */
                        safe_str("<a xch_cmd=\"", buff, &e);
                        safe_str(buff1, buff, &e);
                        safe_str("\"> ", buff, &e);
                        html_escape(buff1, buff, &e);
                        safe_str(" </a>", buff, &e);
                    }
                    else
                    {
                        /* Append this exit to the list */
                        safe_str(buff1, buff, &e);
                    }
                }
            }
        }
    }

    if (!Transparent(loc))
    {
        if (Html(player))
        {
            safe_str("\r\n", buff, &e);
            *e = 0;
            notify_html(player, buff);
        }
        else
        {
            *e = 0;
            notify(player, buff);
        }
    }
    free_lbuf(buff);
    free_lbuf(buff1);
}

#define CONTENTS_LOCAL  0
#define CONTENTS_NESTED 1
#define CONTENTS_REMOTE 2

static void look_contents(dbref player, dbref loc, const char *contents_name, int style)
{
    dbref thing;
    char *buff;
    char *html_buff, *html_cp;
    char remote_num[32];

    // Check to see if he can see the location.
    //
#ifdef REALITY_LVLS
     bool can_see_loc = ( !Dark(loc) && IsReal(player, loc)
#else
     bool can_see_loc = (  !Dark(loc)
#endif /* REALITY_LVLS */
                       || (mudconf.see_own_dark && Examinable(player, loc)));

    dbref aowner;
    int aflags;
    char *ContentsFormatBuffer = atr_pget(loc, A_CONFORMAT, &aowner, &aflags);
    char *ContentsFormat = ContentsFormatBuffer;

    bool bDisplayContents = true;
    if (*ContentsFormat)
    {
        char *VisibleObjectList = alloc_lbuf("look_contents.VOL");
        char *tPtr = VisibleObjectList;

        ITL pContext;
        ItemToList_Init(&pContext, VisibleObjectList, &tPtr, '#');

        DOLIST(thing, Contents(loc))
        {
#if defined(WOD_REALMS) || defined(REALITY_LVLS)
            if (  can_see(player, thing, can_see_loc)
               && (REALM_DO_HIDDEN_FROM_YOU != DoThingToThingVisibility(player,
                                                thing, ACTION_IS_STATIONARY)) )
#else
            if (can_see(player, thing, can_see_loc))
#endif
            {
                if (!ItemToList_AddInteger(&pContext, thing))
                {
                    break;
                }
            }
        }
        ItemToList_Final(&pContext);

        char *ContentsNameScratch = alloc_lbuf("look_contents.CNS");
        tPtr = ContentsNameScratch;

        safe_str(contents_name, ContentsNameScratch, &tPtr);
        *tPtr = '\0';

        char *FormatOutput = alloc_lbuf("look_contents.FO");
        tPtr = FormatOutput;

        char* ParameterList[] =
            { VisibleObjectList, ContentsNameScratch };

        char *preserve[MAX_GLOBAL_REGS];
        int preserve_len[MAX_GLOBAL_REGS];
        save_and_clear_global_regs("look_contents_save", preserve, preserve_len);

        mux_exec(FormatOutput, &tPtr, loc, player, player,
                EV_FCHECK | EV_EVAL | EV_TOP,
                &ContentsFormat, ParameterList, 2);

        restore_global_regs("look_contents_restore", preserve, preserve_len);
        notify(player, FormatOutput);

        free_lbuf(FormatOutput);
        free_lbuf(ContentsNameScratch);
        free_lbuf(VisibleObjectList);

        bDisplayContents = false;
    }
    free_lbuf(ContentsFormatBuffer);

    if (!bDisplayContents)
    {
        return;
    }

    html_buff = html_cp = alloc_lbuf("look_contents");

    // Check to see if there is anything there.
    //
    DOLIST(thing, Contents(loc))
    {
#if defined(WOD_REALMS) || defined(REALITY_LVLS)
        if (  can_see(player, thing, can_see_loc)
           && (REALM_DO_HIDDEN_FROM_YOU != DoThingToThingVisibility(player, thing, ACTION_IS_STATIONARY)))
#else
        if (can_see(player, thing, can_see_loc))
#endif
        {
            // Something exists! Show him everything.
            //
            notify(player, contents_name);
            DOLIST(thing, Contents(loc))
            {
#if defined(WOD_REALMS) || defined(REALITY_LVLS)
                if (  can_see(player, thing, can_see_loc)
                   && (REALM_DO_HIDDEN_FROM_YOU != DoThingToThingVisibility(player, thing, ACTION_IS_STATIONARY)))
#else
                if (can_see(player, thing, can_see_loc))
#endif
                {
                    buff = unparse_object(player, thing, true);
                    html_cp = html_buff;
                    if (Html(player))
                    {
                        safe_str("<a xch_cmd=\"look ", html_buff, &html_cp);
                        switch (style)
                        {
                        case CONTENTS_LOCAL:
                            safe_str(Name(thing), html_buff, &html_cp);
                            break;
                        case CONTENTS_NESTED:
                            safe_str(Name(Location(thing)), html_buff, &html_cp);
                            safe_str("'s ", html_buff, &html_cp);
                            safe_str(Name(thing), html_buff, &html_cp);
                            break;

                        case CONTENTS_REMOTE:

                            remote_num[0] = '#';
                            mux_ltoa(thing, remote_num+1);
                            safe_str(remote_num, html_buff, &html_cp);
                            break;

                        default:

                            break;
                        }
                        safe_str("\">", html_buff, &html_cp);
                        html_escape(buff, html_buff, &html_cp);
                        safe_str("</a>\r\n", html_buff, &html_cp);
                        *html_cp = 0;
                        notify_html(player, html_buff);
                    }
                    else
                    {
                        notify(player, buff);
                    }
                    free_lbuf(buff);
                }
            }
            break;  // we're done.
        }
    }
    free_lbuf(html_buff);
}

typedef struct
{
    int mask;
    int letter;
} ATTR_DECODE_ENTRY, *PATTR_DECODE_ENTRY;

static ATTR_DECODE_ENTRY attr_decode_table[] =
{
    { AF_LOCK,    '+' },
    { AF_NOPROG,  '$' },
    { AF_CASE,    'C' },
    { AF_HTML,    'H' },
    { AF_PRIVATE, 'I' },
    { AF_NOPARSE, 'P' },
    { AF_REGEXP,  'R' },
    { AF_VISUAL,  'V' },
    { AF_MDARK,   'M' },
    { AF_WIZARD,  'W' },
    { 0, 0 }
};

size_t decode_attr_flags(int aflags, char *buff)
{
    char *p = buff;
    PATTR_DECODE_ENTRY pEntry;
    for (pEntry = attr_decode_table; pEntry->mask; pEntry++)
    {
        if (aflags & pEntry->mask)
        {
            *p++ = pEntry->letter;
        }
    }
    *p = '\0';
    return p - buff;
}

static void view_atr
(
    dbref player,
    dbref thing,
    ATTR *ap,
    char *text,
    dbref aowner,
    int aflags,
    bool skip_tag
)
{
    char *buf;

    if (ap->flags & AF_IS_LOCK)
    {
        BOOLEXP *pBoolExp = parse_boolexp(player, text, true);
        text = unparse_boolexp(player, pBoolExp);
        free_boolexp(pBoolExp);
    }

    // If we don't control the object or own the attribute, hide the
    // attr owner and flag info.
    //
    if (  !Controls(player, thing)
       && Owner(player) != aowner)
    {
        if (  skip_tag
           && ap->number == A_DESC)
        {
            buf = text;
        }
        else
        {
            buf = tprintf("%s%s:%s %s", ANSI_HILITE, ap->name, ANSI_NORMAL, text);
        }
        notify(player, buf);
        return;
    }

    // Generate flags.
    //
    char xbuf[11];
    decode_attr_flags(aflags, xbuf);

    if (  aowner != Owner(thing)
       && aowner != NOTHING)
    {
        buf = tprintf("%s%s [#%d%s]:%s %s", ANSI_HILITE,
            ap->name, aowner, xbuf, ANSI_NORMAL, text);
    }
    else if (*xbuf)
    {
        buf = tprintf("%s%s [%s]:%s %s", ANSI_HILITE, ap->name,
            xbuf, ANSI_NORMAL, text);
    }
    else if (  !skip_tag
            || ap->number != A_DESC)
    {
        buf = tprintf("%s%s:%s %s", ANSI_HILITE, ap->name, ANSI_NORMAL, text);
    }
    else
    {
        buf = text;
    }
    notify(player, buf);
}

static void look_atrs1
(
    dbref player,
    dbref thing,
    dbref othing,
    bool  check_exclude,
    bool  hash_insert
)
{
    dbref aowner;
    int ca, aflags;
    ATTR *pattr;
    char *as, *buf;

    ATTR cattr;
    for (ca = atr_head(thing, &as); ca; ca = atr_next(&as))
    {
        if (  ca == A_DESC
           || ca == A_LOCK)
        {
            continue;
        }
        pattr = atr_num(ca);
        if (!pattr)
        {
            continue;
        }

        memcpy(&cattr, pattr, sizeof(ATTR));

        // Should we exclude this attr?
        //
        if (  check_exclude
           && (  (pattr->flags & AF_PRIVATE)
              || hashfindLEN(&ca, sizeof(ca), &mudstate.parent_htab)))
        {
            continue;
        }

        buf = atr_get(thing, ca, &aowner, &aflags);
        if (bCanReadAttr(player, othing, &cattr, false))
        {
            if (!(check_exclude && (aflags & AF_PRIVATE)))
            {
                if (hash_insert)
                {
                    hashaddLEN(&ca, sizeof(ca), pattr,
                        &mudstate.parent_htab);
                }
                view_atr(player, thing, &cattr, buf, aowner, aflags, false);
            }
        }
        free_lbuf(buf);
    }
}

static void look_atrs(dbref player, dbref thing, bool check_parents)
{
    dbref parent;
    int lev;
    bool check_exclude, hash_insert;

    if (!check_parents)
    {
        look_atrs1(player, thing, thing, false, false);
    }
    else
    {
        hash_insert = true;
        check_exclude = false;
        hashflush(&mudstate.parent_htab);
        ITER_PARENTS(thing, parent, lev)
        {
            if (!Good_obj(Parent(parent)))
            {
                hash_insert = false;
            }
            look_atrs1(player, parent, thing, check_exclude, hash_insert);
            check_exclude = true;
        }
    }
}

static bool show_a_desc(dbref player, dbref loc)
{
    int iDescDefault = A_DESC;
    int iADescDefault = A_ADESC;
#if defined(WOD_REALMS) || defined(REALITY_LVLS)
    int iRealmDirective = DoThingToThingVisibility(player, loc, ACTION_IS_STATIONARY);
    if (REALM_DO_HIDDEN_FROM_YOU == iRealmDirective)
    {
        return true;
    }
    LetDescriptionsDefault(loc, &iDescDefault, &iADescDefault, iRealmDirective);
#endif

    bool ret = false;

    dbref aowner;
    int aflags;
    bool indent = (isRoom(loc) && mudconf.indent_desc && atr_get_raw(loc, A_DESC));

    char *DescFormatBuffer = atr_pget(loc, A_DESCFORMAT, &aowner, &aflags);
    char *DescFormat = DescFormatBuffer;
    if (*DescFormat)
    {
        char *FormatOutput = alloc_lbuf("look_description.FO");
        char *tPtr = FormatOutput;

        ATTR *cattr = atr_num(iDescDefault);

        char *tbuf1 = atr_pget(loc, iDescDefault, &aowner, &aflags);
        char *str = tbuf1;
        char *temp = alloc_lbuf("look_description.ET");
        char *bp = temp;
        mux_exec(temp, &bp, loc, player, player,
               EV_FCHECK | EV_EVAL | EV_TOP,
               &str, (char **)NULL, 0);
        *bp = '\0';

        char *attrname = alloc_lbuf("look_description.AN");
        char *cp = attrname;

        safe_str(cattr->name, attrname, &cp);
        *cp = '\0';
        char* ParameterList[] =
            { temp, attrname };

        mux_exec(FormatOutput, &tPtr, loc, player, player,
                EV_FCHECK | EV_EVAL | EV_TOP,
                &DescFormat, ParameterList, 2);

        notify(player, FormatOutput);
#ifdef REALITY_LVLS
        did_it_rlevel(player, loc, 0, NULL, A_ODESC, NULL, iADescDefault, (char **) NULL, 0);
#else
        did_it(player, loc, 0, NULL, A_ODESC, NULL, iADescDefault, (char **) NULL, 0);
#endif /* REALITY_LVLS */

        free_lbuf(tbuf1);
        free_lbuf(attrname);
        free_lbuf(FormatOutput);
        free_lbuf(temp);

        ret = true;
    }
    else
    {
        char *got;
        if (Html(player))
        {
            got = atr_pget(loc, A_HTDESC, &aowner, &aflags);
            if (*got)
            {
#ifdef REALITY_LVLS
                did_it_rlevel(player, loc, A_HTDESC, NULL, A_ODESC, NULL, A_ADESC, (char **) NULL, 0);
#else
                did_it(player, loc, A_HTDESC, NULL, A_ODESC, NULL, A_ADESC, (char **) NULL, 0);
#endif /* REALITY_LVLS */
                ret = true;
            }
            else
            {
                free_lbuf(got);
                got = atr_pget(loc, iDescDefault, &aowner, &aflags);
                if (*got)
                {
                    if (indent)
                    {
                        raw_notify_newline(player);
                    }
#ifdef REALITY_LVLS
                    did_it_rlevel(player, loc, iDescDefault, NULL, A_ODESC, NULL, iADescDefault, (char **) NULL, 0);
#else
                    did_it(player, loc, iDescDefault, NULL, A_ODESC, NULL, iADescDefault, (char **) NULL, 0);
#endif /* REALITY_LVLS */
                    if (indent)
                    {
                        raw_notify_newline(player);
                    }
                    ret = true;
                }
            }
        }
        else if (*(got = atr_pget(loc, iDescDefault, &aowner, &aflags)))
        {
            if (indent)
            {
                raw_notify_newline(player);
            }
#ifdef REALITY_LVLS
            did_it_rlevel(player, loc, iDescDefault, NULL, A_ODESC, NULL, iADescDefault, (char **) NULL, 0);
#else
            did_it(player, loc, iDescDefault, NULL, A_ODESC, NULL, iADescDefault, (char **) NULL, 0);
#endif /* REALITY_LVLS */
            if (indent)
            {
                raw_notify_newline(player);
            }
            ret = true;
        }
        free_lbuf(got);
    }
    free_lbuf(DescFormatBuffer);
    return ret;
}

static void look_simple(dbref player, dbref thing, bool obey_terse)
{
    // Only makes sense for things that can hear.
    //
    if (!Hearer(player))
    {
        return;
    }

#if defined(WOD_REALMS) || defined(REALITY_LVLS)
    int iRealmDirective = DoThingToThingVisibility(player, thing, ACTION_IS_STATIONARY);
    if (REALM_DO_HIDDEN_FROM_YOU == iRealmDirective)
    {
        notify(player, NOMATCH_MESSAGE);
        return;
    }
#endif

    // Get the name and db-number if we can examine it.
    //
    int can_see_thing = Examinable(player, thing);
    if (can_see_thing)
    {
        char *buff = unparse_object(player, thing, true);
        notify(player, buff);
        free_lbuf(buff);
    }
    int iDescDefault = A_DESC;
    int iADescDefault = A_ADESC;

#if defined(WOD_REALMS) || defined(REALITY_LVLS)
    LetDescriptionsDefault(thing, &iDescDefault, &iADescDefault, iRealmDirective);
#endif

    int pattr = (obey_terse && Terse(player)) ? 0 : iDescDefault;
    if (!show_a_desc(player, thing))
    {
        notify(player, "You see nothing special.");
#ifdef REALITY_LVLS
        did_it_rlevel(player, thing, 0, NULL, A_ODESC, NULL, iADescDefault,
            (char **)NULL, 0);
#else
        did_it(player, thing, pattr, NULL, A_ODESC, NULL, iADescDefault,
            (char **)NULL, 0);
#endif /* REALITY_LVLS */
    }

    if (  !mudconf.quiet_look
       && (  !Terse(player)
          || mudconf.terse_look))
    {
        look_atrs(player, thing, false);
    }
}

static void show_desc(dbref player, dbref loc, int key)
{
    char *got;
    dbref aowner;
    int aflags;

    if (  (key & LK_OBEYTERSE)
       && Terse(player))
    {
#ifdef REALITY_LVLS
        did_it_rlevel(player, loc, 0, NULL, A_ODESC, NULL, A_ADESC, (char **)NULL, 0);
#else
        did_it(player, loc, 0, NULL, A_ODESC, NULL, A_ADESC, (char **)NULL, 0);
#endif /* REALITY_LVLS */
    }
    else if (  !isRoom(loc)
            && (key & LK_IDESC))
    {
        if (*(got = atr_pget(loc, A_IDESC, &aowner, &aflags)))
        {
#ifdef REALITY_LVLS
           did_it_rlevel(player, loc, A_IDESC, NULL, A_ODESC, NULL, A_ADESC, (char **)NULL, 0);
#else
            did_it(player, loc, A_IDESC, NULL, A_ODESC, NULL, A_ADESC, (char **)NULL, 0);
#endif /* REALITY_LVLS */
        }
        else
        {
            show_a_desc(player, loc);
        }
        free_lbuf(got);
    }
    else
    {
        show_a_desc(player, loc);
    }
}

void look_in(dbref player, dbref loc, int key)
{
    // Only makes sense for things that can hear.
    //
    if (!Hearer(player))
    {
        return;
    }

    // If he needs the VMRL URL, send it:
    //
    if (key & LK_SHOWVRML)
    {
        show_vrml_url(player, loc);
    }

    // Use @nameformat (by Marlek) if it's present, otherwise, use the
    // name and if the player can link to it, it's dbref as well.
    //
    dbref aowner;
    int aflags;
    char *NameFormatBuffer = atr_pget(loc, A_NAMEFORMAT, &aowner, &aflags);
    char *NameFormat = NameFormatBuffer;

    if (*NameFormat)
    {
        char *FormatOutput = alloc_lbuf("look_name.FO");
        char *tPtr = FormatOutput;

        char *preserve[MAX_GLOBAL_REGS];
        int preserve_len[MAX_GLOBAL_REGS];
        save_and_clear_global_regs("look_in_save", preserve, preserve_len);

        mux_exec(FormatOutput, &tPtr, loc, player, player,
                EV_FCHECK | EV_EVAL | EV_TOP,
                &NameFormat, 0, 0);

        restore_global_regs("look_in_restore", preserve, preserve_len);
        notify(player, FormatOutput);

        free_lbuf(FormatOutput);
    }
    else
    {
        // Okay, no @NameFormat.  Show the normal name.
        //
        char *buff = unparse_object(player, loc, true);
        if (Html(player))
        {
            notify_html(player, "<center><h3>");
            notify(player, buff);
            notify_html(player, "</h3></center>");
        }
        else
        {
            notify(player, buff);
        }
        free_lbuf(buff);
    }
    free_lbuf(NameFormatBuffer);

    if (!Good_obj(loc))
    {
        // If we went to NOTHING et al, then skip the rest.
        //
        return;
    }

    // Tell him the description.
    //
    int showkey = 0;
    if (loc == Location(player))
    {
        showkey |= LK_IDESC;
    }
    if (key & LK_OBEYTERSE)
    {
        showkey |= LK_OBEYTERSE;
    }
    show_desc(player, loc, showkey);

    bool is_terse = (key & LK_OBEYTERSE) ? Terse(player) : false;

    // Tell him the appropriate messages if he has the key.
    //
    if (isRoom(loc))
    {
        int pattr, oattr, aattr;
        if (could_doit(player, loc, A_LOCK))
        {
            pattr = A_SUCC;
            oattr = A_OSUCC;
            aattr = A_ASUCC;
        }
        else
        {
            pattr = A_FAIL;
            oattr = A_OFAIL;
            aattr = A_AFAIL;
        }
        if (is_terse)
        {
            pattr = 0;
        }
        did_it(player, loc, pattr, NULL, oattr, NULL, aattr, (char **)NULL, 0);
    }

    // Tell him the attributes, contents and exits.
    //
    if (  (key & LK_SHOWATTR)
       && !mudconf.quiet_look
       && !is_terse)
    {
        look_atrs(player, loc, false);
    }
    if (  !is_terse
       || mudconf.terse_contents)
    {
        look_contents(player, loc, "Contents:", CONTENTS_LOCAL);
    }
    if (  (key & LK_SHOWEXIT)
       && (  !is_terse
          || mudconf.terse_exits))
    {
        look_exits(player, loc, "Obvious exits:");
    }
}

void do_look(dbref executor, dbref caller, dbref enactor, int key, char *name)
{
    int look_key = LK_SHOWATTR | LK_SHOWEXIT;
    if (!mudconf.terse_look)
    {
        look_key |= LK_OBEYTERSE;
    }

    dbref loc = Location(executor);
    dbref thing;
    if (!name || !*name)
    {
        thing = loc;
        if (Good_obj(thing))
        {
            if (key & LOOK_OUTSIDE)
            {
                if (  isRoom(thing)
                   || Opaque(thing))
                {
                    notify_quiet(executor, "You can't look outside.");
                    return;
                }
                thing = Location(thing);
            }
            look_in(executor, thing, look_key);
        }
        return;
    }

    // Look for the target locally.
    //
    thing = (key & LOOK_OUTSIDE) ? loc : executor;
    init_match(thing, name, NOTYPE);
    match_exit_with_parents();
    match_neighbor();
    match_possession();
    if (Long_Fingers(executor))
    {
        match_absolute();
        match_player();
    }
    match_here();
    match_me();
    match_master_exit();
    thing = match_result();

    // Not found locally, check possessive.
    //
    if (!Good_obj(thing))
    {
        thing = match_status(executor, match_possessed(executor,
            ((key & LOOK_OUTSIDE) ? loc : executor), name, thing, false));
    }

    // If we found something, go handle it.
    //
    if (Good_obj(thing))
    {
#ifdef REALITY_LVLS
        if (!IsReal(executor, thing))
            return;
#endif /* REALITY_LVLS */
        switch (Typeof(thing))
        {
        case TYPE_ROOM:

            look_in(executor, thing, look_key);
            break;

        case TYPE_THING:
        case TYPE_PLAYER:

            look_simple(executor, thing, !mudconf.terse_look);
            if (  !Opaque(thing)
               && (  mudconf.terse_contents
                  || !Terse(executor)))
            {
                look_contents(executor, thing, "Carrying:", CONTENTS_NESTED);
            }
            break;

        case TYPE_EXIT:

            look_simple(executor, thing, !mudconf.terse_look);
            if (  Transparent(thing)
               && Location(thing) != NOTHING)
            {
                look_key &= ~LK_SHOWATTR;
                look_in(executor, Location(thing), look_key);
            }
            break;

        default:

            look_simple(executor, thing, !mudconf.terse_look);
            break;
        }
    }
}

static void debug_examine(dbref player, dbref thing)
{
    dbref aowner;
    char *buf;
    int aflags, ca;
    BOOLEXP *pBoolExp;
    ATTR *pattr;
    char *as, *cp;

    notify(player, tprintf("Number  = %d", thing));
    if (!Good_obj(thing))
    {
        return;
    }

    notify(player, tprintf("Name    = %s", Name(thing)));
    notify(player, tprintf("Location= %d", Location(thing)));
    notify(player, tprintf("Contents= %d", Contents(thing)));
    notify(player, tprintf("Exits   = %d", Exits(thing)));
    notify(player, tprintf("Link    = %d", Link(thing)));
    notify(player, tprintf("Next    = %d", Next(thing)));
    notify(player, tprintf("Owner   = %d", Owner(thing)));
    notify(player, tprintf("Pennies = %d", Pennies(thing)));
    notify(player, tprintf("Zone    = %d", Zone(thing)));
    buf = flag_description(player, thing);
    notify(player, tprintf("Flags   = %s", buf));
    free_mbuf(buf);
    buf = powers_list(player, thing);
    notify(player, tprintf("Powers  = %s", buf));
    free_lbuf(buf);
#ifdef REALITY_LVLS
    buf = rxlevel_description(player, thing);
    notify(player, tprintf("RxLevel = %s", buf));
    free_lbuf(buf);
    buf = txlevel_description(player, thing);
    notify(player, tprintf("TxLevel = %s", buf));
    free_lbuf(buf);
#endif /* REALITY_LVLS */
    buf = atr_get(thing, A_LOCK, &aowner, &aflags);
    pBoolExp = parse_boolexp(player, buf, true);
    free_lbuf(buf);
    notify(player, tprintf("Lock    = %s", unparse_boolexp(player, pBoolExp)));
    free_boolexp(pBoolExp);

    buf = alloc_lbuf("debug_dexamine");
    cp = buf;
    safe_str("Attr list: ", buf, &cp);

    for (ca = atr_head(thing, &as); ca; ca = atr_next(&as))
    {
        pattr = atr_num(ca);
        if (!pattr)
        {
            continue;
        }

        atr_get_info(thing, ca, &aowner, &aflags);
        if (bCanReadAttr(player, thing, pattr, false))
        {
            if (pattr)
            {
                // Valid attr.
                //
                safe_str(pattr->name, buf, &cp);
                safe_chr(' ', buf, &cp);
            }
            else
            {
                safe_str(tprintf("%d ", ca), buf, &cp);
            }
        }
    }
    *cp = '\0';
    notify(player, buf);
    free_lbuf(buf);

    for (ca = atr_head(thing, &as); ca; ca = atr_next(&as))
    {
        pattr = atr_num(ca);
        if (!pattr)
        {
            continue;
        }

        buf = atr_get(thing, ca, &aowner, &aflags);
        if (bCanReadAttr(player, thing, pattr, false))
        {
            view_atr(player, thing, pattr, buf, aowner, aflags, 0);
        }
        free_lbuf(buf);
    }
}

static void exam_wildattrs
(
    dbref player,
    dbref thing,
    bool do_parent
)
{
    int atr;
    bool got_any = false;
    for (atr = olist_first(); atr != NOTHING; atr = olist_next())
    {
        ATTR *ap = atr_num(atr);
        if (!ap)
        {
            continue;
        }
        int   aflags;
        dbref aowner;
        char *buf;
        if (  do_parent
           && !(ap->flags & AF_PRIVATE))
        {
            buf = atr_pget(thing, atr, &aowner, &aflags);
        }
        else
        {
            buf = atr_get(thing, atr, &aowner, &aflags);
        }

        // Decide if the player should see the attr: If obj is
        // Examinable and has rights to see, yes. If a player and has
        // rights to see, yes... except if faraway, attr=DESC, and
        // remote DESC-reading is not turned on. If I own the attrib
        // and have rights to see, yes... except if faraway, attr=DESC,
        // and remote DESC-reading is not turned on.
        //
        if (  Examinable(player, thing)
           && bCanReadAttr(player, thing, ap, do_parent))
        {
            got_any = true;
            view_atr(player, thing, ap, buf, aowner, aflags, 0);
        }
        else if (bCanReadAttr(player, thing, ap, isPlayer(thing) ? do_parent : false))
        {
            got_any = true;
            if (aowner == Owner(player))
            {
                view_atr(player, thing, ap, buf, aowner, aflags, 0);
            }
            else if (  atr == A_DESC
                    && (  mudconf.read_rem_desc
                       || nearby(player, thing)))
            {
                show_desc(player, thing, 0);
            }
            else if (atr != A_DESC)
            {
                view_atr(player, thing, ap, buf, aowner, aflags, 0);
            }
            else
            {
                notify(player, "<Too far away to get a good look>");
            }
        }
        free_lbuf(buf);
    }
    if (!got_any)
    {
        notify_quiet(player, "No matching attributes found.");
    }
}

void do_examine(dbref executor, dbref caller, dbref enactor, int key, char *name)
{
    // This command is pointless if the player can't hear.
    //
    if (!Hearer(executor))
    {
        return;
    }

    dbref content, exit, aowner, loc;
    char savec;
    char *temp, *buf, *buf2;
    BOOLEXP *pBoolExp;
    int aflags;
    bool control;
    bool do_parent = ((key & EXAM_PARENT) ? true : false);

    dbref thing = NOTHING;
    if (  !name
       || !*name)
    {
        thing = Location(executor);
        if (thing == NOTHING)
        {
            return;
        }
    }
    else
    {
        // Check for obj/attr first.
        //
        olist_push();
        if (parse_attrib_wild(executor, name, &thing, do_parent, true, false))
        {
            exam_wildattrs(executor, thing, do_parent);
            olist_pop();
            return;
        }
        olist_pop();

        // Look it up.
        //
        init_match(executor, name, NOTYPE);
        match_everything(MAT_EXIT_PARENTS);
        thing = noisy_match_result();
        if (!Good_obj(thing))
        {
            return;
        }
    }

#if defined(WOD_REALMS) || defined(REALITY_LVLS)
    if (REALM_DO_HIDDEN_FROM_YOU == DoThingToThingVisibility(executor, thing, ACTION_IS_STATIONARY))
    {
        notify(executor, NOMATCH_MESSAGE);
        return;
    }
#endif

    // Check for the /debug switch.
    //
    if (key & EXAM_DEBUG)
    {
        if (!Examinable(executor, thing))
        {
            notify_quiet(executor, NOPERM_MESSAGE);
        }
        else
        {
            debug_examine(executor, thing);
        }
        return;
    }
    control = (  Examinable(executor, thing)
              || Link_exit(executor, thing));

    if (control)
    {
        buf2 = unparse_object(executor, thing, false);
        notify(executor, buf2);
        free_lbuf(buf2);
        if (mudconf.ex_flags)
        {
            buf2 = flag_description(executor, thing);
            notify(executor, buf2);
            free_mbuf(buf2);
        }
    }
    else
    {
        if (  key == EXAM_DEFAULT
           && !mudconf.exam_public)
        {
            if (mudconf.read_rem_name)
            {
                buf2 = alloc_lbuf("do_examine.pub_name");
                strcpy(buf2, Name(thing));
                notify(executor,
                    tprintf("%s is owned by %s",
                    buf2, Name(Owner(thing))));
                free_lbuf(buf2);
            }
            else
            {
                notify(executor, tprintf("Owned by %s", Name(Owner(thing))));
            }
            return;
        }
    }

    temp = alloc_lbuf("do_examine.info");

    if (  control
       || mudconf.read_rem_desc
       || nearby(executor, thing))
    {
        temp = atr_get_str(temp, thing, A_DESC, &aowner, &aflags);
        if (*temp)
        {
            if (  Examinable(executor, thing)
               || (aowner == Owner(executor)))
            {
                view_atr(executor, thing, atr_num(A_DESC), temp,
                    aowner, aflags, true);
            }
            else
            {
                show_desc(executor, thing, 0);
            }
        }
    }
    else
    {
        notify(executor, "<Too far away to get a good look>");
    }

    if (control)
    {
        // Print owner, key, and value.
        //
        savec = mudconf.many_coins[0];
        mudconf.many_coins[0] = mux_toupper(mudconf.many_coins[0]);
        buf2 = atr_get(thing, A_LOCK, &aowner, &aflags);
        pBoolExp = parse_boolexp(executor, buf2, true);
        buf = unparse_boolexp(executor, pBoolExp);
        free_boolexp(pBoolExp);
        strcpy(buf2, Name(Owner(thing)));
        notify(executor, tprintf("Owner: %s  Key: %s %s: %d", buf2, buf, mudconf.many_coins, Pennies(thing)));
        free_lbuf(buf2);
        mudconf.many_coins[0] = savec;

        // Print the zone
        //
        if (mudconf.have_zones)
        {
            buf2 = unparse_object(executor, Zone(thing), false);
            notify(executor, tprintf("Zone: %s", buf2));
            free_lbuf(buf2);
        }

        // Print parent
        //
        loc = Parent(thing);
        if (loc != NOTHING)
        {
            buf2 = unparse_object(executor, loc, false);
            notify(executor, tprintf("Parent: %s", buf2));
            free_lbuf(buf2);
        }
        buf2 = powers_list(executor, thing);
        notify(executor, tprintf("Powers: %s", buf2));
        free_lbuf(buf2);
#ifdef REALITY_LVLS
        /* Show Rx and Tx levels */

        buf2 = rxlevel_description(executor, thing);
        notify(executor, buf2);
        free_lbuf(buf2);
        buf2 = txlevel_description(executor, thing);
        notify(executor, buf2);
        free_lbuf(buf2);
#endif /* REALITY_LVLS */
    }
    if (!(key & EXAM_BRIEF))
    {
        look_atrs(executor, thing, do_parent);
    }

    // Show him interesting stuff
    //
    if (control)
    {
        // Contents
        //
        if (Contents(thing) != NOTHING)
        {
            notify(executor, "Contents:");
            DOLIST(content, Contents(thing))
            {
                buf2 = unparse_object(executor, content, false);
                notify(executor, buf2);
                free_lbuf(buf2);
            }
        }

        // Show stuff that depends on the object type.
        //
        switch (Typeof(thing))
        {
        case TYPE_ROOM:
            // Tell him about exits
            //
            if (Exits(thing) != NOTHING)
            {
                notify(executor, "Exits:");
                DOLIST(exit, Exits(thing))
                {
                    buf2 = unparse_object(executor, exit, false);
                    notify(executor, buf2);
                    free_lbuf(buf2);
                }
            }
            else
            {
                notify(executor, "No exits.");
            }

            // print dropto if present
            //
            if (Dropto(thing) != NOTHING)
            {
                buf2 = unparse_object(executor, Dropto(thing), false);
                notify(executor, tprintf("Dropped objects go to: %s", buf2));
                free_lbuf(buf2);
            }
            break;

        case TYPE_THING:
        case TYPE_PLAYER:

            // Tell him about exits
            //
            if (Exits(thing) != NOTHING)
            {
                notify(executor, "Exits:");
                DOLIST(exit, Exits(thing))
                {
                    buf2 = unparse_object(executor, exit, false);
                    notify(executor, buf2);
                    free_lbuf(buf2);
                }
            }
            else
            {
                notify(executor, "No exits.");
            }

            // Print home
            //
            loc = Home(thing);
            buf2 = unparse_object(executor, loc, false);
            notify(executor, tprintf("Home: %s", buf2));
            free_lbuf(buf2);

            // print location if player can link to it
            //
            loc = Location(thing);
            if (  Location(thing) != NOTHING
               && (  Examinable(executor, loc)
                  || Examinable(executor, thing)
                  || Linkable(executor, loc)))
            {
                buf2 = unparse_object(executor, loc, false);
                notify(executor, tprintf("Location: %s", buf2));
                free_lbuf(buf2);
            }
            break;

        case TYPE_EXIT:
            buf2 = unparse_object(executor, Exits(thing), false);
            notify(executor, tprintf("Source: %s", buf2));
            free_lbuf(buf2);

            // print destination.
            //
            switch (Location(thing))
            {
            case NOTHING:
                // Special case. unparse_object() normally returns -1 as '*NOTHING*'.
                //
                notify(executor, "Destination: *UNLINKED*");
                break;

            default:
                buf2 = unparse_object(executor, Location(thing), false);
                notify(executor, tprintf("Destination: %s", buf2));
                free_lbuf(buf2);
                break;
            }
            break;

        default:
            break;
        }
    }
    else if (  !Opaque(thing)
            && nearby(executor, thing))
    {
        if (Has_contents(thing))
        {
            look_contents(executor, thing, "Contents:", CONTENTS_REMOTE);
        }
        if (!isExit(thing))
        {
            look_exits(executor, thing, "Obvious exits:");
        }
    }
    free_lbuf(temp);

    if (!control)
    {
        if (mudconf.read_rem_name)
        {
            buf2 = alloc_lbuf("do_examine.pub_name");
            strcpy(buf2, Name(thing));
            notify(executor, tprintf("%s is owned by %s", buf2, Name(Owner(thing))));
            free_lbuf(buf2);
        }
        else
        {
            notify(executor, tprintf("Owned by %s", Name(Owner(thing))));
        }
    }
}

void do_score(dbref executor, dbref caller, dbref enactor, int key)
{
    notify(executor, tprintf("You have %d %s.", Pennies(executor),
        (Pennies(executor) == 1) ?  mudconf.one_coin : mudconf.many_coins));
}

void do_inventory(dbref executor, dbref caller, dbref enactor, int key)
{
    dbref thing;
    char *buff, *e;
    const char *s;

    thing = Contents(executor);
    if (thing == NOTHING)
    {
        notify(executor, "You aren't carrying anything.");
    }
    else
    {
        notify(executor, "You are carrying:");
        DOLIST(thing, thing)
        {
            buff = unparse_object(executor, thing, true);
            notify(executor, buff);
            free_lbuf(buff);
        }
    }

    thing = Exits(executor);
    if (thing != NOTHING)
    {
        notify(executor, "Exits:");
        e = buff = alloc_lbuf("look_exits");
        DOLIST(thing, thing)
        {
            // Chop off first exit alias to display.
            //
            for (s = Name(thing); *s && (*s != ';'); s++)
            {
                safe_chr(*s, buff, &e);
            }
            safe_str("  ", buff, &e);
        }
        *e = 0;
        notify(executor, buff);
        free_lbuf(buff);
    }
    do_score(executor, caller, executor, 0);
}

void do_entrances(dbref executor, dbref caller, dbref enactor, int key, char *name)
{
    dbref thing, i, j;
    char *exit, *message;
    int control_thing, count, low_bound, high_bound;
    FWDLIST *fp;

    parse_range(&name, &low_bound, &high_bound);
    if (  !name
       || !*name)
    {
        if (Has_location(executor))
        {
            thing = Location(executor);
        }
        else
        {
            thing = executor;
        }
        if (!Good_obj(thing))
        {
            return;
        }
    }
    else
    {
        init_match(executor, name, NOTYPE);
        match_everything(MAT_EXIT_PARENTS);
        thing = noisy_match_result();
        if (!Good_obj(thing))
        {
            return;
        }
    }

    if (!payfor(executor, mudconf.searchcost))
    {
        notify(executor, tprintf("You don't have enough %s.",
            mudconf.many_coins));
        return;
    }
    message = alloc_lbuf("do_entrances");
    control_thing = Examinable(executor, thing);
    count = 0;
    for (i = low_bound; i <= high_bound; i++)
    {
        if (control_thing || Examinable(executor, i))
        {
            switch (Typeof(i))
            {
            case TYPE_EXIT:
                if (Location(i) == thing)
                {
                    exit = unparse_object(executor, Exits(i), false);
                    notify(executor, tprintf("%s (%s)", exit, Name(i)));
                    free_lbuf(exit);
                    count++;
                }
                break;
            case TYPE_ROOM:
                if (Dropto(i) == thing)
                {
                    exit = unparse_object(executor, i, false);
                    notify(executor, tprintf("%s [dropto]", exit));
                    free_lbuf(exit);
                    count++;
                }
                break;
            case TYPE_THING:
            case TYPE_PLAYER:
                if (Home(i) == thing)
                {
                    exit = unparse_object(executor, i, false);
                    notify(executor, tprintf("%s [home]", exit));
                    free_lbuf(exit);
                    count++;
                }
                break;
            }

            // Check for parents.
            //
            if (Parent(i) == thing)
            {
                exit = unparse_object(executor, i, false);
                notify(executor, tprintf("%s [parent]", exit));
                free_lbuf(exit);
                count++;
            }

            // Check for forwarding.
            //
            if (H_Fwdlist(i))
            {
                fp = fwdlist_get(i);
                if (!fp)
                {
                    continue;
                }
                for (j = 0; j < fp->count; j++)
                {
                    if (fp->data[j] != thing)
                    {
                        continue;
                    }
                    exit = unparse_object(executor, i, false);
                    notify(executor, tprintf("%s [forward]", exit));
                    free_lbuf(exit);
                    count++;
                }
            }
        }
    }
    free_lbuf(message);
    notify(executor, tprintf("%d entrance%s found.", count,
        (count == 1) ? "" : "s"));
}

// Check the current location for bugs.
//
static void sweep_check(dbref player, dbref what, int key, bool is_loc)
{
    dbref aowner, parent;
    int atr, aflags, lev;
    char *buf, *buf2, *bp, *as, *buff, *s;
    ATTR *ap;

    bool canhear    = false;
    bool cancom     = false;
    bool isplayer   = false;
    bool ispuppet   = false;
    bool isconnected = false;
    bool is_parent  = false;

    if (  (key & SWEEP_LISTEN)
       && (  (  isExit(what)
             || is_loc)
          && Audible(what)))
    {
        canhear = true;
    }
    else if (key & SWEEP_LISTEN)
    {
        buff = NULL;
        if (Monitor(what))
        {
            buff = alloc_lbuf("Hearer");
        }

        for (atr = atr_head(what, &as); atr; atr = atr_next(&as))
        {
            if (atr == A_LISTEN)
            {
                canhear = true;
                break;
            }
            if (Monitor(what))
            {
                ap = atr_num(atr);
                if (  !ap
                   || (ap->flags & AF_NOPROG))
                {
                    continue;
                }

                atr_get_str(buff, what, atr, &aowner, &aflags);

                // Make sure we can execute it.
                //
                if (  (buff[0] != AMATCH_LISTEN)
                   || (aflags & AF_NOPROG))
                {
                    continue;
                }

                // Make sure there's a : in it.
                //
                for (s = buff + 1; *s && (*s != ':'); s++)
                {
                    ; // Nothing.
                }
                if (s)
                {
                    canhear = true;
                    break;
                }
            }
        }
        if (buff)
        {
            free_lbuf(buff);
        }
    }
    if (  (key & SWEEP_COMMANDS)
       && !isExit(what))
    {
        // Look for commands on the object and parents too.
        //
        ITER_PARENTS(what, parent, lev)
        {
            if (Commer(parent))
            {
                cancom = true;
                if (lev)
                {
                    is_parent = true;
                    break;
                }
            }
        }
    }
    if (key & SWEEP_CONNECT)
    {
        if (  Connected(what)
           || (  Puppet(what)
              && Connected(Owner(what)))
           || (  mudconf.player_listen
              && isPlayer(what)
              && canhear
              && Connected(Owner(what))))
        {
            isconnected = true;
        }
    }
    if (  (key & SWEEP_PLAYER)
       || isconnected)
    {
        if (isPlayer(what))
        {
            isplayer = true;
        }
        if (Puppet(what))
        {
            ispuppet = true;
        }
    }
    if (  canhear
       || cancom
       || isplayer
       || ispuppet
       || isconnected)
    {
        buf = alloc_lbuf("sweep_check.types");
        bp = buf;

        if (cancom)
        {
            safe_str("commands ", buf, &bp);
        }
        if (canhear)
        {
            safe_str("messages ", buf, &bp);
        }
        if (isplayer)
        {
            safe_str("player ", buf, &bp);
        }
        if (ispuppet)
        {
            safe_str("puppet(", buf, &bp);
            safe_str(Name(Owner(what)), buf, &bp);
            safe_str(") ", buf, &bp);
        }
        if (isconnected)
        {
            safe_str("connected ", buf, &bp);
        }
        if (is_parent)
        {
            safe_str("parent ", buf, &bp);
        }
        bp[-1] = '\0';
        if (!isExit(what))
        {
            notify(player, tprintf("  %s is listening. [%s]",
                Name(what), buf));
        }
        else
        {
            buf2 = alloc_lbuf("sweep_check.name");
            strcpy(buf2, Name(what));
            for (bp = buf2; *bp && (*bp != ';'); bp++) ;
            *bp = '\0';
            notify(player, tprintf("  %s is listening. [%s]", buf2, buf));
            free_lbuf(buf2);
        }
        free_lbuf(buf);
    }
}

void do_sweep(dbref executor, dbref caller, dbref enactor, int key, char *where)
{
    dbref here, sweeploc;
    int where_key, what_key;

    where_key = key & (SWEEP_ME | SWEEP_HERE | SWEEP_EXITS);
    what_key = key & (SWEEP_COMMANDS | SWEEP_LISTEN | SWEEP_PLAYER | SWEEP_CONNECT);

    if (where && *where)
    {
        sweeploc = match_controlled(executor, where);
        if (!Good_obj(sweeploc))
        {
            return;
        }
    }
    else
    {
        sweeploc = executor;
    }

    if (!where_key)
        where_key = -1;
    if (!what_key)
        what_key = -1;
    else if (what_key == SWEEP_VERBOSE)
        what_key = SWEEP_VERBOSE | SWEEP_COMMANDS;

    // Check my location.  If I have none or it is dark, check just me.
    //
    if (where_key & SWEEP_HERE)
    {
        notify(executor, "Sweeping location...");
        if (Has_location(sweeploc))
        {
            here = Location(sweeploc);
            if (  here == NOTHING
               || (  Dark(here)
                  && !mudconf.sweep_dark
                  && !Examinable(executor, here)))
            {
                notify_quiet(executor,
                    "Sorry, it is dark here and you can't search for bugs");
                sweep_check(executor, sweeploc, what_key, false);
            }
            else
            {
                sweep_check(executor, here, what_key, true);
                for (here = Contents(here); here != NOTHING; here = Next(here))
                {
                    sweep_check(executor, here, what_key, false);
                }
            }
        }
        else
        {
            sweep_check(executor, sweeploc, what_key, false);
        }
    }

    // Check exits in my location
    //
    if (  (where_key & SWEEP_EXITS)
       && Has_location(sweeploc))
    {
        notify(executor, "Sweeping exits...");
        for (here = Exits(Location(sweeploc)); here != NOTHING; here = Next(here))
        {
            sweep_check(executor, here, what_key, false);
        }
    }

    // Check my inventory
    //
    if (  (where_key & SWEEP_ME)
       && Has_contents(sweeploc))
    {
        notify(executor, "Sweeping inventory...");
        for (here = Contents(sweeploc); here != NOTHING; here = Next(here))
        {
            sweep_check(executor, here, what_key, false);
        }
    }

    // Check carried exits
    //
    if (  (where_key & SWEEP_EXITS)
       && Has_exits(sweeploc))
    {
        notify(executor, "Sweeping carried exits...");
        for (here = Exits(sweeploc); here != NOTHING; here = Next(here))
        {
            sweep_check(executor, here, what_key, false);
        }
    }
    notify(executor, "Sweep complete.");
}

/* Output the sequence of commands needed to duplicate the specified
 * object.  If you're moving things to another system, your mileage
 * will almost certainly vary.  (i.e. different flags, etc.)
 */

extern NAMETAB indiv_attraccess_nametab[];

void do_decomp
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *name,
    char *qual
)
{
    BOOLEXP *pBoolExp;
    char *got, *thingname, *as, *ltext, *buff;
    dbref aowner, thing;
    int val, aflags, ca;
    ATTR *pattr;
    NAMETAB *np;
    bool wild_decomp;

    // Check for obj/attr first.
    //
    olist_push();
    if (parse_attrib_wild(executor, name, &thing, false, true, false))
    {
        wild_decomp = true;
    }
    else
    {
        wild_decomp = false;
        init_match(executor, name, TYPE_THING);
        match_everything(MAT_EXIT_PARENTS);
        thing = noisy_match_result();
    }

    // get result
    //
    if (thing == NOTHING)
    {
        olist_pop();
        return;
    }

    if (!Examinable(executor, thing))
    {
        notify_quiet(executor,
              "You can only decompile things you can examine.");
        olist_pop();
        return;
    }

    thingname = atr_get(thing, A_LOCK, &aowner, &aflags);
    pBoolExp = parse_boolexp(executor, thingname, true);

    // Determine the name of the thing to use in reporting and then
    // report the command to make the thing.
    //
    if (qual && *qual)
    {
        strcpy(thingname, qual);
    }
    else
    {
        if (key == DECOMP_DBREF)
        {
            strcpy(thingname, tprintf("#%d",thing));
        }
        else
        {
            switch (Typeof(thing))
            {
            case TYPE_THING:
                strcpy(thingname, Name(thing));
                val = OBJECT_DEPOSIT(Pennies(thing));
                notify(executor,
                    tprintf("@create %s=%d", translate_string(thingname, true),
                    val));
                break;

            case TYPE_ROOM:
                strcpy(thingname, "here");
                notify(executor, tprintf("@dig/teleport %s",
                    translate_string(Name(thing), true)));
                break;

            case TYPE_EXIT:
                strcpy(thingname, Name(thing));
                notify(executor,
                    tprintf("@open %s", translate_string(thingname, true)));
                for (got = thingname; *got; got++)
                {
                    if (*got == EXIT_DELIMITER)
                    {
                        *got = '\0';
                        break;
                    }
                }
                break;

            case TYPE_PLAYER:
                if (executor == thing)
                {
                    strcpy(thingname, "me");
                }
                else
                {
                    strcpy(thingname, Name(thing));
                }
                break;
            }
        }
    }

    // Strip out ANSI in one place rather than have it done in
    // several places.
    //
    size_t len;
    char *p = strip_ansi(thingname, &len);
    memcpy(thingname, p, len+1);

    // Report the lock (if any).
    //
    if (  !wild_decomp
       && pBoolExp != TRUE_BOOLEXP)
    {
        notify(executor, tprintf("@lock %s=%s", thingname,
            unparse_boolexp_decompile(executor, pBoolExp)));
    }
    free_boolexp(pBoolExp);

    // Report attributes.
    //
    buff = alloc_mbuf("do_decomp.attr_name");
    for (ca = (wild_decomp ? olist_first() : atr_head(thing, &as));
        (wild_decomp) ? (ca != NOTHING) : (ca != 0);
        ca = (wild_decomp ? olist_next() : atr_next(&as)))
    {
        if (  ca == A_NAME
           || ca == A_LOCK)
        {
            continue;
        }
        pattr = atr_num(ca);
        if (!pattr)
        {
            continue;
        }
        if (  (pattr->flags & AF_NOCMD)
           && !(pattr->flags & AF_IS_LOCK))
        {
            continue;
        }

        got = atr_get(thing, ca, &aowner, &aflags);
        if (bCanReadAttr(executor, thing, pattr, false))
        {
            if (pattr->flags & AF_IS_LOCK)
            {
                pBoolExp = parse_boolexp(executor, got, true);
                ltext = unparse_boolexp_decompile(executor, pBoolExp);
                free_boolexp(pBoolExp);
                notify(executor, tprintf("@lock/%s %s=%s", pattr->name,
                    thingname, ltext));
            }
            else
            {
                strcpy(buff, pattr->name);
                notify(executor, tprintf("%c%s %s=%s", ((ca < A_USER_START) ?
                    '@' : '&'), buff, thingname, got));
                for (np = indiv_attraccess_nametab; np->name; np++)
                {
                    if (  (aflags & np->flag)
                       && check_access(executor, np->perm)
                       && (!(np->perm & CA_NO_DECOMP)))
                    {
                        notify(executor, tprintf("@set %s/%s = %s", thingname,
                            buff, np->name));
                    }
                }

                if (aflags & AF_LOCK)
                {
                    notify(executor, tprintf("@lock %s/%s", thingname, buff));
                }
            }
        }
        free_lbuf(got);
    }
    free_mbuf(buff);

    if (!wild_decomp)
    {
        decompile_flags(executor, thing, thingname);
        decompile_powers(executor, thing, thingname);
#ifdef REALITY_LVLS
        decompile_rlevels(executor, thing, thingname);
#endif /* REALITY_LVLS */
    }

    // If the object has a parent, report it.
    //
    if (  !wild_decomp
       && (Parent(thing) != NOTHING))
    {
        notify(executor, tprintf("@parent %s=#%d", thingname, Parent(thing)));
    }

    // If the object has a zone, report it.
    //
    int zone;
    if (  !wild_decomp
       && Good_obj(zone = Zone(thing)))
    {
        notify(executor, tprintf("@chzone %s=#%d", thingname, zone));
    }

    free_lbuf(thingname);
    olist_pop();
}

// show_vrml_url
//
void show_vrml_url(dbref thing, dbref loc)
{
    char *vrml_url;
    dbref aowner;
    int aflags;

    // If they don't care about HTML, just return.
    //
    if (!Html(thing))
    {
        return;
    }

    vrml_url = atr_pget(loc, A_VRML_URL, &aowner, &aflags);
    if (*vrml_url)
    {
        char *vrml_message, *vrml_cp;

        vrml_message = vrml_cp = alloc_lbuf("show_vrml_url");
        safe_str("<img xch_graph=load href=\"", vrml_message, &vrml_cp);
        safe_str(vrml_url, vrml_message, &vrml_cp);
        safe_str("\">", vrml_message, &vrml_cp);
        *vrml_cp = 0;
        notify_html(thing, vrml_message);
        free_lbuf(vrml_message);
    }
    else
    {
        notify_html(thing, "<img xch_graph=hide>");
    }
    free_lbuf(vrml_url);
}
