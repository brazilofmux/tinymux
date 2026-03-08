/*! \file stubslave.cpp
 * \brief This slave hosts modules in a separate process.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"

#include "libmux.h"
#include "modules.h"

QUEUE_INFO    Queue_In;
QUEUE_INFO    Queue_Out;

static MUX_CLASS_INFO stubslave_classes[] =
{
    { CID_StubSlave }
};
#define NUM_CLASSES (sizeof(stubslave_classes)/sizeof(stubslave_classes[0]))

bool bStubSlaveShutdown = false;

DEFINE_FACTORY(CStubSlaveFactory)

extern "C" MUX_RESULT DCL_API Stub_PipePump(void)
{
    static uint8_t arg[QUEUE_BLOCK_SIZE];
    size_t nWanted = sizeof(arg);
    while (  Pipe_GetBytes(&Queue_Out, &nWanted, arg)
          && 0 < nWanted)
    {
        if (write(1, arg, nWanted) < 0)
        {
            return MUX_E_FAIL;
        }
        nWanted = sizeof(arg);
    }

    // If we are shutting down, don't wait for any more input from the pipe.
    //
    if (!bStubSlaveShutdown)
    {
        int len = read(0, arg, sizeof(arg));
        if (0 < len)
        {
            Pipe_AppendBytes(&Queue_In, len, arg);
        }
        else if (len < 0)
        {
            return MUX_E_FAIL;
        }
    }
    return MUX_S_OK;
}

void Stub_ShoveChars(void)
{
    QUEUE_INFO Queue_Frame;
    Pipe_InitializeQueueInfo(&Queue_Frame);

    MUX_RESULT mr = MUX_S_OK;
    while (  !bStubSlaveShutdown
          && MUX_SUCCEEDED(mr))
    {
        mr = Stub_PipePump();
        Pipe_DecodeFrames(CHANNEL_INVALID, &Queue_Frame);
    }
    Stub_PipePump();
}

extern "C" MUX_RESULT DCL_API stubslave_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    if (CID_StubSlave == cid)
    {
        CStubSlaveFactory *pStubSlaveFactory = nullptr;
        try
        {
            pStubSlaveFactory = new CStubSlaveFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pStubSlaveFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pStubSlaveFactory->QueryInterface(iid, ppv);
        pStubSlaveFactory->Release();
    }
    return mr;
}

int main(int argc, char *argv[])
{
    Pipe_InitializeQueueInfo(&Queue_In);
    Pipe_InitializeQueueInfo(&Queue_Out);

    MUX_RESULT mr = MUX_S_OK;
    mr = mux_InitModuleLibrary(IsSlaveProcess);
    if (MUX_SUCCEEDED(mr))
    {
        mr = mux_RegisterClassObjects(NUM_CLASSES, stubslave_classes, stubslave_GetClassObject);
        if (MUX_SUCCEEDED(mr))
        {
            mr = mux_InitModuleLibraryPump(Stub_PipePump, &Queue_In, &Queue_Out);
            if (MUX_SUCCEEDED(mr))
            {
                Stub_ShoveChars();
                mr = mux_RevokeClassObjects(NUM_CLASSES, stubslave_classes);
            }
        }
        mr = mux_FinalizeModuleLibrary();
    }

    return MUX_RESULT_TO_EXIT_STATUS(mr);
}

// CStubSlave component which is not directly accessible.
//
class CStubSlave : public mux_ISlaveControl
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t     AddRef(void);
    virtual uint32_t     Release(void);

    // mux_ISlaveControl
    //
#if defined(WINDOWS_FILES)
    virtual MUX_RESULT AddModule(const UTF8 aModuleName[], const UTF16 aFileName[]);
#elif defined(UNIX_FILES)
    virtual MUX_RESULT AddModule(const UTF8 aModuleName[], const UTF8 aFileName[]);
#endif // UNIX_FILES
    virtual MUX_RESULT RemoveModule(const UTF8 aModuleName[]);
    virtual MUX_RESULT ModuleInfo(int iModule, MUX_MODULE_INFO *pModuleInfo);
    virtual MUX_RESULT ModuleMaintenance(void);
    virtual MUX_RESULT ShutdownSlave(void);

    CStubSlave(void);
    virtual ~CStubSlave();

private:
    uint32_t m_cRef;
};

CStubSlave::CStubSlave(void) : m_cRef(1)
{
}

CStubSlave::~CStubSlave()
{
}

MUX_RESULT CStubSlave::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_ISlaveControl *>(this);
    }
    else if (IID_ISlaveControl == iid)
    {
        *ppv = static_cast<mux_ISlaveControl *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CStubSlave::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CStubSlave::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

#if defined(WINDOWS_FILES)
MUX_RESULT CStubSlave::AddModule(const UTF8 aModuleName[], const UTF16 aFileName[])
#elif defined(UNIX_FILES)
MUX_RESULT CStubSlave::AddModule(const UTF8 aModuleName[], const UTF8 aFileName[])
#endif // UNIX_FILES
{
    return mux_AddModule(aModuleName, aFileName);
}

MUX_RESULT CStubSlave::RemoveModule(const UTF8 aModuleName[])
{
    return mux_RemoveModule(aModuleName);
}

MUX_RESULT CStubSlave::ModuleInfo(int iModule, MUX_MODULE_INFO *pModuleInfo)
{
    return mux_ModuleInfo(iModule, pModuleInfo);
}

MUX_RESULT CStubSlave::ModuleMaintenance(void)
{
    return mux_ModuleMaintenance();
}

MUX_RESULT CStubSlave::ShutdownSlave(void)
{
    bStubSlaveShutdown = true;
    return MUX_S_OK;
}

// Factory for CStubSlave component which is not directly accessible.
//
CStubSlaveFactory::CStubSlaveFactory(void) : m_cRef(1)
{
}

CStubSlaveFactory::~CStubSlaveFactory()
{
}

MUX_RESULT CStubSlaveFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else if (mux_IID_IClassFactory == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CStubSlaveFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CStubSlaveFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CStubSlaveFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (nullptr != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CStubSlave *pStubSlave = nullptr;
    try
    {
        pStubSlave = new CStubSlave;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pStubSlave)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pStubSlave->QueryInterface(iid, ppv);
    pStubSlave->Release();
    return mr;
}

MUX_RESULT CStubSlaveFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}
