/*! \file timer.cpp
 * \brief Mini-task scheduler for timed events.
 *
 */

#include "stdafx.h"

CScheduler scheduler;

void init_timer(void)
{
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();
}

#define INITIAL_TASKS 100

CTaskHeap::CTaskHeap(void)
{
    m_nCurrent = 0;
    m_iVisitedMark = 0;
    m_nAllocated = INITIAL_TASKS;
    m_pHeap = new PTASK_RECORD[m_nAllocated];
    if (!m_pHeap)
    {
        m_nAllocated = 0;
    }
}

CTaskHeap::~CTaskHeap(void)
{
    while (m_nCurrent--)
    {
        PTASK_RECORD pTask = m_pHeap[m_nCurrent];
        if (pTask)
        {
            delete pTask;
        }
        m_pHeap[m_nCurrent] = NULL;
    }
    if (m_pHeap)
    {
        delete [] m_pHeap;
    }
}

bool CTaskHeap::Insert(PTASK_RECORD pTask, SCHCMP *pfCompare)
{
    if (m_nCurrent == m_nAllocated)
    {
        if (!Grow())
        {
            return false;
        }
    }
    pTask->m_iVisitedMark = m_iVisitedMark-1;

    m_pHeap[m_nCurrent] = pTask;
    m_nCurrent++;
    SiftUp(m_nCurrent-1, pfCompare);
    return true;
}

int GrowFiftyPercent(int x, int low, int high)
{
    if (x < 0)
    {
        x = 0;
    }

    // Calcuate 150% of x clipping goal at INT_MAX.
    //
    int half = x >> 1;
    int goal;
    if (INT_MAX - half <= x)
    {
        goal = INT_MAX;
    }
    else
    {
        goal = x + half;
    }

    // Clip result between requested boundaries.
    //
    if (goal < low)
    {
        goal = low;
    }
    else if (high < goal)
    {
        goal = high;
    }
    return goal;
}

bool CTaskHeap::Grow(void)
{
    // Grow the heap.
    //
    int n = GrowFiftyPercent(m_nAllocated, INITIAL_TASKS, INT_MAX);
    PTASK_RECORD *p = new (std::nothrow) PTASK_RECORD[n];
    if (NULL == p)
    {
        return false;
    }

    memcpy(p, m_pHeap, sizeof(PTASK_RECORD)*m_nCurrent);
    m_nAllocated = n;
    delete [] m_pHeap;
    m_pHeap = p;

    return true;
}

void CTaskHeap::Shrink(void)
{
    // Shrink the heap.
    //
    int n = m_nAllocated/2;
    if (  n <= INITIAL_TASKS
       || n <= m_nCurrent)
    {
        return;
    }

    PTASK_RECORD *p = new (std::nothrow) PTASK_RECORD[n];
    if (NULL == p)
    {
        return;
    }

    memcpy(p, m_pHeap, sizeof(PTASK_RECORD)*m_nCurrent);
    m_nAllocated = n;
    delete [] m_pHeap;
    m_pHeap = p;
}


PTASK_RECORD CTaskHeap::PeekAtTopmost(void)
{
    if (m_nCurrent <= 0)
    {
        return NULL;
    }
    return m_pHeap[0];
}

PTASK_RECORD CTaskHeap::RemoveTopmost(SCHCMP *pfCompare)
{
    return Remove(0, pfCompare);
}

void CTaskHeap::CancelTask(FTASK *fpTask, void *arg_voidptr, int arg_Integer)
{
    for (int i = 0; i < m_nCurrent; i++)
    {
        PTASK_RECORD p = m_pHeap[i];
        if (  p->fpTask == fpTask
           && p->arg_voidptr == arg_voidptr
           && p->arg_Integer == arg_Integer)
        {
            p->fpTask = NULL;
        }
    }
}

static int ComparePriority(PTASK_RECORD pTaskA, PTASK_RECORD pTaskB)
{
    int i = (pTaskA->iPriority) - (pTaskB->iPriority);
    if (i == 0)
    {
        // Must subtract so that ticket rollover is handled properly.
        //
        return  (pTaskA->m_Ticket) - (pTaskB->m_Ticket);
    }
    return i;
}

static int CompareWhen(PTASK_RECORD pTaskA, PTASK_RECORD pTaskB)
{
    // Can't simply subtract because comparing involves a truncation cast.
    //
    if (pTaskA->ltaWhen < pTaskB->ltaWhen)
    {
        return -1;
    }
    else if (pTaskA->ltaWhen > pTaskB->ltaWhen)
    {
        return 1;
    }
    else
    {
        // Must subtract so that ticket rollover is handled properly.
        //
        return  (pTaskA->m_Ticket) - (pTaskB->m_Ticket);
    }
}

