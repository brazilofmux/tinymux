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

typedef struct QueueBlock
{
    void  *pBuffer;
    size_t nBuffer;
    struct QueueBlock *qbNext;
    struct QueueBlock *qbPrev;
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

void Pipe_AddToQueue(QUEUE_INFO *pqi, size_t nBuffer, void *pBuffer)
{
}

void Pipe_ParseQueue(QUEUE_INFO *pqi)
{
}

#define MAX_STRING 1000

void Stub_ShoveChars(int fdServer)
{
    for (;;)
    {
        char arg[MAX_STRING];
        int len = read(0, arg, sizeof(arg)-1);
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

        Pipe_AddToQueue(pQueue_In, len, arg);
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
