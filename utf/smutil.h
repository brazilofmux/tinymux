#ifndef _SMUTIL_H_
#define _SMUTIL_H_

#define UNI_EOF ((UTF32)-1)

char *ReadLine(FILE *fp, char *buffer, size_t bufsize);
void ParseFields(char *buffer, int max_fields, int &nFields, char *aFields[]);
void ParsePoints(char *buffer, int max_points, int &nPoints, const char *aPoints[]);
UTF32 DecodeCodePoint(const char *p);

typedef struct State
{
    int           iState;
    struct State *merged;
    struct State *next[256];
} State;

typedef struct OutputControl
{
    FILE *fpBody;
    FILE *fpInclude;
    char *UpperPrefix;
    char *LowerPrefix;
} OutputControl;

typedef struct OutputStatus
{
    int nStates;
    int nColumns;
    int SizeOfState;
    int SizeOfBlobOffset;
    int SizeOfMachine;
} OutputStatus;

#define NUM_STATES 20000
#define NUM_ACCEPTING_STATES 20000

class StateMachine
{
public:
    StateMachine(void);
    void Init(void);
    void RecordString(UTF8 *pStart, UTF8 *pEnd, int AcceptingState);
    void TestString(UTF8 *pStart, UTF8 *pEnd, int AcceptingState);
    void SetUndefinedStates(int AcceptingState);
    void MergeAcceptingStates(void);
    void RemoveDuplicateRows(void);
    void DetectDuplicateColumns(void);
    void NumberStates(void);
    void OutputTables(OutputControl *poc, OutputStatus *pos);
    void Final(void);
    ~StateMachine();

private:
    State *AllocateState(void);
    void ValidateStatePointer(State *pState, int iLine);
    void FreeState(State *p);
    bool RowsEqual(State *p, State *q);
    bool ColumnsEqual(int iColumn, int jColumn);

    // Special States.
    //
    State  m_Undefined;
    char   m_aAcceptingStates[NUM_ACCEPTING_STATES];
    State *m_StartingState;

    int    m_nStates;
    int    m_nLargestAcceptingState;
    State *m_stt[NUM_STATES];
    UTF8   m_itt[256];
    bool   m_ColumnPresent[256];
    int    m_nColumns;
};

#endif // _SMUTIL_H_
