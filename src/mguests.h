// mguests.h
//
// $Id: mguests.h,v 1.4 2002-02-13 19:21:41 sdennis Exp $
//

#ifndef  __MGUESTS_H
#define __MGUESTS_H

#include "copyright.h"
#include "autoconf.h"
#include "interface.h"

// Zenty does OOP.
//
class CGuests
{
private:
    dbref *Guests;                 // Main housing of guest dbrefs
    int   nGuests;                 // Number of them
    void  GrowGuests(int);         // They keep growing and growing
    int   MakeGuestChar(void);     // Make the guest character
    void  DestroyGuestChar(dbref); // Destroy the guest character
    void  WipeAttrs(dbref guest);  // Wipe all the attrbutes

public:
    void  ListAll(dbref);          // @list guests
    void  StartUp();               // Public handler for Startup
    char  *Create(DESC *d);        // Public handler for creation
    void  CleanUp(void);           // Public handler for cleaning guests.
};

extern CGuests Guest;

// Zenty's Not-So-Secret Guest Password.
//
#define GUEST_PASSWORD "ZentyRULEZ" // Zenty: You know it baby!

#endif
