/*! \file integers.cpp
 * \brief Top-level driver for building a state machine which recognizes a
 * code point and indicates an associated integer (or accepting state).
 *
 * The input file is composed of lines.  Each line is broken in
 * semicolon-delimited fields.  The code point to recognize is taken from the
 * first field.  The associated integer (decimal base) is taken from the
 * second field.
 *
 * The constructed state machine associates the recognized code point with
 * the given value.  The integer value could be anything -- index into a
 * separately constructed table as is the case with mux_color, or an 8-bit
 * character as is the case with the UTF8-to-Latin1 and UTF8-to-ASCII down
 * conversions.
 *
 * It is not always necessary for the state machine to look at every byte
 * of a code point to determine the associated code point(s).  For this
 * reason, to advance to the next code requires a method separate from the
 * state machine produced here.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <ctype.h>
#include <string.h>

#include "ConvertUTF.h"
#include "smutil.h"

UTF32 ReadCodePointAndValue(FILE *fp, int &Value)
{
    char buffer[1024];
    char *p = ReadLine(fp, buffer, sizeof(buffer));
    if (NULL == p)
    {
        Value = -1;
        return UNI_EOF;
    }

    int nFields;
    char *aFields[2];
    ParseFields(p, 2, nFields, aFields);
    if (nFields < 2)
    {
        return UNI_EOF;
    }

    // Field #0 - Code Point (base 16)
    //
    int codepoint = DecodeCodePoint(aFields[0]);

    // Field #1 - Integer (base 10)
    //
    p = aFields[1];
    if (!isdigit(*p))
    {
        Value = -1;
    }
    else
    {
        Value = 0;
        do
        {
            Value = Value * 10 + (*p - '0');
            p++;
        } while (isdigit(*p));
    }

    return codepoint;
}

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
    UTF32 nextcode = ReadCodePointAndValue(fp, Value);

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
                fprintf(stderr, "Input Translation Table and State Transition Table do not work.\n");
                exit(0);
            }
        }
        UTF32 nextcode2 = ReadCodePointAndValue(fp, Value);
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
    UTF32 nextcode = ReadCodePointAndValue(fp, Value);

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

        UTF32 nextcode2 = ReadCodePointAndValue(fp, Value);
        if (nextcode2 < nextcode)
        {
            fprintf(stderr, "Codes in file are not in order.\n");
            exit(0);
        }
        nextcode = nextcode2;
    }
}

void LoadStrings(FILE *fp, FILE *fpBody, FILE *fpInclude)
{
    int cIncluded = 0;

    fseek(fp, 0, SEEK_SET);
    int Value;
    UTF32 nextcode = ReadCodePointAndValue(fp, Value);
    if (Value < 0)
    {
        fprintf(stderr, "Value missing from code U-%06X\n", static_cast<unsigned int>(nextcode));
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

        UTF32 nextcode2 = ReadCodePointAndValue(fp, Value);
        if (nextcode2 < nextcode)
        {
            fprintf(stderr, "Codes in file are not in order.\n");
            exit(0);
        }
        else if (  UNI_EOF != nextcode2
                && Value < 0)
        {
            fprintf(stderr, "Value missing from code U-%06X\n", static_cast<unsigned int>(nextcode2));
            exit(0);
        }
        nextcode = nextcode2;
    }
    fprintf(fpBody, "// %d code points.\n", cIncluded);
    fprintf(fpInclude, "// %d code points.\n", cIncluded);
    fprintf(stderr, "%d code points.\n", cIncluded);

    OutputStatus os;
    sm.OutputTables(NULL, &os);
    fprintf(stderr, "%d states, %d columns, %d bytes\n", os.nStates, os.nColumns, os.SizeOfMachine);
}

bool g_bDefault = false;
int  g_iDefaultState = '?';

void BuildAndOutputTable(FILE *fp, FILE *fpBody, FILE *fpInclude, char *UpperPrefix, char *LowerPrefix)
{
    // Construct State Transition Table.
    //
    sm.Init();
    LoadStrings(fp, fpBody, fpInclude);
    TestTable(fp);

    // Leaving states undefined leads to a smaller table because row and
    // column compression can take advantages of these 'don't care'
    // posibilities, but queries to undefined code points will gives undefined
    // answers.  Alternatively, a 'default' state can be give on the command
    // line.
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
    OutputControl oc;
    oc.fpBody = fpBody;
    oc.fpInclude = fpInclude;
    oc.UpperPrefix = UpperPrefix;
    oc.LowerPrefix = LowerPrefix;
    sm.OutputTables(&oc, NULL);
}

int main(int argc, char *argv[])
{
    const char *pPrefix = NULL;
    const char *pFilename = NULL;

    if (argc < 3)
    {
#if 0
        fprintf(stderr, "Usage: %s [-d ch] prefix unicodedata.txt\n", argv[0]);
        exit(0);
#else
        pFilename = "NumericDecimal.txt";
        pPrefix   = "digit";
        g_bDefault = false;
        g_iDefaultState = '?';
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
                if (j+1 < argc)
                {
                    j++;
                    g_iDefaultState = atoi(argv[j]);
                }
                else
                {
                    g_iDefaultState = '?';
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
    FILE *fpBody = fopen("utf8tables.cpp.txt", "a");
    FILE *fpInclude = fopen("utf8tables.h.txt", "a");
    if (  NULL == fp
       || NULL == fpBody
       || NULL == fpInclude)
    {
        fprintf(stderr, "Cannot open %s, utf8tables.cpp.txt, or utf8tables.h.txt\n", pFilename);
        exit(0);
    }

    fprintf(fpBody, "// utf/%s\n//\n", pFilename);
    fprintf(fpInclude, "// utf/%s\n//\n", pFilename);

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

    BuildAndOutputTable(fp, fpBody, fpInclude, pPrefixUpper, pPrefixLower);
    //VerifyTables(fp);

    fprintf(fpInclude, "\n");
    fprintf(fpBody, "\n");

    fclose(fp);
    fclose(fpBody);
    fclose(fpInclude);
}
