#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <ctype.h>
#include <string.h>

#include "ConvertUTF.h"
#include "smutil.h"

#if 0
// 270 code points.
// 5 states, 42 columns, 466 bytes
//
#define DIGIT_START_STATE (0)
#define DIGIT_ACCEPTING_STATES_START (5)

unsigned char digit_itt[256] =
{
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 5, 10, 11, 12, 13, 14, 15, 12, 16, 3, 4, 5, 6, 7, 13, 14, 15, 12, 16, 17, 4, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 19, 20, 21, 22, 23, 24, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 29, 17, 39, 32, 33, 34, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 39, 40, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 39, 40, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41, 41
};

unsigned char digit_stt[5][42] =
{
    {   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,   6,   6,   6,  13,  14,   5,   7,   0,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,   0,   1,   3},
    {   9,   9,   9,   9,   9,   9,   5,   6,   7,   8,  10,  11,  12,  13,  14,   1,   0,   0,   0,   0,   2,   0,   2,   0,   2,   0,   2,   0,   2,   0,   2,   0,   2,   0,   2,   0,   0,   0,   0,   0,   0,   0},
    {  13,   0,  13,   0,   0,   0,   9,   0,  11,   0,   0,   0,   0,  11,   0,  13,   0,   0,   0,   9,  10,  11,  12,  13,  14,   5,   6,   7,   8,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0},
    {   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0},
    {   7,   8,   7,  10,  11,  12,  13,  14,   5,   6,   8,   8,   8,   5,   6,   7,   9,  10,  12,  13,  14,   5,   6,   7,   8,   9,  10,  11,  12,   9,  10,  11,  12,  13,  14,   5,   6,   7,   8,  11,   0,   0}
};

void VerifyTables(FILE *fp)
{
    fprintf(stderr, "Testing final ITT and STT.\n");
    fseek(fp, 0, SEEK_SET);
    int Value;
    UTF32 Othercase;
    UTF32 nextcode = ReadCodePoint(fp, &Value, &Othercase);

    // Value
    //
    while (UNI_EOF != nextcode)
    {
        UTF32 Source[2];
        Source[0] = nextcode;
        Source[1] = L'\0';
        const UTF32 *pSource = Source;

        UTF8 Target[5];
        UTF8 *pTarget = Target;

        ConversionResult cr;
        cr = ConvertUTF32toUTF8(&pSource, pSource+1, &pTarget, pTarget+sizeof(Target)-1, lenientConversion);

        if (conversionOK == cr)
        {
            int iState = DIGIT_START_STATE;
            UTF8 *p = Target;
            while (  p < pTarget
                  && iState < DIGIT_ACCEPTING_STATES_START)
            {
                iState = digit_stt[iState][digit_itt[(unsigned char)*p]];
                p++;
            }

            int j = iState - DIGIT_ACCEPTING_STATES_START;
            if (j != Value)
            {
                printf("Input Translation Table and State Transition Table do not work.\n");
                exit(0);
            }
        }
        UTF32 nextcode2 = ReadCodePoint(fp, &Value, &Othercase);
        if (nextcode2 < nextcode)
        {
            fprintf(stderr, "Codes in file are not in order.\n");
            exit(0);
        }
        nextcode = nextcode2;
    }
}
#endif

StateMachine sm;

void TestTable(FILE *fp)
{
    fprintf(stderr, "Testing STT table.\n");
    fseek(fp, 0, SEEK_SET);
    int Value;
    UTF32 Othercase;
    UTF32 nextcode = ReadCodePoint(fp, &Value, &Othercase);

    while (UNI_EOF != nextcode)
    {
        UTF32 Source[2];
        Source[0] = nextcode;
        Source[1] = L'\0';
        const UTF32 *pSource = Source;

        UTF8 Target[5];
        UTF8 *pTarget = Target;

        ConversionResult cr;
        cr = ConvertUTF32toUTF8(&pSource, pSource+1, &pTarget, pTarget+sizeof(Target)-1, lenientConversion);

        if (conversionOK == cr)
        {
            sm.TestString(Target, pTarget, Value);
        }

        UTF32 nextcode2 = ReadCodePoint(fp, &Value, &Othercase);
        if (nextcode2 < nextcode)
        {
            fprintf(stderr, "Codes in file are not in order.\n");
            exit(0);
        }
        nextcode = nextcode2;
    }
}

