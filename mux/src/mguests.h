// mguests.h
//
// $Id: mguests.h,v 1.2 2003-02-05 06:20:59 jake Exp $
//

#ifndef __MGUESTS_H
#define __MGUESTS_H

#include "copyright.h"
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

    bool  CheckGuest(dbref);
    void  ListAll(dbref);          // @list guests
    void  StartUp();
    const char  *Create(DESC *d);
    void  CleanUp(void);
};

extern CGuests Guest;

#define GUEST_PASSWORD "Guest"

#endif // !__MGUESTS_H
