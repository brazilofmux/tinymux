#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <ctype.h>
#include <string.h>

#include "ConvertUTF.h"
#include "smutil.h"

bool isPrivateUse(int ch)
{
    return (  (  UNI_PU1_START <= ch
              && ch <= UNI_PU1_END)
           || (  UNI_PU2_START <= ch
              && ch <= UNI_PU2_END)
           || (  UNI_PU3_START <= ch
              && ch <= UNI_PU3_END));
}

static UTF32 DecodeCodePoint(char *p)
{
    if (!isxdigit(*p))
    {
        // The first field was empty or contained invalid data.
        //
        return UNI_EOF;
    }

    int codepoint = 0;
    while (isxdigit(*p))
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
            return UNI_EOF;
        }
        codepoint = (codepoint << 4) + ch;
        p++;
    }
    return codepoint;
}

UTF32 ReadCodePoint(FILE *fp, int *pValue, UTF32 *pOthercase)
{
    char buffer[1024];
    char *p;

    for (;;)
    {
        if (fgets(buffer, sizeof(buffer), fp) == NULL)
        {
            *pValue = -1;
            *pOthercase = UNI_EOF;
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

    // Field #6 - Decimal Digit Property.
    //
    int Value;
    p = aFields[6];
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
    *pValue = Value;

    // Field #12 - Simple Uppercase Mapping.
    //
    int Uppercase = DecodeCodePoint(aFields[12]);

    // Field #13 = Simple Lowercase Mapping.
    //
    int Lowercase = DecodeCodePoint(aFields[13]);

    if (  Uppercase < 0
       && Lowercase < 0)
    {
        *pOthercase = UNI_EOF;
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
            *pOthercase = Uppercase;
        }
        else
        {
            *pOthercase = Lowercase;
        }
    }
    return codepoint;
}

State *StateMachine::AllocateState(void)
{
    State *p = new State;
    int i;
    for (i = 0; i < 256; i++)
    {
        p->next[i] = &m_Undefined;
    }
    p->merged = NULL;
    return p;
}

void StateMachine::FreeState(State *p)
{
    delete p;
}

    State *m_StartingState;

    int    m_nStates;
    State *m_stt[NUM_STATES];
    UTF8   m_itt[256];
    bool   m_ColumnPresent[256];
    int    m_nColumns;

    int    m_cIncluded;
    int    m_cExcluded;
    int    m_cError;

StateMachine::StateMachine(void)
{
    m_nStates = 0;
    Init();
}

void StateMachine::Init(void)
{
    Final();
    m_StartingState = AllocateState();
    m_stt[m_nStates++] = m_StartingState;

    int i;
    m_nColumns = 256;
    for (i = 0; i < m_nColumns; i++)
    {
        m_itt[i] = i;
        m_ColumnPresent[i] = true;
    }
    m_nLargestAcceptingState = -1;
}

void StateMachine::Final(void)
{
    int i;
    for (i = 0; i < m_nStates; i++)
    {
        FreeState(m_stt[i]);
        m_stt[i] = NULL;
    }
    m_StartingState = NULL;
    m_nStates = 0;
}

StateMachine::~StateMachine()
{
    Final();
}

void StateMachine::RecordString(UTF8 *pStart, UTF8 *pEnd, int AcceptingState)
{
    if (m_nLargestAcceptingState < AcceptingState)
    {
        m_nLargestAcceptingState = AcceptingState;
    }

    State *pState = m_StartingState;
    while (pStart < pEnd-1)
    {
        UTF8 ch = *pStart;
        if (&m_Undefined == pState->next[ch])
        {
            State *p = AllocateState();
            m_stt[m_nStates++] = p;
            pState->next[ch] = p;
            pState = p;
        }
        else if (  (State *)(m_aAcceptingStates) <= pState->next[ch]
                && pState->next[ch] <= (State *)(m_aAcceptingStates + sizeof(m_aAcceptingStates)))
        {
            fprintf(stderr, "Already recorded.  This shouldn't happen.\n");
            exit(0);
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
        if (&m_Undefined == pState->next[ch])
        {
            pState->next[ch] = (State *)(m_aAcceptingStates + AcceptingState);
        }
        else if (  (State *)(m_aAcceptingStates) <= pState->next[ch]
                && pState->next[ch] <= (State *)(m_aAcceptingStates + sizeof(m_aAcceptingStates)))
        {
            fprintf(stderr, "Already recorded.  This shouldn't happen.\n");
            exit(0);
        }
        else
        {
            fprintf(stderr, "Already recorded as prefix of another string. This shouldn't happen.\n");
            exit(0);
        }
        pStart++;
    }
}

void StateMachine::ReportStatus(void)
{
    int SizeOfState;
    int SizeOfMachine;
    MinimumMachineSize(&SizeOfState, &SizeOfMachine);
    fprintf(stderr, "%d states, %d columns, %d bytes\n", m_nStates, m_nColumns, SizeOfMachine);
}

bool StateMachine::RowsEqual(State *p, State *q)
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
        if (  &m_Undefined == p->next[i]
           || &m_Undefined == q->next[i])
        {
            // We interpret undefined transitions as 'Do not care'.
            //
            continue;
        }
        else if (p->next[i] != q->next[i])
        {
            return false;
        }
    }
    return true;
}

