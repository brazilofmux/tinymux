// svdreport.cpp -- Aggregate User Statistics module.
//
// $Id: svdreport.cpp,v 1.3 2003-01-05 18:08:59 sdennis Exp $
//
// MUX 2.2
// Copyright (C) 1998 through 2003 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.  
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

