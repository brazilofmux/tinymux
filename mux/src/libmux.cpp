/*! \file libmux.cpp
 * \brief Base-level module support
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include <map>

#include "config.h"

#ifdef HAVE_DLOPEN
#include <dlfcn.h>
#endif // HAVE_DLOPEN

#include "libmux.h"

extern "C"
{
    typedef MUX_RESULT FPCANUNLOADNOW(void);
    typedef MUX_RESULT FPREGISTER(void);
    typedef MUX_RESULT FPUNREGISTER(void);
};

#if defined(WINDOWS_DYNALIB)
typedef HINSTANCE MODULE_HANDLE;
#define MOD_OPEN(m)  LoadLibrary(m)
#define MOD_SYM(h,s) GetProcAddress(h,s)
#define MOD_CLOSE(h) FreeLibrary(h)
#elif defined(UNIX_DYNALIB)
typedef void     *MODULE_HANDLE;
#define MOD_OPEN(m)  dlopen((char *)m, RTLD_LAZY)
#define MOD_SYM(h,s) dlsym(h,s)
#define MOD_CLOSE(h) dlclose(h)
#elif defined(PRETEND_DYNALIB)
typedef void     *MODULE_HANDLE;
extern MODULE_HANDLE mux_dlopen(const char *m);
extern void *mux_dlsym(MODULE_HANDLE h, const char *s);
extern void mux_dlclose(MODULE_HANDLE h);
#define MOD_OPEN(m) mux_dlopen(m)
#define MOD_SYM(h,s) mux_dlsym(h,s)
#define MOD_CLOSE(h) mux_dlclose(h)
#endif

typedef enum LIBRARYSTATE
{
    eLibraryDown = 1,
    eLibraryInitialized,
    eLibraryGoingDown
} LibraryState;

typedef enum MODULESTATE
{
    eModuleInitialized = 1,
    eModuleRegistering,
    eModuleRegistered,
    eModuleUnregistering,
    eModuleUnloadable
} ModuleState;

struct ltstr
{
    bool operator()(const UTF8 *s1, const UTF8 *s2) const
    {
        return strcmp((const char *)s1, (const char *)s2) < 0;
    }
};

class Module
{
public:
    Module() : fpGetClassObject(nullptr), fpCanUnloadNow(nullptr), fpRegister(nullptr), fpUnregister(nullptr), hInst(nullptr), pModuleName(nullptr),
        pFileName(nullptr), bLoaded(false), eState(eModuleInitialized)
    { }
    FPGETCLASSOBJECT *fpGetClassObject;
    FPCANUNLOADNOW   *fpCanUnloadNow;
    FPREGISTER       *fpRegister;
    FPUNREGISTER     *fpUnregister;
    MODULE_HANDLE    hInst;
    const UTF8       *pModuleName;
#if defined(WINDOWS_FILES)
    const UTF16      *pFileName;
#elif defined(UNIX_FILES)
    UTF8             *pFileName;
#endif // UNIX_FILES
    bool             bLoaded;
    ModuleState      eState;
};

static Module g_MainModule;
static Module *g_pModule = nullptr;
static std::map<MUX_CID, Module *> g_ModulesByClass;
static std::map<const UTF8 *, Module *, ltstr> g_ModulesByName;
static std::map<MUX_IID, MUX_INTERFACE_INFO *> g_Interfaces;

static PipePump   *g_fpPipePump = nullptr;
static QUEUE_INFO *g_pQueue_In  = nullptr;
static QUEUE_INFO *g_pQueue_Out = nullptr;

static std::map<UINT32, CHANNEL_INFO *> g_Channels;
static UINT32 nNextChannel;

static LibraryState    g_LibraryState   = eLibraryDown;
static process_context g_ProcessContext = IsUninitialized;

// TODO: The uniqueness tests are probably too strong.  It may be desireable
// for several modules to offer an implementation for the same classes.  If
// so, the conflict could be determined by the order the modules are loaded.
//
// This opens the problem of whether the second-priority implementation of a
// class begins to appear if the first-priority implementation de-registers
// itself.
//
// For now, these extra tests may catch bugs, and we'll punt on these
// interesting questions.
//

/*! \brief Make private copy of string.
 *
 * \param  UTF8[]   String to copy.
 * \return          Pointer to private copy of string.
 */

static UTF8 *CopyUTF8(const UTF8 *pString)
{
    size_t n = strlen((const char *)pString);
    UTF8 *p = nullptr;

    try
    {
        p = new UTF8[n+1];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr != p)
    {
        memcpy(p, pString, n+1);
    }
    return p;
}

#if defined(WINDOWS_FILES)
static UTF16 *CopyUTF16(const UTF16 *pString)
{
    size_t n = wcslen(pString);
    UTF16 *p = nullptr;

    try
    {
        p = new UTF16[n+1];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr != p)
    {
        memcpy(p, pString, (n+1) * sizeof(UTF16));
    }
    return p;
}
#endif // WINDOWS_FILES

#define MINIMUM_SIZE 8

static UINT32 GrowByFactor(UINT32 i)
{
    if (i < MINIMUM_SIZE)
    {
        return MINIMUM_SIZE;
    }
    else
    {
        return 2*i;
    }
}

/*! \brief Adds a module.
 *
 * \param aModuleName[]  Filename of Module
 * \return               Module context record, nullptr if out of memory or
 *                       duplicate found.
 */

#if defined(WINDOWS_FILES)
static Module *ModuleAdd(const UTF8 aModuleName[], const UTF16 aFileName[])
#elif defined(UNIX_FILES)
static Module *ModuleAdd(const UTF8 aModuleName[], const UTF8 aFileName[])
#endif // UNIX_FILES
{
    // If the module name is already being used, we won't add it again.
    //
    if (g_ModulesByName.end() == g_ModulesByName.find(aModuleName))
    {
        // Ensure that enough room is available to append a new Module.
        //
        Module *pModule = nullptr;
        try
        {
            pModule = new Module;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pModule)
        {
            return nullptr;
        }

        // Fill in new Module
        //
        pModule->fpGetClassObject = nullptr;
        pModule->fpCanUnloadNow = nullptr;
        pModule->fpRegister = nullptr;
        pModule->fpUnregister = nullptr;
        pModule->hInst = nullptr;
        pModule->pModuleName = CopyUTF8(aModuleName);
#if defined(WINDOWS_FILES)
        pModule->pFileName = CopyUTF16(aFileName);
#elif defined(UNIX_FILES)
        pModule->pFileName = CopyUTF8(aFileName);
#endif // UNIX_FILES
        pModule->bLoaded = false;
        pModule->eState  = eModuleInitialized;

        if (  nullptr != pModule->pModuleName
           && nullptr != pModule->pFileName)
        {
            g_ModulesByName[pModule->pModuleName] = pModule;
            return pModule;
        }
        else
        {
            // Clean up after failing to copy string.
            //
            if (nullptr != pModule->pModuleName)
            {
                delete [] pModule->pModuleName;
                pModule->pModuleName = nullptr;
            }

            if (nullptr != pModule->pFileName)
            {
                delete [] pModule->pFileName;
                pModule->pFileName = nullptr;
            }

            delete pModule;
        }
    }
    return nullptr;
}