void CScheduler::DeferTask(const CLinearTimeAbsolute& ltaWhen, int iPriority,
                           FTASK *fpTask, void *arg_voidptr, int arg_Integer)
{
    PTASK_RECORD pTask = new TASK_RECORD;
    if (!pTask) return;

    pTask->ltaWhen = ltaWhen;
    pTask->iPriority = iPriority;
    pTask->fpTask = fpTask;
    pTask->arg_voidptr = arg_voidptr;
    pTask->arg_Integer = arg_Integer;
    pTask->m_Ticket = m_Ticket++;

    // Must add to the WhenHeap so that network is still serviced.
    //
    if (!m_WhenHeap.Insert(pTask, CompareWhen))
    {
        delete pTask;
    }
}

void CScheduler::DeferImmediateTask(int iPriority, FTASK *fpTask, void *arg_voidptr, int arg_Integer)
{
    PTASK_RECORD pTask = new TASK_RECORD;
    if (!pTask) return;

    //pTask->ltaWhen = ltaWhen;
    pTask->iPriority = iPriority;
    pTask->fpTask = fpTask;
    pTask->arg_voidptr = arg_voidptr;
    pTask->arg_Integer = arg_Integer;
    pTask->m_Ticket = m_Ticket++;

    // Must add to the WhenHeap so that network is still serviced.
    //
    if (!m_WhenHeap.Insert(pTask, CompareWhen))
    {
        delete pTask;
    }
}

void CScheduler::CancelTask(FTASK *fpTask, void *arg_voidptr, int arg_Integer)
{
    m_WhenHeap.CancelTask(fpTask, arg_voidptr, arg_Integer);
    m_PriorityHeap.CancelTask(fpTask, arg_voidptr, arg_Integer);
}

void CScheduler::ReadyTasks(const CLinearTimeAbsolute& ltaNow)
{
    // Move ready-to-run tasks off the WhenHeap and onto the PriorityHeap.
    //
    PTASK_RECORD pTask = m_WhenHeap.PeekAtTopmost();
    while (  pTask
          && pTask->ltaWhen < ltaNow)
    {
        pTask = m_WhenHeap.RemoveTopmost(CompareWhen);
        if (pTask)
        {
            if (  NULL == pTask->fpTask
               || !m_PriorityHeap.Insert(pTask, ComparePriority))
            {
                delete pTask;
            }
        }
        pTask = m_WhenHeap.PeekAtTopmost();
    }
}

int CScheduler::RunTasks(const CLinearTimeAbsolute& ltaNow)
{
    ReadyTasks(ltaNow);
    return RunAllTasks();
}

int CScheduler::RunTasks(int iCount)
{
    int nTasks = 0;
    while (iCount--)
    {
        PTASK_RECORD pTask = m_PriorityHeap.PeekAtTopmost();
        if (!pTask) break;

        if (pTask->iPriority > m_minPriority)
        {
            // This is related to CF_DEQUEUE and also to untimed (SUSPENDED)
            // semaphore entries that we would like to manage together with
            // the timed ones.
            //
            break;
        }
        pTask = m_PriorityHeap.RemoveTopmost(ComparePriority);
        if (pTask)
        {
            if (pTask->fpTask)
            {
                pTask->fpTask(pTask->arg_voidptr, pTask->arg_Integer);
                nTasks++;
            }
            delete pTask;
        }
    }
    return nTasks;
}

int CScheduler::RunAllTasks(void)
{
    int nTotalTasks = 0;

    int nTasks;
    do
    {
        nTasks = RunTasks(100);
        nTotalTasks += nTasks;
    } while (nTasks);

    return nTotalTasks;
}

bool CScheduler::WhenNext(CLinearTimeAbsolute  *ltaWhen)
{
    // Check the Priority Queue first.
    //
    PTASK_RECORD pTask = m_PriorityHeap.PeekAtTopmost();
    if (pTask)
    {
        if (pTask->iPriority <= m_minPriority)
        {
            ltaWhen->SetSeconds(0);
            return true;
        }
    }

    // Check the When Queue next.
    //
    pTask = m_WhenHeap.PeekAtTopmost();
    if (pTask)
    {
        *ltaWhen = pTask->ltaWhen;
        return true;
    }
    return false;
}

#define HEAP_LEFT_CHILD(x) (2*(x)+1)
#define HEAP_RIGHT_CHILD(x) (2*(x)+2)
#define HEAP_PARENT(x) (((x)-1)/2)

void CTaskHeap::SiftDown(int iSubRoot, SCHCMP *pfCompare)
{
    int parent = iSubRoot;
    int child = HEAP_LEFT_CHILD(parent);

    PTASK_RECORD Ref = m_pHeap[parent];

    while (child < m_nCurrent)
    {
        int rightchild = HEAP_RIGHT_CHILD(parent);
        if (rightchild < m_nCurrent)
        {
            if (pfCompare(m_pHeap[rightchild], m_pHeap[child]) < 0)
            {
                child = rightchild;
            }
        }
        if (pfCompare(Ref, m_pHeap[child]) <= 0)
            break;

        m_pHeap[parent] = m_pHeap[child];
        parent = child;
        child = HEAP_LEFT_CHILD(parent);
    }
    m_pHeap[parent] = Ref;
}

