// mguests.cpp - multiguest code originally ported from DarkZone 
//
// $Id: mguests.cpp,v 1.5 2001-10-08 02:18:03 sdennis Exp $
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

typedef int object_flag_type;

dbref create_guest(char *name, char *password)
{
    dbref player;
    char *buff;

    if (!Wizard(mudconf.guest_nuker) || !Good_obj(mudconf.guest_nuker))
        mudconf.guest_nuker = 1;

    buff = alloc_lbuf("create_guest");

    /*
     * Make the player. 
     */

    player = create_player(name, password, mudconf.guest_nuker, 0, 1);

    if (player == NOTHING) {
        log_text("GUEST: failed in create_player" ENDLINE);
        return NOTHING;
    }
    /*
     * Turn the player into a guest. 
     */

    s_Guest(player);
    move_object(player, mudconf.start_room);
    s_Flags(player, Flags(player) & ~WIZARD);
    s_Pennies(player, Pennies(mudconf.guest_char));
    s_Zone(player, Zone(mudconf.guest_char));
    s_Parent(player, Parent(mudconf.guest_char));

    /*
     * Make sure the guest is locked. 
     */
    do_lock(player, player, A_LOCK, tprintf("#%d", player), "me");
    do_lock(player, player, A_LENTER, tprintf("#%d", player), "me");

    /*
     * Copy all attributes. 
     */
    atr_cpy(GOD, player, mudconf.guest_char);
    free_lbuf(buff);
    return player;
}

void destroy_guest(dbref guest)
{
    if (!Guest(guest))
    {
        return;
    }

    if (  !Wizard(mudconf.guest_nuker)
       || !Good_obj(mudconf.guest_nuker))
    {
        mudconf.guest_nuker = 1;
    }

    atr_add_raw(guest, A_DESTROYER, Tiny_ltoa_t(mudconf.guest_nuker));
    destroy_player(mudconf.guest_nuker, guest);
}

char *make_guest(DESC *d)
{
    int i;
    dbref player, p2;
    static char name[50];

    /*
     * Nuke extra guests as new guests connect. 
     */

    for (i = 0; i < mudconf.number_guests; i++) {
        sprintf(name, "%s%d", mudconf.guest_prefix, i + 1);
        p2 = lookup_player(GOD, name, 0);
        if (p2 != NOTHING && !Connected(p2))
            destroy_guest(p2);
    }

    /*
     * Locate a free guest ID, and eat it. 
     */

    for (i = 0; i < mudconf.number_guests; i++) {
        sprintf(name, "%s%d", mudconf.guest_prefix, i + 1);
        player = lookup_player(GOD, name, 0);
        if (player == NOTHING)
            break;
    }

    if (i == mudconf.number_guests) {
        queue_string(d, "GAME: All guest ID's are busy, please try again later.\n");
        return NULL;
    }
    sprintf(name, "%s%d", mudconf.guest_prefix, i + 1);
    player = create_guest(name, mudconf.guest_prefix);

    if (player == NOTHING) {
        queue_string(d, "GAME: Error creating guest ID, please try again later.\n");
        log_text(tprintf("GUEST: Error creating guest ID. '%s' already exists." ENDLINE,
                 name));
        return NULL;
    }
    return Name(player);
}