/*! \brief Removes a module from the module table.
 *
 * \param pModule      Module context record to remove and destroy.
 */

static void ModuleRemove(Module *pModule)
{
    std::map<const UTF8 *, Module *, ltstr>::iterator it1 = g_ModulesByName.begin();
    while (g_ModulesByName.end() != it1)
    {
        if (it1->second == pModule)
        {
            g_ModulesByName.erase(it1++);
        }
        else
        {
            ++it1;
        }
    }

    std::map<MUX_CID, Module *>::iterator it2 = g_ModulesByClass.begin();
    while (g_ModulesByClass.end() != it2)
    {
        if (it2->second == pModule)
        {
            g_ModulesByClass.erase(it2++);
        }
        else
        {
            ++it2;
        }
    }

    // Free associated memory.
    //
    if (nullptr != pModule->pModuleName)
    {
        delete [] pModule->pModuleName;
        pModule->pModuleName = nullptr;
    }

    if (nullptr != pModule->pFileName)
    {
        delete [] pModule->pFileName;
        pModule->pFileName = nullptr;
    }

    delete pModule;
}

/*! \brief Loads a known module.
 *
 * \param pModule   Module context record.
 */

static void ModuleLoad(Module *pModule)
{
    if (  pModule->bLoaded
       || eModuleUnloadable == pModule->eState)
    {
        // Module is already in loaded state or it is unloadable.
        //
        return;
    }

    pModule->hInst = MOD_OPEN(pModule->pFileName);
    if (nullptr != pModule->hInst)
    {
        pModule->fpGetClassObject = (FPGETCLASSOBJECT *)MOD_SYM(pModule->hInst, "mux_GetClassObject");
        pModule->fpCanUnloadNow   = (FPCANUNLOADNOW *)MOD_SYM(pModule->hInst, "mux_CanUnloadNow");
        pModule->fpRegister       = (FPREGISTER *)MOD_SYM(pModule->hInst, "mux_Register");
        pModule->fpUnregister     = (FPUNREGISTER *)MOD_SYM(pModule->hInst, "mux_Unregister");

        if (  nullptr != pModule->fpGetClassObject
           && nullptr != pModule->fpCanUnloadNow
           && nullptr != pModule->fpRegister
           && nullptr != pModule->fpUnregister)
        {
            pModule->bLoaded = true;
        }
        else
        {
            pModule->fpGetClassObject = nullptr;
            pModule->fpCanUnloadNow   = nullptr;
            pModule->fpRegister       = nullptr;
            pModule->fpUnregister     = nullptr;
            MOD_CLOSE(pModule->hInst);
            pModule->eState = eModuleUnloadable;
        }
    }
    else
    {
        pModule->eState = eModuleUnloadable;
    }
}

/*! \brief Unloads a known module.
 *
 * \param pModule   Module context record.
 */

static void ModuleUnload(Module *pModule)
{
    if (pModule->bLoaded)
    {
        MOD_CLOSE(pModule->hInst);
        pModule->hInst = nullptr;
        pModule->fpGetClassObject = nullptr;
        pModule->fpCanUnloadNow = nullptr;
        pModule->fpRegister = nullptr;
        pModule->fpUnregister = nullptr;
        pModule->bLoaded = false;
    }
}

/*! \brief Creates an instance of the given class with the given interface.
 *
 * \param  cid   Class ID
 * \param  iid   Interface ID
 * \return       MUX_RESULT
 */

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_CreateInstance(MUX_CID cid, mux_IUnknown *pUnknownOuter, create_context ctx, MUX_IID iid, void **ppv)
{
    if (eLibraryInitialized != g_LibraryState)
    {
        return MUX_E_NOTREADY;
    }

    MUX_RESULT mr = MUX_S_OK;

    if (  (UseSameProcess & ctx)
       || (  IsMainProcess == g_ProcessContext
          && (UseMainProcess & ctx))
       || (  IsSlaveProcess == g_ProcessContext
          && (UseSlaveProcess & ctx)))
    {
        // In-proc component.
        //
        Module *pModule = nullptr;
        std::map<MUX_CID, Module *>::iterator it = g_ModulesByClass.find(cid);
        if (g_ModulesByClass.end() != it && nullptr != (pModule = it->second))
        {
            if (pModule == &g_MainModule)
            {
                if (nullptr == pModule->fpGetClassObject)
                {
                    mr = MUX_E_CLASSNOTAVAILABLE;
                }
            }
            else if (!pModule->bLoaded)
            {
                ModuleLoad(pModule);
                if (!pModule->bLoaded)
                {
                    mr = MUX_E_CLASSNOTAVAILABLE;
                }
            }

            if (MUX_SUCCEEDED(mr))
            {
                mux_IClassFactory *pIClassFactory = nullptr;
                mr = pModule->fpGetClassObject(cid, mux_IID_IClassFactory, (void **)&pIClassFactory);
                if (  MUX_SUCCEEDED(mr)
                   && nullptr != pIClassFactory)
                {
                    mr = pIClassFactory->CreateInstance(pUnknownOuter, iid, ppv);
                    pIClassFactory->Release();
                }
            }
        }
        else
        {
            mr = MUX_E_CLASSNOTAVAILABLE;
        }
    }
    else if (nullptr != g_fpPipePump)
    {
        // Out-of-Proc component.
        //
        // 1. Send cid and iid to a priori endpoint on the other side and
        //    block until the other side responds with a return frame.
        //
        QUEUE_INFO qiFrame;

        Pipe_InitializeQueueInfo(&qiFrame);
        Pipe_AppendBytes(&qiFrame, sizeof(cid), (UINT8*)(&cid));
        Pipe_AppendBytes(&qiFrame, sizeof(iid), (UINT8*)(&iid));

        mr = Pipe_SendCallPacketAndWait(0, &qiFrame);

        if (MUX_SUCCEEDED(mr))
        {
            mr = mux_UnmarshalInterface(&qiFrame, iid, ppv);
        }

        Pipe_EmptyQueue(&qiFrame);
    }
    else
    {
        mr = MUX_E_CLASSNOTAVAILABLE;
    }
    return mr;
}

