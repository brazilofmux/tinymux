// The following does not yet agree with the behavior of strings.cpp.
// Currently, strings.cpp looks at the upper and lower fields of
// UnicodeData-style field and guesses whether to use the upper or lower case.
// It does not yet support sequences of code points.
//
/*! \file strings.cpp
 * \brief Top-level driver for building a state machine which recognizes a
 * code point and indicates an associated sequence of code point(s) -- for
 * example, upper case, lower case, title case, or possibly certain
 * canonicalizations.
 *
 * The input file is composed of lines.  Each line is broken in
 * semicolon-delimited fields.  The code point to recognize is taken from the
 * first field.  The associated sequence of code points is taken from the
 * second field.
 *
 * The constructed state machine associates the recognized code point with
 * one of potentially many accepting states.  Each accepting states
 * corresponds to an entry in an output table which contains enough
 * information of constructing the sequence of associated code points.
 * Potentially, several output tables (one for each method of constructing the
 * associated code point sequence) may be generated.
 *
 * For example, upper case and lower case character occur in runs.  It is
 * possible to construct all of the associated code points in a range by
 * flippping the same bits in the corresponding range of given code points.
 * Concretely, the ASCII range 'a-z' differ from 'A-Z' in one bit (0x20).
 *
 * On the other hand, sometimes, multiple corresponding code points are
 * associated, and they must be quoted explicitly.
 *
 * It is not always necessary for the state machine to look at every byte
 * of a code point to determine the associated code point(s).  For this
 * reason, to advance to the next code requires a method separate from the
 * state machine produced here.
 *
 * $Id$
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <ctype.h>
#include <string.h>

#include "ConvertUTF.h"
#include "smutil.h"

StateMachine sm;

static struct
{
    UTF8 *p;
    size_t n;
} aOutputTable[5000];
int   nOutputTable;

UTF32 ReadCodePointAndOthercase(FILE *fp, UTF32 &Othercase)
{
    char buffer[1024];
    char *p;

    for (;;)
    {
        if (fgets(buffer, sizeof(buffer), fp) == NULL)
        {
            Othercase = UNI_EOF;
            return UNI_EOF;
        }
        p = strchr(buffer, '#');
        if (NULL != p)
        {
            // Ignore comment.
            //
            *p = '\0';
        }
        p = buffer;

        // Skip leading whitespace.
        //
        while (isspace(*p))
        {
            p++;
        }

        // Look for end of string or comment.
        //
        if ('\0' == *p)
        {
            // We skip blank lines.
            //
            continue;
        }
        break;
    }

#define MAX_FIELDS 15

    int   nFields = 0;
    char *aFields[MAX_FIELDS];
    for (nFields = 0; nFields < MAX_FIELDS; )
    {
        // Skip leading whitespace.
        //
        while (isspace(*p))
        {
            p++;
        }

        aFields[nFields++] = p;
        char *q = strchr(p, ';');
        if (NULL == q)
        {
            // Trim trailing whitespace.
            //
            size_t i = strlen(p) - 1;
            while (isspace(p[i]))
            {
                p[i] = '\0';
            }
            break;
        }
        else
        {
            *q = '\0';
            p = q + 1;

            // Trim trailing whitespace.
            //
            q--;
            while (isspace(*q))
            {
                *q = '\0';
                q--;
            }
        }
    }

    // Field #0 - Code Point
    //
    int codepoint = DecodeCodePoint(aFields[0]);

    // Field #12 - Simple Uppercase Mapping.
    //
    int Uppercase = DecodeCodePoint(aFields[12]);

    // Field #13 = Simple Lowercase Mapping.
    //
    int Lowercase = DecodeCodePoint(aFields[13]);

    if (  Uppercase < 0
       && Lowercase < 0)
    {
        Othercase = UNI_EOF;
    }
    else
    {
        if (Uppercase < 0)
        {
            Uppercase = codepoint;
        }
        if (Lowercase < 0)
        {
            Lowercase = codepoint;
        }

        if (Lowercase == codepoint)
        {
            Othercase = Uppercase;
        }
        else
        {
            Othercase = Lowercase;
        }
    }
    return codepoint;
}

void TestTable(FILE *fp)
{
    fprintf(stderr, "Testing STT table.\n");
    fseek(fp, 0, SEEK_SET);
    UTF32 Othercase;
    UTF32 nextcode = ReadCodePointAndOthercase(fp, Othercase);

    // Othercase
    //
    while (UNI_EOF != nextcode)
    {
        UTF32 SourceA[2];
        SourceA[0] = nextcode;
        SourceA[1] = L'\0';
        const UTF32 *pSourceA = SourceA;

        UTF8 TargetA[5];
        UTF8 *pTargetA = TargetA;

        ConversionResult cr;
        cr = ConvertUTF32toUTF8(&pSourceA, pSourceA+1, &pTargetA, pTargetA+sizeof(TargetA)-1, lenientConversion);

        if (conversionOK != cr)
        {
            nextcode = ReadCodePointAndOthercase(fp, Othercase);
            continue;
        }

        UTF32 SourceB[2];
        SourceB[0] = Othercase;
        SourceB[1] = L'\0';
        const UTF32 *pSourceB = SourceB;

        UTF8 TargetB[5];
        UTF8 *pTargetB = TargetB;

        cr = ConvertUTF32toUTF8(&pSourceB, pSourceB+1, &pTargetB, pTargetB+sizeof(TargetB)-1, lenientConversion);

        if (conversionOK == cr)
        {
            if (pTargetA - TargetA != pTargetB - TargetB)
            {
                fprintf(stderr, "Different UTF-8 length between cases is unsupported.\n");
                exit(0);
            }

            // Calculate XOR string.
            //
            UTF8 Xor[5];
            UTF8 *pA = TargetA;
            UTF8 *pB = TargetB;
            UTF8 *pXor = Xor;

            while (pA < pTargetA)
            {
                *pXor = *pA ^ *pB;
                pA++;
                pB++;
                pXor++;
            }
            size_t nXor = pXor - Xor;

            int i;
            bool bFound = false;
            for (i = 0; i < nOutputTable; i++)
            {
                if (memcmp(aOutputTable[i].p, Xor, nXor) == 0)
                {
                    bFound = true;
                    break;
                }
            }

            if (!bFound)
            {
                printf("Output String not found. This should not happen.\n");
                exit(0);
            }

            sm.TestString(TargetA, pTargetA, i);
        }

        UTF32 nextcode2 = ReadCodePointAndOthercase(fp, Othercase);
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
    UTF32 Othercase;
    UTF32 nextcode = ReadCodePointAndOthercase(fp, Othercase);

    // Othercase
    //
    nOutputTable = 0;
    while (UNI_EOF != nextcode)
    {
        UTF32 SourceA[2];
        SourceA[0] = nextcode;
        SourceA[1] = L'\0';
        const UTF32 *pSourceA = SourceA;

        UTF8 TargetA[5];
        UTF8 *pTargetA = TargetA;

        ConversionResult cr;
        cr = ConvertUTF32toUTF8(&pSourceA, pSourceA+1, &pTargetA, pTargetA+sizeof(TargetA)-1, lenientConversion);

        if (conversionOK != cr)
        {
            nextcode = ReadCodePointAndOthercase(fp, Othercase);
            continue;
        }

        UTF32 SourceB[2];
        SourceB[0] = Othercase;
        SourceB[1] = L'\0';
        const UTF32 *pSourceB = SourceB;

        UTF8 TargetB[5];
        UTF8 *pTargetB = TargetB;

        cr = ConvertUTF32toUTF8(&pSourceB, pSourceB+1, &pTargetB, pTargetB+sizeof(TargetB)-1, lenientConversion);

        if (conversionOK == cr)
        {
            if (pTargetA - TargetA != pTargetB - TargetB)
            {
                fprintf(stderr, "Different UTF-8 length between cases is unsupported.\n");
                exit(0);
            }

            // Calculate XOR string.
            //
            UTF8 Xor[5];
            UTF8 *pA = TargetA;
            UTF8 *pB = TargetB;
            UTF8 *pXor = Xor;

            while (pA < pTargetA)
            {
                *pXor = *pA ^ *pB;
                pA++;
                pB++;
                pXor++;
            }
            size_t nXor = pXor - Xor;

            int i;
            bool bFound = false;
            for (i = 0; i < nOutputTable; i++)
            {
                if (memcmp(aOutputTable[i].p, Xor, nXor) == 0)
                {
                    bFound = true;
                    break;
                }
            }

            if (!bFound)
            {
                aOutputTable[nOutputTable].p = new UTF8[nXor];
                memcpy(aOutputTable[nOutputTable].p, Xor, nXor);
                aOutputTable[nOutputTable].n = nXor;
                i = nOutputTable++;
            }

            cIncluded++;
            sm.RecordString(TargetA, pTargetA, i);
        }

        UTF32 nextcode2 = ReadCodePointAndOthercase(fp, Othercase);
        if (nextcode2 < nextcode)
        {
            fprintf(stderr, "Codes in file are not in order.\n");
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

    printf("const UTF8 *%s_ott[%d] =\n", LowerPrefix, nOutputTable);
    printf("{\n");
    int i;
    for (i = 0; i < nOutputTable; i++)
    {
        UTF8 *p = aOutputTable[i].p;
        printf("    T(\"");
        size_t n = aOutputTable[i].n;
        while (n--)
        {
            printf("\\x%02X", *p);
            p++;
        }

        if (i != nOutputTable - 1)
        {
            printf("\"),\n");
        }
        else
        {
            printf("\")\n");
        }

        delete aOutputTable[i].p;
        aOutputTable[i].p = NULL;
    }
    nOutputTable = 0;
    printf("};\n");
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