void LoadStrings(FILE *fp)
{
    int cIncluded = 0;

    fseek(fp, 0, SEEK_SET);
    int Value;
    UTF32 Othercase;
    UTF32 nextcode = ReadCodePoint(fp, &Value, &Othercase);
    if (Value < 0)
    {
        fprintf(stderr, "Value missing from code U-%06X\n", nextcode);
        exit(0);
    }

    while (UNI_EOF != nextcode)
    {
        UTF32 Source[2];
        Source[0] = nextcode;
        Source[1] = L'\0';
        const UTF32 *pSource = Source;

        UTF8 Target[5];
        UTF8 *pTarget = Target;

        ConversionResult cr;
        cr = ConvertUTF32toUTF8(&pSource, pSource+1, &pTarget, pTarget+sizeof(Target)-1, lenientConversion);

        if (conversionOK == cr)
        {
            cIncluded++;
            sm.RecordString(Target, pTarget, Value);
        }

        UTF32 nextcode2 = ReadCodePoint(fp, &Value, &Othercase);
        if (nextcode2 < nextcode)
        {
            fprintf(stderr, "Codes in file are not in order.\n");
            exit(0);
        }
        else if (  UNI_EOF != nextcode2
                && Value < 0)
        {
            fprintf(stderr, "Value missing from code U-%06X\n", nextcode2);
            exit(0);
        }
        nextcode = nextcode2;
    }
    printf("// %d code points.\n", cIncluded);
    fprintf(stderr, "%d code points.\n", cIncluded);
    sm.ReportStatus();
}

bool g_bReplacement = false;
int  g_iReplacementState = '?';

void BuildAndOutputTable(FILE *fp, char *UpperPrefix, char *LowerPrefix)
{
    // Construct State Transition Table.
    //
    sm.Init();
    LoadStrings(fp);
    TestTable(fp);

    // Leaving states undefined leads to a smaller table.  On the other hand,
    // do not make queries for code points outside the expected set.
    //
    if (g_bReplacement)
    {
        sm.SetUndefinedStates(g_iReplacementState);
        TestTable(fp);
    }

    // Optimize State Transition Table.
    //
    sm.MergeAcceptingStates();
    TestTable(fp);
    sm.MergeAcceptingStates();
    TestTable(fp);
    sm.MergeAcceptingStates();
    TestTable(fp);
    sm.RemoveDuplicateRows();
    TestTable(fp);
    sm.RemoveDuplicateRows();
    TestTable(fp);
    sm.RemoveDuplicateRows();
    TestTable(fp);
    sm.DetectDuplicateColumns();

    // Output State Transition Table.
    //
    sm.NumberStates();
    sm.OutputTables(UpperPrefix, LowerPrefix);
}

int main(int argc, char *argv[])
{
    char *pPrefix = NULL;
    char *pFilename = NULL;

    if (argc < 3)
    {
#if 0
        fprintf(stderr, "Usage: %s [-c ch] prefix unicodedata.txt\n", argv[0]);
        exit(0);
#else
        pFilename = "NumericDecimal.txt";
        pPrefix   = "digit";
        g_bReplacement = false;
        g_iReplacementState = '?';
#endif
    }
    else
    {
        int j;
        for (j = 1; j < argc; j++)
        {
            if (0 == strcmp(argv[j], "-c"))
            {
                g_bReplacement = true;
                if (j+1 < argc)
                {
                    j++;
                    g_iReplacementState = atoi(argv[j]);
                }
                else
                {
                    g_iReplacementState = '?';
                }
            }
            else
            {
                if (NULL == pPrefix)
                {
                    pPrefix = argv[j];
                }
                else if (NULL == pFilename)
                {
                    pFilename = argv[j];
                }
            }
        }
    }

    FILE *fp = fopen(pFilename, "rb");
    if (NULL == fp)
    {
        fprintf(stderr, "Cannot open %s\n", pFilename);
        exit(0);
    }

    size_t nPrefix = strlen(pPrefix);
    char *pPrefixLower = new char[nPrefix+1];
    char *pPrefixUpper = new char[nPrefix+1];
    memcpy(pPrefixLower, pPrefix, nPrefix+1);
    memcpy(pPrefixUpper, pPrefix, nPrefix+1);

    size_t i;
    for (i = 0; i < nPrefix; i++)
    {
        if (isupper(pPrefixLower[i]))
        {
            pPrefixLower[i] = static_cast<char>(tolower(pPrefixLower[i]));
        }

        if (islower(pPrefixUpper[i]))
        {
            pPrefixUpper[i] = static_cast<char>(toupper(pPrefixUpper[i]));
        }
    }

    BuildAndOutputTable(fp, pPrefixUpper, pPrefixLower);
    //VerifyTables(fp);

    fclose(fp);
}