/*! \brief Register class ids and factory implemented by the process binary.
 *
 * Modules must pass nullptr for pfGetClassObject, but the main program (netmux
 * or stubslave) must pass a non-nullptr pfGetClassObject.  For modules, the
 * class factory is obtained by using the mux_GetClassObject export.
 *
 * \param nci                  Number of components to register.
 * \param aci                  Table of component-related info.
 * \param fpGetClassObject     Pointer to Factory capable of creating
 *                             instances of the given components.
 * \return                     MUX_RESULT
 */

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RegisterClassObjects(int nci, MUX_CLASS_INFO aci[], FPGETCLASSOBJECT *fpGetClassObject)
{
    if (eLibraryInitialized != g_LibraryState)
    {
        return MUX_E_NOTREADY;
    }

    if (  nci <= 0
       || nullptr == aci)
    {
        return MUX_E_INVALIDARG;
    }

    // Modules export a mux_GetClassObject handler, but the main program
    // (netmux or stubslave) must pass its handler in here. Also, it doesn't
    // make sense to load and unload netmux.  But, we want to allow the main
    // program to provide module interfaces, so some special-casing is done to
    // allow that.
    //
    if (  (  nullptr != g_pModule
          && nullptr != fpGetClassObject)
       || (  nullptr == g_pModule
          && nullptr == fpGetClassObject))
    {
        return MUX_E_INVALIDARG;
    }

    // Verify that the requested class ids are not already registered.
    //
    int i;
    for (i = 0; i < nci; i++)
    {
        std::map<MUX_CID, Module *>::iterator it = g_ModulesByClass.find(aci[i].cid);
        if (g_ModulesByClass.end() != it)
        {
            return MUX_E_INVALIDARG;
        }
    }

    // Find corresponding Module. Since we're the one that requested the module to register its classes, we know
    // which module is registering.
    //
    Module *pModule = g_pModule;
    if (nullptr == pModule)
    {
        // These classes are implemented in the main program (netmux or
        // stubslave).
        //
        pModule = &g_MainModule;
        if (nullptr != pModule->fpGetClassObject)
        {
            // The main program is attempting to register another handler.
            //
            return MUX_E_FAIL;
        }
    }

    // If these classes are implemented in the main program (netmux or
    // stubslave), save the private GetClassObject method.
    //
    if (&g_MainModule == pModule)
    {
        pModule->fpGetClassObject = fpGetClassObject;
    }

    for (i = 0; i < nci; i++)
    {
        MUX_CLASS_INFO *ci = &aci[i];
        std::map<MUX_CID, Module *>::iterator it = g_ModulesByClass.find(ci->cid);
        if (g_ModulesByClass.end() == it)
        {
            g_ModulesByClass[ci->cid] = pModule;
        }
    }
    return MUX_S_OK;
}

