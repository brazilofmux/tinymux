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
 * one of potentially many accepting states.  Each accepting state
 * corresponds to an entry in an output table which contains enough
 * information to construct the sequence of associated code points.
 * Potentially, several output tables (one for each method of constructing the
 * associated code point sequence) may be generated.
 *
 * For example, many times, upper case and lower case characters occur in
 * runs.  It is possible to construct all of the associated code points in a
 * range by flippping the same bits in the corresponding range of given code
 * points.  Concretely, the ASCII range 'a-z' differ from 'A-Z' in one bit
 * (0x20).
 *
 * Another approach is to define a range and extract a portion of the given
 * code point to be used as an index within that range to determine the
 * associated code point.
 *
 * Sometimes, multiple corresponding code points are associated, and they must
 * be quoted explicitly.
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
    UTF8  *p;
    size_t n_bytes;
    size_t n_points;
    int    n_refs;
} aLiteralTable[5000];
int nLiteralTable = 0;

static struct
{
    UTF8  *p;
    size_t n_bytes;
    size_t n_points;
    int    n_refs;
} aXorTable[5000];
int nXorTable = 0;

bool g_bDefault = false;
int  g_iDefaultState = 0;

#define MAX_POINTS 10
UTF32 ReadCodePointAndRelatedCodePoints(FILE *fp, int &nRelatedPoints, UTF32 aRelatedPoints[])
{
    nRelatedPoints = 0;

    char buffer[1024];
    char *p = ReadLine(fp, buffer, sizeof(buffer));
    if (NULL == p)
    {
        return UNI_EOF;
    }

    int nFields;
    char *aFields[2];
    ParseFields(buffer, sizeof(aFields)/sizeof(aFields[0]), nFields, aFields);
    if (nFields < 2)
    {
        return UNI_EOF;
    }

    // Field #0 - Code Point
    //
    int codepoint = DecodeCodePoint(aFields[0]);


    // Field #1 - Associated Code Points.
    //
    int   nPoints;
    char *aPoints[MAX_POINTS];
    ParsePoints(aFields[1], sizeof(aPoints)/sizeof(aPoints[0]), nPoints, aPoints);
    if (nPoints < 1)
    {
        fprintf(stderr, "At least one related code point is required.\n");
        exit(0);
        return UNI_EOF;
    }

    for (int i = 0; i < nPoints; i++)
    {
        aRelatedPoints[i] = DecodeCodePoint(aPoints[i]);
    }
    nRelatedPoints = nPoints;
    return codepoint;
}

void BuildOutputTable(FILE *fp)
{
    fprintf(stderr, "Building Output Table.\n");
    fseek(fp, 0, SEEK_SET);

    int   nRelatedPoints;
    UTF32 aRelatedPoints[MAX_POINTS];
    UTF32 nextcode = ReadCodePointAndRelatedCodePoints(fp, nRelatedPoints, aRelatedPoints);

    while (UNI_EOF != nextcode)
    {
        UTF32 SourceA[2];
        SourceA[0] = nextcode;
        SourceA[1] = L'\0';
        const UTF32 *pSourceA = SourceA;

        UTF8 TargetA[5];
        UTF8 *pTargetA = TargetA;

        ConversionResult cr;
        cr = ConvertUTF32toUTF8(&pSourceA, pSourceA+1, &pTargetA,
            pTargetA+sizeof(TargetA)-1, lenientConversion);

        if (conversionOK != cr)
        {
            nextcode = ReadCodePointAndRelatedCodePoints(fp, nRelatedPoints, aRelatedPoints);
            continue;
        }

        UTF32 SourceB[MAX_POINTS+1];
        int i;
        for (i = 0; i < nRelatedPoints; i++)
        {
            SourceB[i] = aRelatedPoints[i];
        }
        SourceB[i] = L'\0';
        const UTF32 *pSourceB = SourceB;

        UTF8 TargetB[5*MAX_POINTS];
        UTF8 *pTargetB = TargetB;

        cr = ConvertUTF32toUTF8(&pSourceB, pSourceB+nRelatedPoints, &pTargetB,
            pTargetB+sizeof(TargetB)-1, lenientConversion);

        if (conversionOK == cr)
        {
            bool bFound = false;
            if (pTargetA - TargetA != pTargetB - TargetB)
            {
                // Build Literal entry.
                //
                UTF8 *pLiteral  = TargetB;
                size_t nLiteral = pTargetB - TargetB;

                for (i = 0; i < nLiteralTable; i++)
                {
                    if (  aLiteralTable[i].n_bytes == nLiteral
                       && memcmp(aLiteralTable[i].p, pLiteral, nLiteral) == 0)
                    {
                        bFound = true;
                        break;
                    }
                }
    
                if (!bFound)
                {
                    aLiteralTable[nLiteralTable].p = new UTF8[nLiteral];
                    memcpy(aLiteralTable[nLiteralTable].p, pLiteral, nLiteral);
                    aLiteralTable[nLiteralTable].n_bytes = nLiteral;
                    aLiteralTable[nLiteralTable].n_points = nRelatedPoints;
                    aLiteralTable[nLiteralTable].n_refs   = 0;
                    nLiteralTable++;
                }
            }
            else
            {
                // Build XOR entry.
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
    
                for (i = 0; i < nXorTable; i++)
                {
                    if (  aXorTable[i].n_bytes == nXor
                       && memcmp(aXorTable[i].p, Xor, nXor) == 0)
                    {
                        bFound = true;
                        break;
                    }
                }
    
                if (!bFound)
                {
                    aXorTable[nXorTable].p = new UTF8[nXor];
                    memcpy(aXorTable[nXorTable].p, Xor, nXor);
                    aXorTable[nXorTable].n_bytes  = nXor;
                    aXorTable[nXorTable].n_points = nRelatedPoints;
                    aXorTable[nXorTable].n_refs   = 0;
                    nXorTable++;
                }
            }
        }

        UTF32 nextcode2 = ReadCodePointAndRelatedCodePoints(fp, nRelatedPoints, aRelatedPoints);
        if (nextcode2 < nextcode)
        {
            fprintf(stderr, "Codes in file are not in order.\n");
            exit(0);
        }
        nextcode = nextcode2;
    }
}

