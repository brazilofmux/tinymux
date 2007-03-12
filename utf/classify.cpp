#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <ctype.h>
#include <string.h>

#include "ConvertUTF.h"
#include "smutil.h"

//#define VERIFY

#ifdef  VERIFY
// 219 included, 1113893 excluded, 0 errors.
// 12 states, 26 columns, 568 bytes
//
#define PRINT_START_STATE (0)
#define PRINT_ACCEPTING_STATES_START (12)

const unsigned char print_itt[256] =
{
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       1,   1,   1,   1,   1,   1,   1,   1,    1,   1,   1,   1,   1,   1,   1,   1,
       1,   1,   1,   1,   1,   1,   1,   1,    1,   1,   1,   1,   1,   1,   1,   1,
       1,   1,   1,   1,   1,   1,   1,   1,    1,   1,   1,   1,   1,   1,   1,   1,
       1,   1,   1,   1,   1,   1,   1,   1,    1,   1,   1,   1,   1,   1,   1,   1,
       1,   1,   1,   1,   1,   1,   1,   1,    1,   1,   1,   1,   1,   1,   1,   1,
       1,   1,   1,   1,   1,   1,   1,   1,    1,   1,   1,   1,   1,   1,   1,   0,

       2,   3,   4,   3,   5,   3,   6,   3,    3,   3,   3,   3,   3,   3,   3,   3,
       3,   3,   7,   8,   9,   3,   3,   3,    9,   9,   9,   3,  10,   9,   9,   3,
      11,  11,  12,  13,  13,  13,  14,  13,   13,  13,  13,  13,  15,  13,  13,  13,
      14,  13,  13,  13,  13,  13,  13,  13,   16,  14,  14,  13,  13,  17,  16,  18,
       0,   0,  19,  20,   0,  21,  22,   0,    0,   0,   0,  23,   0,   0,   0,   0,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,  24,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,  25,
       0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,   0,   0

};

const unsigned char print_stt[12][26] =
{
    {  12,  13,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,   1,   2,   3,   4,   5,   6,  10},
    {  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  13,  13,  13,  13,  13,  13,  13,  13,  12,  12,  12,  12,  12,  12,  12},
    {  12,  12,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  12,  12,  12,  12,  12,  12,  12},
    {  12,  12,  12,  12,  12,  12,  12,  13,  13,  12,  12,  13,  12,  12,  12,  12,  13,  13,  12,  12,  12,  12,  12,  12,  12,  12},
    {  12,  12,  12,  12,  12,  12,  12,  13,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12},
    {  12,  12,  12,  12,  12,  12,  13,  12,  12,  12,  13,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12},
    {  12,  12,   7,  12,   8,   9,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12},
    {  12,  12,  12,  12,  12,  12,  12,  12,  13,  13,  13,  13,  13,  12,  13,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12},
    {  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  13,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12},
    {  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  13,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12},
    {  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  11,  12,  12,  12,  12,  12,  12,  12},
    {  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  12,  13,  12,  12,  12,  12,  12,  12,  12,  12}
};

