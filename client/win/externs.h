/*! \file externs.h
 * \brief Prototypes for externs not defined elsewhere.
 *
 */

#ifndef EXTERNS_H
#define EXTERNS_H

#define LBUF_SIZE 8000

// From timer.cpp
//
void init_timer(void);

// Using a heap as the data structure for representing this priority
// has some attributes which we depend on:
//
// 1. Most importantly, actions scheduled for the same time (i.e.,
//    immediately) keep the order that they were inserted into the
//    heap.
//
// If you ever re-implement this object using another data structure,
// please remember to maintain this property.
//
typedef void FTASK(void *, int);

typedef struct
{
    CLinearTimeAbsolute ltaWhen;

    int        iPriority;
    int        m_Ticket;        // This is the order in which the task was scheduled.
    FTASK      *fpTask;
    void       *arg_voidptr;
    int        arg_Integer;
    int        m_iVisitedMark;
} TASK_RECORD, *PTASK_RECORD;

#define PRIORITY_SYSTEM  100
#define PRIORITY_PLAYER  200
#define PRIORITY_OBJECT  300
#define PRIORITY_SUSPEND 400

// CF_DEQUEUE driven minimum priority levels.
//
#define PRIORITY_CF_DEQUEUE_ENABLED  PRIORITY_OBJECT
#define PRIORITY_CF_DEQUEUE_DISABLED (PRIORITY_PLAYER-1)

typedef int SCHCMP(PTASK_RECORD, PTASK_RECORD);
typedef int SCHLOOK(PTASK_RECORD);

class CTaskHeap
{
private:
    int m_nAllocated;
    int m_nCurrent;
    PTASK_RECORD *m_pHeap;

    int m_iVisitedMark;

    bool Grow(void);
    void SiftDown(int, SCHCMP *);
    void SiftUp(int, SCHCMP *);
    PTASK_RECORD Remove(int, SCHCMP *);
    void Update(int iNode, SCHCMP *pfCompare);
    void Sort(SCHCMP *pfCompare);
    void Remake(SCHCMP *pfCompare);

public:
    CTaskHeap();
    ~CTaskHeap();

    void Shrink(void);
    bool Insert(PTASK_RECORD, SCHCMP *);
    PTASK_RECORD PeekAtTopmost(void);
    PTASK_RECORD RemoveTopmost(SCHCMP *);
    void CancelTask(FTASK *fpTask, void *arg_voidptr, int arg_Integer);

#define IU_DONE        0
#define IU_NEXT_TASK   1
#define IU_REMOVE_TASK 2
#define IU_UPDATE_TASK 3
    int TraverseUnordered(SCHLOOK *pfLook, SCHCMP *pfCompare);
    int TraverseOrdered(SCHLOOK *pfLook, SCHCMP *pfCompare);
};

class CScheduler
{
private:
    CTaskHeap m_WhenHeap;
    CTaskHeap m_PriorityHeap;
    int       m_Ticket;
    int       m_minPriority;

public:
    void TraverseUnordered(SCHLOOK *pfLook);
    void TraverseOrdered(SCHLOOK *pfLook);
    CScheduler(void) { m_Ticket = 0; m_minPriority = PRIORITY_CF_DEQUEUE_ENABLED; }
    void DeferTask(const CLinearTimeAbsolute& ltWhen, int iPriority, FTASK *fpTask, void *arg_voidptr, int arg_Integer);
    void DeferImmediateTask(int iPriority, FTASK *fpTask, void *arg_voidptr, int arg_Integer);
    bool WhenNext(CLinearTimeAbsolute *);
    int  RunTasks(int iCount);
    int  RunAllTasks(void);
    int  RunTasks(const CLinearTimeAbsolute& tNow);
    void ReadyTasks(const CLinearTimeAbsolute& tNow);
    void CancelTask(FTASK *fpTask, void *arg_voidptr, int arg_Integer);
    void Shrink(void);

    void SetMinPriority(int arg_minPriority);
    int  GetMinPriority(void) { return m_minPriority; }
};

extern CScheduler scheduler;

#endif // EXTERNS_H
