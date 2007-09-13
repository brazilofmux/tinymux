/*! \file stubslave.cpp
 * \brief This slave hosts modules in a separate process.
 *
 * $Id$
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"

#if defined(HAVE_DLOPEN) || defined(WIN32)

#include "libmux.h"
#include "modules.h"

#define QUEUE_BLOCK_SIZE 32768

typedef struct QueueBlock
{
    struct QueueBlock *pNext;
    struct QueueBlock *pPrev;
    char  *pBuffer;
    size_t nBuffer;
    char   aBuffer[QUEUE_BLOCK_SIZE];
} QUEUE_BLOCK;

typedef struct
{
    QUEUE_BLOCK *pHead;
    QUEUE_BLOCK *pTail;
} QUEUE_INFO;

QUEUE_INFO   *pQueue_In = NULL;
QUEUE_INFO   *pQueue_Out = NULL;

CHANNEL_INFO *aChannels = NULL;
int           nChannels = 0;
int           nChannelsAllocated = 0;

#define CHANNELS_FIRST 100

void Pipe_InitializeChannelZero(FCALL *pfCall0, FMSG *pfMsg0, FDISC *pfDisc0)
{
    try
    {
        aChannels = new CHANNEL_INFO[CHANNELS_FIRST];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL != aChannels)
    {
        nChannelsAllocated      = CHANNELS_FIRST;
        aChannels[0].nChannel   = 0;
        aChannels[0].pfCall     = pfCall0;
        aChannels[0].pfMsg      = pfMsg0;
        aChannels[0].pfDisc     = pfDisc0;
        aChannels[0].pInterface = NULL;

        nChannels = 1;
    }
}

MUX_RESULT Channel0_Call(CHANNEL_INFO *pci, size_t nBuffer, void *pBuffer)
{
    return MUX_E_NOTIMPLEMENTED;
}

void Pipe_AppendToQueue(QUEUE_INFO *pqi, size_t n, const char *p)
{
    if (  0 != n
       || NULL != p)
    {
        // Continue copying data to the end of the queue until it is all consumed.
        //
        QUEUE_BLOCK *pBlock = NULL;
        while (0 < n)
        {
            // We need an empty or partially filled QUEUE_BLOCK.
            //
            if (  NULL == pqi->pTail
               || pqi->pTail->aBuffer + QUEUE_BLOCK_SIZE <= pqi->pTail->pBuffer + pqi->pTail->nBuffer)
            {
                // The last block is full or not there, so allocate a new QUEUE_BLOCK.
                //
                try
                {
                    pBlock = new QUEUE_BLOCK;
                }
                catch (...)
                {
                    ; // Nothing.
                }

                if (NULL != pBlock)
                {
                    pBlock->pNext   = NULL;
                    pBlock->pPrev   = NULL;
                    pBlock->pBuffer = pBlock->aBuffer;
                    pBlock->nBuffer = 0;
                }

                // Append the newly allocated block to the end of the queue.
                //
                if (NULL == pqi->pTail)
                {
                    pqi->pHead = pBlock;
                    pqi->pTail = pBlock;
                }
                else
                {
                    pBlock->pPrev = pqi->pTail;
                    pqi->pTail->pNext = pBlock;
                    pqi->pTail = pBlock;
                }
            }
            else
            {
                pBlock = pqi->pTail;
            }
        }

        // Allocate space out of last QUEUE_BLOCK
        //
        char  *pFree = pBlock->pBuffer + pBlock->nBuffer;
        size_t nFree = QUEUE_BLOCK_SIZE - pBlock->nBuffer - (pBlock->pBuffer - pBlock->aBuffer);
        size_t nCopy = nFree;
        if (n < nCopy)
        {
            nCopy = n;
        }

        memcpy(pFree, p, nCopy);
        n -= nCopy;
    }
}

void Pipe_ParseQueue(QUEUE_INFO *pqi)
{
}

#define MAX_STRING 1024

void Stub_ShoveChars(int fdServer)
{
    for (;;)
    {
        char arg[MAX_STRING];
        int len = read(fdServer, arg, sizeof(arg));
        if (len == 0)
        {
            break;
        }

        if (len < 0)
        {
            if (EINTR == errno)
            {
                errno = 0;
                continue;
            }
            break;
        }

        Pipe_AppendToQueue(pQueue_In, len, arg);
        Pipe_ParseQueue(pQueue_In);
    }
}

int main(int argc, char *argv[])
{
    mux_InitModuleLibrary(IsSlaveProcess, NULL);
    Pipe_InitializeChannelZero(Channel0_Call, NULL, NULL);
    Stub_ShoveChars(0);
    mux_FinalizeModuleLibrary();
    return 0;
}

#endif
