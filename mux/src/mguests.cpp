// mguests.cpp -- Multiguest code originally ported from DarkZone.
// Multiguest code rewritten by Matthew J. Leavitt (zenty).
// Idea for @list guest from Ashen-Shugar and the great team of RhostMUSH
//
// $Id: mguests.cpp,v 1.3 2002-06-04 18:12:00 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <stdlib.h>

#include "mudconf.h"
#include "mguests.h"
#include "db.h"
#include "interface.h"
#include "mail.h"
#include "attrs.h"
#include "powers.h"

#define GUEST_HYSTERESIS 20

CGuests Guest;

CGuests::CGuests(void)
{
    Guests = NULL;
    nGuests = 0;
    nMaxGuests = 0;
}

CGuests::~CGuests(void)
{
    if (Guests)
    {
        MEMFREE(Guests);
        Guests = NULL;
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

    SizeGuests(mudconf.min_guests);

    // Search the Database for Guest chars and snag em.
    //
    int i;
    for (i = 0; i < mudconf.number_guests; i++)
    {
        sprintf(name, "%s%d", mudconf.guest_prefix, i + 1);
        dbref player = lookup_player(GOD, name, 0);
        if (  player != NOTHING
           && Guest(player))
        {
            SizeGuests(nGuests+1);
            Guests[nGuests] = player;
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
    if (Guests)
    {
        memcpy(newGuests, Guests, nGuests * sizeof(dbref));
        MEMFREE(Guests);
        Guests = NULL;
    }
    Guests = newGuests;
    nMaxGuests = nMin;
}

char *CGuests::Create(DESC *d)
{
    // We don't have a main guest character, break out.
    //
    if (!Good_obj(mudconf.guest_char))
    {
        return NULL;
    }

    // If we have a guest character, let's use it
    //
    int i;
    for (i = 0; i < nGuests; i++)
    {
        // If we have something that isn't a guest in the list, lets
        // just drop it and make a new one.
        //
        if (  !Good_obj(Guests[i])
           || isGarbage(Guests[i])
           || !isPlayer(Guests[i])
           || !Guest(Guests[i]))
        {
            Guests[i] = MakeGuestChar();
            return Name(Guests[i]);
        }

        if (!Connected(Guests[i]))
        {
            // Lets try to grab our own name, if we don't have it.
            //
            sprintf(name, "%s%d", mudconf.guest_prefix, nGuests);
            dbref player = lookup_player(GOD, name, 0);
            if (player == NOTHING)
            {
                delete_player_name(Guests[i], Name(Guests[i]));
                s_Name(Guests[i], name);
                add_player_name(Guests[i], Name(Guests[i]));
            }

            // Reset the flags back to the default.
            //
            db[Guests[i]].fs = mudconf.player_flags;

            // Add the type and remove wizard.
            //
            db[Guests[i]].fs.word[FLAG_WORD1] |= TYPE_PLAYER;
            db[Guests[i]].fs.word[FLAG_WORD1] &= ~WIZARD;

            // Make sure they're a guest.
            //
            s_Guest(player);

            move_object(player, mudconf.start_room);
            s_Pennies(player, Pennies(mudconf.guest_char));
            s_Zone(player, Zone(mudconf.guest_char));
            s_Parent(player, Parent(mudconf.guest_char));

            // Wipe the attributes.
            //
            WipeAttrs(Guests[i]);
            s_Pass(player, crypt(GUEST_PASSWORD, "XX"));

            // Copy them back.
            //
            atr_cpy(GOD, player, mudconf.guest_char);
            return Name(Guests[i]);
        }
    }

    if (nGuests >= mudconf.number_guests)
    {
        queue_string(d, "Sorry, All guests are currently busy. Try again later.\n");
        return NULL;
    }
    dbref newGuest = MakeGuestChar();
    if (newGuest == NOTHING)
    {
        queue_string(d, "GAME: Error creating guest, please try again later.\n");
        return NULL;
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
           || isGarbage(Guests[i])
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
            if (  Good_obj(Guests[i])
               && !isGarbage(Guests[i]))
            {
                DestroyGuestChar(Guests[i]);
            }
            nGuests--;
        }
    }
    SizeGuests(nGuests);
}

int CGuests::MakeGuestChar(void)
{
    // Search for the first available name.
    //
    int i;
    dbref player;
    for (i = 0; i < mudconf.number_guests;i++)
    {
        sprintf(name, "%s%d", mudconf.guest_prefix, i + 1);
        player = lookup_player(GOD, name, 0);
        if (player == NOTHING)
        {
            break;
        }
    }

    // Make the player.
    //
    player = create_player(name, GUEST_PASSWORD, mudconf.guest_nuker, 0, 1);

    // No Player Created?? Return error.
    //
    if (player == NOTHING)
    {
        log_text("GUEST: failed in create_player" ENDLINE);
        return NOTHING;
    }

    // Lets make the player a guest, move it into the starting room,
    // don't let it be a wizard, and setup other basics.
    //
    s_Guest(player);
    move_object(player, mudconf.start_room);
    db[player].fs.word[FLAG_WORD1] &= ~WIZARD;
    s_Pennies(player, Pennies(mudconf.guest_char));
    s_Zone(player, Zone(mudconf.guest_char));
    s_Parent(player, Parent(mudconf.guest_char));

    // Copy the attributes.
    //
    atr_cpy(GOD, player, mudconf.guest_char);

    // Lock em!
    //
    do_lock(player, player, player, A_LOCK, 2, tprintf("#%d", player), "=me");
    do_lock(player, player, player, A_LENTER, 2, tprintf("#%d", player), "=me");

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
        mudconf.guest_nuker = 1;
    }

    // Destroy it!
    //
    destroy_player(mudconf.guest_nuker, guest);
}

void CGuests::WipeAttrs(dbref guest)
{
    char *atext = alloc_lbuf("do_wipe.atext");
    olist_push();
    int attr;
    for (attr = olist_first(); attr != NOTHING; attr = olist_next())
    {
        ATTR *ap = atr_num(attr);
        if (ap)
        {
            // Get the attr and make sure we can modify it.
            //
            dbref aowner;
            int   aflags;
            atr_get_str(atext, guest, ap->number, &aowner, &aflags);
            if (Set_attr(1, guest, ap, aflags))
            {
                atr_clr(guest, ap->number);
            }
        }
    }
    olist_pop();
    free_lbuf(atext);
}

BOOL CGuests::CheckGuest(dbref player)
{
    int i;
    for (i = 0; i < nGuests; i++)
    {
        if (Guests[i] == player)
        {
            return TRUE;
        }
    }
    return FALSE;
}

// @list guests, thanks Rhost for the idea!
//
void CGuests::ListAll(dbref player)
{
    notify(player, "--------------------------- Current Guests Listing ---------------------------");
    notify(player, "*Guest #  : Name            dbref  Status     Last Site");
    notify(player, "------------------------------------------------------------------------------");\
    char *buff = alloc_lbuf("CGuests-ListAll");
    int i;
    char *LastSite=alloc_lbuf("CGuests-LastSite");
    for (i = 0; i < nGuests; i++)
    {
        dbref aowner;
        int aflags;
        atr_get_str(LastSite, Guests[i], A_LASTSITE, &aowner, &aflags);
        sprintf(buff, "%sGuest %-3d: %-15s #%-5d %-10s %s",
                (i<mudconf.min_guests ? "*" : " "),
                i, Name(Guests[i]), Guests[i],
                (Connected(Guests[i]) ? "Online" : "NotOnline"),
                LastSite);
        notify(player, buff);
        if (!Good_obj(Guests[i]))
        {
            notify(player, tprintf("*** Guest %d (#%d) is an invalid object!",
                                   i, Guests[i]));
        }
    }
    free_lbuf(LastSite);
    notify(player, tprintf("-----------------------------  Total Guests: %-3d -----------------------------", nGuests));
    free_lbuf(buff);
}

char CGuests::name[50];
