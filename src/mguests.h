// mguests.h
//
// $Id: mguests.h,v 1.8 2002-02-14 18:39:36 sdennis Exp $
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
    static char name[50];
    dbref *Guests;
    int   nMaxGuests;              // Size of Guests[].
    int   nGuests;                 // Number of guests stored in Guests[].
    void  SizeGuests(int);
    int   MakeGuestChar(void);     // Make the guest character
    void  DestroyGuestChar(dbref); // Destroy the guest character
    void  WipeAttrs(dbref guest);  // Wipe all the attrbutes

public:
    CGuests(void);
    ~CGuests(void);

    BOOL  CheckGuest(dbref);
    void  ListAll(dbref);          // @list guests
    void  StartUp();
    char  *Create(DESC *d);
    void  CleanUp(void);
};

extern CGuests Guest;

// Zenty's Not-So-Secret Guest Password.
//
#define GUEST_PASSWORD "ZentyRULEZ" // Zenty: You know it baby!

#endif
