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

QUEUE_INFO    Queue_In;
QUEUE_INFO    Queue_Out;
QUEUE_INFO    Queue_Frame;

// Decoder
//
int           iState = 0;
size_t        nLength = 0;
UINT32        nChannel = 0;
size_t        nLengthRemaining = 0;

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

void Pipe_InitializeQueueInfo(QUEUE_INFO *pqi)
{
    pqi->pHead = NULL;
    pqi->pTail = NULL;
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

void Pipe_FreeQueue(QUEUE_INFO *pqi)
{
    if (NULL != pqi)
    {
        QUEUE_BLOCK *pBlock = pqi->pHead;

        // Free all the QUEUE_BLOCKs finally the owning QUEUE_INFO structure.
        //
        while (NULL != pBlock)
        {
            QUEUE_BLOCK *qBlock = pBlock->pNext;
            delete pBlock;
            pBlock = qBlock;
        }
        delete pqi;
    }
}

// CallMagic   0xC39B71F9 - 17, 14,  9, 20
// ReturnMagic 0x35972DD0 -  7, 13,  6, 18
// MsgMagic    0xF69E1836 - 19, 15,  3,  8
// DiscMagic   0x960AA381 - 12,  1, 16, 10
// EndMagic    0x27118B26 -  5,  2, 11,  4
//

const UINT8 decoder_itt[256] =
{
//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
//
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  0,  0,  // 0
    0,  2,  0,  0,  0,  0,  0,  0,  3,  0,  0,  0,  0,  0,  0,  0,  // 1
    0,  0,  0,  0,  0,  0,  4,  5,  0,  0,  0,  0,  0,  6,  0,  0,  // 2
    0,  0,  0,  0,  0,  7,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 3
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 4
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 5
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 6
    0,  9,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 7

    0, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0, 11,  0,  0,  0,  0,  // 8
    0,  0,  0,  0,  0,  0, 12, 13,  0,  0,  0, 14,  0,  0, 15,  0,  // 9
    0,  0,  0, 16,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // A
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // B
    0,  0,  0, 17,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // C
    18, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // D
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // E
    0,  0,  0,  0,  0,  0, 19,  0,  0, 20,  0,  0,  0,  0,  0,  0   // F
};

const UINT8 decoder_stt[21][23] =
{
//     0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16  17  18  19  20
//
    {  0,  0,  0,  0,  0,  0,  0, 14,  0,  0,  0,  0, 20,  0,  0,  0,  0,  1,  0, 17,  0 }, //  0 Start
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  2,  0,  0,  0,  0,  0,  0 }, //  1 Call0
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 }, //  2 Call1
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4 }, //  3 Call2
    {  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5 }, //  4 Call3/Return3/Msg3/Disc3
    {  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6 }, //  5 Length0
    {  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7 }, //  6 Length1
    {  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8 }, //  7 Length2
    { 12, 12, 12, 12, 12,  9, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12 }, //  8 Length3
    { 12, 12, 10, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12 }, //  9 End0
    { 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12 }, // 10 End1
    { 12, 12, 12, 12, 13, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12 }, // 11 End2
    {  0,  0,  0,  0,  0,  0,  0, 14,  0,  0,  0,  0, 20,  0,  0,  0,  0,  1,  0, 17,  0 }, // 12 Cleanup
    {  0,  0,  0,  0,  0,  0,  0, 14,  0,  0,  0,  0, 20,  0,  0,  0,  0,  1,  0, 17,  0 }, // 13 Accept
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15,  0,  0,  0,  0,  0,  0,  0 }, // 14 Return0
    {  0,  0,  0,  0,  0,  0, 16,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 }, // 15 Return1
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4,  0,  0 }, // 16 Return2
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 18,  0,  0,  0,  0,  0 }, // 17 Msg0
    {  0,  0,  0, 19,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 }, // 18 Msg1
    {  0,  0,  0,  0,  0,  0,  0,  0,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 }, // 19 Msg2
    {  0, 21,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 }, // 20 Disc0
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 22,  0,  0,  0,  0 }, // 21 Disc1
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 }  // 22 Disc2
};

// Decode bytes out of Queue_In to Queue_Frame.
//
void Pipe_DecodeFrames(void)
{
}

#define MAX_STRING 1024

void Stub_ShoveChars(int fdServer)
{
    Pipe_InitializeQueueInfo(Queue_In);
    Pipe_InitializeQueueInfo(Queue_Out);
    Pipe_InitializeQueueInfo(Queue_Frame);

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

        Pipe_AppendToQueue(Queue_In, len, arg);
        Pipe_DecodeFrames(Queue_In);
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
