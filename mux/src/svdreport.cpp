// svdreport.cpp -- Aggregate User Statistics module.
//
// $Id: svdreport.cpp,v 1.2 2002-06-04 00:47:28 sdennis Exp $
//
// MUX 2.1
// Copyright (C) 1998 through 2001 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved. Permission is given to
// use this code for building and hosting text-based game servers.
// Permission is given to use this code for other non-commercial
// purposes. To use this code for commercial purposes other than
// building/hosting text-based game servers, contact the author at
// Stephen Dennis <sdennis@svdltd.com> for another license.
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"

#define NPERIODS 24
void do_report(dbref executor, dbref caller, dbref enactor, int extra)
{
    char *buff = alloc_mbuf("do_report");
    int nBin[NPERIODS];
    int i;

    for (i = 0; i < NPERIODS; i++)
    {
        nBin[i] = 0;
    }

    CLinearTimeAbsolute ltaNow, ltaPlayer;
    ltaNow.GetLocal();

    const int PeriodInSeconds = 28800;

    int iPlayer;
    DO_WHOLE_DB(iPlayer)
    {
        if (isPlayer(iPlayer))
        {
            int aowner, aflags;
            char *player_last = atr_get(iPlayer, A_LAST, &aowner, &aflags);

            if (ltaPlayer.SetString(player_last))
            {
                CLinearTimeDelta ltd(ltaPlayer, ltaNow);
                int ltdSeconds = ltd.ReturnSeconds();
                int iBin = ltdSeconds / PeriodInSeconds;
                if (0 <= iBin && iBin < NPERIODS)
                {
                    nBin[iBin]++;
                }
            }
            free_lbuf(player_last);
        }
    }

    int iHour, nSum = 0;
    notify(executor, "Day   Hours     Players  Total");
    for (i = 0, iHour = 0; i < NPERIODS; i++, iHour += 8)
    {
        nSum += nBin[i];
        sprintf(buff, "%3d %03d - %03d: %6d %6d", iHour/24 + 1, iHour, iHour+8, nBin[i], nSum);
        notify(executor, buff);
    }
    free_mbuf(buff);
}