void CTaskHeap::SiftUp(int child, SCHCMP *pfCompare)
{
    while (child)
    {
        int parent = HEAP_PARENT(child);
        if (pfCompare(m_pHeap[parent], m_pHeap[child]) <= 0)
            break;

        PTASK_RECORD Tmp;
        Tmp = m_pHeap[child];
        m_pHeap[child] = m_pHeap[parent];
        m_pHeap[parent] = Tmp;

        child = parent;
    }
}

PTASK_RECORD CTaskHeap::Remove(int iNode, SCHCMP *pfCompare)
{
    if (iNode < 0 || m_nCurrent <= iNode) return NULL;

    PTASK_RECORD pTask = m_pHeap[iNode];

    m_nCurrent--;
    m_pHeap[iNode] = m_pHeap[m_nCurrent];
    SiftDown(iNode, pfCompare);
    SiftUp(iNode, pfCompare);

    return pTask;
}

void CTaskHeap::Update(int iNode, SCHCMP *pfCompare)
{
    if (iNode < 0 || m_nCurrent <= iNode)
    {
        return;
    }

    SiftDown(iNode, pfCompare);
    SiftUp(iNode, pfCompare);
}

void CScheduler::TraverseUnordered(SCHLOOK *pfLook)
{
    if (m_WhenHeap.TraverseUnordered(pfLook, CompareWhen))
    {
        m_PriorityHeap.TraverseUnordered(pfLook, ComparePriority);
    }
}

void CScheduler::TraverseOrdered(SCHLOOK *pfLook)
{
    m_PriorityHeap.TraverseOrdered(pfLook, ComparePriority);
    m_WhenHeap.TraverseOrdered(pfLook, CompareWhen);
}

// The following guarantees that in spite of any changes to the heap
// we will visit every record exactly once. It does not attempt to
// visit these records in any particular order.
//
int CTaskHeap::TraverseUnordered(SCHLOOK *pfLook, SCHCMP *pfCompare)
{
    // Indicate that everything has not been visited.
    //
    m_iVisitedMark++;
    if (m_iVisitedMark == 0)
    {
        // We rolled over. Yeah, this code will probably never run, but
        // let's go ahead and mark all the records as visited anyway.
        //
        for (int i = 0; i < m_nCurrent; i++)
        {
            PTASK_RECORD p = m_pHeap[i];
            p->m_iVisitedMark = m_iVisitedMark;
        }
        m_iVisitedMark++;
    }

    int bUnvisitedRecords;
    do
    {
        bUnvisitedRecords = false;
        for (int i = 0; i < m_nCurrent; i++)
        {
            PTASK_RECORD p = m_pHeap[i];

            if (p->m_iVisitedMark == m_iVisitedMark)
            {
                // We have already seen this one.
                //
                continue;
            }
            bUnvisitedRecords = true;
            p->m_iVisitedMark = m_iVisitedMark;

            int cmd = pfLook(p);
            switch (cmd)
            {
            case IU_REMOVE_TASK:
                Remove(i, pfCompare);
                break;

            case IU_DONE:
                return false;

            case IU_UPDATE_TASK:
                Update(i, pfCompare);
                break;
            }
        }
    } while (bUnvisitedRecords);
    return true;
}

// The following does not allow for changes during the traversal, but
// but it does visit each record in sorted order (When-order or
// Priority-order depending on the heap).
//
int CTaskHeap::TraverseOrdered(SCHLOOK *pfLook, SCHCMP *pfCompare)
{
    Sort(pfCompare);

    for (int i = m_nCurrent-1; i >= 0; i--)
    {
        PTASK_RECORD p = m_pHeap[i];
        int cmd = pfLook(p);
        if (IU_DONE == cmd)
        {
            break;
        }
    }

    Remake(pfCompare);
    return true;
}

void CTaskHeap::Sort(SCHCMP *pfCompare)
{
    int s_nCurrent = m_nCurrent;
    while (m_nCurrent--)
    {
        PTASK_RECORD p = m_pHeap[m_nCurrent];
        m_pHeap[m_nCurrent] = m_pHeap[0];
        m_pHeap[0] = p;
        SiftDown(0, pfCompare);
    }
    m_nCurrent = s_nCurrent;
}

void CTaskHeap::Remake(SCHCMP *pfCompare)
{
    int s_nCurrent = m_nCurrent;
    m_nCurrent = 0;
    while (s_nCurrent--)
    {
        m_nCurrent++;
        SiftUp(m_nCurrent-1, pfCompare);
    }
}

void CScheduler::SetMinPriority(int arg_minPriority)
{
    m_minPriority = arg_minPriority;
}

void CScheduler::Shrink(void)
{
    m_WhenHeap.Shrink();
    m_PriorityHeap.Shrink();
}
