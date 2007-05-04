// muxcli.cpp
//
// $Id$
//
#include "copyright.h"
#include <string.h>
#include "muxcli.h"

// 0 -- A non-option argument.
// 1 -- A short-option argument.
// 2 -- A long-option argument.
// 3 -- An 'end of options' indicator.
//
static int iArgType(char *pArg)
{
    // How many characters from "--" does the argument match?
    //
    static char aHHN[3] = "--";
    int iType = 0;
    for (; iType < 3 && aHHN[iType] == pArg[iType]; iType++)
    {
        // Nothing
    }
    if (iType > 3)
    {
        iType = 3;
    }

    // "-" is a special case. It is a non-option argument.
    //
    if (iType == 1 && pArg[1] == '\0')
    {
        iType = 0;
    }
    return iType;
}

// Examples:
//
// 1. prog -c123   --> (c,123)
// 2. prog -c 123  --> (c,123)
// 3. prog -c=123  --> (c,123)
// 4. prog -cs 123 --> (c,123) (s)
// 5. prog -sc=123 --> (s) (c,123)
// 6. prog -cs123 --> (c,s123)
//
void CLI_Process
(
    int argc,
    char *argv[],
    CLI_OptionEntry *aOptionTable,
    int nOptionTable,
    CLI_CALLBACKFUNC *pFunc
)
{
    int minNonOption = 0;
    int bEndOfOptions = 0;
    for (int i = 1; i < argc; i++)
    {
        char *pArgv = argv[i];
        int iType = 0;
        if (!bEndOfOptions)
        {
            iType = iArgType(pArgv);
        }

        if (iType == 0)
        {
            // Non-option argument.
            //
            if (minNonOption <= i)
            {
                // We haven't associated it with an option, yet, so
                // pass it in by itself.
                //
                pFunc(0, pArgv);
            }
            continue;
        }

        if (minNonOption < i+1)
        {
            minNonOption = i+1;
        }

        if (iType == 3)
        {
            // A "--" causes the remaining unpaired arguments to be
            // treated as non-option arguments.
            //
            bEndOfOptions = 1;
            continue;
        }

        char *p = pArgv+iType;

        if (iType == 2)
        {
            // We have a long option.
            //
            char *pEqual = strchr(p, '=');
            size_t nLen;
            if (pEqual)
            {
                nLen = pEqual - p;
            }
            else
            {
                nLen = strlen(p);
            }
            for (int j = 0; j < nOptionTable; j++)
            {
                if (  !strncmp(aOptionTable[j].m_Flag, p, nLen)
                   && aOptionTable[j].m_Flag[nLen] == '\0')
                {
                    switch (aOptionTable[j].m_ArgControl)
                    {
                    case CLI_NONE:
                        pFunc(aOptionTable + j, 0);
                        break;

                    case CLI_OPTIONAL:
                    case CLI_REQUIRED:
                        if (pEqual)
                        {
                            pFunc(aOptionTable + j, pEqual+1);
                            break;
                        }
                        int bFound = 0;
                        for (; minNonOption < argc; minNonOption++)
                        {
                            int iType2 = iArgType(argv[minNonOption]);
                            if (iType2 == 0)
                            {
                                pFunc(aOptionTable + j, argv[minNonOption]);
                                minNonOption++;
                                bFound = 1;
                                break;
                            }
                            else if (iType2 == 3)
                            {
                                // End of options. Stop.
                                //
                                break;
                            }
                        }
                        if (  !bFound
                           && aOptionTable[j].m_ArgControl == CLI_OPTIONAL)
                        {
                            pFunc(aOptionTable + j, 0);
                        }
                        break;
                    }
                    break;
                }
            }
            continue;
        }

        // At this point, the only possibilities left are a short
        // option.
        //
        while (*p)
        {
            int ch = *p++;
            for (int j = 0; j < nOptionTable; j++)
            {
                if (  aOptionTable[j].m_Flag[0] == ch
                   && aOptionTable[j].m_Flag[1] == '\0')
                {
                    switch (aOptionTable[j].m_ArgControl)
                    {
                    case CLI_NONE:
                        pFunc(aOptionTable + j, 0);
                        break;

                    case CLI_OPTIONAL:
                    case CLI_REQUIRED:
                        if (*p)
                        {
                            // Value follows option letter
                            //
                            if (*p == '=')
                            {
                                p++;
                            }

                            pFunc(aOptionTable + j, p);
                            p = "";
                            break;
                        }
                        int bFound = 0;
                        for (; minNonOption < argc; minNonOption++)
                        {
                            int iType2 = iArgType(argv[minNonOption]);
                            if (iType2 == 0)
                            {
                                pFunc(aOptionTable + j, argv[minNonOption]);
                                minNonOption++;
                                bFound = 1;
                                break;
                            }
                            else if (iType2 == 3)
                            {
                                // End of options. Stop.
                                //
                                break;
                            }
                        }
                        if (  !bFound
                           && aOptionTable[j].m_ArgControl == CLI_OPTIONAL)
                        {
                            pFunc(aOptionTable + j, 0);
                        }
                        break;
                    }
                    break;
                }
            }
        }
    }
}
