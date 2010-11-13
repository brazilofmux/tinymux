#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <ctype.h>
#include <string.h>

#include "ConvertUTF.h"
#include "smutil.h"

char *ReadLine(FILE *fp, char *buffer, size_t bufsize)
{
    for (;;)
    {
        if (NULL == fgets(buffer, bufsize, fp))
        {
            return NULL;
        }
        char *p = strchr(buffer, '#');
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
        if ('\0' != *p)
        {
            return p;
        }
    }
}

void ParseFields(char *buffer, int max_fields, int &nFields, char *aFields[])
{
    nFields = 0;
    char *p = buffer;
    while (  '\0' != p[0]
          && nFields < max_fields)
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
}

void ParsePoints(char *buffer, int max_points, int &nPoints, const char *aPoints[])
{
    nPoints = 0;

    char *p = buffer;
    while (  '\0' != p[0]
          && nPoints < max_points)
    {
        // Skip leading whitespace.
        //
        while (isspace(*p))
        {
            p++;
        }

        aPoints[nPoints++] = p;
        char *q = strchr(p, ' ');
        if (NULL == q)
        {
            break;
        }
        else
        {
            *q = '\0';
            p = q + 1;
        }
    }
}

UTF32 DecodeCodePoint(const char *p)
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

State *StateMachine::AllocateState(void)
{
    State *p = new State;
    int i;
    for (i = 0; i < 256; i++)
    {
        p->next[i] = &m_Undefined;
        ValidateStatePointer(p->next[i], __LINE__);
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
    if (  AcceptingState < 0
       || NUM_ACCEPTING_STATES < AcceptingState)
    {
        fprintf(stderr, "Accepting state exceeds supported range.\n");
        exit(0);
    }

    if (m_nLargestAcceptingState < AcceptingState)
    {
        m_nLargestAcceptingState = AcceptingState;
    }

    State *pState = m_StartingState;
    while (pStart < pEnd-1)
    {
        UTF8 ch = *pStart;
        ValidateStatePointer(pState, __LINE__);
        ValidateStatePointer(pState->next[ch], __LINE__);
        if (&m_Undefined == pState->next[ch])
        {
            State *p = AllocateState();
            m_stt[m_nStates++] = p;
            if (NUM_STATES <= m_nStates)
            {
                fprintf(stderr, "Limit of %d states exceeded.\n", NUM_STATES);
                exit(0);
            }
            pState->next[ch] = p;
            pState = p;
        }
        else if (  (State *)(m_aAcceptingStates) <= pState->next[ch]
                && pState->next[ch] < (State *)(m_aAcceptingStates + sizeof(m_aAcceptingStates)))
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
        ValidateStatePointer(pState, __LINE__);
        ValidateStatePointer(pState->next[ch], __LINE__);
        if (&m_Undefined == pState->next[ch])
        {
            pState->next[ch] = (State *)(m_aAcceptingStates + AcceptingState);
            ValidateStatePointer(pState->next[ch], __LINE__);
        }
        else if (  (State *)(m_aAcceptingStates) <= pState->next[ch]
                && pState->next[ch] < (State *)(m_aAcceptingStates + sizeof(m_aAcceptingStates)))
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

bool StateMachine::RowsEqual(State *p, State *q)
{
    ValidateStatePointer(p, __LINE__);
    ValidateStatePointer(q, __LINE__);

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
        ValidateStatePointer(p->next[i], __LINE__);
        ValidateStatePointer(q->next[i], __LINE__);
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
        ValidateStatePointer(p, __LINE__);
        ValidateStatePointer(p->next[iColumn], __LINE__);
        ValidateStatePointer(p->next[jColumn], __LINE__);
        if (  &m_Undefined == p->next[iColumn]
           || &m_Undefined == p->next[jColumn])
        {
            // We interpret undefined transitions as 'Do not care'.
            //
            continue;
        }
        else
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
        ValidateStatePointer(pi, __LINE__);
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
            ValidateStatePointer(pi->next[k], __LINE__);
            if (&m_Undefined == pi->next[k])
            {
                // Undefined State will match everything.
                //
                continue;
            }
            else if (  pi->next[k] < (State *)(m_aAcceptingStates)
                    || (State *)(m_aAcceptingStates + sizeof(m_aAcceptingStates)) <= pi->next[k])
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
            ValidateStatePointer(pLastState, __LINE__);
        }

        if (bMatched)
        {
            // Prune (i)th row so as to arrive at the accepting state one transition earlier.
            //
            pi->merged = pLastState;
            ValidateStatePointer(pi->merged, __LINE__);
        }
    }

    // Update all pointers to refer to merged state.
    //
    for (i = 0; i < m_nStates; i++)
    {
        State *pi = m_stt[i];
        ValidateStatePointer(pi, __LINE__);
        if (NULL == pi->merged)
        {
            int j;
            for (j = 0; j < 256; j++)
            {
                State *pj = pi->next[j];
                ValidateStatePointer(pj, __LINE__);
                if (NULL != pj->merged)
                {
                    ValidateStatePointer(pj->merged, __LINE__);
                    pi->next[j] = pj->merged;
                    ValidateStatePointer(pi->next[j], __LINE__);
                }
            }
        }
    }

    // Free duplicate states and shrink state table accordingly.
    //
    for (i = 0; i < m_nStates;)
    {
        State *pi = m_stt[i];
        ValidateStatePointer(pi, __LINE__);
        if (NULL == pi->merged)
        {
            i++;
        }
        else
        {
            ValidateStatePointer(pi->merged, __LINE__);
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

    OutputStatus os;
    OutputTables(NULL, &os);
    fprintf(stderr, "%d states, %d columns, %d bytes\n", os.nStates, os.nColumns, os.SizeOfMachine);
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
        ValidateStatePointer(pi, __LINE__);
        if (NULL == pi->merged)
        {
            for (j = i+1; j < m_nStates; j++)
            {
                State *pj = m_stt[j];
                ValidateStatePointer(pj, __LINE__);
                if (NULL == pj->merged)
                {
                    if (RowsEqual(pi, pj))
                    {
                        // Merge (j)th row into (i)th row.
                        //
                        pj->merged = pi;
                        ValidateStatePointer(pj, __LINE__);

                        // Let (j)th row defined transitions override (i)th
                        // row undefined transitions.
                        //
                        int u;
                        for (u = 0; u < 256; u++)
                        {
                            ValidateStatePointer(pi->next[u], __LINE__);
                            if (&m_Undefined == pi->next[u])
                            {
                                pi->next[u] = pj->next[u];
                                ValidateStatePointer(pi->next[u], __LINE__);
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
        ValidateStatePointer(pi, __LINE__);
        if (NULL == pi->merged)
        {
            for (j = 0; j < 256; j++)
            {
                State *pj = pi->next[j];
                ValidateStatePointer(pj, __LINE__);
                if (NULL != pj->merged)
                {
                    ValidateStatePointer(pj->merged, __LINE__);
                    pi->next[j] = pj->merged;
                    ValidateStatePointer(pi->next[j], __LINE__);
                }
            }
        }
    }

    // Free duplicate states and shrink state table accordingly.
    //
    for (i = 0; i < m_nStates;)
    {
        State *pi = m_stt[i];
        ValidateStatePointer(pi, __LINE__);
        if (NULL == pi->merged)
        {
            i++;
        }
        else
        {
            ValidateStatePointer(pi->merged, __LINE__);
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

    OutputStatus os;
    OutputTables(NULL, &os);
    fprintf(stderr, "%d states, %d columns, %d bytes\n", os.nStates, os.nColumns, os.SizeOfMachine);
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
                    ValidateStatePointer(m_stt[u]->next[i], __LINE__);
                    if (&m_Undefined == m_stt[u]->next[i])
                    {
                        ValidateStatePointer(m_stt[u]->next[j], __LINE__);
                        m_stt[u]->next[i] = m_stt[u]->next[j];
                        ValidateStatePointer(m_stt[u]->next[i], __LINE__);
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

    OutputStatus os;
    OutputTables(NULL, &os);
    fprintf(stderr, "%d states, %d columns, %d bytes\n", os.nStates, os.nColumns, os.SizeOfMachine);
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
            ValidateStatePointer(m_stt[i]->next[j], __LINE__);
            if (&m_Undefined == m_stt[i]->next[j])
            {
                m_stt[i]->next[j] = (State *)(m_aAcceptingStates + AcceptingState);
                ValidateStatePointer(m_stt[i]->next[j], __LINE__);
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

void StateMachine::ValidateStatePointer(State *pState, int iLine)
{
#if 0
    char *p = reinterpret_cast<char *>(pState);
    if (  m_aAcceptingStates <= p
       && p < m_aAcceptingStates + sizeof(m_aAcceptingStates))
    {
        return;
    }
    else if (&m_Undefined == pState)
    {
        return;
    }
    else
    {
        int i;
        for (i = 0; i < m_nStates; i++)
        {
            if (m_stt[i] == pState)
            {
                return;
            }
        }
    }

    fprintf(stderr, "Invalid state pointer. This should not happen. Line %d\n", iLine);
    exit(0);
#endif
}

void StateMachine::OutputTables(OutputControl *poc, OutputStatus *pos)
{
    OutputStatus os;
    os.nStates = m_nStates;
    os.nColumns = m_nColumns;

    // The internal states start with 0. The accepting states (numbering
    // m_nLargestAcceptingState) follow immediately. Finally, we allocate a
    // single error state.
    //
    int iAcceptingStatesStart = m_nStates;
    int TotalStates = iAcceptingStatesStart + m_nLargestAcceptingState + 1;
    if (TotalStates < 256)
    {
        os.SizeOfState = sizeof(unsigned char);
    }
    else if (TotalStates < 65536)
    {
        os.SizeOfState = sizeof(unsigned short);
    }
    else
    {
        os.SizeOfState = sizeof(unsigned int);
    }

    // Mapping from state number to position in blob which contains
    // run-length-compressed row.
    //
    int *piCopy = NULL;
    int *piBlobOffsets = NULL;;
    int *piBlob = NULL;

    int nBlob = 0;
    piCopy = new int[m_nColumns];
    piBlobOffsets = new int[m_nStates];
    piBlob = new int[2*m_nStates*m_nColumns];
    if (  NULL == piCopy
       || NULL == piBlobOffsets
       || NULL == piBlob)
    {
        fprintf(stderr, "CleanUp, line %d\n", __LINE__);
        goto CleanUp;
    }

    int i, j, k, t;
    for (i = 0; i < m_nStates; i++)
    {
        int kLastValue = 0;
        int kRunCount = 0;
        int kCopyCount = 0;

        piBlobOffsets[i] = nBlob;

        State *pi = m_stt[i];

        for (j = 0; j < 256; j++)
        {
            if (!m_ColumnPresent[j])
            {
                continue;
            }

            State *pj = pi->next[j];
            ValidateStatePointer(pj, __LINE__);

            char *p = reinterpret_cast<char *>(pj);
            if (  m_aAcceptingStates <= p
               && p < m_aAcceptingStates + sizeof(m_aAcceptingStates))
            {
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

            if (0 == j)
            {
                piCopy[0] = k;
                kCopyCount = 1;
                kRunCount = 0;
                kLastValue = k;
            }
            else if (kLastValue == k)
            {
                if (0 < kRunCount)
                {
                    if (kRunCount < 127)
                    {
                        kRunCount++;
                    }
                    else
                    {
                        // Emit leftover RUN phrase.
                        //
                        if (2 * m_nStates * m_nColumns <= nBlob)
                        {
                            fprintf(stderr, "CleanUp, line %d\n", __LINE__);
                            goto CleanUp;
                        }
                        piBlob[nBlob++] = kRunCount;
                        if (2 * m_nStates * m_nColumns <= nBlob)
                        {
                            fprintf(stderr, "CleanUp, line %d\n", __LINE__);
                            goto CleanUp;
                        }
                        piBlob[nBlob++] = kLastValue;

                        piCopy[0] = k;
                        kCopyCount = 1;
                        kRunCount = 0;
                        kLastValue = k;
                    }
                }
                else if (1 == kCopyCount)
                {
                    kCopyCount = 0;
                    kRunCount = 2;
                }
                else // if (1 < kCopyCount)
                {
                    // Emit leftover COPY phrase.
                    //
                    if (2 * m_nStates * m_nColumns <= nBlob)
                    {
                        fprintf(stderr, "CleanUp, line %d\n", __LINE__);
                        goto CleanUp;
                    }
                    piBlob[nBlob++] = -(kCopyCount-1);
                    for (t = 0; t < kCopyCount - 1; t++)
                    {
                        if (2 * m_nStates * m_nColumns <= nBlob)
                        {
                            fprintf(stderr, "CleanUp, line %d\n", __LINE__);
                            goto CleanUp;
                        }
                        piBlob[nBlob++] = piCopy[t];
                    }

                    kCopyCount = 0;
                    kRunCount = 2;
                    kLastValue = k;
                }
            }
            else // if (kLastValue != k)
            {
                if (0 < kCopyCount)
                {
                    if (m_nColumns <= kCopyCount)
                    {
                        fprintf(stderr, "CleanUp, line %d\n", __LINE__);
                        goto CleanUp;
                    }
                    piCopy[kCopyCount++] = k;
                    kLastValue = k;
                    if (127 < kCopyCount)
                    {
                        fprintf(stderr, "CleanUp, line %d\n", __LINE__);
                        goto CleanUp;
                    }
                }
                else if (1 == kRunCount)
                {
                    piCopy[0] = kLastValue;
                    piCopy[1] = k;
                    kCopyCount = 2;
                    kLastValue = k;
                }
                else // if (1 < kRunCount)
                {
                    // Emit RUN phrase.
                    //
                    if (2 * m_nStates * m_nColumns <= nBlob)
                    {
                        fprintf(stderr, "CleanUp, line %d\n", __LINE__);
                        goto CleanUp;
                    }
                    piBlob[nBlob++] = kRunCount;
                    if (2 * m_nStates * m_nColumns <= nBlob)
                    {
                        fprintf(stderr, "CleanUp, line %d\n", __LINE__);
                        goto CleanUp;
                    }
                    piBlob[nBlob++] = kLastValue;

                    piCopy[0] = k;
                    kCopyCount = 1;
                    kRunCount = 0;
                    kLastValue = k;
                }
            }
        }

        if (0 < kRunCount)
        {
            // Emit leftover RUN phrase.
            //
            if (2 * m_nStates * m_nColumns <= nBlob)
            {
                fprintf(stderr, "CleanUp, line %d\n", __LINE__);
                goto CleanUp;
            }
            piBlob[nBlob++] = kRunCount;
            if (2 * m_nStates * m_nColumns <= nBlob)
            {
                fprintf(stderr, "CleanUp, line %d\n", __LINE__);
                goto CleanUp;
            }
            piBlob[nBlob++] = kLastValue;
        }
        else if (0 < kCopyCount)
        {
            // Emit leftover COPY phrase.
            //
            if (2 * m_nStates * m_nColumns <= nBlob)
            {
                fprintf(stderr, "CleanUp, line %d\n", __LINE__);
                goto CleanUp;
            }
            piBlob[nBlob++] = -kCopyCount;
            for (t = 0; t < kCopyCount; t++)
            {
                if (2 * m_nStates * m_nColumns <= nBlob)
                {
                    fprintf(stderr, "CleanUp, line %d\n", __LINE__);
                    goto CleanUp;
                }
                piBlob[nBlob++] = piCopy[t];
            }
        }
    }

    if (nBlob < 256)
    {
        os.SizeOfBlobOffset = sizeof(unsigned char);
    }
    else if (nBlob < 65536)
    {
        os.SizeOfBlobOffset = sizeof(unsigned short);
    }
    else
    {
        os.SizeOfBlobOffset = sizeof(unsigned int);
    }

    // The input translation table, which maps bytes to columns, always
    // occupies 256 bytes.  The state machine is a two-dimensional compressed
    // table (a blob and a map of state numbers to positions within this blob).
    //
    os.SizeOfMachine = m_nStates * os.SizeOfBlobOffset + nBlob * os.SizeOfState + 256;

    // Test compressed state table.
    //
    for (i = 0; i < m_nStates; i++)
    {
        State *pi = m_stt[i];

        int iColumn = 0;
        for (j = 0; j < 256; j++)
        {
            if (!m_ColumnPresent[j])
            {
                continue;
            }

            State *pj = pi->next[j];
            ValidateStatePointer(pj, __LINE__);

            char *p = reinterpret_cast<char *>(pj);
            if (  m_aAcceptingStates <= p
               && p < m_aAcceptingStates + sizeof(m_aAcceptingStates))
            {
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

            // Validate that position (i,iColumn) gives k.
            //
            int Offset = piBlobOffsets[i];
            int t = iColumn;
            int result;
            for (;;)
            {
                int y = piBlob[Offset];
                if (0 < y)
                {
                    // RUN phrase.
                    //
                    if (t < y)
                    {
                        result = piBlob[Offset+1];
                        break;
                    }
                    else
                    {
                        t -= y;
                        Offset += 2;
                    }
                }
                else
                {
                    // COPY phrase.
                    //
                    y = -y;
                    if (t < y)
                    {
                        result = piBlob[Offset+t+1];
                        break;
                    }
                    else
                    {
                        t -= y;
                        Offset += y + 1;
                    }
                }
            }

            if (result != k)
            {
                fprintf(stderr, "Compressed state machine invalid. CleanUp, line %d\n", __LINE__);
                fprintf(stderr, "(%d,%d)-->%d instead of %d\n", i, iColumn, result, k);
            }

            iColumn++;
        }
    }

    if (NULL != poc)
    {
        fprintf(poc->fpInclude, "// %d states, %d columns, %d bytes\n//\n", m_nStates, m_nColumns, os.SizeOfMachine);
        fprintf(poc->fpBody, "// %d states, %d columns, %d bytes\n//\n", m_nStates, m_nColumns, os.SizeOfMachine);

        fprintf(poc->fpInclude, "#define %s_START_STATE (0)\n", poc->UpperPrefix);
        fprintf(poc->fpInclude, "#define %s_ACCEPTING_STATES_START (%d)\n", poc->UpperPrefix, iAcceptingStatesStart);

        fprintf(poc->fpInclude, "extern const unsigned char %s_itt[256];\n", poc->LowerPrefix);
        fprintf(poc->fpBody, "const unsigned char %s_itt[256] =\n", poc->LowerPrefix);
        fprintf(poc->fpBody, "{\n");
        for (i = 0; i < 256; i++)
        {
            j = i % 16;
            if (0 == j)
            {
                fprintf(poc->fpBody, "    ");
            }

            fprintf(poc->fpBody, " %3d", m_itt[i]);
            if (i < 256-1)
            {
                fprintf(poc->fpBody, ",");
            }

            if (7 == j)
            {
                fprintf(poc->fpBody, " ");
            }

            if (15 == j)
            {
                fprintf(poc->fpBody, "\n");
            }

            if (127 == i)
            {
                fprintf(poc->fpBody, "\n");
            }
        }
        fprintf(poc->fpBody, "\n};\n\n");

        switch (os.SizeOfBlobOffset)
        {
        case 1:
            fprintf(poc->fpInclude, "extern const unsigned char %s_sot[%d];\n", poc->LowerPrefix, m_nStates);
            fprintf(poc->fpBody, "const unsigned char %s_sot[%d] =\n", poc->LowerPrefix, m_nStates);
            break;

        case 2:
            fprintf(poc->fpInclude, "extern const unsigned short %s_sot[%d];\n", poc->LowerPrefix, m_nStates);
            fprintf(poc->fpBody, "const unsigned short %s_sot[%d] =\n", poc->LowerPrefix, m_nStates);
            break;

        default:
            fprintf(poc->fpInclude, "extern const unsigned long %s_sot[%d];\n", poc->LowerPrefix, m_nStates);
            fprintf(poc->fpBody, "const unsigned long %s_sot[%d] =\n", poc->LowerPrefix, m_nStates);
            break;
        }
        fprintf(poc->fpBody, "{\n");
        for (i = 0; i < m_nStates; i++)
        {
            j = i % 16;
            if (0 == j)
            {
                fprintf(poc->fpBody, "    ");
            }

            fprintf(poc->fpBody, " %4d", piBlobOffsets[i]);
            if (i < m_nStates-1)
            {
                fprintf(poc->fpBody, ",");
            }

            if (7 == j)
            {
                fprintf(poc->fpBody, " ");
            }

            if (15 == j)
            {
                fprintf(poc->fpBody, "\n");
            }
        }
        fprintf(poc->fpBody, "\n};\n\n");

        switch (os.SizeOfState)
        {
        case 1:
            fprintf(poc->fpInclude, "extern const unsigned char %s_sbt[%d];\n", poc->LowerPrefix, nBlob);
            fprintf(poc->fpBody, "const unsigned char %s_sbt[%d] =\n", poc->LowerPrefix, nBlob);
            break;

        case 2:
            fprintf(poc->fpInclude, "extern const unsigned short %s_sbt[%d];\n", poc->LowerPrefix, nBlob);
            fprintf(poc->fpBody, "const unsigned short %s_sbt[%d] =\n", poc->LowerPrefix, nBlob);
            break;

        default:
            fprintf(poc->fpInclude, "extern const unsigned long %s_sbt[%d];\n", poc->LowerPrefix, nBlob);
            fprintf(poc->fpBody, "const unsigned long %s_sbt[%d] =\n", poc->LowerPrefix, nBlob);
            break;
        }
        fprintf(poc->fpBody, "{\n");
        for (i = 0; i < nBlob; i++)
        {
            j = i % 16;
            if (0 == j)
            {
                fprintf(poc->fpBody, "    ");
            }

            int iBlob = piBlob[i];
            if (0 <= iBlob)
            {
                fprintf(poc->fpBody, " %3d", iBlob);
            }
            else if (-127 <= iBlob)
            {
                // Negative entries are COPY phrases.
                //
                fprintf(poc->fpBody, " %3d", 256+iBlob);
            }
            else
            {
                // Negative entries should not be larger than -127.
                //
                fprintf(stderr, "CleanUp, line %d\n", __LINE__);
                goto CleanUp;
            }

            if (i < nBlob-1)
            {
                fprintf(poc->fpBody, ",");
            }

            if (7 == j)
            {
                fprintf(poc->fpBody, " ");
            }

            if (15 == j)
            {
                fprintf(poc->fpBody, "\n");
            }
        }
        fprintf(poc->fpBody, "\n};\n");
    }

CleanUp:
    delete piCopy;
    delete piBlobOffsets;
    delete piBlob;

    if (NULL != pos)
    {
        *pos = os;
    }
}

void StateMachine::TestString(UTF8 *pStart, UTF8 *pEnd, int AcceptingState)
{
    State *pState = m_StartingState;
    while (  pStart < pEnd
          && &m_Undefined != pState
          && (  pState < (State *)(m_aAcceptingStates)
             || (State *)(m_aAcceptingStates + sizeof(m_aAcceptingStates)) <= pState))
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
