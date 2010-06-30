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

    const int iModeNone  = 0;
    const int iModeRTP6H = 1;
    const int iModeRTT5X = 2;
    const int iModeUPP6H = 3;
    int iMode = iModeNone;

    if (strcmp("--rt-p6h", argv[1]) == 0)
    {
        iMode = iModeRTP6H;
    }
    else if (strcmp("--rt-t5x", argv[1]) == 0)
    {
        iMode = iModeRTT5X;
    }
    else if (strcmp("--up-p6h", argv[1]) == 0)
    {
        iMode = iModeUPP6H;
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

    if (iModeRTP6H == iMode)
    {
        extern int p6hparse();
        extern FILE *p6hin;
        p6hin = fpin;
        p6hparse();

        g_p6hgame.Validate();
        g_p6hgame.Write(fpout);
        p6hin = NULL;
    }
    else if (iModeRTT5X == iMode)
    {
        extern int t5xparse();
        extern FILE *t5xin;
        t5xin = fpin;
        t5xparse();

        g_t5xgame.Validate();
        g_t5xgame.Write(fpout);
        t5xin = NULL;
    }
    else if (iModeUPP6H == iMode)
    {
        extern int p6hparse();
        extern FILE *p6hin;
        p6hin = fpin;
        p6hparse();

        g_p6hgame.Validate();
        g_p6hgame.Upgrade();
        g_p6hgame.Validate();
        g_p6hgame.Write(fpout);
        p6hin = NULL;
    }
    fclose(fpout);
    fclose(fpin);
    return 0;
}
