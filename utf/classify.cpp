#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <ctype.h>
#include <string.h>

#include "ConvertUTF.h"

bool isPrivateUse(int ch)
{
    return (  (  UNI_PU1_START <= ch
              && ch <= UNI_PU1_END)
           || (  UNI_PU2_START <= ch
              && ch <= UNI_PU2_END)
           || (  UNI_PU3_START <= ch
              && ch <= UNI_PU3_END));
}

typedef struct State
{
    int           iState;
    struct State *merged;
    struct State *next[256];
} State;

// Special States.
//
State Undefined;
State NotMember;
State Member;

State *StartingState;

#define NUM_STATES 20000
int nStates;
State *stt[NUM_STATES];
UTF8 itt[256];
bool ColumnPresent[256];
int nColumns;

State *AllocateState(void)
{
    State *p = new State;
    int i;
    for (i = 0; i < 256; i++)
    {
        p->next[i] = &Undefined;
    }
    p->merged = NULL;
    return p;
}

void FreeState(State *p)
{
    delete p;
}

void RecordString(UTF8 *pStart, UTF8 *pEnd, bool bMember)
{
    State *pState = StartingState;
    while (pStart < pEnd-1)
    {
        UTF8 ch = *pStart;
        if (&Member == pState->next[ch])
        {
            printf("Already recorded. This shouldn't happen.\n");
            exit(0);
        }
        else if (&NotMember == pState->next[ch])
        {
            printf("Already recorded as not a member. This shouldn't happen.\n");
            exit(0);
        }
        else if (&Undefined == pState->next[ch])
        {
            State *p = AllocateState();
            stt[nStates++] = p;
            pState->next[ch] = p;
            pState = p;
        }
        else
        {
            pState = pState->next[ch];
        }
        pStart++;
    }

    if (pStart < pEnd)
    {
        UTF8 ch = *pStart;
        if (&Member == pState->next[ch])
        {
            printf("Already recorded. This shouldn't happen.\n");
            exit(0);
        }
        else if (&NotMember == pState->next[ch])
        {
            printf("Already recorded as not a member. This shouldn't happen.\n");
            exit(0);
        }
        else if (&Undefined == pState->next[ch])
        {
            if (bMember)
            {
                pState->next[ch] = &Member;
            }
            else
            {
                pState->next[ch] = &NotMember;
            }
        }
        else
        {
            printf("Already recorded as prefix of another string. This shouldn't happen.\n");
            exit(0);
        }
        pStart++;
    }
}

int cIncluded;
int cExcluded;
int cError;

void ReportStatus(void)
{
    int SizeOfState;
    if (nStates < 256)
    {
        SizeOfState = sizeof(unsigned char);
    }
    else if (nStates < 65536)
    {
        SizeOfState = sizeof(unsigned short);
    }
    else
    {
        SizeOfState = sizeof(unsigned int);
    }

    int nSize = nStates*SizeOfState*nColumns + 256;
    printf("%d included, %d excluded, %d errors, %d states, %d columns, %d bytes\n", cIncluded, cExcluded, cError, nStates, nColumns, nSize);
}

bool RowsEqual(State *p, State *q)
{
    if (p == q)
    {
        return true;
    }
    else if (  NULL == p
            || NULL == q)
    {
        return false;
    }

    int i;
    for (i = 0; i < 256; i++)
    {
        if (p->next[i] != q->next[i])
        {
            return false;
        }
    }
    return true;
}

bool ColumnsEqual(unsigned char iColumn, unsigned char jColumn)
{
    int i;
    for (i = 0; i < nStates; i++)
    {
        State *p = stt[i];
        if (p->next[iColumn] != p->next[jColumn])
        {
            return false;
        }
    }
    return true;
}