bool StateMachine::ColumnsEqual(int iColumn, int jColumn)
{
    int i;
    for (i = 0; i < m_nStates; i++)
    {
        State *p = m_stt[i];
#if 1
        if (  &m_Undefined == p->next[iColumn]
           || &m_Undefined == p->next[jColumn])
        {
            // We interpret undefined transitions as 'Do not care'.
            //
            continue;
        }
        else
#endif
        if (p->next[iColumn] != p->next[jColumn])
        {
            return false;
        }
    }
    return true;
}

void StateMachine::MergeAcceptingStates(void)
{
    fprintf(stderr, "Pruning away all states which only ever lead to one accepting state.\n");
    int i;
    for (i = 0; i < m_nStates; i++)
    {
        m_stt[i]->merged = NULL;
    }

    for (i = 0; i < m_nStates; i++)
    {
        State *pi = m_stt[i];
        if (m_StartingState == pi)
        {
            // We can't remove the starting state.
            //
            continue;
        }

        bool bMatched = false;
        State *pLastState = NULL;
        int k;
        for (k = 0; k < 256; k++)
        {
            if (&m_Undefined == pi->next[k])
            {
                // Undefined State will match everything.
                //
                continue;
            }
            else if (  pi->next[k] < (State *)(m_aAcceptingStates)
                    || (State *)(m_aAcceptingStates + sizeof(m_aAcceptingStates)) < pi->next[k])
            {
                // Not at accepting state. We can't eliminate this transition.
                //
                bMatched = false;
                break;
            }

            if (NULL == pLastState)
            {
                bMatched = true;
                pLastState = pi->next[k];
            }
            else if (pLastState != pi->next[k])
            {
                bMatched = false;
                break;
            }
        }

        if (bMatched)
        {
            // Prune (i)th row so as to arrive at the accepting state one transition earlier.
            //
            pi->merged = pLastState;
        }
    }

    // Update all pointers to refer to merged state.
    //
    for (i = 0; i < m_nStates; i++)
    {
        State *pi = m_stt[i];
        if (NULL == pi->merged)
        {
            int j;
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
    for (i = 0; i < m_nStates;)
    {
        State *pi = m_stt[i];
        if (NULL == pi->merged)
        {
            i++;
        }
        else
        {
            FreeState(pi);
            m_stt[i] = NULL;

            int k;
            m_nStates--;
            for (k = i; k < m_nStates; k++)
            {
                m_stt[k] = m_stt[k+1];
            }
        }
    }
    ReportStatus();
}

void StateMachine::RemoveDuplicateRows(void)
{
    fprintf(stderr, "Merging states which lead to the same state.\n");
    int i, j;
    for (i = 0; i < m_nStates; i++)
    {
        m_stt[i]->merged = NULL;
    }

    // Find and mark duplicate rows.
    //
    for (i = 0; i < m_nStates; i++)
    {
        State *pi = m_stt[i];
        if (NULL == pi->merged)
        {
            for (j = i+1; j < m_nStates; j++)
            {
                State *pj = m_stt[j];
                if (NULL == pj->merged)
                {
                    if (RowsEqual(pi, pj))
                    {
                        // Merge (j)th row into (i)th row.
                        //
                        pj->merged = pi;

                        // Let (j)th row defined transitions override (i)th
                        // row undefined transitions.
                        //
                        int u;
                        for (u = 0; u < 256; u++)
                        {
                            if (&m_Undefined == pi->next[u])
                            {
                                pi->next[u] = pj->next[u];
                            }
                        }
                    }
                }
            }
        }
    }

    // Update all pointers to refer to merged state.
    //
    for (i = 0; i < m_nStates; i++)
    {
        State *pi = m_stt[i];
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
    for (i = 0; i < m_nStates;)
    {
        State *pi = m_stt[i];
        if (NULL == pi->merged)
        {
            i++;
        }
        else
        {
            FreeState(pi);
            m_stt[i] = NULL;

            int k;
            m_nStates--;
            for (k = i; k < m_nStates; k++)
            {
                m_stt[k] = m_stt[k+1];
            }
        }
    }
    ReportStatus();
}

void StateMachine::DetectDuplicateColumns(void)
{
    fprintf(stderr, "Detecting duplicate columns and constructing Input Translation Table.\n");
    int i;
    for (i = 0; i < 256; i++)
    {
        m_itt[i] = static_cast<UTF8>(i);
        m_ColumnPresent[i] = true;
    }

    for (i = 0; i < 256; i++)
    {
        if (!m_ColumnPresent[i])
        {
            continue;
        }

        int j;
        for (j = i+1; j < 256; j++)
        {
            if (ColumnsEqual(i, j))
            {
                m_itt[j] = static_cast<UTF8>(i);
                m_ColumnPresent[j] = false;

                // Let (j)th column defined transitions override (i)th
                // column undefined transitions.
                //
                int u;
                for (u = 0; u < m_nStates; u++)
                {
                    if (&m_Undefined == m_stt[u]->next[i])
                    {
                        m_stt[u]->next[i] = m_stt[u]->next[j];
                    }
                }
            }
        }
    }

    m_nColumns = 0;
    for (i = 0; i < 256; i++)
    {
        if (m_ColumnPresent[i])
        {
            m_itt[i] = static_cast<UTF8>(m_nColumns);
            m_nColumns++;
        }
        else
        {
            m_itt[i] = m_itt[m_itt[i]];
        }
    }
    ReportStatus();
}

void StateMachine::SetUndefinedStates(int AcceptingState)
{
    fprintf(stderr, "Setting all undefined states to specified accepting state.\n");
    int i;
    for (i = 0; i < m_nStates; i++)
    {
        int j;
        for (j = 0; j < 256; j++)
        {
            if (&m_Undefined == m_stt[i]->next[j])
            {
                m_stt[i]->next[j] = (State *)(m_aAcceptingStates + AcceptingState);
            }
        }
    }
}

void StateMachine::NumberStates(void)
{
    int i;
    for (i = 0; i < m_nStates; i++)
    {
        m_stt[i]->iState = i;
    }
}

void StateMachine::MinimumMachineSize(int *pSizeOfState, int *pSizeOfMachine)
{
    int SizeOfState;
    int TotalStates = m_nStates + m_nLargestAcceptingState + 1;
    if (TotalStates < 256)
    {
        SizeOfState = sizeof(unsigned char);
    }
    else if (TotalStates < 65536)
    {
        SizeOfState = sizeof(unsigned short);
    }
    else
    {
        SizeOfState = sizeof(unsigned int);
    }
    *pSizeOfState = SizeOfState;
    *pSizeOfMachine = m_nStates*SizeOfState*m_nColumns + 256;
}

void StateMachine::OutputTables(char *UpperPrefix, char *LowerPrefix)
{
    int SizeOfState;
    int SizeOfMachine;
    MinimumMachineSize(&SizeOfState, &SizeOfMachine);

    printf("// %d states, %d columns, %d bytes\n", m_nStates, m_nColumns, SizeOfMachine);
    printf("//\n");

    int iAcceptingStatesStart = m_nStates;

    printf("#define %s_START_STATE (0)\n", UpperPrefix);
    printf("#define %s_ACCEPTING_STATES_START (%d)\n", UpperPrefix, iAcceptingStatesStart);
    printf("\n");

    printf("unsigned char %s_itt[256] =\n", LowerPrefix);
    printf("{\n    ");
    int i;
    for (i = 0; i < 256; i++)
    {
        printf(" %d", m_itt[i]);
        if (i < 256-1)
        {
            printf(",");
        }
    }
    printf("\n};\n\n");

    switch (SizeOfState)
    {
    case 1:
        printf("unsigned char %s_stt[%d][%d] =\n", LowerPrefix, m_nStates, m_nColumns);
        break;

    case 2:
        printf("unsigned short %s_stt[%d][%d] =\n", LowerPrefix, m_nStates, m_nColumns);
        break;

    default:
        printf("unsigned long %s_stt[%d][%d] =\n", LowerPrefix, m_nStates, m_nColumns);
        break;
    }
    printf("{\n");
    for (i = 0; i < m_nStates; i++)
    {
        State *pi = m_stt[i];

        printf("    {");
        int j;
        for (j = 0; j < 256; j++)
        {
            if (!m_ColumnPresent[j])
            {
                continue;
            }

            State *pj = pi->next[j];

            int k;
            if (  (State *)(m_aAcceptingStates) <= pj
               && pj <= (State *)(m_aAcceptingStates + sizeof(m_aAcceptingStates)))
            {
                char *p = reinterpret_cast<char *>(pj);
                k = static_cast<int>(iAcceptingStatesStart + (p - m_aAcceptingStates));
            }
            else if (&m_Undefined == pj)
            {
                // This is a don't care.
                //
                k = 0;
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
        if (i < m_nStates - 1)
        {
            printf(",");
        }
        printf("\n");
    }
    printf("};\n");
}

void StateMachine::TestString(UTF8 *pStart, UTF8 *pEnd, int AcceptingState)
{
    State *pState = m_StartingState;
    while (  pStart < pEnd
          && &m_Undefined != pState
          && (  pState < (State *)(m_aAcceptingStates)
             || (State *)(m_aAcceptingStates + sizeof(m_aAcceptingStates)) < pState))
    {
        pState = pState->next[(unsigned char)*pStart];
        pStart++;
    }

    if (&m_Undefined == pState)
    {
        fprintf(stderr, "Final State is undefined.\n");
        exit(0);
    }

    char *p = reinterpret_cast<char *>(pState);
    int iState = static_cast<int>(p - m_aAcceptingStates);

    if (iState != AcceptingState)
    {
        fprintf(stderr, "State Transition Table does not work.\n");
        exit(0);
    }
}