#include "omega.h"
#include "p6hgame.h"
#include "t5xgame.h"

// --------------------------------------------------------------------------
// StringCloneLen: allocate memory and copy string
//
char *StringCloneLen(const char *str, size_t nStr)
{
    char *buff = (char *)malloc(nStr+1);
    if (buff)
    {
        memcpy(buff, str, nStr);
        buff[nStr] = '\0';
    }
    else
    {
        exit(1);
    }
    return buff;
}

// --------------------------------------------------------------------------
// StringClone: allocate memory and copy string
//
char *StringClone(const char *str)
{
    return StringCloneLen(str, strlen(str));
}

void Usage()
{
    fprintf(stderr, "Version: %s\n", OMEGA_VERSION);
    fprintf(stderr, "omega <options> <infile> <outfile>\n");
    fprintf(stderr, "Supported options:\n");
    fprintf(stderr, "  --rt-p6h - Round-trip PennMUSH flatfile\n");
    fprintf(stderr, "  --rt-t5x - Round-trip TinyMUX flatfile\n");
    fprintf(stderr, "  --up-p6h - Upgrade pre-1.7.7p40 PennMUSH flatfile\n");
    fprintf(stderr, "  --cv-p6h - Convert PennMUSH flatfile to TinyMUX\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  --reset-password - Reset #1 password to 'potrzebie'\n");
}

int main(int argc, const char *argv[])
{
    FILE *fpin = NULL;
    FILE *fpout = NULL;

    typedef enum
    {
        eModeNone,
        eModeRTP6H,
        eModeRTT5X,
        eModeUPP6H,
        eModeCVP6H,
    } Mode;
    bool fResetPassword = false;
    Mode mode = eModeNone;

    int iArg = 1;
    while (iArg < argc)
    {
        if (strcmp("--rt-p6h", argv[iArg]) == 0)
        {
            mode = eModeRTP6H;
            iArg++;
        }
        else if (strcmp("--rt-t5x", argv[iArg]) == 0)
        {
            mode = eModeRTT5X;
            iArg++;
        }
        else if (strcmp("--up-p6h", argv[iArg]) == 0)
        {
            mode = eModeUPP6H;
            iArg++;
        }
        else if (strcmp("--cv-p6h", argv[iArg]) == 0)
        {
            mode = eModeCVP6H;
            iArg++;
        }
        else if (strcmp("--reset-password", argv[iArg]) == 0)
        {
            fResetPassword = true;
            iArg++;
        }
        else
        {
            break;
        }
    }

    if (eModeNone == mode)
    {
        Usage();
        return 1;
    }

    if (iArg + 2 != argc)
    {
        Usage();
        return 1;
    }

    fpin = fopen(argv[iArg], "rb");
    if (NULL == fpin)
    {
        fprintf(stderr, "Input file, %s, not found.\n", argv[iArg]);
        return 1;
    }
    fpout = fopen(argv[iArg+1], "wb");
    if (NULL == fpout)
    {
        fclose(fpin);
        fprintf(stderr, "Output file, %s, not found.\n", argv[iArg+1]);
        return 1;
    }

    if (eModeRTP6H == mode)
    {
        extern int p6hparse();
        extern FILE *p6hin;
        p6hin = fpin;
        p6hparse();
        p6hin = NULL;

        g_p6hgame.Validate();
        if (fResetPassword)
        {
            g_p6hgame.ResetPassword();
        }
        g_p6hgame.Write(fpout);
    }
    else if (eModeRTT5X == mode)
    {
        extern int t5xparse();
        extern FILE *t5xin;
        t5xin = fpin;
        t5xparse();
        t5xin = NULL;

        g_t5xgame.Validate();
        if (fResetPassword)
        {
            g_p6hgame.ResetPassword();
        }
        g_t5xgame.Write(fpout);
    }
    else if (eModeUPP6H == mode)
    {
        extern int p6hparse();
        extern FILE *p6hin;
        p6hin = fpin;
        p6hparse();
        p6hin = NULL;

        g_p6hgame.Validate();
        g_p6hgame.Upgrade();
        g_p6hgame.Validate();
        if (fResetPassword)
        {
            g_p6hgame.ResetPassword();
        }
        g_p6hgame.Write(fpout);
    }
    else if (eModeCVP6H == mode)
    {
        extern int p6hparse();
        extern FILE *p6hin;
        p6hin = fpin;
        p6hparse();
        p6hin = NULL;

        g_p6hgame.Validate();
        g_p6hgame.Upgrade();
        g_p6hgame.Validate();
        g_t5xgame.ConvertFromP6H();
        g_t5xgame.Validate();
        if (fResetPassword)
        {
            g_t5xgame.ResetPassword();
        }
        g_t5xgame.Write(fpout);
    }
    fclose(fpout);
    fclose(fpin);
    return 0;
}
