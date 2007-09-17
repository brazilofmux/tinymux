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

void Stub_PipePump(void)
{
    static UINT8 arg[QUEUE_BLOCK_SIZE];
    size_t nWanted = sizeof(arg);
    while (  Pipe_GetBytes(&Queue_Out, &nWanted, arg)
          && 0 < nWanted)
    {
        write(1, arg, nWanted);
        nWanted = sizeof(arg);
    }

    int len = read(0, arg, sizeof(arg));
    if (0 < len)
    {
        Pipe_AppendBytes(&Queue_In, len, arg);
    }
}

bool bStubSlaveShutdown = false;

void Stub_ShoveChars(void)
{
    QUEUE_INFO Queue_Frame;
    Pipe_InitializeQueueInfo(&Queue_Frame);

    while (!bStubSlaveShutdown)
    {
        Stub_PipePump();
        Pipe_DecodeFrames(CHANNEL_INVALID, &Queue_Frame);
    }
}

int main(int argc, char *argv[])
{
    Pipe_InitializeQueueInfo(&Queue_In);
    Pipe_InitializeQueueInfo(&Queue_Out);

    mux_InitModuleLibrary(IsSlaveProcess, Stub_PipePump, &Queue_In, &Queue_Out);
    mux_AddModule(T("sum"), T("./bin/sum.so"));

    Stub_ShoveChars();
    mux_FinalizeModuleLibrary();
    return 0;
}

#endif
