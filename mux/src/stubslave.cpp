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

FILE *g_fpLog = NULL;

void LogBytes(size_t n, const void *p)
{
    UINT8 *pb = (UINT8 *)p;
    FILE *fp = fopen("stubslave.log", "a+");
    if (NULL == fp)
    {
        return;
    }

    fprintf(fp, "Bytes: ");
    for (int i = 0; i < n; i++)
    {
        fprintf(fp, "%02X", pb[i]);
    }
    fprintf(fp, "\n");
    fclose(fp);
}

void LogLine(int lineno)
{
    FILE *fp = fopen("stubslave.log", "a+");
    if (NULL == fp)
    {
        return;
    }

    fprintf(fp, "Line: %d\n", lineno);
    fclose(fp);
}

void Stub_ShoveChars(int fdServer)
{
    LogLine(__LINE__);
    for (;;)
    {
        LogLine(__LINE__);
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

        LogBytes(len, arg);

        Pipe_AppendBytes(&Queue_In, len, arg);
        LogLine(__LINE__);
        Pipe_DecodeFrames(0xFFFFFFFFUL, &Queue_Frame);
        LogLine(__LINE__);
    }
}

void Stub_PipePump(void)
{
    LogLine(__LINE__);
}

MUX_RESULT Channel0_Call(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    LogLine(__LINE__);
    return MUX_E_NOTIMPLEMENTED;
}

int main(int argc, char *argv[])
{
    LogLine(__LINE__);

    Pipe_InitializeQueueInfo(&Queue_In);
    Pipe_InitializeQueueInfo(&Queue_Out);
    Pipe_InitializeQueueInfo(&Queue_Frame);

    LogLine(__LINE__);
    mux_InitModuleLibrary(IsSlaveProcess, Stub_PipePump, &Queue_In, &Queue_Out);
    LogLine(__LINE__);
    Pipe_InitializeChannelZero(Channel0_Call, NULL, NULL);
    LogLine(__LINE__);
    Stub_ShoveChars(0);
    LogLine(__LINE__);
    mux_FinalizeModuleLibrary();
    LogLine(__LINE__);
    return 0;
}

#endif
