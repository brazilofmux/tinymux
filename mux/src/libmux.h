/*! \file libmux.h
 * \brief Module support
 *
 * $Id$
 *
 * To support loadable modules, we implement a poor man's COM. There is no
 * support for appartments, remote servers, registry.  There is no RPC or
 * sockets, and most-likely, no opportunity to use any existing RPC tools for
 * building interfaces either.
 *
 * There is currently no support for out of process servers or marshalling.
 *
 * There is no support for multiple threads, but methods are expected to be
 * re-entrant.
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

typedef enum
{
    UseSameProcess  = 1,
    UseMainProcess  = 2,
    UseSlaveProcess = 3,
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

#ifdef WIN32
const MUX_IID mux_IID_IUnknown          = 0x0000000100000010i64;
const MUX_IID mux_IID_IClassFactory     = 0x0000000100000011i64;
const MUX_IID mux_IID_IRpcChannelBuffer = 0x0000000100000012i64;
const MUX_IID mux_IID_IRpcProxyBuffer   = 0x0000000100000013i64;
const MUX_IID mux_IID_IRpcStubBuffer    = 0x0000000100000014i64;
const MUX_IID mux_IID_IPSFactoryBuffer  = 0x0000000100000015i64;
const MUX_IID mux_IID_IMarshal          = 0x0000000100000016i64;
#else
const MUX_IID mux_IID_IUnknown          = 0x0000000100000010ull;
const MUX_IID mux_IID_IClassFactory     = 0x0000000100000011ull;
const MUX_IID mux_IID_IRpcChannelBuffer = 0x0000000100000012ull;
const MUX_IID mux_IID_IRpcProxyBuffer   = 0x0000000100000013ull;
const MUX_IID mux_IID_IRpcStubBuffer    = 0x0000000100000014ull;
const MUX_IID mux_IID_IPSFactoryBuffer  = 0x0000000100000015ull;
const MUX_IID mux_IID_IMarshal          = 0x0000000100000016ull;
#endif

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

typedef struct
{
    void   *pBuffer;
    size_t cbBuffer;
    UINT32 iMethod;
} MUX_RPCMESSAGE;

interface mux_IRpcChannelBuffer : public mux_IUnknown
{
public:
    virtual MUX_RESULT GetBuffer(MUX_RPCMESSAGE *pMessage, MUX_IID riid) = 0;
    virtual MUX_RESULT SendReceive(MUX_RPCMESSAGE *pMessage, UINT32 *pStatus) = 0;
    virtual MUX_RESULT FreeBuffer(MUX_RPCMESSAGE *pMessage) = 0;
    virtual marshal_context GetMarshalContext(void) = 0;
    virtual MUX_RESULT IsConnected(void) = 0;
};

interface mux_IRpcProxyBuffer : public mux_IUnknown
{
public:
    virtual MUX_RESULT Connect(mux_IRpcChannelBuffer *pRpcChannelBuffer) = 0;
    virtual void       Disconnect(void);
};

interface mux_IRpcStubBuffer : public mux_IUnknown
{
public:
    virtual MUX_RESULT Connect(mux_IUnknown *pUnknownServer) = 0;
    virtual void       Disconnect(void) = 0;
    virtual MUX_RESULT Invoke(MUX_RPCMESSAGE *pMessage, mux_IRpcChannelBuffer *pRpcChannelBuffer) = 0;
    virtual MUX_RESULT IsSupported(MUX_IID riid) = 0;
    virtual UINT32     CountRefs(void) = 0;
};

interface mux_IPSFactoryBuffer : public mux_IUnknown
{
public:
    virtual MUX_RESULT CreateProxy(mux_IUnknown *pUnknownOuter, MUX_IID riid, mux_IRpcProxyBuffer **ppProxy, void **ppv) = 0;
    virtual MUX_RESULT CreateStub(MUX_IID riid, mux_IUnknown *pUnknownOuter, mux_IRpcStubBuffer *ppStub) = 0;
};

interface mux_IMarshal : public mux_IUnknown
{
public:
    virtual MUX_RESULT GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid) = 0;
    virtual MUX_RESULT MarshalInterface(size_t *pnBuffer, char **pBuffer, MUX_IID riid, marshal_context ctx) = 0;
    virtual MUX_RESULT UnmarshalInterface(size_t nBuffer, char *pBuffer, MUX_IID riid, void **ppv) = 0;
    virtual MUX_RESULT ReleaseMarshalData(char *pBuffer) = 0;
    virtual MUX_RESULT DisconnectObject(void) = 0;
};

extern "C"
{
    typedef MUX_RESULT DCL_API FPGETCLASSOBJECT(MUX_CID cid, MUX_IID iid, void **ppv);
}

// All components must be registered.  Currently, only the id is required.
//
typedef struct
{
    MUX_CID cid;
} CLASS_INFO;

// It is not required that all interfaces be registered.  However, if an
// interface needs to be marshalled, it must have an associated proxy-stub
// component and therefore must be registered.
//
typedef struct
{
    MUX_IID iid;
    MUX_CID cidProxyStub;
} INTERFACE_INFO;

// APIs available to main program (netmux or stubslave) and dynamic modules.
//
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_CreateInstance(MUX_CID cid, mux_IUnknown *pUnknownOuter, create_context ctx, MUX_IID iid, void **ppv);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RegisterClassObjects(int nci, CLASS_INFO aci[], FPGETCLASSOBJECT *pfGetClassObject);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RevokeClassObjects(int nci, CLASS_INFO aci[]);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RegisterInterfaces(int nii, INTERFACE_INFO aii[]);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RevokeInterfaces(int nii, INTERFACE_INFO aii[]);

typedef struct
{
    const UTF8 *pName;
    bool       bLoaded;
} MUX_MODULE_INFO;

typedef void PipePump(void);

// APIs intended only for use by main program (netmux or stubslave).
//
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_InitModuleLibrary(process_context ctx, PipePump *fpPipePump);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_FinalizeModuleLibrary(void);

#ifdef WIN32
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_AddModule(const UTF8 aModuleName[], const UTF16 aFileName[]);
#else
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_AddModule(const UTF8 aModuleName[], const UTF8 aFileName[]);
#endif // WIN32
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RemoveModule(const UTF8 aModuleName[]);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_ModuleInfo(int iModule, MUX_MODULE_INFO *pModuleInfo);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_ModuleMaintenance(void);

extern "C" bool DCL_EXPORT DCL_API mux_ReceiveData(size_t nBuffer, const void *pBuffer);

#endif // LIBMUX_H
