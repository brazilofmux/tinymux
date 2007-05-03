// Report.h -- Aggregate User Statistics module.
//
// $Id: svdreport.h,v 1.1 2000-04-11 07:14:48 sdennis Exp $
//
// MUX 2.0
// Copyright (C) 1998 through 2000 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved. Permission is given to
// use this code for building and hosting text-based game servers.
// Permission is given to use this code for other non-commercial
// purposes. To use this code for commercial purposes other than
// building/hosting text-based game servers, contact the author at
// Stephen Dennis <sdennis@svdltd.com> for another license.
//
void report_login(dbref player, const CLinearTimeAbsolute& ltaLast, const CLinearTimeAbsolute& ltaNow);
void report_logout(dbref player, const CLinearTimeAbsolute& ltConnected, const CLinearTimeAbsolute& ltCurrent);
void report_create(dbref player, const CLinearTimeAbsolute& ltCurrent);
void report_deny(dbref player);
