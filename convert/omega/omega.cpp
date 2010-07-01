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
    fprintf(stderr, "omega <options> <infile> <outfile>\n");
    fprintf(stderr, "Supported options:\n");
    fprintf(stderr, "  --rt-p6h - Round-trip PennMUSH flatfile\n");
    fprintf(stderr, "  --rt-t5x - Round-trip TinyMUX flatfile\n");
    fprintf(stderr, "  --up-p6h - Upgrade pre-1.7.7p40 PennMUSH flatfile\n");
    fprintf(stderr, "  --cv-p6h - Convert PennMUSH flatfile to TinyMUX\n");
}

int main(int argc, const char *argv[])
{
    if (4 != argc)
    {
        Usage();
        return 1;
    }

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
    Mode mode = eModeNone;

    if (strcmp("--rt-p6h", argv[1]) == 0)
    {
        mode = eModeRTP6H;
    }
    else if (strcmp("--rt-t5x", argv[1]) == 0)
    {
        mode = eModeRTT5X;
    }
    else if (strcmp("--up-p6h", argv[1]) == 0)
    {
        mode = eModeUPP6H;
    }
    else if (strcmp("--cv-p6h", argv[1]) == 0)
    {
        mode = eModeCVP6H;
    }
    else
    {
        Usage();
        return 1;
    }

    fpin = fopen(argv[2], "rb");
    if (NULL == fpin)
    {
        fprintf(stderr, "Input file, %s, not found.\n", argv[2]);
        return 1;
    }
    fpout = fopen(argv[3], "wb");
    if (NULL == fpout)
    {
        fclose(fpin);
        fprintf(stderr, "Output file, %s, not found.\n", argv[3]);
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
        g_p6hgame.ConvertT5X();
        g_t5xgame.Validate();
        g_t5xgame.Write(fpout);
    }
    fclose(fpout);
    fclose(fpin);
    return 0;
}
