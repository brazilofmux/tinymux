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
    static UINT8 arg[QUEUE_BLOCK_SIZE];
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
class CStubSlave : public mux_ISlaveControl, public mux_IMarshal
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IMarshal
    //
    virtual MUX_RESULT GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid);
    virtual MUX_RESULT MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void *pv, marshal_context ctx);
    virtual MUX_RESULT UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv);
    virtual MUX_RESULT ReleaseMarshalData(QUEUE_INFO *pqi);
    virtual MUX_RESULT DisconnectObject(void);

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
    UINT32 m_cRef;
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
    else if (mux_IID_IMarshal == iid)
    {
        *ppv = static_cast<mux_IMarshal *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CStubSlave::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CStubSlave::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CStubSlave::GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid)
{
    UNUSED_PARAMETER(ctx);

    if (nullptr == pcid)
    {
        return MUX_E_INVALIDARG;
    }
    else if (  IID_ISlaveControl == riid
            && CrossProcess == ctx)
    {
        // We only support cross-process at the moment.
        //
        *pcid = CID_StubSlaveProxy;
        return MUX_S_OK;
    }
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CStubSlave_Disconnect(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    UNUSED_PARAMETER(pqi);

    // Get our interface pointer from the channel.
    //
    mux_IUnknown *pIUnknown= static_cast<mux_IUnknown *>(pci->pInterface);
    pci->pInterface = nullptr;

    // Tear down our side of the communication.  Our callback functions will
    // no longer be called.
    //
    Pipe_FreeChannel(pci);

    if (nullptr != pIUnknown)
    {
        pIUnknown->Release();
        return MUX_S_OK;
    }
    else
    {
        return MUX_E_NOINTERFACE;
    }
}

MUX_RESULT CStubSlave_Call(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    mux_ISlaveControl *pISlaveControl = static_cast<mux_ISlaveControl *>(pci->pInterface);
    if (nullptr == pISlaveControl)
    {
        return MUX_E_NOINTERFACE;
    }

    UINT32 iMethod;
    size_t nWanted = sizeof(iMethod);
    if (  !Pipe_GetBytes(pqi, &nWanted, &iMethod)
       || nWanted != sizeof(iMethod))
    {
        return MUX_E_INVALIDARG;
    }

    MUX_RESULT mr = MUX_S_OK;

    // The IUnknown methods (0, 1, and 2) do not make it across, so we don't
    // attempt to handle them here.  Instead, when the reference count on
    // CStubSlaveProxy goes to zero, it drops the connection and destroys itself.
    // We see that as a call to CStubSlave_Disconnect.
    //
    switch (iMethod)
    {
    case 3: // MUX_RESULT AddModule(const UTF8 aModuleName[], const UTF8/UTF16 aFileName[]);
        {
            struct FRAME
            {
                size_t nModuleName;
                size_t nFileName;
            } CallFrame;

            nWanted = sizeof(CallFrame);
            if (  !Pipe_GetBytes(pqi, &nWanted, &CallFrame)
               || nWanted != sizeof(CallFrame))
            {
                return MUX_E_INVALIDARG;
            }

            UTF8  *pModuleName = nullptr;
#if defined(WINDOWS_FILES)
            UTF16 *pFileName = nullptr;
#elif defined(UNIX_FILES)
            UTF8  *pFileName = nullptr;
#endif // UNIX_FILES

            try
            {
                pModuleName = new UTF8[CallFrame.nModuleName];
#if defined(WINDOWS_FILES)
                pFileName   = new UTF16[CallFrame.nFileName];
#elif defined(UNIX_FILES)
                pFileName   = new UTF8[CallFrame.nFileName];
#endif // UNIX_FILES
            }
            catch (...)
            {
                ; // Nothing.
            }

            if (  nullptr != pModuleName
               && nullptr != pFileName)
            {
                nWanted = CallFrame.nModuleName;
                if (  Pipe_GetBytes(pqi, &nWanted, pModuleName)
                   && nWanted == CallFrame.nModuleName)
                {
                    nWanted = CallFrame.nFileName;
                    if (  Pipe_GetBytes(pqi, &nWanted, pFileName)
                       && nWanted == CallFrame.nFileName)
                    {
                        struct RETURN
                        {
                            MUX_RESULT mr;
                        } ReturnFrame;

                        ReturnFrame.mr = pISlaveControl->AddModule(pModuleName, pFileName);

                        Pipe_EmptyQueue(pqi);
                        Pipe_AppendBytes(pqi, sizeof(ReturnFrame), &ReturnFrame);
                        mr = MUX_S_OK;
                    }
                }
            }
            else
            {
                mr = MUX_E_OUTOFMEMORY;
            }

            if (nullptr != pModuleName)
            {
                delete pModuleName;
                pModuleName = nullptr;
            }
            if (nullptr != pFileName)
            {
                delete pFileName;
                pFileName = nullptr;
            }
        }
        break;

    case 4: // MUX_RESULT RemoveModule(const UTF8 aModuleName[]);
        {
            struct FRAME
            {
                size_t nModuleName;
            } CallFrame;

            nWanted = sizeof(CallFrame);
            if (  !Pipe_GetBytes(pqi, &nWanted, &CallFrame)
               || nWanted != sizeof(CallFrame))
            {
                return MUX_E_INVALIDARG;
            }

            UTF8  *pModuleName = nullptr;

            try
            {
                pModuleName = new UTF8[CallFrame.nModuleName];
            }
            catch (...)
            {
                ; // Nothing.
            }

            if (nullptr != pModuleName)
            {
                nWanted = CallFrame.nModuleName;
                if (  Pipe_GetBytes(pqi, &nWanted, pModuleName)
                   && nWanted == CallFrame.nModuleName)
                {
                    struct RETURN
                    {
                        MUX_RESULT mr;
                    } ReturnFrame;

                    ReturnFrame.mr = pISlaveControl->RemoveModule(pModuleName);

                    Pipe_EmptyQueue(pqi);
                    Pipe_AppendBytes(pqi, sizeof(ReturnFrame), &ReturnFrame);
                    mr = MUX_S_OK;
                }
            }
            else
            {
                mr = MUX_E_OUTOFMEMORY;
            }

            if (nullptr != pModuleName)
            {
                delete pModuleName;
                pModuleName = nullptr;
            }
        }
        break;

    case 5: // MUX_RESULT ModuleInfo(int iModule, MUX_MODULE_INFO *pModuleInfo);
        {
            struct FRAME
            {
                int iModule;
            } CallFrame;

            nWanted = sizeof(CallFrame);
            if (  !Pipe_GetBytes(pqi, &nWanted, &CallFrame)
               || nWanted != sizeof(CallFrame))
            {
                return MUX_E_INVALIDARG;
            }

            struct RETURN
            {
                size_t     nName;
                bool       bLoaded;
                MUX_RESULT mr;
            } ReturnFrame;

            MUX_MODULE_INFO ModuleInfo;
            ReturnFrame.mr = pISlaveControl->ModuleInfo(CallFrame.iModule, &ModuleInfo);

            Pipe_EmptyQueue(pqi);
            if (  MUX_SUCCEEDED(ReturnFrame.mr)
               && MUX_S_FALSE != ReturnFrame.mr)
            {
                ReturnFrame.bLoaded = ModuleInfo.bLoaded;
                ReturnFrame.nName   = strlen((const char *)ModuleInfo.pName)+1;
            }
            else
            {
                ReturnFrame.bLoaded = false;
                ReturnFrame.nName   = 0;
            }

            Pipe_AppendBytes(pqi, sizeof(ReturnFrame), &ReturnFrame);
            Pipe_AppendBytes(pqi, ReturnFrame.nName, ModuleInfo.pName);

            mr = MUX_S_OK;
        }
        break;

    case 6: // MUX_RESULT ModuleMaintenance(void);
        {
            struct RETURN
            {
                MUX_RESULT mr;
            } ReturnFrame;

            ReturnFrame.mr = pISlaveControl->ModuleMaintenance();

            Pipe_EmptyQueue(pqi);
            Pipe_AppendBytes(pqi, sizeof(ReturnFrame), &ReturnFrame);
            mr = MUX_S_OK;
        }
        break;

    case 7: // MUX_RESULT ShutdownSlave(void);
        {
            struct RETURN
            {
                MUX_RESULT mr;
            } ReturnFrame;

            ReturnFrame.mr = pISlaveControl->ShutdownSlave();

            Pipe_EmptyQueue(pqi);
            Pipe_AppendBytes(pqi, sizeof(ReturnFrame), &ReturnFrame);
            mr = MUX_S_OK;
        }
        break;
    }
    return mr;
}

MUX_RESULT CStubSlave::MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void *pv, marshal_context ctx)
{
    // Parameter validation and initialization.
    //
    MUX_RESULT mr = MUX_S_OK;
    if (nullptr == pqi)
    {
        mr = MUX_E_INVALIDARG;
    }
    else if (IID_ISlaveControl != riid)
    {
        mr = MUX_E_FAIL;
    }
    else if (CrossProcess != ctx)
    {
        mr = MUX_E_NOTIMPLEMENTED;
    }
    else
    {
        mux_ISlaveControl *pISlaveControl = nullptr;
        if (nullptr == pv)
        {
            mr = QueryInterface(IID_ISlaveControl, (void **)&pISlaveControl);
        }
        else
        {
            mux_IUnknown *pIUnknown = static_cast<mux_IUnknown *>(pv);
            mr = pIUnknown->QueryInterface(IID_ISlaveControl, (void **)&pISlaveControl);
        }
        if (MUX_SUCCEEDED(mr))
        {
            // Construct a packet sufficient to allow the proxy to communicate with us.
            //
            CHANNEL_INFO *pChannel = Pipe_AllocateChannel(CStubSlave_Call, nullptr, CStubSlave_Disconnect);
            if (nullptr != pChannel)
            {
                pChannel->pInterface = pISlaveControl;
                Pipe_AppendBytes(pqi, sizeof(pChannel->nChannel), (UTF8*)(&pChannel->nChannel));
                mr =  MUX_S_OK;
            }
            else
            {
                pISlaveControl->Release();
                pISlaveControl = nullptr;
                mr = MUX_E_OUTOFMEMORY;
            }
        }
    }
    return mr;
}

MUX_RESULT CStubSlave::UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv)
{
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CStubSlave::ReleaseMarshalData(QUEUE_INFO *pqi)
{
    // Since the Marshal Data is like an extra reference on an object, if the
    // Marshaled Data is never unmarshalled, libmux should use this function
    // to release the reference to the component.  This is only implemented on
    // the server side -- not the proxy.
    //
    UINT32 nChannel;
    size_t nWanted = sizeof(nChannel);
    if (  Pipe_GetBytes(pqi, &nWanted, &nChannel)
       && sizeof(nChannel) == nWanted)
    {
        CHANNEL_INFO *pChannel = Pipe_FindChannel(nChannel);
        if (nullptr != pChannel)
        {
            CStubSlave_Disconnect(pChannel, pqi);
        }
    }
    return MUX_S_OK;
}

MUX_RESULT CStubSlave::DisconnectObject(void)
{
    // This is called when the hosting process is about to go down to give the
    // component a chance to notify its proxy that it is about to shut down.
    // This is only implemented on the server side -- not the proxy.
    //
    // TODO: There isn't a mechanism for sending such a notification, yet.
    //
    return MUX_S_OK;
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

UINT32 CStubSlaveFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CStubSlaveFactory::Release(void)
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
