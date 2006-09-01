// muxcli.h
//
// $Id: muxcli.h,v 1.5 2006/01/01 18:20:57 sdennis Exp $
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
