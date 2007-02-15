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

void StateMachine::RecordString(UTF8 *pStart, UTF8 *pEnd, bool bMember)
{
    State *pState = m_StartingState;
    while (pStart < pEnd-1)
    {
        UTF8 ch = *pStart;
        if (&m_Member == pState->next[ch])
        {
            fprintf(stderr, "Already recorded. This shouldn't happen.\n");
            exit(0);
        }
        else if (&m_NotMember == pState->next[ch])
        {
            fprintf(stderr, "Already recorded as not a member. This shouldn't happen.\n");
            exit(0);
        }
        else if (&m_Undefined == pState->next[ch])
        {
            State *p = AllocateState();
            m_stt[m_nStates++] = p;
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
        if (&m_Member == pState->next[ch])
        {
            fprintf(stderr, "Already recorded. This shouldn't happen.\n");
            exit(0);
        }
        else if (&m_NotMember == pState->next[ch])
        {
            fprintf(stderr, "Already recorded as not a member. This shouldn't happen.\n");
            exit(0);
        }
        else if (&m_Undefined == pState->next[ch])
        {
            if (bMember)
            {
                pState->next[ch] = &m_Member;
            }
            else
            {
                pState->next[ch] = &m_NotMember;
            }
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
        if (p->next[i] != q->next[i])
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
        if (p->next[iColumn] != p->next[jColumn])
        {
            return false;
        }
    }
    return true;
}

void StateMachine::RemoveAllNonMemberRows()
{
    fprintf(stderr, "Pruning away all states which never lead to a Member state.\n");
    int i;
    for (i = 0; i < m_nStates; i++)
    {
        m_stt[i]->merged = NULL;
    }

    int j;
    for (i = 0; i < m_nStates; i++)
    {
        bool bAllNonMember = true;
        for (j = 0; j < 256; j++)
        {
            if (&m_NotMember != m_stt[i]->next[j])
            {
                bAllNonMember = false;
                break;
            }
        }

        if (bAllNonMember)
        {
            // Prune (i)th row so as to arrive at NotMember state one transition earlier.
            //
            m_stt[i]->merged = &m_NotMember;
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

void StateMachine::RemoveDuplicateRows(void)
{
    fprintf(stderr, "Merging states which lead to the same states.\n");
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

void StateMachine::SetUndefinedStates(void)
{
    fprintf(stderr, "Setting all invalid UTF-8 sequences to NotMember.\n");
    int i;
    for (i = 0; i < m_nStates; i++)
    {
        int j;
        for (j = 0; j < 256; j++)
        {
            if (&m_Undefined == m_stt[i]->next[j])
            {
                m_stt[i]->next[j] = &m_NotMember;
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
    if (m_nStates < 256)
    {
        SizeOfState = sizeof(unsigned char);
    }
    else if (m_nStates < 65536)
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

    int iMemberState = m_nStates;
    int iNotMemberState = m_nStates+1;

    printf("#define %s_START_STATE (0)\n", UpperPrefix);
    printf("#define %s_ISMEMBER_STATE (%d)\n", UpperPrefix, iMemberState);
    printf("#define %s_ISNOTMEMBER_STATE (%d)\n", UpperPrefix, iNotMemberState);
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
            if (&m_Member == pj)
            {
                k = iMemberState; 
            }
            else if (&m_NotMember == pj)
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
        if (i < m_nStates - 1)
        {
            printf(",");
        }
        printf("\n");
    }
    printf("};\n");
}

void StateMachine::TestString(UTF8 *pStart, UTF8 *pEnd, bool bMember)
{
    State *pState = m_StartingState;
    while (  pStart < pEnd
          && &m_NotMember != pState
          && &m_Member != pState)
    {
        pState = pState->next[(unsigned char)*pStart];
        pStart++;
    }

    if (  (  &m_Member == pState
          && !bMember)
       || (  &m_NotMember == pState
          && bMember))
    {
        fprintf(stderr, "State Transition Table does not work.\n");
        exit(0);
    }
}