// mguests.h
//
// $Id: mguests.h,v 1.3 2002-02-13 18:57:29 sdennis Exp $
//

#ifndef  __MGUESTS_H
#define __MGUESTS_H

#include "copyright.h"
#include "autoconf.h"
#include "interface.h"

// Object Oriented Programming. I should be shot!
class CGuests
{
 private:
    dbref *Guests; // Main housing of guest dbrefs
    int nGuests; // Number of them
    void GrowGuests(int); // They keep growing and growing
    int MakeGuestChar(void); // Make the guest character
    void DestroyGuestChar(dbref); // Destroy the guest character
    void WipeAttrs(dbref guest); // Wipe all the attrbutes
 public:
    void ListAll(dbref); // @list guests
    void StartUp(); // Public handler for Startup
    char *Create(DESC *d); // Public handler for creation
    void CleanUp(void); // Public handler for cleaning guests.
};

extern CGuests Guest;

// Let's have a definable password... prevent idiots from conning to #'s.
// Pay no attention to the praise I give myself!
#define GUEST_PASSWORD "ZentyRULEZ"  // You know it baby!

#endif
