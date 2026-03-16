/*! \file pairs.cpp
 * \brief Top-level driver for building a state machine which recognizes a
 * pair of code points and indicates an associated integer (accepting state).
 *
 * The input file is composed of lines.  Each line is broken in
 * semicolon-delimited fields.  The first field contains two space-separated
 * code points (hex).  The second field is the result code point (hex).
 *
 * The constructed state machine feeds the UTF-8 bytes of both code points
 * (concatenated) through the DFA.  Because result code points can exceed
 * the state machine's accepting state range, we use indirection: the
 * accepting state is a 1-based index into a side table of result values.
 * Accepting state 0 means "no composition".
 *
 * This is used for NFC canonical composition: (starter, combining) -> composed.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <ctype.h>
#include <string.h>

#include "ConvertUTF.h"
#include "smutil.h"

#define MAX_PAIRS 2000

static int g_nResults = 0;
static int g_Results[MAX_PAIRS];   // Result code points, 1-indexed.

// Assign or find an index for a result code point.
//
int ResultIndex(int cp)
{
    // Linear scan is fine for ~1000 entries during generation.
    //
    for (int i = 0; i < g_nResults; i++)
    {
        if (g_Results[i] == cp)
        {
            return i + 1;  // 1-based; 0 = no match.
        }
    }
    if (g_nResults >= MAX_PAIRS)
    {
        fprintf(stderr, "Too many unique results (max %d).\n", MAX_PAIRS);
        exit(EXIT_FAILURE);
    }
    g_Results[g_nResults] = cp;
    g_nResults++;
    return g_nResults;  // 1-based.
}

struct PairEntry
{
    UTF32 cp1;
    UTF32 cp2;
    int   index;  // 1-based index into g_Results.
};

static int g_nPairs = 0;
static PairEntry g_Pairs[MAX_PAIRS];

// Read a line and parse: "CP1 CP2;RESULT"
// Returns false at EOF.
//
bool ReadPairAndValue(FILE *fp, UTF32 &cp1, UTF32 &cp2, int &index)
{
    char buffer[1024];
    char *p = ReadLine(fp, buffer, sizeof(buffer));
    if (nullptr == p)
    {
        return false;
    }

    int nFields;
    char *aFields[2];
    ParseFields(p, 2, nFields, aFields);
    if (nFields < 2)
    {
        return false;
    }

    // Field #0 - Two code points separated by space.
    //
    char *pKey = aFields[0];
    cp1 = DecodeCodePoint(pKey);

    // Advance past the first code point to find the second.
    //
    while (*pKey && !isspace(*pKey)) pKey++;
    while (*pKey && isspace(*pKey)) pKey++;
    cp2 = DecodeCodePoint(pKey);

    // Field #1 - Result code point (hex).
    //
    int resultCp = DecodeCodePoint(aFields[1]);
    index = ResultIndex(resultCp);

    return true;
}

// Convert two code points to a concatenated UTF-8 byte sequence.
//
int PairToUTF8(UTF32 cp1, UTF32 cp2, UTF8 *buf, int bufsize)
{
    UTF32 Source[2];
    const UTF32 *pSource;
    UTF8 *pTarget;
    ConversionResult cr;

    Source[0] = cp1;
    Source[1] = 0;
    pSource = Source;
    pTarget = buf;
    cr = ConvertUTF32toUTF8(&pSource, pSource + 1, &pTarget, buf + bufsize, lenientConversion);
    if (conversionOK != cr)
    {
        return 0;
    }
    int n1 = (int)(pTarget - buf);

    Source[0] = cp2;
    pSource = Source;
    pTarget = buf + n1;
    cr = ConvertUTF32toUTF8(&pSource, pSource + 1, &pTarget, buf + bufsize, lenientConversion);
    if (conversionOK != cr)
    {
        return 0;
    }

    return (int)(pTarget - buf);
}

StateMachine sm;

void TestTable(FILE *fp)
{
    fprintf(stderr, "Testing STT table.\n");
    fseek(fp, 0, SEEK_SET);

    UTF32 cp1, cp2;
    int index;
    while (ReadPairAndValue(fp, cp1, cp2, index))
    {
        UTF8 Target[10];
        int nBytes = PairToUTF8(cp1, cp2, Target, sizeof(Target));
        if (nBytes > 0)
        {
            sm.TestString(Target, Target + nBytes, index);
        }
    }
}

void LoadPairs(FILE *fp, FILE *fpBody, FILE *fpInclude)
{
    int cIncluded = 0;

    fseek(fp, 0, SEEK_SET);

    UTF32 cp1, cp2;
    int index;
    while (ReadPairAndValue(fp, cp1, cp2, index))
    {
        UTF8 Target[10];
        int nBytes = PairToUTF8(cp1, cp2, Target, sizeof(Target));
        if (nBytes > 0)
        {
            cIncluded++;
            sm.RecordString(Target, Target + nBytes, index);

            // Also record for verification.
            //
            if (g_nPairs < MAX_PAIRS)
            {
                g_Pairs[g_nPairs].cp1 = cp1;
                g_Pairs[g_nPairs].cp2 = cp2;
                g_Pairs[g_nPairs].index = index;
                g_nPairs++;
            }
        }
    }

    fprintf(fpBody, "// %d composition pairs.\n", cIncluded);
    fprintf(fpInclude, "// %d composition pairs.\n", cIncluded);
    fprintf(stderr, "%d composition pairs.\n", cIncluded);

    OutputStatus os;
    sm.OutputTables(nullptr, &os);
    fprintf(stderr, "%d states, %d columns, %d bytes\n", os.nStates, os.nColumns, os.SizeOfMachine);
}

bool g_bDefault = false;
int  g_iDefaultState = 0;

void OutputResultTable(FILE *fpBody, FILE *fpInclude, char *UpperPrefix, char *LowerPrefix)
{
    // Output the result code point table.
    // Index 0 is unused (means "no composition"); indices are 1-based.
    //
    fprintf(fpInclude, "#define %s_NFC_COMPOSE_RESULTS (%d)\n", UpperPrefix, g_nResults);
    fprintf(fpInclude, "extern LIBMUX_API const UTF32 %s_nfc_compose_result[%d];\n",
            LowerPrefix, g_nResults + 1);

    fprintf(fpBody, "const UTF32 %s_nfc_compose_result[%d] =\n{\n", LowerPrefix, g_nResults + 1);
    fprintf(fpBody, "    0x000000,  // index 0: no composition\n");
    for (int i = 0; i < g_nResults; i++)
    {
        fprintf(fpBody, "    0x%06X%s  // index %d\n",
                g_Results[i],
                (i < g_nResults - 1) ? "," : "",
                i + 1);
    }
    fprintf(fpBody, "};\n\n");
}

void BuildAndOutputTable(FILE *fp, FILE *fpBody, FILE *fpInclude, char *UpperPrefix, char *LowerPrefix)
{
    sm.Init();
    LoadPairs(fp, fpBody, fpInclude);
    TestTable(fp);

    if (g_bDefault)
    {
        sm.SetUndefinedStates(g_iDefaultState);
        TestTable(fp);
    }

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

    sm.NumberStates();
    OutputControl oc;
    oc.fpBody = fpBody;
    oc.fpInclude = fpInclude;
    oc.UpperPrefix = UpperPrefix;
    oc.LowerPrefix = LowerPrefix;
    sm.OutputTables(&oc, nullptr);

    // Output the result lookup table.
    //
    OutputResultTable(fpBody, fpInclude, UpperPrefix, LowerPrefix);
}

int main(int argc, char *argv[])
{
    const char *pPrefix = nullptr;
    const char *pFilename = nullptr;

    if (argc < 3)
    {
        fprintf(stderr, "Usage: pairs [-d val] prefix inputfile.txt\n");
        exit(0);
    }

    const char *pBodyFile = "utf8tables.cpp.txt";
    const char *pIncludeFile = "utf8tables.h.txt";

    int j;
    for (j = 1; j < argc; j++)
    {
        if (0 == strcmp(argv[j], "-d"))
        {
            g_bDefault = true;
            if (j + 1 < argc)
            {
                j++;
                g_iDefaultState = atoi(argv[j]);
            }
        }
        else if (0 == strcmp(argv[j], "-o") && j + 1 < argc)
        {
            pBodyFile = argv[++j];
        }
        else if (0 == strcmp(argv[j], "-i") && j + 1 < argc)
        {
            pIncludeFile = argv[++j];
        }
        else
        {
            if (nullptr == pPrefix)
            {
                pPrefix = argv[j];
            }
            else if (nullptr == pFilename)
            {
                pFilename = argv[j];
            }
        }
    }

    FILE *fp = fopen(pFilename, "rb");
    FILE *fpBody = fopen(pBodyFile, "a");
    FILE *fpInclude = fopen(pIncludeFile, "a");
    if (  nullptr == fp
       || nullptr == fpBody
       || nullptr == fpInclude)
    {
        fprintf(stderr, "Cannot open %s, %s, or %s\n", pFilename, pBodyFile, pIncludeFile);
        exit(0);
    }

    fprintf(fpBody, "// utf/%s\n//\n", pFilename);
    fprintf(fpInclude, "// utf/%s\n//\n", pFilename);

    size_t nPrefix = strlen(pPrefix);
    char *pPrefixLower = new char[nPrefix + 1];
    char *pPrefixUpper = new char[nPrefix + 1];
    memcpy(pPrefixLower, pPrefix, nPrefix + 1);
    memcpy(pPrefixUpper, pPrefix, nPrefix + 1);

    for (size_t i = 0; i < nPrefix; i++)
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

    fprintf(fpInclude, "\n");
    fprintf(fpBody, "\n");

    fclose(fp);
    fclose(fpBody);
    fclose(fpInclude);
}