void RemoveAllNonMemberRows()
{
    printf("Pruning away all states which never lead to a Member state.\n");
    int i;
    for (i = 0; i < nStates; i++)
    {
        stt[i]->merged = NULL;
    }

    int j;
    for (i = 0; i < nStates; i++)
    {
        bool bAllNonMember = true;
        for (j = 0; j < 256; j++)
        {
            if (&NotMember != stt[i]->next[j])
            {
                bAllNonMember = false;
                break;
            }
        }

        if (bAllNonMember)
        {
            // Prune (i)th row so as to arrive at NotMember state one transition earlier.
            //
            stt[i]->merged = &NotMember;
        }
    }

    // Update all pointers to refer to merged state.
    //
    for (i = 0; i < nStates; i++)
    {
        State *pi = stt[i];
        if (NULL == pi->merged)
        {
            for (j = 0; j < 256; j++)
            {
                State *pj = pi->next[j];
                if (NULL != pj->merged)
                {
                    pi->next[j] = pj->merged;
                }
            }
        }
    }

    // Free duplicate states and shrink state table accordingly.
    //
    for (i = 0; i < nStates;)
    {
        State *pi = stt[i];
        if (NULL == pi->merged)
        {
            i++;
        }
        else
        {
            FreeState(pi);
            stt[i] = NULL;

            int k;
            nStates--;
            for (k = i; k < nStates; k++)
            {
                stt[k] = stt[k+1];
            }
        }
    }
    ReportStatus();
}

void RemoveDuplicateRows(void)
{
    printf("Merging states which lead to the same states.\n");
    int i, j;
    for (i = 0; i < nStates; i++)
    {
        stt[i]->merged = NULL;
    }

    // Find and mark duplicate rows.
    //
    for (i = 0; i < nStates; i++)
    {
        State *pi = stt[i];
        if (NULL == pi->merged)
        {
            for (j = i+1; j < nStates; j++)
            {
                State *pj = stt[j];
                if (NULL == pj->merged)
                {
                    if (RowsEqual(pi, pj))
                    {
                        // Merge (j)th row into (i)th row.
                        //
                        pj->merged = pi;
                    }
                }
            }
        }
    }

    // Update all pointers to refer to merged state.
    //
    for (i = 0; i < nStates; i++)
    {
        State *pi = stt[i];
        if (NULL == pi->merged)
        {
            for (j = 0; j < 256; j++)
            {
                State *pj = pi->next[j];
                if (NULL != pj->merged)
                {
                    pi->next[j] = pj->merged;
                }
            }
        }
    }

    // Free duplicate states and shrink state table accordingly.
    //
    for (i = 0; i < nStates;)
    {
        State *pi = stt[i];
        if (NULL == pi->merged)
        {
            i++;
        }
        else
        {
            FreeState(pi);
            stt[i] = NULL;

            int k;
            nStates--;
            for (k = i; k < nStates; k++)
            {
                stt[k] = stt[k+1];
            }
        }
    }
    ReportStatus();
}

void DetectDuplicateColumns(void)
{
    printf("Detecting duplicate columns and constructing Input Translation Table.\n");
    int i;
    for (i = 0; i < 256; i++)
    {
        itt[i] = i;
        ColumnPresent[i] = true;
    }

    for (i = 0; i < 256; i++)
    {
        if (!ColumnPresent[i])
        {
            continue;
        }

        int j;
        for (j = i+1; j < 256; j++)
        {
            if (ColumnsEqual(i, j))
            {
                itt[j] = i;
                ColumnPresent[j] = false;
            }
        }
    }

    nColumns = 0;
    for (i = 0; i < 256; i++)
    {
        if (ColumnPresent[i])
        {
            itt[i] = nColumns;
            nColumns++;
        }
        else
        {
            itt[i] = itt[itt[i]];
        }
    }
    ReportStatus();
}

void SetUndefinedStates(void)
{
    printf("Setting all invalid UTF-8 sequences to NotMember.\n");
    int i;
    for (i = 0; i < nStates; i++)
    {
        int j;
        for (j = 0; j < 256; j++)
        {
            if (&Undefined == stt[i]->next[j])
            {
                stt[i]->next[j] = &NotMember;
            }
        }
    }
}

int ReadCodePoint(FILE *fp)
{
    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), fp) == NULL)
    {
        return -1;
    }

    int code = 0;
    char *p = buffer;
    while (  '\0' != *p
          && ';' != *p)
    {
        char ch = *p;
        if (  ch <= '9'
           && '0' <= ch)
        {
            ch = ch - '0';
        }
        else if (  ch <= 'F'
                && 'A' <= ch)
        {
            ch = ch - 'A' + 10;
        }
        else if (  ch <= 'f'
                && 'a' <= ch)
        {
            ch = ch - 'a' + 10;
        }
        else
        {
            return -1;
        }
        code = (code << 4) + ch;
        p++;
    }
    return code;
}

