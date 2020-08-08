/*! \file libmux.h
 * \brief Module support
 *
 * To support loadable modules, we implement a poor man's COM.  There is no
 * support for appartments or remote servers.  The registry is constructed by
 * libmux as each module is loaded, but it exists only for as long as a
 * process with libmux is running.
 *
 * Most-likely, there is no opportunity to use any existing RPC or IDL tools
 * for building these interfaces.
 *
 * There is support for spawning a stubslave process which can then load
 * modules out-of-proc.  There is a primative poor man's RPC for communicating
 * with stubslave across a pipe and marhshaling across interfaces and
 * arguments.  Custom Marshaling works, and Standard Marshaling is planned.
 *
 * While there is no support for multiple threads, methods are expected to be
 * re-entrant.  Don't be surprised if your call to another process results
 * in your being called again and again.
 */

#ifndef LIBMUX_H
#define LIBMUX_H

typedef int MUX_RESULT;
typedef UINT64 MUX_CID;
typedef UINT64 MUX_IID;

#define MUX_S_OK                 (0)
#define MUX_S_FALSE              (1)
#define MUX_E_FAIL              (-1)
#define MUX_E_OUTOFMEMORY       (-2)
#define MUX_E_CLASSNOTAVAILABLE (-3)
#define MUX_E_NOINTERFACE       (-4)
#define MUX_E_NOTIMPLEMENTED    (-5)
#define MUX_E_INVALIDARG        (-6)
#define MUX_E_UNEXPECTED        (-7)
#define MUX_E_NOTREADY          (-8)
#define MUX_E_NOTFOUND          (-9)
#define MUX_E_NOAGGREGATION     (-10)

#define MUX_FAILED(x)    ((MUX_RESULT)(x) < 0)
#define MUX_SUCCEEDED(x) (0 <= (MUX_RESULT)(x))
#define MUX_RESULT_TO_EXIT_STATUS(x) (MUX_SUCCEEDED(x)?0:(((int)(x))<255?(-(int)(x)):255))

typedef enum
{
    UseSameProcess  = 1,
    UseMainProcess  = 2,
    UseSlaveProcess = 4,
    UseAnyContext   = 7
} create_context;

typedef enum
{
    CrossProcess = 0,
    CrossThread  = 1
} marshal_context;

typedef enum
{
    IsUninitialized  = 0,
    IsMainProcess    = 1,
    IsSlaveProcess   = 2
} process_context;

const MUX_CID mux_CID_StandardMarshaler = UINT64_C(0x0000000100000001);

const MUX_IID mux_IID_IUnknown          = UINT64_C(0x0000000100000010);
const MUX_IID mux_IID_IClassFactory     = UINT64_C(0x0000000100000011);
const MUX_IID mux_IID_IRpcProxyBuffer   = UINT64_C(0x0000000100000013);
const MUX_IID mux_IID_IRpcStubBuffer    = UINT64_C(0x0000000100000014);
const MUX_IID mux_IID_IPSFactoryBuffer  = UINT64_C(0x0000000100000015);
const MUX_IID mux_IID_IMarshal          = UINT64_C(0x0000000100000016);

const UINT32  CHANNEL_INVALID           = 0xFFFFFFFFul;

#define interface class

interface mux_IUnknown
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) = 0;
    virtual UINT32     AddRef(void) = 0;
    virtual UINT32     Release(void) = 0;
};

interface mux_IClassFactory : public mux_IUnknown
{
public:
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv) = 0;
    virtual MUX_RESULT LockServer(bool bLock) = 0;
};

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
    size_t      nBytes;
} QUEUE_INFO;

typedef MUX_RESULT FCALL(struct channel_info *pci, QUEUE_INFO *pqi);
typedef MUX_RESULT FMSG(struct channel_info *pci, QUEUE_INFO *pqi);
typedef MUX_RESULT FDISC(struct channel_info *pci, QUEUE_INFO *pqi);

typedef struct channel_info
{
     UINT32    nChannel;
     FCALL    *pfCall;
     FMSG     *pfMsg;
     FDISC    *pfDisc;
     void     *pInterface;
} CHANNEL_INFO, *PCHANNEL_INFO;

extern "C" PCHANNEL_INFO DCL_EXPORT DCL_API Pipe_AllocateChannel(FCALL *pfCall, FMSG *pfMsg, FDISC *pfDisc);
extern "C" void          DCL_EXPORT DCL_API Pipe_AppendBytes(QUEUE_INFO *pqi, size_t n, const void *p);
extern "C" void          DCL_EXPORT DCL_API Pipe_AppendQueue(QUEUE_INFO *pqiOut, QUEUE_INFO *pqiIn);
extern "C" bool          DCL_EXPORT DCL_API Pipe_DecodeFrames(UINT32 nReturnChannel, QUEUE_INFO *pqiFrame);
extern "C" void          DCL_EXPORT DCL_API Pipe_EmptyQueue(QUEUE_INFO *pqi);
extern "C" PCHANNEL_INFO DCL_EXPORT DCL_API Pipe_FindChannel(UINT32 nChannel);
extern "C" void          DCL_EXPORT DCL_API Pipe_FreeChannel(CHANNEL_INFO *pci);
extern "C" bool          DCL_EXPORT DCL_API Pipe_GetByte(QUEUE_INFO *pqi, UINT8 ach[1]);
extern "C" bool          DCL_EXPORT DCL_API Pipe_GetBytes(QUEUE_INFO *pqi, size_t *pn, void *pch);
extern "C" void          DCL_EXPORT DCL_API Pipe_InitializeQueueInfo(QUEUE_INFO *pqi);
extern "C" size_t        DCL_EXPORT DCL_API Pipe_QueueLength(QUEUE_INFO *pqi);
extern "C" MUX_RESULT    DCL_EXPORT DCL_API Pipe_SendCallPacketAndWait(UINT32 nChannel, QUEUE_INFO *pqi);
extern "C" MUX_RESULT    DCL_EXPORT DCL_API Pipe_SendMsgPacket(UINT32 nChannel, QUEUE_INFO *pqi);
extern "C" MUX_RESULT    DCL_EXPORT DCL_API Pipe_SendDiscPacket(UINT32 nChannel, QUEUE_INFO *pqi);


