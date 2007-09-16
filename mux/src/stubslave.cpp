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

QUEUE_INFO    Queue_In;
QUEUE_INFO    Queue_Out;
QUEUE_INFO    Queue_Frame;

void Stub_ShoveChars(int fdServer)
{
    for (;;)
    {
        UINT8 arg[QUEUE_BLOCK_SIZE];
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

        Pipe_AppendBytes(&Queue_In, len, arg);
        Pipe_DecodeFrames(0xFFFFFFFFUL, &Queue_Frame);
        
        size_t nWanted = sizeof(arg);
        while (  Pipe_GetBytes(&Queue_Out, &nWanted, arg)
              && 0 < nWanted)
        {
            write(1, arg, nWanted);
        }
    }
}

void Stub_PipePump(void)
{
}

MUX_RESULT Channel0_Call(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    struct CF
    {
        MUX_CID cid;
        MUX_IID iid;
    } CallFrame;

    MUX_RESULT mr = MUX_S_OK;

    size_t nWanted = sizeof(CallFrame);
    if (  Pipe_GetBytes(pqi, &nWanted, &CallFrame)
       && nWanted == sizeof(CallFrame))
    {
        mux_IMarshal *pIMarshal = NULL;
        mr = mux_CreateInstance(CallFrame.cid, NULL, UseSameProcess, mux_IID_IMarshal, (void **)&pIMarshal);
        if (MUX_SUCCEEDED(mr))
        {
            MUX_CID cidProxy = 0;
            mr = pIMarshal->GetUnmarshalClass(CallFrame.iid, CrossProcess, &cidProxy);
            if (MUX_SUCCEEDED(mr))
            {
                Pipe_AppendBytes(pqi, sizeof(cidProxy), &cidProxy);
                mr = pIMarshal->MarshalInterface(pqi, CallFrame.iid, CrossProcess);
            }
            pIMarshal->Release();
        }
    }
    else
    {
       Pipe_EmptyQueue(pqi);
       mr = MUX_E_INVALIDARG;
    }
    return mr;
}

int main(int argc, char *argv[])
{
    Pipe_InitializeQueueInfo(&Queue_In);
    Pipe_InitializeQueueInfo(&Queue_Out);
    Pipe_InitializeQueueInfo(&Queue_Frame);

    mux_InitModuleLibrary(IsSlaveProcess, Stub_PipePump, &Queue_In, &Queue_Out);
    mux_AddModule(T("sum"), T("./bin/sum.so"));
    
    Pipe_InitializeChannelZero(Channel0_Call, NULL, NULL);
    Stub_ShoveChars(0);
    mux_FinalizeModuleLibrary();
    return 0;
}

#endif