void TestTable(FILE *fp)
{
    printf("Testing STT table.\n");
    fseek(fp, 0, SEEK_SET);
    int nextcode = ReadCodePoint(fp);
    int i;
    for (i = 0; i <= UNI_MAX_LEGAL_UTF32; i++)
    {
        bool bMember;
        if (i == nextcode)
        {
            if (!isPrivateUse(i))
            {
                bMember = true;
            }
            else
            {
                bMember = false;
            }

            if (0 <= nextcode)
            {
                nextcode = ReadCodePoint(fp);
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
            State *pState = StartingState;
            UTF8 *p = Target;
            while (  p < pTarget
                  && &NotMember != pState
                  && &Member != pState)
            {
                pState = pState->next[(unsigned char)*p];
                p++;
            }

            if (  (  &Member == pState
                  && !bMember)
               || (  &NotMember == pState
                  && bMember))
            {
                printf("State Transition Table does not work.\n");
                exit(0);
            }
        }
    }
}

void BuildTables(FILE *fp)
{
    StartingState = AllocateState();
    stt[nStates++] = StartingState;

    int i;
    nColumns = 256;

    fseek(fp, 0, SEEK_SET);
    int nextcode = ReadCodePoint(fp);
    for (i = 0; i <= UNI_MAX_LEGAL_UTF32; i++)
    {
        bool bMember;
        if (i == nextcode)
        {
            if (!isPrivateUse(i))
            {
                bMember = true;
                cIncluded++;
            }
            else
            {
                bMember = false;
                cExcluded++;
            }

            if (0 <= nextcode)
            {
                nextcode = ReadCodePoint(fp);
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
            RecordString(Target, pTarget, bMember);
        }
        else
        {
            cError++;
        }
    }
    ReportStatus();
}

void NumberStates(void)
{
    int i;
    for (i = 0; i < nStates; i++)
    {
        stt[i]->iState = i;
    }
}

void OutputTables(char *UpperPrefix, char *LowerPrefix)
{
    int iMemberState = nStates;
    int iNotMemberState = nStates+1;

    printf("#define %s_START_STATE (0)\n", UpperPrefix);
    printf("#define %s_ISMEMBER_STATE (%d)\n", UpperPrefix, iMemberState);
    printf("#define %s_ISNOTMEMBER_STATE (%d)\n", UpperPrefix, iNotMemberState);
    printf("\n");

    printf("unsigned char %s_itt[256] =\n", LowerPrefix);
    printf("{\n    ");
    int i;
    for (i = 0; i < 256; i++)
    {
        printf(" %d", itt[i]);
        if (i < 256-1)
        {
            printf(",");
        }
    }
    printf("\n};\n\n");

    if (nStates < 256)
    {
        printf("unsigned char %s_stt[%d][%d] =\n", LowerPrefix, nStates, nColumns);
    }
    else if (nStates < 65536)
    {
        printf("unsigned short %s_stt[%d][%d] =\n", LowerPrefix, nStates, nColumns);
    }
    else
    {
        printf("unsigned long %s_stt[%d][%d] =\n", LowerPrefix, nStates, nColumns);
    }
    printf("{\n");
    for (i = 0; i < nStates; i++)
    {
        State *pi = stt[i];

        printf("    {");
        int j;
        for (j = 0; j < 256; j++)
        {
            if (!ColumnPresent[j])
            {
                continue;
            }

            State *pj = pi->next[j];

            int k;
            if (&Member == pj)
            {
                k = iMemberState; 
            }
            else if (&NotMember == pj)
            {
                k = iNotMemberState;
            }
            else
            {
                k = pj->iState;
            }

            if (0 != j)
            {
                printf(",");
            }
            printf(" %3d", k);
        }
        printf("}");
        if (i < nStates - 1)
        {
            printf(",");
        }
        printf("\n");
    }
    printf("};\n");
}

void BuildAndOutputTable(FILE *fp, char *UpperPrefix, char *LowerPrefix)
{
    nStates = 0;
    cIncluded = 0;
    cExcluded = 0;
    cError = 0;

    // Construct State Transition Table.
    //
    BuildTables(fp);
    TestTable(fp);
    SetUndefinedStates();
    TestTable(fp);

    // Optimize State Transition Table.
    //
    RemoveAllNonMemberRows();
    TestTable(fp);
    RemoveAllNonMemberRows();
    TestTable(fp);
    RemoveAllNonMemberRows();
    TestTable(fp);
    RemoveDuplicateRows();
    TestTable(fp);
    DetectDuplicateColumns();

    // Output State Transition Table.
    //
    NumberStates();
    OutputTables(UpperPrefix, LowerPrefix);
}

#if 0
// 270 included, 1113842 excluded, 0 errors, 14 states, 32 columns, 704 bytes.
//
#define DIGIT_START_STATE (0)
#define DIGIT_ISMEMBER_STATE (14)
#define DIGIT_ISNOTMEMBER_STATE (15)

unsigned char digit_itt[256] =
{
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 2, 2, 2, 2, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 7, 8, 9, 8, 8, 8, 8, 8, 8, 8, 10, 10, 10, 11, 10, 12, 13, 14, 14, 14, 14, 15, 16, 17, 16, 18, 19, 20, 19, 20, 19, 20, 21, 22, 21, 22, 21, 22, 21, 21, 21, 23, 10, 24, 25, 10, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 26, 0, 27, 0, 0, 0, 0, 28, 29, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 30, 31, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

unsigned char digit_stt[14][32] =
{
    {  15,  14,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,   1,   2,   3,   6,   9,  10},
    {  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  14,  14,  14,  14,  14,  14,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15},
    {  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  14,  14,  14,  15,  15,  15,  15,  15,  15,  15,  15},
    {  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,   4,  15,   4,   4,  15,   4,  15,   4,   5,   5,   1,  15,  15,  15,  15,  15,  15},
    {  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  14,  14,  14,  14,  14,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15},
    {  15,  15,  15,  15,  15,  15,  15,  14,  14,  14,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15},
    {  15,  15,  15,   7,  15,  15,  15,  15,  15,  15,  15,  15,   1,   5,  15,   8,  15,   5,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15},
    {  15,  15,  14,  14,  14,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15},
    {  15,  15,  15,  15,  14,  14,  14,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15},
    {  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,   5,  15,  15,  15,  15,  15,  15},
    {  15,  15,  15,  15,  15,  15,  15,  11,  15,  15,  15,  12,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15},
    {  15,  15,  15,  15,  15,  15,  15,  15,  15,   1,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15},
    {  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  13,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15},
    {  15,  15,  15,  15,  15,  15,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  15,  15,  15,  15,  15,  15}
};

void VerifyTables(FILE *fp)
{
    printf("Testing final ITT and STT.\n");
    fseek(fp, 0, SEEK_SET);
    int nextcode = ReadCodePoint(fp);
    int i;
    for (i = 0; i <= UNI_MAX_LEGAL_UTF32; i++)
    {
        bool bMember;
        if (i == nextcode)
        {
            if (!isPrivateUse(i))
            {
                bMember = true;
            }
            else
            {
                bMember = false;
            }

            if (0 <= nextcode)
            {
                nextcode = ReadCodePoint(fp);
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
            int iState = DIGIT_START_STATE;
            UTF8 *p = Target;
            while (  p < pTarget
                  && iState < DIGIT_ISMEMBER_STATE)
            {
                iState = digit_stt[iState][digit_itt[(unsigned char)*p]];
                p++;
            }

            if (  (  DIGIT_ISMEMBER_STATE == iState
                  && !bMember)
               || (  DIGIT_ISNOTMEMBER_STATE == iState
                  && bMember))
            {
                printf("Input Translation Table and State Transition Table do not work.\n");
                exit(0);
            }
        }
    }
}
#endif

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
        pFilename = "NumericDecimal.txt";
        pPrefix   = "digit";
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
            pPrefixLower[i] = tolower(pPrefixLower[i]);
        }

        if (islower(pPrefixUpper[i]))
        {
            pPrefixUpper[i] = toupper(pPrefixUpper[i]);
        }
    }

    BuildAndOutputTable(fp, pPrefixUpper, pPrefixLower);
    //VerifyTables(fp);

    fclose(fp);
}
