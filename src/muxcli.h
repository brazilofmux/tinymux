// muxcli.h
//
// $Id: muxcli.h,v 1.1 2000-11-06 16:55:16 sdennis Exp $
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

#define CLI_USER 0

// The following control whether an option is allowed/required.
//
#define CLI_NONE     0
#define CLI_OPTIONAL 1
#define CLI_REQUIRED 2

typedef struct
{
    char *m_Flag;
    int  m_ArgControl;
    int  m_Unique;
} CLI_OptionEntry;

typedef void CLI_CALLBACKFUNC(CLI_OptionEntry *pOption, char *Value);

void CLI_Process
(
    int argc,
    char *argv[],
    CLI_OptionEntry *aOptionTable,
    int nOptionTable,
    CLI_CALLBACKFUNC *pFunc
);