// The following is part of what is called 'Standard Marshaling'.  Since this
// is only partially implemented, the related interfaces are subject to change.
//
interface mux_IRpcProxyBuffer : public mux_IUnknown
{
public:
    virtual MUX_RESULT Connect(UINT32 nChannel) = 0;
    virtual void       Disconnect(void);
};

interface mux_IRpcStubBuffer : public mux_IUnknown
{
public:
    virtual MUX_RESULT Connect(mux_IUnknown *pUnknownServer) = 0;
    virtual void       Disconnect(void) = 0;
    virtual MUX_RESULT Invoke(QUEUE_INFO *pqi) = 0;
    virtual MUX_RESULT IsSupported(MUX_IID riid) = 0;
    virtual UINT32     CountRefs(void) = 0;
};

interface mux_IPSFactoryBuffer : public mux_IUnknown
{
public:
    virtual MUX_RESULT CreateProxy(mux_IUnknown *pUnknownOuter, MUX_IID riid, mux_IRpcProxyBuffer **ppProxy, void **ppv) = 0;
    virtual MUX_RESULT CreateStub(MUX_IID riid, mux_IUnknown *pUnknownOuter, mux_IRpcStubBuffer **ppStub) = 0;
};

// The following is part of what is called 'Custom Marshaling'.
//
interface mux_IMarshal : public mux_IUnknown
{
public:
    virtual MUX_RESULT GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid) = 0;
    virtual MUX_RESULT MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void *pv, marshal_context ctx) = 0;
    virtual MUX_RESULT UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv) = 0;
    virtual MUX_RESULT ReleaseMarshalData(QUEUE_INFO *pqi) = 0;
    virtual MUX_RESULT DisconnectObject(void) = 0;
};

extern "C"
{
    typedef MUX_RESULT DCL_API FPGETCLASSOBJECT(MUX_CID cid, MUX_IID iid, void **ppv);
}

// All components must be registered.  Currently, only the MUX_CID is required.
//
typedef struct
{
    MUX_CID cid;
} MUX_CLASS_INFO;

// It is not required that all interfaces be registered.  However, if an
// interface needs to be marshalled using Standard Marshaling, it must have an
// associated proxy-stub component and therefore must be registered.
//
typedef struct
{
    MUX_IID iid;
    MUX_CID cidProxyStub;
} MUX_INTERFACE_INFO;

// APIs available to main program (netmux or stubslave) and dynamic modules.
//
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_CreateInstance(MUX_CID cid, mux_IUnknown *pUnknownOuter, create_context ctx, MUX_IID iid, void **ppv);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RegisterClassObjects(int nci, MUX_CLASS_INFO aci[], FPGETCLASSOBJECT *pfGetClassObject);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RevokeClassObjects(int nci, MUX_CLASS_INFO aci[]);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RegisterInterfaces(int nii, MUX_INTERFACE_INFO aii[]);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RevokeInterfaces(int nii, MUX_INTERFACE_INFO aii[]);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_GetStandardMarshal(MUX_IID riid, mux_IUnknown *pIUnknown, marshal_context ctx, mux_IMarshal **ppMarshal);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, mux_IUnknown *pIUnknown, marshal_context ctx);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv);

typedef struct
{
    const UTF8 *pName;
    bool       bLoaded;
} MUX_MODULE_INFO;

typedef MUX_RESULT PipePump(void);

// APIs intended only for use by main program (netmux or stubslave).
//
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_InitModuleLibrary(process_context ctx);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_InitModuleLibraryPump(PipePump *fpPipePump, QUEUE_INFO *pQueue_In, QUEUE_INFO *pQueue_Out);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_FinalizeModuleLibrary(void);

#if defined(WINDOWS_FILES)
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_AddModule(const UTF8 aModuleName[], const UTF16 aFileName[]);
#elif defined(UNIX_FILES)
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_AddModule(const UTF8 aModuleName[], const UTF8 aFileName[]);
#endif // UNIX_FILES
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RemoveModule(const UTF8 aModuleName[]);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_ModuleInfo(int iModule, MUX_MODULE_INFO *pModuleInfo);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_ModuleMaintenance(void);

#define DEFINE_FACTORY(x)                                                                      \
class x : public mux_IClassFactory                                                             \
{                                                                                              \
public:                                                                                        \
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);                                \
    virtual UINT32     AddRef(void);                                                           \
    virtual UINT32     Release(void);                                                          \
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv);   \
    virtual MUX_RESULT LockServer(bool bLock);                                                 \
    x(void);                                                                                   \
    virtual ~x();                                                                              \
                                                                                               \
private:                                                                                       \
    UINT32 m_cRef;                                                                             \
};

#endif // LIBMUX_H
