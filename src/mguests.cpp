// mguests.cpp -- Multiguest code originally ported from DarkZone.
// Multiguest code rewritten by Matthew J. Leavitt (zenty).
// Idea for @list guest from Ashen-Shugar and the great team of RhostMUSH
//
// $Id: mguests.cpp,v 1.11 2002-02-13 19:54:02 sdennis Exp $
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

CGuests Guest;

void CGuests::StartUp(void) 
{
    // Reset the Number of Guests.
    //
    nGuests = 0;

    // No Main Guest Char, or number_guests 0
    //
    if (  !Good_obj(mudconf.guest_char)
       || mudconf.number_guests <= 0)
    {
        return;
    }

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
            GrowGuests(1);
            Guests[nGuests] = player;
            nGuests++;
        }
    }
    
    // Create the Minimum # of guests.
    //
    for (; nGuests < mudconf.min_guests; nGuests++)
    {
        GrowGuests(1);
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

void CGuests::GrowGuests(int amount)
{
    int tGrow = nGuests + amount;
    dbref *newGuests = (dbref *)MEMALLOC((tGrow) * sizeof(dbref));
    memset(newGuests, 0, (tGrow * sizeof(dbref)));
    if (Guests)
    {
        memcpy(newGuests, Guests, (tGrow * sizeof(dbref)));
        MEMFREE(Guests);
        Guests = NULL;
    }
    Guests = newGuests;
}

char *CGuests::Create(DESC *d) 
{
    dbref player;

    // We don't have a main guest character, break out.
    //
    if (!Good_obj(mudconf.guest_char))
    {
        return NULL;
    }

    // If We Have a Guest char, let's use it
    //
    int i;
    for (i=0;i<nGuests;i++) {
        if(!Connected(Guests[i]) && Good_obj(Guests[i])) {
            // Lets try to grab our own name, if we don't have it.
            sprintf(name, "%s%d", mudconf.guest_prefix, nGuests);
            player = lookup_player(GOD, name, 0);
            if(player == NOTHING) {
                delete_player_name(Guests[i], Name(Guests[i]));
                s_Name(Guests[i], name);
                add_player_name(Guests[i], Name(Guests[i]));

            }

            // Reset the flags back to the default.
            db[(Guests[i])].fs = mudconf.player_flags;
            // Add the type and remove wizard.
            db[(Guests[i])].fs.word[FLAG_WORD1] |= TYPE_PLAYER;
            db[(Guests[i])].fs.word[FLAG_WORD1] &= ~WIZARD;
            // Wipe the attributes
            WipeAttrs(Guests[i]);
            // Copy them back.
            atr_cpy(GOD, player, mudconf.guest_char);
            return Name(Guests[i]);
        }
        
    }    

    if(nGuests == mudconf.number_guests) 
    {
        queue_string(d, "Sorry, All guests are currently busy. Try again later.\n");
        return NULL;
    }
    dbref newGuest=MakeGuestChar();
    if (newGuest == NOTHING) {
        queue_string(d, "GAME: Error creating guest ID, please try again later.\n");
        return NULL;
    }
    GrowGuests(1);
    Guests[nGuests]=newGuest;
    nGuests++;
    return Name(newGuest);
}

void CGuests::CleanUp(void) 
{
    int i, currGuest, itmp;

    // Move all connected guests to the beginning of the list.
    // If the number is less than the minimum, don't worry.
    if(nGuests <= mudconf.min_guests) return;

    // Don't screw with the min_guests
    
    for(i=currGuest=mudconf.min_guests;i<nGuests;i++) {
        if(Connected(Guests[i])) {
            if(i > currGuest) {
                itmp=Guests[currGuest];
                Guests[currGuest]=Guests[i];
                Guests[i]=itmp;
            }
            currGuest++;
        }
    }
        
    itmp=nGuests;
    for(i=mudconf.min_guests;i < itmp;i++) {
        if(!Connected(Guests[i])) {
            DestroyGuestChar(Guests[i]);
            nGuests--;
        }
    }
    // Let's chop down to a better size
    dbref *newGuests=(dbref *)MEMALLOC(nGuests * sizeof(dbref));
    memcpy(newGuests, Guests, (nGuests * sizeof(dbref)));
    char *cp=(char *)Guests;
    MEMFREE(cp);
    Guests = newGuests;
    nGuests=mudconf.min_guests;
}               

int CGuests::MakeGuestChar(void)
{
    int i;
    dbref player;

    // Search for the first available Name
    for (i = 0; i < mudconf.number_guests;i++) {
        sprintf(name, "%s%d", mudconf.guest_prefix, i + 1);
        player = lookup_player(GOD, name, 0);
        if (player == NOTHING)
            break;
    }
    
    // Make the player.
    player = create_player(name, GUEST_PASSWORD, mudconf.guest_nuker, 0, 1);

    // No Player Created?? Return error.
    if (player == NOTHING) {
        log_text("GUEST: failed in create_player" ENDLINE);
        return NOTHING;
    }

    // Lets make the player a guest
    s_Guest(player);
    // Move it into the starting room
    move_object(player, mudconf.start_room);
    // Don't let it be a wizard
    db[player].fs.word[FLAG_WORD1] &= ~WIZARD;
    // Setup basics
    s_Pennies(player, Pennies(mudconf.guest_char));
    s_Zone(player, Zone(mudconf.guest_char));
    s_Parent(player, Parent(mudconf.guest_char));

    // Lock em!
    do_lock(player, player, A_LOCK, 2, tprintf("#%d", player), "me");
    do_lock(player, player, A_LENTER, 2, tprintf("#%d", player), "me");

    // Copy the attributes.
    atr_cpy(GOD, player, mudconf.guest_char);
    // return em!
    return player;
}

void CGuests::DestroyGuestChar(dbref guest)
{
    // Don't destroy anything not a guest.
    if (!Guest(guest))
        return;

    // Make sure the nuker is ok
    if (!Wizard(mudconf.guest_nuker) || !Good_obj(mudconf.guest_nuker))
        mudconf.guest_nuker = 1;

    // Destroy it!
    atr_add_raw(guest, A_DESTROYER, Tiny_ltoa_t(mudconf.guest_nuker));
    destroy_player(mudconf.guest_nuker, guest);
}

void CGuests::WipeAttrs(dbref guest)
{
    dbref aowner;
    int attr, aflags;
    ATTR *ap;
    char *atext;

    olist_push();
    atext = alloc_lbuf("do_wipe.atext");

    for (attr = olist_first(); attr != NOTHING; attr = olist_next()) {
        ap = atr_num(attr);
        if (ap) {
            // Get the attr and make sure we can modify it.
            atr_get_str(atext, guest, ap->number, &aowner, &aflags);
            if (Set_attr(1, guest, ap, aflags))
                atr_clr(guest, ap->number);
        }
    }

    free_lbuf(atext);
    olist_pop();
}

// @list guests, thanks Rhost for the idea!
void CGuests::ListAll(dbref player)
{
    dbref aowner;
    int i, aflags;
    char *LastSite, *buff;

    notify(player, "--------------------------- Current Guests Listing ---------------------------");
    notify(player, "*Guest #  : Name            dbref  Status     Last Site");
    notify(player, "------------------------------------------------------------------------------");\
    buff=alloc_lbuf("CGuests-ListAll");
    for(i=0;i<nGuests;i++) {        
        LastSite = atr_get(Guests[i], A_LASTSITE, &aowner, &aflags);
        sprintf(buff, "%sGuest %-3d: %-15s #%-5d %-10s %s",
                (i<mudconf.min_guests ? "*" : " "),
                i, Name(Guests[i]), Guests[i],
                (Connected(Guests[i]) ? "Online" : "NotOnline"),
                LastSite);
        notify(player, buff);
        if(!Good_obj(Guests[i]))
            notify(player, tprintf("*** Guest %d (#%d) is an invalid object!",
                                   i, Guests[i]));
                                   
        free_lbuf(LastSite);
    }
    notify(player, tprintf("-----------------------------  Total Guests: %-3d -----------------------------", nGuests));
    free_lbuf(buff);
}

char CGuests::name[50];
