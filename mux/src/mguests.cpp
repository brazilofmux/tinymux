/*! \file mguests.cpp
 * \brief Multiguest system.
 *
 * Multiguest code originally ported from DarkZone.
 * Multiguest code rewritten by Matthew J. Leavitt (zenty).
 * Idea for \@list guest from Ashen-Shugar and the great team of RhostMUSH.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <stdlib.h>

#include "attrs.h"
#include "comsys.h"
#include "mguests.h"
#include "powers.h"

#define GUEST_HYSTERESIS 20

CGuests Guest;

CGuests::CGuests(void)
{
    Guests = nullptr;
    nGuests = 0;
    nMaxGuests = 0;
}

CGuests::~CGuests(void)
{
    if (Guests)
    {
        MEMFREE(Guests);
        Guests = nullptr;
        nGuests = 0;
        nMaxGuests = 0;
    }
}

void CGuests::StartUp(void)
{
    if (  !Good_obj(mudconf.guest_char)
       || mudconf.number_guests <= 0)
    {
        // The guest system is effectively disabled.
        //
        return;
    }


    // SizeGuests(mudconf.min_guests);

    // Search the Database for Guest chars and snag em.
    //
    dbref thing;
    DO_WHOLE_DB(thing)
    {
        if (  Guest(thing)
           && isPlayer(thing))
        {
            SizeGuests(nGuests+1);
            Guests[nGuests] = thing;
            nGuests++;
        }
    }

    // Create the Minimum # of guests.
    //
    for (; nGuests < mudconf.min_guests; nGuests++)
    {
        SizeGuests(nGuests+1);
        Guests[nGuests] = MakeGuestChar();

        // If we couldn't create guest character, break out and let later
        // functions handle it.
        //
        if (Guests[nGuests] == NOTHING)
        {
            break;
        }
    }
}

void CGuests::SizeGuests(int nMin)
{
    // We must have at least nMin, but if nMin is sufficiently below
    // nMaxGuests, we can also shrink.
    //
    if (nMin < nGuests)
    {
        nMin = nGuests;
    }
    if (  nMaxGuests <= nMin + GUEST_HYSTERESIS
       && nMin <= nMaxGuests)
    {
        return;
    }

    dbref *newGuests = (dbref *)MEMALLOC(nMin * sizeof(dbref));
    ISOUTOFMEMORY(newGuests);
    if (Guests)
    {
        memcpy(newGuests, Guests, nGuests * sizeof(dbref));
        MEMFREE(Guests);
        Guests = nullptr;
    }
    Guests = newGuests;
    nMaxGuests = nMin;
}

const UTF8 *CGuests::Create(DESC *d)
{
    // If we have a guest character, let's use it
    //
    int i;
    for (i = 0; i < nGuests; i++)
    {
        dbref guest_player = Guests[i];

        // If we have something in the list that isn't a guest, lets
        // just drop it and make a new one.
        //
        if (  !Good_obj(guest_player)
           || !isPlayer(guest_player)
           || !Guest(guest_player))
        {
            guest_player = Guests[i] = MakeGuestChar();
            if (NOTHING == guest_player)
            {
                queue_string(d, T("Error creating guest, please try again later.\n"));
                return nullptr;
            }
            else
            {
                return Name(guest_player);
            }
        }

        if (!Connected(guest_player))
        {
            // Lets try to grab our own name, if we don't have it.
            //
            bool bAlias = false;
            mux_sprintf(name, sizeof(name), T("%s%d"), mudconf.guest_prefix, i+1);
            dbref j = lookup_player_name(name, bAlias);
            if (NOTHING == j)
            {
                delete_player_name(guest_player, Name(guest_player), false);
                s_Name(guest_player, name);
                add_player_name(guest_player, Name(guest_player), false);
            }
            else
            {
                // Release comsys and @mail state.
                //
                ReleaseAllResources(guest_player);
                AddToGuestChannel(guest_player);
            }

            // Copy flags from guest prototype.
            //
            db[guest_player].fs = db[mudconf.guest_char].fs;

            // Strip flags, enforce PLAYER type.
            //
            FLAG aClearFlags[3];
            FLAG aSetFlags[3];

            aClearFlags[FLAG_WORD1] = WIZARD
                                    | TYPE_MASK
                                    | mudconf.stripped_flags.word[FLAG_WORD1];
            aClearFlags[FLAG_WORD2] = mudconf.stripped_flags.word[FLAG_WORD2];
            aClearFlags[FLAG_WORD3] = mudconf.stripped_flags.word[FLAG_WORD3];
            aSetFlags[FLAG_WORD1] = TYPE_PLAYER;
            aSetFlags[FLAG_WORD2] = 0;
            aSetFlags[FLAG_WORD3] = 0;

            SetClearFlags(guest_player, aClearFlags, aSetFlags);

            // Make sure they're a guest.
            //
            s_Guest(guest_player);

            move_object(guest_player, mudconf.start_room);
            s_Pennies(guest_player, Pennies(mudconf.guest_char));
            s_Zone(guest_player, Zone(mudconf.guest_char));
            s_Parent(guest_player, Parent(mudconf.guest_char));

            // Wipe the attributes.
            //
            WipeAttrs(guest_player);
            ChangePassword(guest_player, (UTF8 *)GUEST_PASSWORD);

            // Copy them back.
            //
            atr_cpy(guest_player, mudconf.guest_char, true);
            return Name(guest_player);
        }
    }

    if (nGuests >= mudconf.number_guests)
    {
        queue_string(d, T("All guests are currently busy, please try again later.\n"));
        return nullptr;
    }
    dbref newGuest = MakeGuestChar();
    if (newGuest == NOTHING)
    {
        queue_string(d, T("Error creating guest, please try again later.\n"));
        return nullptr;
    }
    SizeGuests(nGuests+1);
    Guests[nGuests] = newGuest;
    nGuests++;
    return Name(newGuest);
}

void CGuests::CleanUp(void)
{
    // Verify that our existing pool of guests are reasonable. Replace any
    // unreasonable dbrefs.
    //
    int nPool = nGuests;
    if (mudconf.min_guests < nGuests)
    {
        nPool = mudconf.min_guests;
    }
    int i;
    for (i = 0; i < nPool; i++)
    {
        if (  !Good_obj(Guests[i])
           || !isPlayer(Guests[i])
           || !Guest(Guests[i]))
        {
            Guests[i] = MakeGuestChar();
        }
    }

    if (nGuests <= mudconf.min_guests)
    {
        return;
    }

    // We have more than the minimum number of guests in the pool. Let's
    // see if there are any guests beyond that minimum number that we can
    // trim out of the pool.

    // PASS 1: Move connected guests to the beginning of the list.
    //
    int itmp;
    int currGuest = mudconf.min_guests;
    for (i = mudconf.min_guests; i < nGuests; i++)
    {
        if (Connected(Guests[i]))
        {
            if (i > currGuest)
            {
                itmp = Guests[currGuest];
                Guests[currGuest] = Guests[i];
                Guests[i] = itmp;
            }
            currGuest++;
        }
    }

    // PASS 2: Destroy unconnected guests.
    //
    itmp = nGuests;
    for (i = mudconf.min_guests; i < itmp;i++)
    {
        if (!Connected(Guests[i]))
        {
            if (Good_obj(Guests[i]))
            {
                DestroyGuestChar(Guests[i]);
            }
            nGuests--;
        }
    }
    SizeGuests(nGuests);
}

dbref CGuests::MakeGuestChar(void)
{
    // Search for the first available name.
    //
    int i;
    dbref player;
    bool bFound = false;
    for (i = 0; i < mudconf.number_guests;i++)
    {
        bool bAlias = false;
        mux_sprintf(name, sizeof(name), T("%s%d"), mudconf.guest_prefix, i + 1);
        player = lookup_player_name(name, bAlias);
        if (NOTHING == player)
        {
            bFound = true;
            break;
        }
    }
    if (!bFound)
    {
        return NOTHING;
    }

    // Make the player.
    //
    const UTF8 *pmsg;
    player = create_player(name, (UTF8 *)GUEST_PASSWORD, mudconf.guest_nuker, false, &pmsg);

    // No Player Created?? Return error.
    //
    if (player == NOTHING)
    {
        log_text(T("GUEST: failed in create_player" ENDLINE));
        return NOTHING;
    }

    // Lets make the player a guest, move it into the starting room,
    // don't let it be a wizard, and setup other basics.
    //
    AddToGuestChannel(player);
    s_Guest(player);
    move_object(player, mudconf.start_room);

    // Copy flags from guest prototype and restore the player type.
    //
    FLAGSET f = db[mudconf.guest_char].fs;
    f.word[FLAG_WORD1] |= TYPE_PLAYER;
    db[player].fs = f;

    // Strip flags.
    //
    FLAG aClearFlags[3];

    aClearFlags[FLAG_WORD1] = WIZARD
                            | mudconf.stripped_flags.word[FLAG_WORD1];
    aClearFlags[FLAG_WORD2] = mudconf.stripped_flags.word[FLAG_WORD2];
    aClearFlags[FLAG_WORD3] = mudconf.stripped_flags.word[FLAG_WORD3];

    SetClearFlags(player, aClearFlags, nullptr);

    s_Pennies(player, Pennies(mudconf.guest_char));
    s_Zone(player, Zone(mudconf.guest_char));
    s_Parent(player, Parent(mudconf.guest_char));

    // Copy the attributes.
    //
    atr_cpy(player, mudconf.guest_char, true);

    // Lock em!
    //
    do_lock(player, player, player, 0, A_LOCK, 2, tprintf(T("#%d"), player), (UTF8 *)"=me", nullptr, 0);
    do_lock(player, player, player, 0, A_LENTER, 2, tprintf(T("#%d"), player), (UTF8 *)"=me", nullptr, 0);

    // return em!
    //
    return player;
}

void CGuests::DestroyGuestChar(dbref guest)
{
    // Don't destroy anything not a guest.
    //
    if (!Guest(guest))
    {
        return;
    }

    // Make sure the nuker is ok.
    //
    if (  !Wizard(mudconf.guest_nuker)
       || !Good_obj(mudconf.guest_nuker))
    {
        mudconf.guest_nuker = GOD;
    }

    // Destroy it!
    //
    destroy_player(mudconf.guest_nuker, guest);
}

void CGuests::WipeAttrs(dbref guest)
{
    olist_push();

    int atr;
    unsigned char *as;
    for (atr = atr_head(guest, &as); atr; atr = atr_next(&as))
    {
        ATTR *ap = atr_num(atr);
        if (ap)
        {
            // Get the attr and make sure we can modify it.
            //
            if (bCanSetAttr(GOD, guest, ap))
            {
                atr_clr(guest, ap->number);
            }
        }
    }
    olist_pop();
}

bool CGuests::CheckGuest(dbref player)
{
    int i;
    for (i = 0; i < nGuests; i++)
    {
        if (Guests[i] == player)
        {
            return true;
        }
    }
    return false;
}

// @list guests, thanks Rhost for the idea!
//
void CGuests::ListAll(dbref player)
{
    notify(player, T("--------------------------- Current Guests Listing ---------------------------"));
    notify(player, T("*Guest #  : Name            dbref  Status     Last Site"));
    notify(player, T("------------------------------------------------------------------------------"));\
    UTF8 *buff = alloc_lbuf("CGuests-ListAll");
    int i;
    UTF8 *LastSite=alloc_lbuf("CGuests-LastSite");
    for (i = 0; i < nGuests; i++)
    {
        dbref aowner;
        int aflags;
        atr_get_str(LastSite, Guests[i], A_LASTSITE, &aowner, &aflags);
        mux_sprintf(buff, LBUF_SIZE, T("%sGuest %-3d: %-15s #%-5d %-10s %s"),
                (i<mudconf.min_guests ? "*" : " "),
                i, Name(Guests[i]), Guests[i],
                (Connected(Guests[i]) ? "Online" : "NotOnline"),
                LastSite);
        notify(player, buff);
        if (!Good_obj(Guests[i]))
        {
            notify(player, tprintf(T("*** Guest %d (#%d) is an invalid object!"),
                                   i, Guests[i]));
        }
    }
    free_lbuf(LastSite);
    notify(player, tprintf(T("-----------------------------  Total Guests: %-3d -----------------------------"), nGuests));
    free_lbuf(buff);
}

void CGuests::AddToGuestChannel(dbref player)
{
    if (  mudconf.guests_channel[0] != '\0'
       && mudconf.guests_channel_alias[0] != '\0')
    {
        do_addcom(player, player, player, 0, 0, 2,
            mudconf.guests_channel_alias, mudconf.guests_channel, nullptr, 0);
    }
}

UTF8 CGuests::name[50];