/*! \brief De-register class ids and possibly the handler implemented by the
 *         process binary.
 *
 * \param nci    Number of components to revoke
 * \param aci    Table of component-related info.
 * \return       MUX_RESULT
 */

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RevokeClassObjects(int nci, MUX_CLASS_INFO aci[])
{
    if (eLibraryDown == g_LibraryState)
    {
        return MUX_E_NOTREADY;
    }

    if (  nci <= 0
       || nullptr == aci)
    {
        return MUX_E_INVALIDARG;
    }

    // Verify that all class ids in this request are handled by the same module.
    //
    Module *pModule = nullptr;
    int i;
    for (i = 0; i < nci; i++)
    {
        Module *q = nullptr;
        std::map<MUX_CID, Module *>::iterator it = g_ModulesByClass.find(aci[i].cid);
        if (g_ModulesByClass.end() == it || nullptr == (q = it->second))
        {
            // Attempt to revoke a class ids which were never registered.
            //
            return MUX_E_INVALIDARG;
        }
        else if (nullptr == pModule)
        {
            pModule = q;
        }
        else if (q != pModule)
        {
            // Attempt to revoke class ids from more than one module.
            //
            return MUX_E_INVALIDARG;
        }
    }

    // If these classes are implemented by the main program (netmux or
    // stubslave), we need to clear the handler as well.
    //
    if (pModule == &g_MainModule)
    {
        pModule->fpGetClassObject = nullptr;
    }

    // Remove the requested class ids.
    //
    for (i = 0; i < nci; i++)
    {
        g_ModulesByClass.erase(aci[i].cid);
    }
    return MUX_S_OK;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RegisterInterfaces(int nii, MUX_INTERFACE_INFO aii[])
{
    if (eLibraryInitialized != g_LibraryState)
    {
        return MUX_E_NOTREADY;
    }

    if (  nii <= 0
       || nullptr == aii)
    {
        return MUX_E_INVALIDARG;
    }

    for (int i = 0; i < nii; i++)
    {
        MUX_INTERFACE_INFO *pii = &aii[i];
        if (g_Interfaces.end() == g_Interfaces.find(pii->iid))
        {
            g_Interfaces[pii->iid] = pii;
        }
    }
    return MUX_S_OK;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RevokeInterfaces(int nii, MUX_INTERFACE_INFO aii[])
{
    if (eLibraryDown == g_LibraryState)
    {
        return MUX_E_NOTREADY;
    }

    if (  nii <= 0
       || nullptr == aii)
    {
        return MUX_E_INVALIDARG;
    }

    for (int i = 0; i < nii; i++)
    {
        g_Interfaces.erase(aii[i].iid);
    }
    return MUX_S_OK;
}

/*! \brief Add module to the set of available modules.
 *
 * Modules do not use this.
 *
 * \param UTF8[]   Module @list Name
 * \param UTF8[]   Module File Name
 * \return         MUX_RESULT
 */

#if defined(WINDOWS_FILES)
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_AddModule(const UTF8 aModuleName[], const UTF16 aFileName[])
#elif defined(UNIX_FILES)
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_AddModule(const UTF8 aModuleName[], const UTF8 aFileName[])
#endif // UNIX_FILES
{
    if (eLibraryInitialized != g_LibraryState)
    {
        return MUX_E_NOTREADY;
    }

    MUX_RESULT mr = MUX_S_OK;
    if (nullptr == g_pModule)
    {
        // Create new Module.
        //
        Module *pModule = ModuleAdd(aModuleName, aFileName);
        if (nullptr != pModule)
        {
            // Ask module to register its classes.
            //
            ModuleLoad(pModule);
            if (pModule->bLoaded)
            {
                pModule->eState = eModuleRegistering;
                g_pModule = pModule;
                mr = pModule->fpRegister();
                g_pModule = nullptr;
                pModule->eState = eModuleRegistered;
            }
            else
            {
                mr = MUX_E_FAIL;
            }
        }
        else
        {
            mr = MUX_E_OUTOFMEMORY;
        }
    }
    else
    {
        mr = MUX_E_NOTREADY;
    }
    return mr;
}

static MUX_RESULT RemoveModule(Module *pModule)
{
    MUX_RESULT mr = MUX_S_OK;

    if (nullptr != pModule)
    {
        // First, aim for an unregistered state.
        //
        if (eModuleRegistered == pModule->eState)
        {
            // It is possible for a module to be registered without it
            // necessarily being loaded, but we can't ask it to revoke its
            // class registrations without loading it.
            //
            if (!pModule->bLoaded)
            {
                ModuleLoad(pModule);
            }

            if (pModule->bLoaded)
            {
                // Ask module to revoke its classes.
                //
                pModule->eState = eModuleUnregistering;
                g_pModule = pModule;
                mr = pModule->fpUnregister();
                g_pModule = nullptr;
                pModule->eState = eModuleInitialized;
            }
            else
            {
                // Without being able to load the module, we're stuck.
                //
                pModule->eState = eModuleUnloadable;
            }
        }

        // Next, aim for an unloaded state.
        //
        if (pModule->bLoaded)
        {
            // Attempt to unload module.
            //
            mr = pModule->fpCanUnloadNow();
            if (  MUX_SUCCEEDED(mr)
               && MUX_S_FALSE != mr)
            {
                ModuleUnload(pModule);
                ModuleRemove(pModule);
                mr = MUX_S_OK;
            }
        }
    }
    else
    {
        mr = MUX_E_INVALIDARG;
    }
    return mr;
}

/*! \brief Remove module from the set of available modules.
 *
 * Modules do not use this.
 *
 * \param UTF8     Filename of dynamic module to remove.
 * \return         MUX_RESULT
 */

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RemoveModule(const UTF8 aModuleName[])
{
    if (eLibraryDown == g_LibraryState)
    {
        return MUX_E_NOTREADY;
    }

    MUX_RESULT mr MUX_S_OK;
    if (nullptr == g_pModule)
    {
        std::map<const UTF8 *, Module *, ltstr>::iterator it = g_ModulesByName.find(aModuleName);
        if (g_ModulesByName.end() != it)
        {
            Module *pModule = it->second;
            if (nullptr != pModule)
            {
                mr = RemoveModule(pModule);
            }
        }
    }
    else
    {
        mr = MUX_E_NOTREADY;
    }
    return mr;
}

/*! \brief Return information about a particular module.
 *
 * Modules do not use this.  Notice that the main program module (netmux or
 * stubslave) is not included.
 *
 * \param UTF8     Filename of dynamic module to remove.
 * \param void **  External module info structure.
 * \return         MUX_S_OK if found, MUX_S_FALSE if at end of list,
 *                 MUX_E_INVALIDARG for invalid arguments.
 */

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_ModuleInfo(int iModule, MUX_MODULE_INFO *pModuleInfo)
{
    if (eLibraryDown == g_LibraryState)
    {
        return MUX_E_NOTREADY;
    }

    if (iModule < 0)
    {
        return MUX_E_INVALIDARG;
    }

    std::map<const UTF8 *, Module *, ltstr>::iterator it = g_ModulesByName.begin();
    while (g_ModulesByName.end() != it)
    {
        if (0 == iModule)
        {
            Module *pModule = it->second;
            pModuleInfo->bLoaded = pModule->bLoaded;
            pModuleInfo->pName   = pModule->pModuleName;
            return MUX_S_OK;
        }
        iModule--;
        ++it;
    }
    return MUX_S_FALSE;
}

/*! \brief Periodic service tick for modules.
 *
 * Modules do not use this.  Notice that the main program module (netmux or
 * stubslave) is not included.
 *
 * \return         MUX_RESULT
 */

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_ModuleMaintenance(void)
{
    if (eLibraryInitialized != g_LibraryState)
    {
        return MUX_E_NOTREADY;
    }

    // We can query each loaded module and unload the ones that are unloadable.
    //
    std::map<const UTF8 *, Module *, ltstr>::iterator it = g_ModulesByName.begin();
    while (g_ModulesByName.end() != it)
    {
        Module *pModule = it->second;
        if (pModule->bLoaded)
        {
            MUX_RESULT mr = pModule->fpCanUnloadNow();
            if (  MUX_SUCCEEDED(mr)
               && MUX_S_FALSE != mr)
            {
                ModuleUnload(pModule);
            }
        }
        ++it;
    }
    return MUX_S_OK;
}

static bool GrowChannels(void);

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_InitModuleLibrary(process_context ctx)
{
    if (eLibraryDown == g_LibraryState)
    {
        g_ProcessContext = ctx;
        g_fpPipePump = nullptr;
        g_pQueue_In  = nullptr;
        g_pQueue_Out = nullptr;
        g_LibraryState = eLibraryInitialized;
        return MUX_S_OK;
    }
    else
    {
        return MUX_E_FAIL;
    }
}

static MUX_RESULT Channel0_Call(CHANNEL_INFO *pci, QUEUE_INFO *pqi);

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_InitModuleLibraryPump(PipePump *fpPipePump, QUEUE_INFO *pQueue_In, QUEUE_INFO *pQueue_Out)
{
    if (  eLibraryInitialized == g_LibraryState
       && nullptr == g_fpPipePump
       && nullptr == g_pQueue_In
       && nullptr == g_pQueue_Out
       && nullptr != fpPipePump
       && nullptr != pQueue_In
       && nullptr != pQueue_Out)
    {
        CHANNEL_INFO *pci = nullptr;
        try
        {
            pci = new CHANNEL_INFO;
        }
        catch (...) {}

        if (nullptr != pci)
        {
            // Initialized Channel 0 is always allocated.
            //
            pci->pfCall     = Channel0_Call;
            pci->pfMsg      = nullptr;
            pci->pfDisc     = nullptr;
            pci->pInterface = nullptr;
            g_Channels[0] = pci;
            nNextChannel = 1;

            // Save pipepump callback and two queues.  Hosting process should
            // service queues when pipepump is called.
            //
            // The module library should deal with packets, call levels, and
            // clean disconnections.  The main program (stubslave or netmux)
            // can handle file descriptors, process spawning, and errors.
            //
            g_fpPipePump = fpPipePump;
            g_pQueue_In  = pQueue_In;
            g_pQueue_Out = pQueue_Out;

            return MUX_S_OK;
        }
    }
    return MUX_E_FAIL;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_FinalizeModuleLibrary(void)
{
    MUX_RESULT mr = MUX_S_OK;

    if (eLibraryInitialized == g_LibraryState)
    {
        g_LibraryState   = eLibraryGoingDown;

        // Give each module a chance to unregister.
        //
        bool bFound = false;
        std::map<const UTF8 *, Module *, ltstr>::iterator it;
        do
        {
            // Find a module in the eModuleRegistered state.  The list we use
            // is only valid until we call RemoveModule().
            //
            bFound = false;
            it = g_ModulesByName.begin();
            while (g_ModulesByName.end() != it)
            {
                Module *pModule = it->second;
                if (eModuleRegistered == pModule->eState)
                {
                    bFound = true;
                    mr = RemoveModule(pModule);
                    break;
                }
                ++it;
            }
        } while (bFound);

        // Attempt to unload the remaining modules politely.
        //
        it = g_ModulesByName.begin();
        while (g_ModulesByName.end() != it)
        {
            Module *pModule = it->second;
            if (pModule->bLoaded)
            {
                mr = pModule->fpCanUnloadNow();
                if (  MUX_SUCCEEDED(mr)
                   && MUX_S_FALSE != mr)
                {
                    ModuleUnload(pModule);
                }
            }
            ++it;
        }

        // If anything is left on the list, there is a bug in someone's code.
        // The server will shortly either shutdown or restart.  To avoid
        // leaking a handle, we will unload the module impolitely.
        //
        it = g_ModulesByName.begin();
        while (g_ModulesByName.end() != it)
        {
            Module *pModule = it->second;
            if (pModule->bLoaded)
            {
                ModuleUnload(pModule);
            }
            ++it;
        }

        g_LibraryState   = eLibraryDown;
        g_ProcessContext = IsUninitialized;
    }
    return MUX_S_OK;
}

class CStandardMarshaler : public mux_IMarshal
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

    CStandardMarshaler(MUX_IID riid, marshal_context ctx);
    virtual ~CStandardMarshaler();

private:
    UINT32          m_cRef;
    MUX_IID         m_riid;
    marshal_context m_ctx;
};

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_GetStandardMarshal(MUX_IID riid, mux_IUnknown *pIUnknown, marshal_context ctx, mux_IMarshal **ppMarshal)
{
    MUX_RESULT mr = MUX_S_OK;
    if (nullptr == pIUnknown)
    {
        mr = MUX_E_NOTIMPLEMENTED;
    }
    else
    {
        CStandardMarshaler *pMarshaler = nullptr;
        try
        {
            pMarshaler = new CStandardMarshaler(riid, ctx);
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pMarshaler)
        {
            mr = MUX_E_OUTOFMEMORY;
        }
        else
        {
            mr = pMarshaler->QueryInterface(mux_IID_IMarshal, (void **)ppMarshal);
            pMarshaler->Release();
        }
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, mux_IUnknown *pIUnknown, marshal_context ctx)
{
    MUX_RESULT mr = MUX_S_OK;

    mux_IMarshal *pIMarshal = nullptr;
    mr = pIUnknown->QueryInterface(mux_IID_IMarshal, (void **)&pIMarshal);
    if (MUX_FAILED(mr))
    {
        mr = mux_GetStandardMarshal(riid, pIUnknown, ctx, &pIMarshal);
    }

    if (MUX_SUCCEEDED(mr))
    {
        MUX_CID cidProxy = 0;
        mr = pIMarshal->GetUnmarshalClass(riid, ctx, &cidProxy);
        if (MUX_SUCCEEDED(mr))
        {
            Pipe_AppendBytes(pqi, sizeof(cidProxy), &cidProxy);
            mr = pIMarshal->MarshalInterface(pqi, riid, pIUnknown, ctx);
        }
        pIMarshal->Release();
    }
    else
    {
        Pipe_EmptyQueue(pqi);
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv)
{
    MUX_RESULT mr = MUX_S_OK;

    MUX_CID cidProxy = 0;
    size_t nWanted = sizeof(cidProxy);
    if (  Pipe_GetBytes(pqi, &nWanted, &cidProxy)
       && sizeof(cidProxy) == nWanted)
    {
        // Open an IMarshal interface on the given proxy and pass it the marshal packet.
        //
        mux_IMarshal *pIMarshal = nullptr;
        mr = mux_CreateInstance(cidProxy, nullptr, UseSameProcess, mux_IID_IMarshal, (void **)&pIMarshal);
        if (MUX_SUCCEEDED(mr))
        {
            mr = pIMarshal->UnmarshalInterface(pqi, riid, ppv);
            pIMarshal->Release();
        }
    }
    else
    {
        mr = MUX_E_CLASSNOTAVAILABLE;
    }
    return mr;
}

static MUX_RESULT Channel0_Call(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    UNUSED_PARAMETER(pci);

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
        // First, we create the requested component and obtain the requested interface.
        //
        mux_IUnknown *pIUnknown = nullptr;
        mr = mux_CreateInstance(CallFrame.cid, nullptr, UseSameProcess, mux_IID_IUnknown, (void **)&pIUnknown);
        if (MUX_SUCCEEDED(mr))
        {
            // Now that we have an interface pointer, we need to Marshal it into a packet and return it on pqi.
            //
            mr = mux_MarshalInterface(pqi, CallFrame.iid, pIUnknown, CrossProcess);
            pIUnknown->Release();
        }
    }
    else
    {
       mr = MUX_E_INVALIDARG;
    }

    if (MUX_FAILED(mr))
    {
       Pipe_EmptyQueue(pqi);
    }
    return mr;
}

extern "C" void DCL_EXPORT DCL_API Pipe_InitializeQueueInfo(QUEUE_INFO *pqi)
{
    pqi->pHead = nullptr;
    pqi->pTail = nullptr;
    pqi->nBytes = 0;
}

extern "C" PCHANNEL_INFO DCL_EXPORT DCL_API Pipe_AllocateChannel(FCALL *pfCall, FMSG *pfMsg, FDISC *pfDisc)
{
    CHANNEL_INFO *pci = nullptr;
    try
    {
        pci = new CHANNEL_INFO;
    }
    catch (...) {}
    if (nullptr != pci)
    {
        pci->nChannel   = nNextChannel++;
        pci->pfCall     = pfCall;
        pci->pfMsg      = pfMsg;
        pci->pfDisc     = pfDisc;
        pci->pInterface = nullptr;
        g_Channels[pci->nChannel] = pci;
    }
    return pci;
}

extern "C" void DCL_EXPORT DCL_API Pipe_FreeChannel(CHANNEL_INFO *pci)
{
    if (nullptr != pci)
    {
        g_Channels.erase(pci->nChannel);
        delete pci;
        pci = nullptr;
    }
}

extern "C" PCHANNEL_INFO DCL_EXPORT DCL_API Pipe_FindChannel(UINT32 nChannel)
{
    std::map<UINT32, CHANNEL_INFO *>::iterator it = g_Channels.find(nChannel);
    if (g_Channels.end() != it)
    {
        return it->second;
    }
    return nullptr;
}

extern "C" void DCL_EXPORT DCL_API Pipe_AppendBytes(QUEUE_INFO *pqi, size_t n, const void *p)
{
    if (  0 != n
       && nullptr != p)
    {
        // Continue copying data to the end of the queue until it is all consumed.
        //
        QUEUE_BLOCK *pBlock = nullptr;
        while (0 < n)
        {
            // We need an empty or partially filled QUEUE_BLOCK.
            //
            if (  nullptr == pqi->pTail
               || pqi->pTail->aBuffer + QUEUE_BLOCK_SIZE <= pqi->pTail->pBuffer + pqi->pTail->nBuffer)
            {
                // The last block is full or not there, so allocate a new QUEUE_BLOCK.
                //
                try
                {
                    pBlock = new QUEUE_BLOCK;
                }
                catch (...)
                {
                    ; // Nothing.
                }

                if (nullptr != pBlock)
                {
                    pBlock->pNext   = nullptr;
                    pBlock->pPrev   = nullptr;
                    pBlock->pBuffer = pBlock->aBuffer;
                    pBlock->nBuffer = 0;
                }
                else
                {
                    // TODO: Out of memory.
                    //
                    return;
                }

                // Append the newly allocated block to the end of the queue.
                //
                if (nullptr == pqi->pTail)
                {
                    pqi->pHead = pBlock;
                    pqi->pTail = pBlock;
                }
                else
                {
                    pBlock->pPrev = pqi->pTail;
                    pqi->pTail->pNext = pBlock;
                    pqi->pTail = pBlock;
                }
            }
            else
            {
                pBlock = pqi->pTail;
            }

            // Allocate space out of last QUEUE_BLOCK
            //
            char  *pFree = pBlock->pBuffer + pBlock->nBuffer;
            size_t nFree = QUEUE_BLOCK_SIZE - pBlock->nBuffer - (pBlock->pBuffer - pBlock->aBuffer);
            size_t nCopy = nFree;
            if (n < nCopy)
            {
                nCopy = n;
            }

            memcpy(pFree, p, nCopy);
            n -= nCopy;
            pBlock->nBuffer += nCopy;
            pqi->nBytes += nCopy;
        }
    }
}

extern "C" void DCL_EXPORT DCL_API Pipe_AppendQueue(QUEUE_INFO *pqiOut, QUEUE_INFO *pqiIn)
{
    if (  nullptr != pqiOut
       && nullptr != pqiIn)
    {
        QUEUE_BLOCK *pBlock = pqiIn->pHead;
        while (nullptr != pBlock)
        {
            Pipe_AppendBytes(pqiOut, pBlock->nBuffer, pBlock->pBuffer);

            QUEUE_BLOCK *qBlock = pBlock->pNext;
            delete pBlock;
            pBlock = qBlock;
        }

        pqiIn->pHead = nullptr;
        pqiIn->pTail = nullptr;
        pqiIn->nBytes = 0;
    }
}

extern "C" void DCL_EXPORT DCL_API Pipe_EmptyQueue(QUEUE_INFO *pqi)
{
    if (nullptr != pqi)
    {
        QUEUE_BLOCK *pBlock = pqi->pHead;

        // Free all the QUEUE_BLOCKs finally the owning QUEUE_INFO structure.
        //
        while (nullptr != pBlock)
        {
            QUEUE_BLOCK *qBlock = pBlock->pNext;
            delete pBlock;
            pBlock = qBlock;
        }

        pqi->pHead = nullptr;
        pqi->pTail = nullptr;
        pqi->nBytes = 0;
    }
}

extern "C" bool DCL_EXPORT DCL_API Pipe_GetByte(QUEUE_INFO *pqi, UINT8 ach[1])
{
    QUEUE_BLOCK *pBlock;

    if (  nullptr != pqi
       && nullptr != (pBlock = pqi->pHead))
    {
        // Advance over empty blocks.
        //
        while (  nullptr != pBlock
              && 0 == pBlock->nBuffer)
        {
            pqi->pHead = pBlock->pNext;
            if (nullptr == pqi->pHead)
            {
                pqi->pTail = nullptr;
            }
            delete pBlock;
            pBlock = pqi->pHead;
        }

        // If there is a block left on the list, it will have something.
        //
        if (nullptr != pBlock)
        {
            ach[0] = pBlock->pBuffer[0];
            pBlock->pBuffer++;
            pBlock->nBuffer--;
            pqi->nBytes--;

            return true;
        }
    }
    return false;
}

extern "C" bool DCL_EXPORT DCL_API Pipe_GetBytes(QUEUE_INFO *pqi, size_t *pn, void *pv)
{
    UINT8 *pch = (UINT8 *)pv;

    size_t nCopied = 0;
    QUEUE_BLOCK *pBlock;

    if (  nullptr != pqi
       && nullptr != pn)
    {
        size_t nWantedBytes = *pn;
        pBlock = pqi->pHead;
        while (  nullptr != pBlock
              && 0 < nWantedBytes)
        {
            // Advance over empty blocks.
            //
            while (  nullptr != pBlock
                  && 0 == pBlock->nBuffer)
            {
                pqi->pHead = pBlock->pNext;
                if (nullptr == pqi->pHead)
                {
                    pqi->pTail = nullptr;
                }
                delete pBlock;
                pBlock = pqi->pHead;
            }

            // If there is a block left on the list, it will have something.
            //
            if (nullptr != pBlock)
            {
                size_t nCopy = pBlock->nBuffer;
                if (nWantedBytes < nCopy)
                {
                    nCopy = nWantedBytes;
                }
                memcpy(pch, pBlock->pBuffer, nCopy);

                pBlock->pBuffer += nCopy;
                pBlock->nBuffer -= nCopy;
                pqi->nBytes -= nCopy;
                nWantedBytes -= nCopy;
                pch += nCopy;
                nCopied += nCopy;
            }
        }

        *pn = nCopied;
        return true;
    }
    return false;
}

extern "C" size_t DCL_EXPORT DCL_API Pipe_QueueLength(QUEUE_INFO *pqi)
{
    size_t n = 0;
    if (nullptr != pqi)
    {
        n = pqi->nBytes;
    }
    return n;
}

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
int           g_iState = 0;
FrameType     g_eType = eUnknown;

union LENGTH
{
    UINT32 n;
    UINT8 ch[4];
} Length = { 0 };

UINT32        g_nChannel = 0;
size_t        g_nLengthRemaining = 0;

const UINT8 CallMagic[4]   = { 0xC3, 0x9B, 0x71, 0xF9 };  // 17, 14,  9, 20
const UINT8 ReturnMagic[4] = { 0x35, 0x97, 0x2D, 0xD0 };  //  7, 13,  6, 18
const UINT8 MsgMagic[4]    = { 0xF6, 0x9E, 0x18, 0x36 };  // 19, 15,  3,  8
const UINT8 DiscMagic[4]   = { 0x96, 0x0A, 0xA3, 0x81 };  // 12,  1, 16, 10
const UINT8 EndMagic[4]    = { 0x27, 0x11, 0x8B, 0x26 };  //  5,  2, 11,  4

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
extern "C" bool DCL_EXPORT DCL_API Pipe_DecodeFrames(UINT32 iReturnChannel, QUEUE_INFO *pqiFrame)
{
    UINT8 buffer[512];

    if (8 == g_iState)
    {
        // We must remain in the Length3 state until we have consumed all of the expected data.
        //
        while (0 < g_nLengthRemaining)
        {
            size_t nWanted = g_nLengthRemaining;
            if (sizeof(buffer) < nWanted)
            {
                nWanted = sizeof(buffer);
            }

            if (  !Pipe_GetBytes(g_pQueue_In, &nWanted, buffer)
               || 0 == nWanted)
            {
                return false;
            }
            Pipe_AppendBytes(pqiFrame, nWanted, buffer);
            g_nLengthRemaining -= nWanted;
        }
    }

    UINT8 ch;
    while (Pipe_GetByte(g_pQueue_In, &ch))
    {
        g_iState = decoder_stt[g_iState][decoder_itt[ch]];
        switch (g_iState)
        {
        case 3: // Call2
            g_eType = eCall;
            break;

        case 16: // Return2
            g_eType = eReturn;
            break;

        case 19: // Msg2
            g_eType = eMessage;
            break;

        case 22: // Disc2
            g_eType = eDisconnect;
            break;

        case 5: // Length0
            Length.ch[0] = ch;
            break;

        case 6: // Length1
            Length.ch[1] = ch;
            break;

        case 7: // Length2
            Length.ch[2] = ch;
            break;

        case 8: // Length3
            Length.ch[3] = ch;
            g_nLengthRemaining = Length.n;

            // We've been told how long to expect the packet to be.
            //
            while (0 < g_nLengthRemaining)
            {
                size_t nWanted = g_nLengthRemaining;
                if (sizeof(buffer) < nWanted)
                {
                    nWanted = sizeof(buffer);
                }

                if (  !Pipe_GetBytes(g_pQueue_In, &nWanted, buffer)
                   || 0 == nWanted)
                {
                    // We'll leave the state machine and try to pick up again at the same place later.
                    //
                    return false;
                }
                Pipe_AppendBytes(pqiFrame, nWanted, buffer);
                g_nLengthRemaining -= nWanted;
            }
            break;

        case 12: // Cleanup

            // Something went wrong. Re-initialize all the decoding variables.
            //
            g_eType   = eUnknown;
            Length.n = 0;
            g_nChannel = 0;
            Pipe_EmptyQueue(pqiFrame);
            break;

        case 13: // Accept
            if (4 <= Length.n)
            {
                size_t nWanted = sizeof(g_nChannel);
                if (  Pipe_GetBytes(pqiFrame, &nWanted, &g_nChannel)
                   && nWanted == sizeof(g_nChannel))
                {
                    if (eReturn == g_eType)
                    {
                        if (g_nChannel == iReturnChannel)
                        {
                            g_eType    = eUnknown;
                            Length.n   = 0;
                            g_nChannel = 0;
                            return true;
                        }
                        else
                        {
                            // TODO: Bad.
                            //
                            break;
                        }
                    }
                    else
                    {
                        std::map<UINT32, CHANNEL_INFO *>::iterator it = g_Channels.find(g_nChannel);
                        PCHANNEL_INFO pci;
                        if (g_Channels.end() != it && nullptr != (pci = it->second))
                        {
                            switch (g_eType)
                            {
                            case eCall:
                                if (nullptr != pci->pfCall)
                                {
                                    MUX_RESULT mr = pci->pfCall(pci, pqiFrame);
                                    if (MUX_FAILED(mr))
                                    {
                                        Pipe_EmptyQueue(pqiFrame);
                                    }

                                    // Send Queue_Frame back to sender.
                                    //
                                    UINT32 nReturn = (UINT32)(sizeof(g_nChannel) + Pipe_QueueLength(pqiFrame));

                                    Pipe_AppendBytes(g_pQueue_Out, sizeof(ReturnMagic), ReturnMagic);
                                    Pipe_AppendBytes(g_pQueue_Out, sizeof(nReturn), &nReturn);
                                    Pipe_AppendBytes(g_pQueue_Out, sizeof(g_nChannel), &g_nChannel);
                                    Pipe_AppendQueue(g_pQueue_Out, pqiFrame);
                                    Pipe_AppendBytes(g_pQueue_Out, sizeof(EndMagic), EndMagic);
                                }
                                break;

                            case eMessage:
                                if (nullptr != pci->pfMsg)
                                {
                                    pci->pfMsg(pci, pqiFrame);
                                }
                                break;

                            case eDisconnect:
                                if (nullptr != pci->pfDisc)
                                {
                                    pci->pfDisc(pci, pqiFrame);
                                }
                                break;

                            default:
                                break;
                            }
                        }
                    }
                }
            }

            // The packet was too short to contain a channel number, the
            // channel did not exist, or the call completed successfully.
            //
            g_eType    = eUnknown;
            Length.n   = 0;
            g_nChannel = 0;
            Pipe_EmptyQueue(pqiFrame);
            break;
        }
    }
    return false;
}

static MUX_RESULT Pipe_SendReceive(UINT32 iReturnChannel, QUEUE_INFO *pqi)
{
    MUX_RESULT mr = MUX_S_OK;
    for (;;)
    {
        mr = g_fpPipePump();
        if (  MUX_FAILED(mr)
           || Pipe_DecodeFrames(iReturnChannel, pqi))
        {
            break;
        }
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API Pipe_SendCallPacketAndWait(UINT32 iReturnChannel, QUEUE_INFO *pqiFrame)
{
    UINT32 nLength = (UINT32)(sizeof(iReturnChannel) + Pipe_QueueLength(pqiFrame));
    Pipe_AppendBytes(g_pQueue_Out, sizeof(CallMagic), CallMagic);
    Pipe_AppendBytes(g_pQueue_Out, sizeof(nLength), &nLength);
    Pipe_AppendBytes(g_pQueue_Out, sizeof(iReturnChannel), &iReturnChannel);
    Pipe_AppendQueue(g_pQueue_Out, pqiFrame);
    Pipe_AppendBytes(g_pQueue_Out, sizeof(EndMagic), EndMagic);
    return Pipe_SendReceive(iReturnChannel, pqiFrame);
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API Pipe_SendMsgPacket(UINT32 iReturnChannel, QUEUE_INFO *pqiFrame)
{
    UINT32 nLength = (UINT32)(sizeof(iReturnChannel) + Pipe_QueueLength(pqiFrame));
    Pipe_AppendBytes(g_pQueue_Out, sizeof(MsgMagic), MsgMagic);
    Pipe_AppendBytes(g_pQueue_Out, sizeof(nLength), &nLength);
    Pipe_AppendBytes(g_pQueue_Out, sizeof(iReturnChannel), &iReturnChannel);
    Pipe_AppendQueue(g_pQueue_Out, pqiFrame);
    Pipe_AppendBytes(g_pQueue_Out, sizeof(EndMagic), EndMagic);
    return MUX_S_OK;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API Pipe_SendDiscPacket(UINT32 iReturnChannel, QUEUE_INFO *pqiFrame)
{
    UINT32 nLength = (UINT32)(sizeof(iReturnChannel) + Pipe_QueueLength(pqiFrame));
    Pipe_AppendBytes(g_pQueue_Out, sizeof(DiscMagic), DiscMagic);
    Pipe_AppendBytes(g_pQueue_Out, sizeof(nLength), &nLength);
    Pipe_AppendBytes(g_pQueue_Out, sizeof(iReturnChannel), &iReturnChannel);
    Pipe_AppendQueue(g_pQueue_Out, pqiFrame);
    Pipe_AppendBytes(g_pQueue_Out, sizeof(EndMagic), EndMagic);
    return MUX_S_OK;
}

// Standard Marshaler which is not directly accessible.
//
CStandardMarshaler::CStandardMarshaler(MUX_IID riid, marshal_context ctx) : m_cRef(1)
{
    m_riid = riid;
    m_ctx = ctx;
}

CStandardMarshaler::~CStandardMarshaler()
{
}

MUX_RESULT CStandardMarshaler::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IMarshal *>(this);
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

UINT32 CStandardMarshaler::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CStandardMarshaler::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CStandardMarshaler::GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid)
{
    *pcid = mux_CID_StandardMarshaler;
    return MUX_S_OK;
}

static MUX_RESULT CStd_Call(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    mux_IRpcStubBuffer *pIRpcStubBuffer = static_cast<mux_IRpcStubBuffer *>(pci->pInterface);
    if (nullptr == pIRpcStubBuffer)
    {
        return MUX_E_NOINTERFACE;
    }
    return pIRpcStubBuffer->Invoke(pqi);
}

static MUX_RESULT CStd_Disconnect(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    mux_IRpcStubBuffer *pIRpcStubBuffer = static_cast<mux_IRpcStubBuffer *>(pci->pInterface);
    if (nullptr == pIRpcStubBuffer)
    {
        return MUX_E_NOINTERFACE;
    }

    pIRpcStubBuffer->Disconnect();
    pIRpcStubBuffer->Release();
    pci->pInterface = nullptr;
    Pipe_FreeChannel(pci);
    return MUX_S_OK;
}

MUX_RESULT CStandardMarshaler::MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void *pv, marshal_context ctx)
{
    MUX_RESULT mr = MUX_S_OK;
    mux_IUnknown *pIUnknown = static_cast<mux_IUnknown *>(pv);

    std::map<MUX_IID, MUX_INTERFACE_INFO *>::iterator it = g_Interfaces.find(riid);
    if (g_Interfaces.end() != it)
    {
        MUX_CID cidProxyStub = it->second->cidProxyStub;
        mux_IPSFactoryBuffer *pIPSFactoryBuffer = nullptr;
        mr = mux_CreateInstance(cidProxyStub, nullptr, UseSameProcess, mux_IID_IPSFactoryBuffer, (void **)&pIPSFactoryBuffer);
        if (MUX_SUCCEEDED(mr))
        {
            mux_IRpcStubBuffer *pIRpcStubBuffer = nullptr;
            mr = pIPSFactoryBuffer->CreateStub(riid, nullptr, &pIRpcStubBuffer);
            pIPSFactoryBuffer->Release();
            if (MUX_SUCCEEDED(mr))
            {
                mr = pIRpcStubBuffer->Connect(pIUnknown);
                if (MUX_SUCCEEDED(mr))
                {
                    CHANNEL_INFO *pChannel = Pipe_AllocateChannel(CStd_Call, nullptr, CStd_Disconnect);
                    if (nullptr != pChannel)
                    {
                        pChannel->pInterface = pIRpcStubBuffer;

                        Pipe_AppendBytes(pqi, sizeof(riid), &riid);
                        Pipe_AppendBytes(pqi, sizeof(pChannel->nChannel), (UTF8*)(&pChannel->nChannel));
                    }
                    else
                    {
                        pIRpcStubBuffer->Disconnect();
                        pIRpcStubBuffer->Release();
                        mr = MUX_E_OUTOFMEMORY;
                    }
                }
                else
                {
                    pIRpcStubBuffer->Release();
                    mr = MUX_E_NOINTERFACE;
                }
            }
            else
            {
                mr = MUX_E_NOINTERFACE;
            }
        }
        else
        {
            mr = MUX_E_NOINTERFACE;
        }
    }
    else
    {
        mr = MUX_E_NOINTERFACE;
    }
    return mr;
}

MUX_RESULT CStandardMarshaler::UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv)
{
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CStandardMarshaler::ReleaseMarshalData(QUEUE_INFO *pqi)
{
    MUX_IID riid;
    size_t nWanted = sizeof(riid);
    if (  Pipe_GetBytes(pqi, &nWanted, &riid)
       && sizeof(riid) == nWanted)
    {
        UINT32 nChannel;
        nWanted = sizeof(nChannel);
        if (  Pipe_GetBytes(pqi, &nWanted, &nChannel)
           && sizeof(nChannel) == nWanted)
        {
            CHANNEL_INFO *pChannel = Pipe_FindChannel(nChannel);
            if (nullptr != pChannel)
            {
                CStd_Disconnect(pChannel, pqi);
            }
        }
    }
    return MUX_S_OK;
}

MUX_RESULT CStandardMarshaler::DisconnectObject(void)
{
    return MUX_S_OK;
}

#if defined(PRETEND_DYNALIB)

MODULE_HANDLE mux_dlopen(const char *m)
{
    UNUSED_PARAMETER(m);
    return nullptr;
}

void *mux_dlsym(MODULE_HANDLE h, const char *s)
{
    UNUSED_PARAMETER(h);
    UNUSED_PARAMETER(s);
    return nullptr;
}

void mux_dlclose(MODULE_HANDLE h)
{
    UNUSED_PARAMETER(h);
}

#endif // PRETEND_DYNALIB