void VerifyTables(FILE *fp)
{
    fprintf(stderr, "Testing final ITT and STT.\n");
    fseek(fp, 0, SEEK_SET);
    int Value;
    UTF32 Othercase;
    UTF32 nextcode = ReadCodePoint(fp, &Value, &Othercase);
    UTF32 i;
    for (i = 0; i <= UNI_MAX_LEGAL_UTF32; i++)
    {
        bool bMember;
        if (i == nextcode)
        {
            bMember = true;
            if (UNI_EOF != nextcode)
            {
                nextcode = ReadCodePoint(fp, &Value, &Othercase);
                if (nextcode <= i)
                {
                    fprintf(stderr, "Codes in file are not in order.\n");
                    exit(0);
                }
            }
        }
        else
        {
            bMember = false;
        }

        UTF32 Source[2];
        Source[0] = i;
        Source[1] = L'\0';
        const UTF32 *pSource = Source;

        UTF8 Target[5];
        UTF8 *pTarget = Target;

        ConversionResult cr;
        cr = ConvertUTF32toUTF8(&pSource, pSource+1, &pTarget, pTarget+sizeof(Target)-1, lenientConversion);

        if (conversionOK == cr)
        {
            int iState = PRINT_START_STATE;
            UTF8 *p = Target;
            while (  p < pTarget
                  && iState < PRINT_ACCEPTING_STATES_START)
            {
                iState = print_stt[iState][print_itt[(unsigned char)*p]];
                p++;
            }

            bool j = ((iState - PRINT_ACCEPTING_STATES_START) == 1) ? true : false;
            if (j != bMember)
            {
                printf("Input Translation Table and State Transition Table do not work.\n");
                exit(0);
            }
        }
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
    UTF32 i;
    for (i = 0; i <= UNI_MAX_LEGAL_UTF32; i++)
    {
        bool bMember;
        if (i == nextcode)
        {
            bMember = true;
            if (UNI_EOF != nextcode)
            {
                nextcode = ReadCodePoint(fp, &Value, &Othercase);
                if (nextcode <= i)
                {
                    fprintf(stderr, "Codes in file are not in order.\n");
                    exit(0);
                }
            }
        }
        else
        {
            bMember = false;
        }

        UTF32 Source[2];
        Source[0] = i;
        Source[1] = L'\0';
        const UTF32 *pSource = Source;

        UTF8 Target[5];
        UTF8 *pTarget = Target;

        ConversionResult cr;
        cr = ConvertUTF32toUTF8(&pSource, pSource+1, &pTarget, pTarget+sizeof(Target)-1, lenientConversion);

        if (conversionOK == cr)
        {
            sm.TestString(Target, pTarget, bMember);
        }
    }
}

void LoadStrings(FILE *fp)
{
    int cIncluded = 0;
    int cExcluded = 0;
    int cErrors   = 0;

    fseek(fp, 0, SEEK_SET);
    int Value;
    UTF32 Othercase;
    UTF32 nextcode = ReadCodePoint(fp, &Value, &Othercase);

    UTF32 i;
    for (i = 0; i <= UNI_MAX_LEGAL_UTF32; i++)
    {
        bool bMember;
        if (i == nextcode)
        {
            bMember = true;
            cIncluded++;
            if (UNI_EOF != nextcode)
            {
                nextcode = ReadCodePoint(fp, &Value, &Othercase);
                if (nextcode <= i)
                {
                    fprintf(stderr, "Codes in file are not in order.\n");
                    exit(0);
                }
            }
        }
        else
        {
            bMember = false;
            cExcluded++;
        }

        UTF32 Source[2];
        Source[0] = i;
        Source[1] = L'\0';
        const UTF32 *pSource = Source;

        UTF8 Target[5];
        UTF8 *pTarget = Target;

        ConversionResult cr;
        cr = ConvertUTF32toUTF8(&pSource, pSource+1, &pTarget, pTarget+sizeof(Target)-1, lenientConversion);

        if (conversionOK == cr)
        {
            sm.RecordString(Target, pTarget, bMember);
        }
        else
        {
            cErrors++;
        }
    }
    printf("// %d included, %d excluded, %d errors.\n", cIncluded, cExcluded, cErrors);
    fprintf(stderr, "%d included, %d excluded, %d errors.\n", cIncluded, cExcluded, cErrors);
    sm.ReportStatus();
}

void BuildAndOutputTable(FILE *fp, char *UpperPrefix, char *LowerPrefix)
{
    // Construct State Transition Table.
    //
    sm.Init();
    LoadStrings(fp);
    TestTable(fp);
    sm.SetUndefinedStates(false);
    TestTable(fp);

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
    char *pPrefix;
    char *pFilename;
    if (argc < 3)
    {
#if 0
        fprintf(stderr, "Usage: %s prefix unicodedata.txt\n", argv[0]);
        exit(0);
#else
        pFilename = "Printable.txt";
        pPrefix   = "print";
#endif
    }
    else
    {
        pPrefix   = argv[1];
        pFilename = argv[2];
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
#ifdef VERIFY
    VerifyTables(fp);
#else
    BuildAndOutputTable(fp, pPrefixUpper, pPrefixLower);
#endif
    fclose(fp);
}
