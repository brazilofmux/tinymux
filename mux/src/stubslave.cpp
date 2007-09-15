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

typedef enum
{
    eUnknown = 0,
    eCall,
    eReturn,
    eMessage,
    eDisconnect
} FrameType;

// Decoder
//
int           iState = 0;
FrameType     eType = eUnknown;
UINT32        nLength = 0;
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

const UINT8 decoder_stt[23][21] =
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
    UINT8 buffer[QUEUE_BLOCK_SIZE];

    if (8 == iState)
    {
        // We must remain in the Length3 state until we have consumed all of the expected data.
        //
        while (0 < nLengthRemaining)
        {
            size_t nWanted = nLengthRemaining;
            if (QUEUE_BLOCK_SIZE < nWanted)
            {
                nWanted = QUEUE_BLOCK_SIZE;
            }

            if (  !Pipe_GetBytes(&Queue_In, &nWanted, buffer)
               || 0 == nWanted)
            {
                return;
            }
            Pipe_AppendBytes(&Queue_Frame, nWanted, buffer);
            nLengthRemaining -= nWanted;
        }
    }

    UINT8 ch;
    while (Pipe_GetByte(&Queue_In, &ch))
    {
        iState = decoder_stt[iState][decoder_itt[ch]];
        switch (iState)
        {
        case 3: // Call2
            eType = eCall;
            break;

        case 16: // Return2
            eType = eReturn;
            break;

        case 19: // Msg2
            eType = eMessage;
            break;

        case 22: // Disc2
            eType = eDisconnect;
            break;

        case 5: // Length0
            nLength = ((UINT32)ch) << 24;
            break;

        case 6: // Length1
            nLength = nLength | (((UINT32)ch) << 16);
            break;

        case 7: // Length2
            nLength = nLength | (((UINT32)ch) << 8);
            break;

        case 8: // Length3
            nLength = nLength | ((UINT32)ch);
            nLengthRemaining = nLength;
              
            // We've been told how long to expect the packet to be.
            //
            while (0 < nLengthRemaining)
            {
                size_t nWanted = nLengthRemaining;
                if (QUEUE_BLOCK_SIZE < nWanted)
                {
                    nWanted = QUEUE_BLOCK_SIZE;
                }

                if (  !Pipe_GetBytes(&Queue_In, &nWanted, buffer)
                   || 0 == nWanted)
                {
                    // We'll leave the state machine and try to pick up again at the same place later.
                    //
                    return;
                }
                Pipe_AppendBytes(&Queue_Frame, nWanted, buffer);
                nLengthRemaining -= nWanted;
            }
            break;

        case 12: // Cleanup

            // Something went wrong. Re-initialize all the decoding variables.
            //
            eType   = eUnknown;
            nLength = 0;
            nChannel = 0;
            Pipe_EmptyQueue(&Queue_Frame);
            break;

        case 13: // Accept
            if (4 <= nLength)
            {
                UINT8 ach[4];
                if (  Pipe_GetByte(&Queue_Frame, &ach[0])
                   && Pipe_GetByte(&Queue_Frame, &ach[1])
                   && Pipe_GetByte(&Queue_Frame, &ach[2])
                   && Pipe_GetByte(&Queue_Frame, &ach[3]))
                {
                    nChannel = ((UINT32)ach[0]) << 24
                             | ((UINT32)ach[1]) << 16
                             | ((UINT32)ach[2]) << 8
                             | ((UINT32)ch);

                    if (  0 <= nChannel 
                       && nChannel < nChannels)
                    {
                        switch (eType)
                        {
                        case eCall:
                            aChannels[nChannel].pfCall(&aChannels[nChannel], &Queue_Frame);

                            // TODO: The Queue_Frame now contains the return
                            // paramters. We need to copy these to Queue_Out
                            // and let them propogate to the other side.
                            //
                            break;

                        case eMessage:
                            aChannels[nChannel].pfMsg(&aChannels[nChannel], &Queue_Frame);
                            break;

                        case eDisconnect:
                            aChannels[nChannel].pfDisc(&aChannels[nChannel], &Queue_Frame);
                            break;
                        }
                    }
                }
            }


            // The packet was too short to contain a channel number, the
            // channel did not exist, or the call completed successfully.
            //
            eType    = eUnknown;
            nLength  = 0;
            nChannel = 0;
            Pipe_EmptyQueue(&Queue_Frame);
            break;
        }
    }
}

void Stub_ShoveChars(int fdServer)
{
    Pipe_InitializeQueueInfo(&Queue_In);
    Pipe_InitializeQueueInfo(&Queue_Out);
    Pipe_InitializeQueueInfo(&Queue_Frame);

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
        Pipe_DecodeFrames();
    }
}

MUX_RESULT Channel0_Call(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    return MUX_E_NOTIMPLEMENTED;
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
