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

typedef int MUX_RESULT;

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
const UINT64 mux_IID_IUnknown      = 0x0000000100000010i64;
const UINT64 mux_IID_IClassFactory = 0x0000000100000011i64;
#else
const UINT64 mux_IID_IUnknown      = 0x0000000100000010ull;
const UINT64 mux_IID_IClassFactory = 0x0000000100000011ull;
#endif

#define interface class

interface mux_IUnknown
{
public:
    virtual MUX_RESULT QueryInterface(UINT64 iid, void **ppv) = 0;
    virtual UINT32     AddRef(void) = 0;
    virtual UINT32     Release(void) = 0;
};

interface mux_IClassFactory : public mux_IUnknown
{
public:
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, UINT64 iid, void **ppv) = 0;
    virtual MUX_RESULT LockServer(bool bLock) = 0;
};

extern "C"
{
    typedef MUX_RESULT DCL_API FPGETCLASSOBJECT(UINT64 cid, UINT64 iid, void **ppv);
}

// APIs available to netmux and dynamic modules.
//
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_CreateInstance(UINT64 cid, mux_IUnknown *pUnknownOuter, create_context ctx, UINT64 iid, void **ppv);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RegisterClassObjects(int ncid, UINT64 acid[], FPGETCLASSOBJECT *pfGetClassObject);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RevokeClassObjects(int ncid, UINT64 acid[]);

typedef struct
{
    const UTF8 *pName;
    bool       bLoaded;
} MUX_MODULE_INFO;

// APIs intended only for use by netmux.
//
#ifdef WIN32
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_AddModule(const UTF8 aModuleName[], const UTF16 aFileName[]);
#else
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_AddModule(const UTF8 aModuleName[], const UTF8 aFileName[]);
#endif // WIN32
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RemoveModule(const UTF8 aModuleName[]);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_ModuleInfo(int iModule, MUX_MODULE_INFO *pModuleInfo);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_ModuleTick(void);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_InitModuleLibrary(process_context ctx);
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_FinalizeModuleLibrary(void);