void TestTable(FILE *fp)
{
    fprintf(stderr, "Testing STT table.\n");
    fseek(fp, 0, SEEK_SET);

    int   nRelatedPoints;
    UTF32 aRelatedPoints[MAX_POINTS];
    UTF32 nextcode = ReadCodePointAndRelatedCodePoints(fp, nRelatedPoints, aRelatedPoints);

    while (UNI_EOF != nextcode)
    {
        UTF32 SourceA[2];
        SourceA[0] = nextcode;
        SourceA[1] = L'\0';
        const UTF32 *pSourceA = SourceA;

        UTF8 TargetA[5];
        UTF8 *pTargetA = TargetA;

        ConversionResult cr;
        cr = ConvertUTF32toUTF8(&pSourceA, pSourceA+1, &pTargetA,
            pTargetA+sizeof(TargetA)-1, lenientConversion);

        if (conversionOK != cr)
        {
            nextcode = ReadCodePointAndRelatedCodePoints(fp, nRelatedPoints, aRelatedPoints);
            continue;
        }

        UTF32 SourceB[MAX_POINTS+1];
        int i;
        for (i = 0; i < nRelatedPoints; i++)
        {
            SourceB[i] = aRelatedPoints[i];
        }
        SourceB[i] = L'\0';
        const UTF32 *pSourceB = SourceB;

        UTF8 TargetB[5*MAX_POINTS];
        UTF8 *pTargetB = TargetB;

        cr = ConvertUTF32toUTF8(&pSourceB, pSourceB+nRelatedPoints, &pTargetB,
            pTargetB+sizeof(TargetB)-1, lenientConversion);

        if (conversionOK == cr)
        {
            int iAcceptingState = g_bDefault ? 1 : 0;

            bool bFound = false;
            if (pTargetA - TargetA != pTargetB - TargetB)
            {
                // Build Literal entry.
                //
                UTF8 *pLiteral  = TargetB;
                size_t nLiteral = pTargetB - TargetB;

                for (i = 0; i < nLiteralTable; i++)
                {
                    if (  aLiteralTable[i].n_bytes == nLiteral
                       && memcmp(aLiteralTable[i].p, pLiteral, nLiteral) == 0)
                    {
                        bFound = true;
                        iAcceptingState += i;
                        break;
                    }
                }
            }
            else
            {
                // Build XOR entry.
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
    
                for (i = 0; i < nXorTable; i++)
                {
                    if (  aXorTable[i].n_bytes == nXor
                       && memcmp(aXorTable[i].p, Xor, nXor) == 0)
                    {
                        bFound = true;
                        iAcceptingState += nLiteralTable + i;
                        break;
                    }
                }
            }

            if (!bFound)
            {
                printf("Output String not found. This should not happen.\n");
                exit(0);
            }

            sm.TestString(TargetA, pTargetA, iAcceptingState);
        }

        UTF32 nextcode2 = ReadCodePointAndRelatedCodePoints(fp, nRelatedPoints, aRelatedPoints);
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
    int   nRelatedPoints;
    UTF32 aRelatedPoints[MAX_POINTS];
    UTF32 nextcode = ReadCodePointAndRelatedCodePoints(fp, nRelatedPoints, aRelatedPoints);

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
            nextcode = ReadCodePointAndRelatedCodePoints(fp, nRelatedPoints, aRelatedPoints);
            continue;
        }

        UTF32 SourceB[2];
        SourceB[0] = aRelatedPoints[0];
        SourceB[1] = L'\0';
        const UTF32 *pSourceB = SourceB;

        UTF8 TargetB[5];
        UTF8 *pTargetB = TargetB;

        cr = ConvertUTF32toUTF8(&pSourceB, pSourceB+1, &pTargetB, pTargetB+sizeof(TargetB)-1, lenientConversion);

        if (conversionOK == cr)
        {
            int iAcceptingState = g_bDefault ? 1 : 0;

            bool bFound = false;
            if (pTargetA - TargetA != pTargetB - TargetB)
            {
                // Build Literal entry.
                //
                UTF8 *pLiteral  = TargetB;
                size_t nLiteral = pTargetB - TargetB;

                for (int i = 0; i < nLiteralTable; i++)
                {
                    if (  aLiteralTable[i].n_bytes == nLiteral
                       && memcmp(aLiteralTable[i].p, pLiteral, nLiteral) == 0)
                    {
                        bFound = true;
                        iAcceptingState += i;
                        aLiteralTable[i].n_refs++;
                        break;
                    }
                }
            }
            else
            {
                // Build XOR entry.
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
    
                for (int i = 0; i < nXorTable; i++)
                {
                    if (  aXorTable[i].n_bytes == nXor
                       && memcmp(aXorTable[i].p, Xor, nXor) == 0)
                    {
                        bFound = true;
                        iAcceptingState += nLiteralTable + i;
                        aXorTable[i].n_refs++;
                        break;
                    }
                }
            }

            if (!bFound)
            {
                printf("Output String not found. This should not happen.\n");
                exit(0);
            }

            cIncluded++;
            sm.RecordString(TargetA, pTargetA, iAcceptingState);
        }

        UTF32 nextcode2 = ReadCodePointAndRelatedCodePoints(fp, nRelatedPoints, aRelatedPoints);
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

void BuildAndOutputTable(FILE *fp, char *UpperPrefix, char *LowerPrefix)
{
    BuildOutputTable(fp);

    // Construct State Transition Table.
    //
    sm.Init();
    LoadStrings(fp);
    TestTable(fp);

    // Leaving states undefined leads to a smaller table.  On the other hand,
    // do not make queries for code points outside the expected set.
    //
    if (g_bDefault)
    {
        sm.SetUndefinedStates(g_iDefaultState);
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

    printf("\n");

    int iLiteralStart = 0;
    int iXorStart = nLiteralTable;
    if (g_bDefault)
    {
        printf("#define %s_DEFAULT (%d)\n", UpperPrefix, g_iDefaultState);
        iLiteralStart++;
        iXorStart++;
    }
    printf("#define %s_LITERAL_START (%d)\n", UpperPrefix, iLiteralStart);
    printf("#define %s_XOR_START (%d)\n", UpperPrefix, iXorStart);

    printf("\n");
    printf("typedef struct\n");
    printf("{\n");
    printf("    size_t n_bytes;\n");
    printf("    size_t n_points;\n");
    printf("    UTF8   *p;\n");
    printf("} string_desc;\n");
    printf("\n");

    printf("const string_desc *%s_ott_literal[%d] =\n", LowerPrefix, nLiteralTable);
    printf("{\n");
    int i;
    for (i = 0; i < nLiteralTable; i++)
    {
        UTF8 *p = aLiteralTable[i].p;
        printf("    { %2d, %2d, ", aLiteralTable[i].n_bytes, aLiteralTable[i].n_points);

        printf("T(\"");
        size_t n = aLiteralTable[i].n_bytes;
        while (n--)
        {
            printf("\\x%02X", *p);
            p++;
        }

        if (i != nLiteralTable - 1)
        {
            printf("\") },");
        }
        else
        {
            printf("\") }");
        }
        printf(" // %d references\n", aLiteralTable[i].n_refs);

        delete aLiteralTable[i].p;
        aLiteralTable[i].p = NULL;
    }
    nLiteralTable = 0;
    printf("};\n");
    printf("\n");

    printf("const string_desc *%s_ott_xor[%d] =\n", LowerPrefix, nXorTable);
    printf("{\n");
    for (i = 0; i < nXorTable; i++)
    {
        UTF8 *p = aXorTable[i].p;
        printf("    { %2d, %2d, ", aXorTable[i].n_bytes, aXorTable[i].n_points);

        printf("T(\"");
        size_t n = aXorTable[i].n_bytes;
        while (n--)
        {
            printf("\\x%02X", *p);
            p++;
        }

        if (i != nXorTable - 1)
        {
            printf("\") },");
        }
        else
        {
            printf("\") }");
        }
        printf(" // %d references\n", aXorTable[i].n_refs);

        delete aXorTable[i].p;
        aXorTable[i].p = NULL;
    }
    nXorTable = 0;
    printf("};\n");
}

int main(int argc, char *argv[])
{
    char *pPrefix = NULL;
    char *pFilename = NULL;

    if (argc < 3)
    {
#if 0
        fprintf(stderr, "Usage: %s [-d] prefix unicodedata.txt\n", argv[0]);
        exit(0);
#else
        pFilename = "NumericDecimal.txt";
        pPrefix   = "digit";
        g_bDefault = false;
        g_iDefaultState = 0;
#endif
    }
    else
    {
        int j;
        for (j = 1; j < argc; j++)
        {
            if (0 == strcmp(argv[j], "-d"))
            {
                g_bDefault = true;
                g_iDefaultState = 0;
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
