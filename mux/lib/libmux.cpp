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
#include "modules.h"

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
#define MOD_OPEN(m)  dlopen(reinterpret_cast<char *>(m), RTLD_LAZY)
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
        return strcmp(reinterpret_cast<const char *>(s1), reinterpret_cast<const char *>(s2)) < 0;
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

static Module g_AprioriModule;
static Module *g_pModule = nullptr;
static std::map<MUX_CID, Module *> g_ModulesByClass;
static std::map<const UTF8 *, Module *, ltstr> g_ModulesByName;
static std::map<MUX_IID, MUX_INTERFACE_INFO *> g_Interfaces;

static PipePump   *g_fpPipePump = nullptr;
static QUEUE_INFO *g_pQueue_In  = nullptr;
static QUEUE_INFO *g_pQueue_Out = nullptr;

static std::map<uint32_t, CHANNEL_INFO *> g_Channels;
static uint32_t nNextChannel;

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
    size_t n = strlen(reinterpret_cast<const char *>(pString));
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
    if (g_ModulesByName.end() != g_ModulesByName.find(aModuleName))
    {
        return nullptr;
    }
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
        pModule->fpGetClassObject = reinterpret_cast<FPGETCLASSOBJECT *>(MOD_SYM(pModule->hInst, "mux_GetClassObject"));
        pModule->fpCanUnloadNow   = reinterpret_cast<FPCANUNLOADNOW *>(MOD_SYM(pModule->hInst, "mux_CanUnloadNow"));
        pModule->fpRegister       = reinterpret_cast<FPREGISTER *>(MOD_SYM(pModule->hInst, "mux_Register"));
        pModule->fpUnregister     = reinterpret_cast<FPUNREGISTER *>(MOD_SYM(pModule->hInst, "mux_Unregister"));

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
        if (nullptr != pModule->hInst)
        {
            MOD_CLOSE(pModule->hInst);
        }
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
            if (pModule == &g_AprioriModule)
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
        Pipe_AppendBytes(&qiFrame, sizeof(cid), reinterpret_cast<uint8_t *>(&cid));
        Pipe_AppendBytes(&qiFrame, sizeof(iid), reinterpret_cast<uint8_t *>(&iid));

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
        pModule = &g_AprioriModule;
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
    if (&g_AprioriModule == pModule)
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
    if (pModule == &g_AprioriModule)
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

    MUX_RESULT mr = MUX_S_OK;
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

// libmux-internal class factory and interface registration.
//
static MUX_CLASS_INFO libmux_classes[] =
{
    { CID_SlaveControlPSFactory   },
    { CID_QuerySinkPSFactory      },
    { CID_QueryControlPSFactory   }
};
#define NUM_LIBMUX_CLASSES (sizeof(libmux_classes)/sizeof(libmux_classes[0]))

static MUX_INTERFACE_INFO libmux_interfaces[] =
{
    { IID_ISlaveControl,  CID_SlaveControlPSFactory   },
    { IID_IQuerySink,     CID_QuerySinkPSFactory      },
    { IID_IQueryControl,  CID_QueryControlPSFactory   }
};
#define NUM_LIBMUX_INTERFACES (sizeof(libmux_interfaces)/sizeof(libmux_interfaces[0]))

// libmux module entry points.  libmux.so registers itself as a Module so
// that its proxy/stub factory classes coexist cleanly with netmux's classes.
// The class factory is defined after the PSFactory classes below.
//
extern "C" MUX_RESULT DCL_API libmux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv);

static MUX_RESULT libmux_Register(void)
{
    MUX_RESULT mr = mux_RegisterClassObjects(NUM_LIBMUX_CLASSES, libmux_classes, nullptr);
    if (MUX_SUCCEEDED(mr))
    {
        mr = mux_RegisterInterfaces(NUM_LIBMUX_INTERFACES, libmux_interfaces);
    }
    return mr;
}

static MUX_RESULT libmux_Unregister(void)
{
    return mux_RevokeClassObjects(NUM_LIBMUX_CLASSES, libmux_classes);
}

static MUX_RESULT libmux_CanUnloadNow(void)
{
    // libmux can never truly unload while the process is running.
    //
    return MUX_S_OK;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_InitModuleLibrary(process_context ctx)
{
    if (eLibraryDown == g_LibraryState)
    {
        g_ProcessContext = ctx;
        g_fpPipePump = nullptr;
        g_pQueue_In  = nullptr;
        g_pQueue_Out = nullptr;
        g_LibraryState = eLibraryInitialized;

        // Register libmux as a Module.  It has its own proxy/stub factory
        // classes that need to coexist with netmux's classes.
        //
#if defined(WINDOWS_FILES)
        Module *pModule = ModuleAdd(T("libmux"), L"./bin/libmux.dll");
#elif defined(UNIX_FILES)
        Module *pModule = ModuleAdd(T("libmux"), T("./bin/libmux.so"));
#endif
        if (nullptr == pModule)
        {
            return MUX_E_OUTOFMEMORY;
        }

        // Open a handle to ourselves so that ModuleUnload can dlclose
        // it during teardown.  This increments our refcount (harmless —
        // netmux still holds the linker reference).
        //
        pModule->hInst = MOD_OPEN(pModule->pFileName);

        // Fill in the entry points directly — libmux knows its own
        // function pointers without needing dlopen/dlsym.
        //
        pModule->fpGetClassObject = libmux_GetClassObject;
        pModule->fpCanUnloadNow   = libmux_CanUnloadNow;
        pModule->fpRegister       = libmux_Register;
        pModule->fpUnregister     = libmux_Unregister;
        pModule->bLoaded = true;

        // Register libmux's classes through the normal module path.
        //
        pModule->eState = eModuleRegistering;
        g_pModule = pModule;
        MUX_RESULT mr = pModule->fpRegister();
        g_pModule = nullptr;
        pModule->eState = eModuleRegistered;

        return mr;
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
    virtual uint32_t     AddRef(void);
    virtual uint32_t     Release(void);

    // mux_IMarshal
    //
    virtual MUX_RESULT GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid);
    virtual MUX_RESULT MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void *pv, marshal_context ctx);
    virtual MUX_RESULT UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv);
    virtual MUX_RESULT ReleaseMarshalData(QUEUE_INFO *pqi);
    virtual MUX_RESULT DisconnectObject(void);

    CStandardMarshaler(void);
    CStandardMarshaler(MUX_IID riid, marshal_context ctx);
    virtual ~CStandardMarshaler();

private:
    uint32_t          m_cRef;
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
        mux_IMarshal *pIMarshal = nullptr;
        if (mux_CID_StandardMarshaler == cidProxy)
        {
            // The standard marshaler is internal to libmux and not registered
            // in the class table.  Create it directly.
            //
            try
            {
                pIMarshal = new CStandardMarshaler();
            }
            catch (...)
            {
                ; // Nothing.
            }

            if (nullptr == pIMarshal)
            {
                mr = MUX_E_OUTOFMEMORY;
            }
        }
        else
        {
            // Open an IMarshal interface on the given proxy and pass it
            // the marshal packet.
            //
            mr = mux_CreateInstance(cidProxy, nullptr, UseSameProcess, mux_IID_IMarshal, (void **)&pIMarshal);
        }

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

extern "C" PCHANNEL_INFO DCL_EXPORT DCL_API Pipe_FindChannel(uint32_t nChannel)
{
    std::map<uint32_t, CHANNEL_INFO *>::iterator it = g_Channels.find(nChannel);
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

extern "C" bool DCL_EXPORT DCL_API Pipe_GetByte(QUEUE_INFO *pqi, uint8_t ach[1])
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
    uint8_t *pch = reinterpret_cast<uint8_t *>(pv);

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

// Frame types for the pipe protocol.
//
// Wire format: Type[1] + Channel[4] + Length[4] + Payload[Length]
//
// Type identifies the frame kind.  Channel routes to the correct stub or
// blocked caller.  Length is the payload size (does not include the 9-byte
// header).  All multi-byte fields are native byte order (same machine).
//
#define FRAME_CALL    0  // Synchronous RPC.  Sender blocks for FRAME_RETURN.
#define FRAME_RETURN  1  // Response to FRAME_CALL, routed by channel.
#define FRAME_MSG     2  // Fire-and-forget.  Same dispatch as Call, no reply.
#define FRAME_DISC    3  // Disconnect.  Tears down the channel.

#define FRAME_HEADER_SIZE  9  // type(1) + channel(4) + length(4)
#define MAX_FRAME_PAYLOAD  (1024 * 1024)  // 1 MB limit per frame.

// Decoder state, persistent across calls for partial-read resumption.
//
static bool     g_bHaveHeader = false;
static uint8_t  g_FrameType = 0;
static uint32_t g_nChannel = 0;
static uint32_t g_nPayloadLength = 0;
static size_t   g_nPayloadRemaining = 0;

// Try to read exactly n bytes from the queue.  Returns true only if all
// n bytes were available.  On false, no bytes are consumed.
//
static bool Pipe_ReadExact(QUEUE_INFO *pqi, size_t n, void *pv)
{
    if (Pipe_QueueLength(pqi) < n)
    {
        return false;
    }
    size_t nGot = n;
    Pipe_GetBytes(pqi, &nGot, pv);
    return nGot == n;
}

// Decode one frame at a time from g_pQueue_In.
//
// Returns true when a FRAME_RETURN matching iReturnChannel has been received
// and its payload is in pqiFrame.  Other frame types are dispatched inline.
//
extern "C" bool DCL_EXPORT DCL_API Pipe_DecodeFrames(uint32_t iReturnChannel, QUEUE_INFO *pqiFrame)
{
    for (;;)
    {
        // Resume reading payload if we were interrupted mid-frame.
        //
        if (g_bHaveHeader && g_nPayloadRemaining > 0)
        {
            uint8_t buffer[512];
            while (0 < g_nPayloadRemaining)
            {
                size_t nWanted = g_nPayloadRemaining;
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
                g_nPayloadRemaining -= nWanted;
            }

            // Fall through to dispatch.
        }
        else if (!g_bHaveHeader)
        {
            // Read the 9-byte header atomically.
            //
            uint8_t header[FRAME_HEADER_SIZE];
            if (!Pipe_ReadExact(g_pQueue_In, FRAME_HEADER_SIZE, header))
            {
                return false;
            }

            g_FrameType = header[0];
            memcpy(&g_nChannel, header + 1, sizeof(g_nChannel));
            memcpy(&g_nPayloadLength, header + 5, sizeof(g_nPayloadLength));

            // Validate.
            //
            if (g_FrameType > FRAME_DISC || g_nPayloadLength > MAX_FRAME_PAYLOAD)
            {
                // Protocol error.  The pipe is broken.
                //
                g_bHaveHeader = false;
                return false;
            }

            g_bHaveHeader = true;
            g_nPayloadRemaining = g_nPayloadLength;

            // Read payload (may be zero-length).
            //
            uint8_t buffer[512];
            while (0 < g_nPayloadRemaining)
            {
                size_t nWanted = g_nPayloadRemaining;
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
                g_nPayloadRemaining -= nWanted;
            }
        }

        // Header and payload are complete.  Dispatch.
        //
        g_bHaveHeader = false;

        if (FRAME_RETURN == g_FrameType)
        {
            if (g_nChannel == iReturnChannel)
            {
                return true;
            }

            // Return for a channel we're not waiting on.  Discard.
            //
            Pipe_EmptyQueue(pqiFrame);
        }
        else
        {
            std::map<uint32_t, CHANNEL_INFO *>::iterator it = g_Channels.find(g_nChannel);
            PCHANNEL_INFO pci;
            if (g_Channels.end() != it && nullptr != (pci = it->second))
            {
                switch (g_FrameType)
                {
                case FRAME_CALL:
                    if (nullptr != pci->pfCall)
                    {
                        MUX_RESULT mr = pci->pfCall(pci, pqiFrame);
                        if (MUX_FAILED(mr))
                        {
                            Pipe_EmptyQueue(pqiFrame);
                        }

                        // Send response back to caller.
                        //
                        uint8_t type = FRAME_RETURN;
                        uint32_t nReturn = static_cast<uint32_t>(Pipe_QueueLength(pqiFrame));

                        Pipe_AppendBytes(g_pQueue_Out, 1, &type);
                        Pipe_AppendBytes(g_pQueue_Out, sizeof(g_nChannel), &g_nChannel);
                        Pipe_AppendBytes(g_pQueue_Out, sizeof(nReturn), &nReturn);
                        Pipe_AppendQueue(g_pQueue_Out, pqiFrame);
                    }
                    break;

                case FRAME_MSG:
                    if (nullptr != pci->pfMsg)
                    {
                        pci->pfMsg(pci, pqiFrame);
                    }
                    break;

                case FRAME_DISC:
                    if (nullptr != pci->pfDisc)
                    {
                        pci->pfDisc(pci, pqiFrame);
                    }
                    break;
                }
            }

            Pipe_EmptyQueue(pqiFrame);
        }
    }
}

static MUX_RESULT Pipe_SendReceive(uint32_t iReturnChannel, QUEUE_INFO *pqi)
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

static void Pipe_WriteHeader(uint8_t type, uint32_t nChannel, uint32_t nPayload)
{
    Pipe_AppendBytes(g_pQueue_Out, 1, &type);
    Pipe_AppendBytes(g_pQueue_Out, sizeof(nChannel), &nChannel);
    Pipe_AppendBytes(g_pQueue_Out, sizeof(nPayload), &nPayload);
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API Pipe_SendCallPacketAndWait(uint32_t nChannel, QUEUE_INFO *pqiFrame)
{
    uint32_t nPayload = static_cast<uint32_t>(Pipe_QueueLength(pqiFrame));
    Pipe_WriteHeader(FRAME_CALL, nChannel, nPayload);
    Pipe_AppendQueue(g_pQueue_Out, pqiFrame);
    return Pipe_SendReceive(nChannel, pqiFrame);
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API Pipe_SendMsgPacket(uint32_t nChannel, QUEUE_INFO *pqiFrame)
{
    uint32_t nPayload = static_cast<uint32_t>(Pipe_QueueLength(pqiFrame));
    Pipe_WriteHeader(FRAME_MSG, nChannel, nPayload);
    Pipe_AppendQueue(g_pQueue_Out, pqiFrame);
    return MUX_S_OK;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API Pipe_SendDiscPacket(uint32_t nChannel, QUEUE_INFO *pqiFrame)
{
    uint32_t nPayload = static_cast<uint32_t>(Pipe_QueueLength(pqiFrame));
    Pipe_WriteHeader(FRAME_DISC, nChannel, nPayload);
    Pipe_AppendQueue(g_pQueue_Out, pqiFrame);
    return MUX_S_OK;
}

// Marshaling helpers for proxy/stub implementations.
//
extern "C" void DCL_EXPORT DCL_API Marshal_PutString(QUEUE_INFO *pqi, const UTF8 *str)
{
    size_t n = (nullptr != str) ? strlen(reinterpret_cast<const char *>(str)) + 1 : 0;
    Pipe_AppendBytes(pqi, sizeof(n), &n);
    if (0 < n)
    {
        Pipe_AppendBytes(pqi, n, str);
    }
}

extern "C" bool DCL_EXPORT DCL_API Marshal_GetString(QUEUE_INFO *pqi, UTF8 *buf, size_t bufSize, const UTF8 **ppStr)
{
    size_t n;
    size_t nWanted = sizeof(n);
    if (  !Pipe_GetBytes(pqi, &nWanted, &n)
       || nWanted != sizeof(n))
    {
        return false;
    }

    if (0 == n)
    {
        *ppStr = nullptr;
        return true;
    }

    if (n > bufSize)
    {
        return false;
    }

    nWanted = n;
    if (  !Pipe_GetBytes(pqi, &nWanted, buf)
       || nWanted != n)
    {
        return false;
    }
    *ppStr = buf;
    return true;
}

extern "C" void DCL_EXPORT DCL_API Marshal_PutUInt32(QUEUE_INFO *pqi, uint32_t val)
{
    Pipe_AppendBytes(pqi, sizeof(val), &val);
}

extern "C" bool DCL_EXPORT DCL_API Marshal_GetUInt32(QUEUE_INFO *pqi, uint32_t *pval)
{
    size_t nWanted = sizeof(*pval);
    return Pipe_GetBytes(pqi, &nWanted, pval) && nWanted == sizeof(*pval);
}

extern "C" void DCL_EXPORT DCL_API Marshal_PutInt(QUEUE_INFO *pqi, int val)
{
    Pipe_AppendBytes(pqi, sizeof(val), &val);
}

extern "C" bool DCL_EXPORT DCL_API Marshal_GetInt(QUEUE_INFO *pqi, int *pval)
{
    size_t nWanted = sizeof(*pval);
    return Pipe_GetBytes(pqi, &nWanted, pval) && nWanted == sizeof(*pval);
}

// Standard Marshaler which is not directly accessible.
//
CStandardMarshaler::CStandardMarshaler(void) : m_cRef(1), m_riid(0), m_ctx(CrossProcess)
{
}

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

uint32_t CStandardMarshaler::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CStandardMarshaler::Release(void)
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
                    CHANNEL_INFO *pChannel = Pipe_AllocateChannel(CStd_Call, CStd_Call, CStd_Disconnect);
                    if (nullptr != pChannel)
                    {
                        pChannel->pInterface = pIRpcStubBuffer;

                        Pipe_AppendBytes(pqi, sizeof(riid), &riid);
                        Pipe_AppendBytes(pqi, sizeof(pChannel->nChannel), reinterpret_cast<UTF8 *>(&pChannel->nChannel));
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
    // The marshal data written by MarshalInterface contains: riid | nChannel.
    //
    MUX_IID riidStored;
    size_t nWanted = sizeof(riidStored);
    if (  !Pipe_GetBytes(pqi, &nWanted, &riidStored)
       || nWanted != sizeof(riidStored))
    {
        return MUX_E_INVALIDARG;
    }

    uint32_t nChannel;
    nWanted = sizeof(nChannel);
    if (  !Pipe_GetBytes(pqi, &nWanted, &nChannel)
       || nWanted != sizeof(nChannel))
    {
        return MUX_E_INVALIDARG;
    }

    // Look up the proxy-stub factory for this interface.
    //
    std::map<MUX_IID, MUX_INTERFACE_INFO *>::iterator it = g_Interfaces.find(riidStored);
    if (g_Interfaces.end() == it)
    {
        return MUX_E_NOINTERFACE;
    }

    MUX_CID cidProxyStub = it->second->cidProxyStub;
    mux_IPSFactoryBuffer *pIPSFactoryBuffer = nullptr;
    MUX_RESULT mr = mux_CreateInstance(cidProxyStub, nullptr, UseSameProcess, mux_IID_IPSFactoryBuffer, (void **)&pIPSFactoryBuffer);
    if (MUX_FAILED(mr))
    {
        return mr;
    }

    mux_IRpcProxyBuffer *pProxyBuffer = nullptr;
    mr = pIPSFactoryBuffer->CreateProxy(nullptr, riidStored, &pProxyBuffer, ppv);
    pIPSFactoryBuffer->Release();
    if (MUX_FAILED(mr))
    {
        return mr;
    }

    mr = pProxyBuffer->Connect(nChannel);
    pProxyBuffer->Release();
    if (MUX_FAILED(mr))
    {
        reinterpret_cast<mux_IUnknown *>(*ppv)->Release();
        *ppv = nullptr;
    }
    return mr;
}

MUX_RESULT CStandardMarshaler::ReleaseMarshalData(QUEUE_INFO *pqi)
{
    MUX_IID riid;
    size_t nWanted = sizeof(riid);
    if (  Pipe_GetBytes(pqi, &nWanted, &riid)
       && sizeof(riid) == nWanted)
    {
        uint32_t nChannel;
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

// Standard Marshaling support for mux_ISlaveControl.
//
// Method numbers (0-2 are IUnknown, not marshalled):
//
//  3: AddModule
//  4: RemoveModule
//  5: ModuleInfo
//  6: ModuleMaintenance
//  7: ShutdownSlave
//

// CSlaveControlProxy: client-side proxy for mux_ISlaveControl.
//
class CSlaveControlProxy : public mux_ISlaveControl, public mux_IRpcProxyBuffer
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    // mux_IRpcProxyBuffer
    //
    virtual MUX_RESULT Connect(uint32_t nChannel);
    virtual void       Disconnect(void);

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

    CSlaveControlProxy(void);
    virtual ~CSlaveControlProxy();

private:
    uint32_t m_cRef;
    uint32_t m_nChannel;
    UTF8    *m_pModuleName;
};

CSlaveControlProxy::CSlaveControlProxy(void) : m_cRef(1), m_nChannel(CHANNEL_INVALID), m_pModuleName(nullptr)
{
}

CSlaveControlProxy::~CSlaveControlProxy()
{
    if (nullptr != m_pModuleName)
    {
        delete [] m_pModuleName;
        m_pModuleName = nullptr;
    }
}

MUX_RESULT CSlaveControlProxy::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_ISlaveControl *>(this);
    }
    else if (IID_ISlaveControl == iid)
    {
        *ppv = static_cast<mux_ISlaveControl *>(this);
    }
    else if (mux_IID_IRpcProxyBuffer == iid)
    {
        *ppv = static_cast<mux_IRpcProxyBuffer *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CSlaveControlProxy::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CSlaveControlProxy::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        if (CHANNEL_INVALID != m_nChannel)
        {
            QUEUE_INFO qiFrame;
            Pipe_InitializeQueueInfo(&qiFrame);
            (void)Pipe_SendDiscPacket(m_nChannel, &qiFrame);
            Pipe_EmptyQueue(&qiFrame);
            m_nChannel = CHANNEL_INVALID;
        }
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CSlaveControlProxy::Connect(uint32_t nChannel)
{
    m_nChannel = nChannel;
    return MUX_S_OK;
}

void CSlaveControlProxy::Disconnect(void)
{
    m_nChannel = CHANNEL_INVALID;
}

#if defined(WINDOWS_FILES)
MUX_RESULT CSlaveControlProxy::AddModule(const UTF8 aModuleName[], const UTF16 aFileName[])
#elif defined(UNIX_FILES)
MUX_RESULT CSlaveControlProxy::AddModule(const UTF8 aModuleName[], const UTF8 aFileName[])
#endif // UNIX_FILES
{
    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    Marshal_PutUInt32(&qiFrame, 3);
    Marshal_PutString(&qiFrame, aModuleName);

#if defined(WINDOWS_FILES)
    size_t nFileName = (wcslen(aFileName) + 1) * sizeof(UTF16);
#elif defined(UNIX_FILES)
    size_t nFileName = strlen(reinterpret_cast<const char *>(aFileName)) + 1;
#endif // UNIX_FILES
    Pipe_AppendBytes(&qiFrame, sizeof(nFileName), &nFileName);
    Pipe_AppendBytes(&qiFrame, nFileName, aFileName);

    MUX_RESULT mr = Pipe_SendCallPacketAndWait(m_nChannel, &qiFrame);
    if (MUX_SUCCEEDED(mr))
    {
        MUX_RESULT mrReturn;
        if (Marshal_GetInt(&qiFrame, &mrReturn))
        {
            mr = mrReturn;
        }
        else
        {
            mr = MUX_E_FAIL;
        }
    }
    Pipe_EmptyQueue(&qiFrame);
    return mr;
}

MUX_RESULT CSlaveControlProxy::RemoveModule(const UTF8 aModuleName[])
{
    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    Marshal_PutUInt32(&qiFrame, 4);
    Marshal_PutString(&qiFrame, aModuleName);

    MUX_RESULT mr = Pipe_SendCallPacketAndWait(m_nChannel, &qiFrame);
    if (MUX_SUCCEEDED(mr))
    {
        MUX_RESULT mrReturn;
        if (Marshal_GetInt(&qiFrame, &mrReturn))
        {
            mr = mrReturn;
        }
        else
        {
            mr = MUX_E_FAIL;
        }
    }
    Pipe_EmptyQueue(&qiFrame);
    return mr;
}

MUX_RESULT CSlaveControlProxy::ModuleInfo(int iModule, MUX_MODULE_INFO *pModuleInfo)
{
    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    Marshal_PutUInt32(&qiFrame, 5);
    Marshal_PutInt(&qiFrame, iModule);

    MUX_RESULT mr = Pipe_SendCallPacketAndWait(m_nChannel, &qiFrame);
    if (MUX_SUCCEEDED(mr))
    {
        struct RETURN
        {
            MUX_RESULT mr;
            bool       bLoaded;
            size_t     nName;
        } ReturnFrame;

        size_t nWanted = sizeof(ReturnFrame);
        if (  Pipe_GetBytes(&qiFrame, &nWanted, &ReturnFrame)
           && nWanted == sizeof(ReturnFrame))
        {
            mr = ReturnFrame.mr;
            pModuleInfo->bLoaded = ReturnFrame.bLoaded;

            if (nullptr != m_pModuleName)
            {
                delete [] m_pModuleName;
                m_pModuleName = nullptr;
            }

            if (  MUX_SUCCEEDED(mr)
               && MUX_S_FALSE != mr
               && 0 < ReturnFrame.nName)
            {
                try
                {
                    m_pModuleName = new UTF8[ReturnFrame.nName];
                }
                catch (...)
                {
                    ; // Nothing.
                }

                if (nullptr != m_pModuleName)
                {
                    nWanted = ReturnFrame.nName;
                    if (  Pipe_GetBytes(&qiFrame, &nWanted, m_pModuleName)
                       && nWanted == ReturnFrame.nName)
                    {
                        pModuleInfo->pName = m_pModuleName;
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
                pModuleInfo->pName = nullptr;
            }
        }
        else
        {
            mr = MUX_E_FAIL;
        }
    }
    Pipe_EmptyQueue(&qiFrame);
    return mr;
}

MUX_RESULT CSlaveControlProxy::ModuleMaintenance(void)
{
    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    Marshal_PutUInt32(&qiFrame, 6);

    MUX_RESULT mr = Pipe_SendCallPacketAndWait(m_nChannel, &qiFrame);
    if (MUX_SUCCEEDED(mr))
    {
        MUX_RESULT mrReturn;
        if (Marshal_GetInt(&qiFrame, &mrReturn))
        {
            mr = mrReturn;
        }
        else
        {
            mr = MUX_E_FAIL;
        }
    }
    Pipe_EmptyQueue(&qiFrame);
    return mr;
}

MUX_RESULT CSlaveControlProxy::ShutdownSlave(void)
{
    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    Marshal_PutUInt32(&qiFrame, 7);

    MUX_RESULT mr = Pipe_SendCallPacketAndWait(m_nChannel, &qiFrame);
    if (MUX_SUCCEEDED(mr))
    {
        MUX_RESULT mrReturn;
        if (Marshal_GetInt(&qiFrame, &mrReturn))
        {
            mr = mrReturn;
        }
        else
        {
            mr = MUX_E_FAIL;
        }
    }
    Pipe_EmptyQueue(&qiFrame);
    return mr;
}

// CSlaveControlStub: server-side stub for mux_ISlaveControl.
//
class CSlaveControlStub : public mux_IRpcStubBuffer
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT Connect(mux_IUnknown *pUnknownServer);
    virtual void       Disconnect(void);
    virtual MUX_RESULT Invoke(QUEUE_INFO *pqi);
    virtual MUX_RESULT IsSupported(MUX_IID riid);
    virtual uint32_t   CountRefs(void);

    CSlaveControlStub(void);
    virtual ~CSlaveControlStub();

private:
    uint32_t           m_cRef;
    mux_ISlaveControl *m_pISlaveControl;
};

CSlaveControlStub::CSlaveControlStub(void) : m_cRef(1), m_pISlaveControl(nullptr)
{
}

CSlaveControlStub::~CSlaveControlStub()
{
    if (nullptr != m_pISlaveControl)
    {
        m_pISlaveControl->Release();
        m_pISlaveControl = nullptr;
    }
}

MUX_RESULT CSlaveControlStub::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IRpcStubBuffer *>(this);
    }
    else if (mux_IID_IRpcStubBuffer == iid)
    {
        *ppv = static_cast<mux_IRpcStubBuffer *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CSlaveControlStub::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CSlaveControlStub::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CSlaveControlStub::Connect(mux_IUnknown *pUnknownServer)
{
    if (nullptr != m_pISlaveControl)
    {
        m_pISlaveControl->Release();
        m_pISlaveControl = nullptr;
    }

    if (nullptr == pUnknownServer)
    {
        return MUX_S_OK;
    }

    return pUnknownServer->QueryInterface(IID_ISlaveControl, (void **)&m_pISlaveControl);
}

void CSlaveControlStub::Disconnect(void)
{
    if (nullptr != m_pISlaveControl)
    {
        m_pISlaveControl->Release();
        m_pISlaveControl = nullptr;
    }
}

MUX_RESULT CSlaveControlStub::Invoke(QUEUE_INFO *pqi)
{
    if (nullptr == m_pISlaveControl)
    {
        return MUX_E_UNEXPECTED;
    }

    uint32_t iMethod;
    if (!Marshal_GetUInt32(pqi, &iMethod))
    {
        return MUX_E_INVALIDARG;
    }

    MUX_RESULT mr = MUX_S_OK;

    switch (iMethod)
    {
    case 3: // AddModule
        {
            UTF8 bufModuleName[SIZEOF_PATHNAME];
            const UTF8 *pModuleName;
            if (!Marshal_GetString(pqi, bufModuleName, sizeof(bufModuleName), &pModuleName))
            {
                return MUX_E_INVALIDARG;
            }

            size_t nFileName;
            size_t nWanted = sizeof(nFileName);
            if (  !Pipe_GetBytes(pqi, &nWanted, &nFileName)
               || nWanted != sizeof(nFileName))
            {
                return MUX_E_INVALIDARG;
            }

#if defined(WINDOWS_FILES)
            UTF16 *pFileName = nullptr;
#elif defined(UNIX_FILES)
            UTF8  *pFileName = nullptr;
#endif // UNIX_FILES
            try
            {
#if defined(WINDOWS_FILES)
                pFileName = new UTF16[nFileName / sizeof(UTF16)];
#elif defined(UNIX_FILES)
                pFileName = new UTF8[nFileName];
#endif // UNIX_FILES
            }
            catch (...)
            {
                ; // Nothing.
            }

            if (nullptr == pFileName)
            {
                return MUX_E_OUTOFMEMORY;
            }

            nWanted = nFileName;
            if (  Pipe_GetBytes(pqi, &nWanted, pFileName)
               && nWanted == nFileName)
            {
                mr = m_pISlaveControl->AddModule(pModuleName, pFileName);
            }
            else
            {
                mr = MUX_E_INVALIDARG;
            }

            delete [] pFileName;

            Pipe_EmptyQueue(pqi);
            Marshal_PutInt(pqi, mr);
        }
        break;

    case 4: // RemoveModule
        {
            UTF8 bufModuleName[SIZEOF_PATHNAME];
            const UTF8 *pModuleName;
            if (!Marshal_GetString(pqi, bufModuleName, sizeof(bufModuleName), &pModuleName))
            {
                return MUX_E_INVALIDARG;
            }

            mr = m_pISlaveControl->RemoveModule(pModuleName);

            Pipe_EmptyQueue(pqi);
            Marshal_PutInt(pqi, mr);
        }
        break;

    case 5: // ModuleInfo
        {
            int iModule;
            if (!Marshal_GetInt(pqi, &iModule))
            {
                return MUX_E_INVALIDARG;
            }

            MUX_MODULE_INFO ModuleInfo;
            mr = m_pISlaveControl->ModuleInfo(iModule, &ModuleInfo);

            struct RETURN
            {
                MUX_RESULT mr;
                bool       bLoaded;
                size_t     nName;
            } ReturnFrame;

            ReturnFrame.mr = mr;
            ReturnFrame.bLoaded = false;
            ReturnFrame.nName = 0;

            if (  MUX_SUCCEEDED(mr)
               && MUX_S_FALSE != mr)
            {
                ReturnFrame.bLoaded = ModuleInfo.bLoaded;
                if (nullptr != ModuleInfo.pName)
                {
                    ReturnFrame.nName = strlen(reinterpret_cast<const char *>(ModuleInfo.pName)) + 1;
                }
            }

            Pipe_EmptyQueue(pqi);
            Pipe_AppendBytes(pqi, sizeof(ReturnFrame), &ReturnFrame);

            if (0 < ReturnFrame.nName)
            {
                Pipe_AppendBytes(pqi, ReturnFrame.nName, ModuleInfo.pName);
            }
        }
        break;

    case 6: // ModuleMaintenance
        {
            mr = m_pISlaveControl->ModuleMaintenance();

            Pipe_EmptyQueue(pqi);
            Marshal_PutInt(pqi, mr);
        }
        break;

    case 7: // ShutdownSlave
        {
            mr = m_pISlaveControl->ShutdownSlave();

            Pipe_EmptyQueue(pqi);
            Marshal_PutInt(pqi, mr);
        }
        break;

    default:
        mr = MUX_E_NOTIMPLEMENTED;
        break;
    }
    return mr;
}

MUX_RESULT CSlaveControlStub::IsSupported(MUX_IID riid)
{
    if (IID_ISlaveControl == riid)
    {
        return MUX_S_OK;
    }
    return MUX_S_FALSE;
}

uint32_t CSlaveControlStub::CountRefs(void)
{
    return (nullptr != m_pISlaveControl) ? 1 : 0;
}

// CSlaveControlPSFactory: proxy-stub factory for mux_ISlaveControl.
//
class CSlaveControlPSFactory : public mux_IPSFactoryBuffer, public mux_IClassFactory
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT CreateProxy(mux_IUnknown *pUnknownOuter, MUX_IID riid, mux_IRpcProxyBuffer **ppProxy, void **ppv);
    virtual MUX_RESULT CreateStub(MUX_IID riid, mux_IUnknown *pUnknownOuter, mux_IRpcStubBuffer **ppStub);

    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);

    CSlaveControlPSFactory(void);
    virtual ~CSlaveControlPSFactory();

private:
    uint32_t m_cRef;
};

CSlaveControlPSFactory::CSlaveControlPSFactory(void) : m_cRef(1)
{
}

CSlaveControlPSFactory::~CSlaveControlPSFactory()
{
}

MUX_RESULT CSlaveControlPSFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IPSFactoryBuffer *>(this);
    }
    else if (mux_IID_IPSFactoryBuffer == iid)
    {
        *ppv = static_cast<mux_IPSFactoryBuffer *>(this);
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

uint32_t CSlaveControlPSFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CSlaveControlPSFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CSlaveControlPSFactory::CreateProxy(mux_IUnknown *pUnknownOuter, MUX_IID riid, mux_IRpcProxyBuffer **ppProxy, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    if (IID_ISlaveControl != riid)
    {
        *ppProxy = nullptr;
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }

    CSlaveControlProxy *pProxy = nullptr;
    try
    {
        pProxy = new CSlaveControlProxy;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pProxy)
    {
        *ppProxy = nullptr;
        *ppv = nullptr;
        return MUX_E_OUTOFMEMORY;
    }

    *ppProxy = static_cast<mux_IRpcProxyBuffer *>(pProxy);
    pProxy->AddRef();
    *ppv = static_cast<mux_ISlaveControl *>(pProxy);
    return MUX_S_OK;
}

MUX_RESULT CSlaveControlPSFactory::CreateStub(MUX_IID riid, mux_IUnknown *pUnknownOuter, mux_IRpcStubBuffer **ppStub)
{
    if (IID_ISlaveControl != riid)
    {
        *ppStub = nullptr;
        return MUX_E_NOINTERFACE;
    }

    CSlaveControlStub *pStub = nullptr;
    try
    {
        pStub = new CSlaveControlStub;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pStub)
    {
        *ppStub = nullptr;
        return MUX_E_OUTOFMEMORY;
    }

    if (nullptr != pUnknownOuter)
    {
        MUX_RESULT mr = pStub->Connect(pUnknownOuter);
        if (MUX_FAILED(mr))
        {
            pStub->Release();
            *ppStub = nullptr;
            return mr;
        }
    }

    *ppStub = static_cast<mux_IRpcStubBuffer *>(pStub);
    return MUX_S_OK;
}

MUX_RESULT CSlaveControlPSFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    if (nullptr != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }
    return QueryInterface(iid, ppv);
}

MUX_RESULT CSlaveControlPSFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// CQuerySinkProxy: client-side proxy for mux_IQuerySink.
//
class CQuerySinkProxy : public mux_IQuerySink, public mux_IRpcProxyBuffer
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t     AddRef(void);
    virtual uint32_t     Release(void);

    // mux_IRpcProxyBuffer
    //
    virtual MUX_RESULT Connect(uint32_t nChannel);
    virtual void       Disconnect(void);

    // mux_IQuerySink
    //
    virtual MUX_RESULT Result(uint32_t iQueryHandle, uint32_t iError, QUEUE_INFO *pqiResultsSet);

    CQuerySinkProxy(void);
    virtual ~CQuerySinkProxy();

private:
    uint32_t m_cRef;
    uint32_t m_nChannel;
};

CQuerySinkProxy::CQuerySinkProxy(void) : m_cRef(1), m_nChannel(CHANNEL_INVALID)
{
}

CQuerySinkProxy::~CQuerySinkProxy()
{
}

MUX_RESULT CQuerySinkProxy::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IQuerySink *>(this);
    }
    else if (IID_IQuerySink == iid)
    {
        *ppv = static_cast<mux_IQuerySink *>(this);
    }
    else if (mux_IID_IRpcProxyBuffer == iid)
    {
        *ppv = static_cast<mux_IRpcProxyBuffer *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CQuerySinkProxy::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CQuerySinkProxy::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        QUEUE_INFO qiFrame;
        Pipe_InitializeQueueInfo(&qiFrame);
        (void)Pipe_SendDiscPacket(m_nChannel, &qiFrame);
        m_nChannel = CHANNEL_INVALID;
        Pipe_EmptyQueue(&qiFrame);

        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CQuerySinkProxy::Connect(uint32_t nChannel)
{
    m_nChannel = nChannel;
    return MUX_S_OK;
}

void CQuerySinkProxy::Disconnect(void)
{
    m_nChannel = CHANNEL_INVALID;
}

MUX_RESULT CQuerySinkProxy::Result(uint32_t iQueryHandle, uint32_t iError, QUEUE_INFO *pqiResultsSet)
{
    MUX_RESULT mr = MUX_S_OK;

    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    Marshal_PutUInt32(&qiFrame, 3);
    Marshal_PutUInt32(&qiFrame, iQueryHandle);
    Marshal_PutUInt32(&qiFrame, iError);
    Pipe_AppendQueue(&qiFrame, pqiResultsSet);

    mr = Pipe_SendCallPacketAndWait(m_nChannel, &qiFrame);

    if (MUX_SUCCEEDED(mr))
    {
        MUX_RESULT mrResult;
        if (Marshal_GetInt(&qiFrame, &mrResult))
        {
            mr = mrResult;
        }
        else
        {
            mr = MUX_E_FAIL;
        }
    }

    Pipe_EmptyQueue(&qiFrame);
    return mr;
}

// CQuerySinkStub: server-side stub for mux_IQuerySink.
//
class CQuerySinkStub : public mux_IRpcStubBuffer
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t     AddRef(void);
    virtual uint32_t     Release(void);

    virtual MUX_RESULT Connect(mux_IUnknown *pUnknownServer);
    virtual void       Disconnect(void);
    virtual MUX_RESULT Invoke(QUEUE_INFO *pqi);
    virtual MUX_RESULT IsSupported(MUX_IID riid);
    virtual uint32_t     CountRefs(void);

    CQuerySinkStub(void);
    virtual ~CQuerySinkStub();

private:
    uint32_t         m_cRef;
    mux_IQuerySink *m_pIQuerySink;
};

CQuerySinkStub::CQuerySinkStub(void) : m_cRef(1), m_pIQuerySink(nullptr)
{
}

CQuerySinkStub::~CQuerySinkStub()
{
    if (nullptr != m_pIQuerySink)
    {
        m_pIQuerySink->Release();
        m_pIQuerySink = nullptr;
    }
}

MUX_RESULT CQuerySinkStub::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IRpcStubBuffer *>(this);
    }
    else if (mux_IID_IRpcStubBuffer == iid)
    {
        *ppv = static_cast<mux_IRpcStubBuffer *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CQuerySinkStub::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CQuerySinkStub::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CQuerySinkStub::Connect(mux_IUnknown *pUnknownServer)
{
    if (nullptr != m_pIQuerySink)
    {
        m_pIQuerySink->Release();
        m_pIQuerySink = nullptr;
    }

    MUX_RESULT mr = MUX_E_INVALIDARG;
    if (nullptr != pUnknownServer)
    {
        mr = pUnknownServer->QueryInterface(IID_IQuerySink, (void **)&m_pIQuerySink);
    }
    return mr;
}

void CQuerySinkStub::Disconnect(void)
{
    if (nullptr != m_pIQuerySink)
    {
        m_pIQuerySink->Release();
        m_pIQuerySink = nullptr;
    }
}

MUX_RESULT CQuerySinkStub::Invoke(QUEUE_INFO *pqi)
{
    if (nullptr == m_pIQuerySink)
    {
        return MUX_E_NOINTERFACE;
    }

    uint32_t iMethod;
    if (!Marshal_GetUInt32(pqi, &iMethod))
    {
        return MUX_E_INVALIDARG;
    }

    switch (iMethod)
    {
    case 3: // MUX_RESULT Result(uint32_t iQueryHandle, uint32_t iError, QUEUE_INFO *pqiResultsSet)
        {
            uint32_t iQueryHandle;
            uint32_t iError;
            if (  !Marshal_GetUInt32(pqi, &iQueryHandle)
               || !Marshal_GetUInt32(pqi, &iError))
            {
                Pipe_EmptyQueue(pqi);
                Marshal_PutInt(pqi, static_cast<int>(MUX_E_INVALIDARG));
                return MUX_S_OK;
            }

            // The remaining queue data is the results set.
            //
            MUX_RESULT mrReturn = m_pIQuerySink->Result(iQueryHandle, iError, pqi);

            Pipe_EmptyQueue(pqi);
            Marshal_PutInt(pqi, mrReturn);
            return MUX_S_OK;
        }
        break;
    }
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQuerySinkStub::IsSupported(MUX_IID riid)
{
    if (IID_IQuerySink == riid)
    {
        return MUX_S_OK;
    }
    return MUX_S_FALSE;
}

uint32_t CQuerySinkStub::CountRefs(void)
{
    return (nullptr != m_pIQuerySink) ? 1 : 0;
}

// CQuerySinkPSFactory: proxy-stub factory for mux_IQuerySink.
//
class CQuerySinkPSFactory : public mux_IPSFactoryBuffer, public mux_IClassFactory
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t     AddRef(void);
    virtual uint32_t     Release(void);

    virtual MUX_RESULT CreateProxy(mux_IUnknown *pUnknownOuter, MUX_IID riid, mux_IRpcProxyBuffer **ppProxy, void **ppv);
    virtual MUX_RESULT CreateStub(MUX_IID riid, mux_IUnknown *pUnknownOuter, mux_IRpcStubBuffer **ppStub);

    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);

    CQuerySinkPSFactory(void);
    virtual ~CQuerySinkPSFactory();

private:
    uint32_t m_cRef;
};

CQuerySinkPSFactory::CQuerySinkPSFactory(void) : m_cRef(1)
{
}

CQuerySinkPSFactory::~CQuerySinkPSFactory()
{
}

MUX_RESULT CQuerySinkPSFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IPSFactoryBuffer *>(this);
    }
    else if (mux_IID_IPSFactoryBuffer == iid)
    {
        *ppv = static_cast<mux_IPSFactoryBuffer *>(this);
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

uint32_t CQuerySinkPSFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CQuerySinkPSFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CQuerySinkPSFactory::CreateProxy(mux_IUnknown *pUnknownOuter, MUX_IID riid, mux_IRpcProxyBuffer **ppProxy, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    if (IID_IQuerySink != riid)
    {
        return MUX_E_NOINTERFACE;
    }

    CQuerySinkProxy *pProxy = nullptr;
    try
    {
        pProxy = new CQuerySinkProxy;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pProxy)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pProxy->QueryInterface(mux_IID_IRpcProxyBuffer, (void **)ppProxy);
    if (MUX_SUCCEEDED(mr))
    {
        mr = pProxy->QueryInterface(riid, ppv);
    }
    pProxy->Release();
    return mr;
}

MUX_RESULT CQuerySinkPSFactory::CreateStub(MUX_IID riid, mux_IUnknown *pUnknownOuter, mux_IRpcStubBuffer **ppStub)
{
    UNUSED_PARAMETER(pUnknownOuter);

    if (IID_IQuerySink != riid)
    {
        return MUX_E_NOINTERFACE;
    }

    CQuerySinkStub *pStub = nullptr;
    try
    {
        pStub = new CQuerySinkStub;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pStub)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pStub->QueryInterface(mux_IID_IRpcStubBuffer, (void **)ppStub);
    pStub->Release();
    return mr;
}

MUX_RESULT CQuerySinkPSFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);
    return QueryInterface(iid, ppv);
}

MUX_RESULT CQuerySinkPSFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// CQueryControlProxy: client-side proxy for mux_IQueryControl.
//
class CQueryControlProxy : public mux_IQueryControl, public mux_IRpcProxyBuffer
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t     AddRef(void);
    virtual uint32_t     Release(void);

    // mux_IRpcProxyBuffer
    //
    virtual MUX_RESULT Connect(uint32_t nChannel);
    virtual void       Disconnect(void);

    // mux_IQueryControl
    //
    virtual MUX_RESULT Connect(const UTF8 *pServer, const UTF8 *pDatabase, const UTF8 *pUser, const UTF8 *pPassword);
    virtual MUX_RESULT Advise(mux_IQuerySink *pIQuerySink);
    virtual MUX_RESULT Query(uint32_t iQueryHandle, const UTF8 *pDatabaseName, const UTF8 *pQuery);

    CQueryControlProxy(void);
    virtual ~CQueryControlProxy();

private:
    uint32_t m_cRef;
    uint32_t m_nChannel;
};

CQueryControlProxy::CQueryControlProxy(void) : m_cRef(1), m_nChannel(CHANNEL_INVALID)
{
}

CQueryControlProxy::~CQueryControlProxy()
{
}

MUX_RESULT CQueryControlProxy::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IQueryControl *>(this);
    }
    else if (IID_IQueryControl == iid)
    {
        *ppv = static_cast<mux_IQueryControl *>(this);
    }
    else if (mux_IID_IRpcProxyBuffer == iid)
    {
        *ppv = static_cast<mux_IRpcProxyBuffer *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CQueryControlProxy::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CQueryControlProxy::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        QUEUE_INFO qiFrame;
        Pipe_InitializeQueueInfo(&qiFrame);
        (void)Pipe_SendDiscPacket(m_nChannel, &qiFrame);
        m_nChannel = CHANNEL_INVALID;
        Pipe_EmptyQueue(&qiFrame);

        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CQueryControlProxy::Connect(uint32_t nChannel)
{
    m_nChannel = nChannel;
    return MUX_S_OK;
}

void CQueryControlProxy::Disconnect(void)
{
    m_nChannel = CHANNEL_INVALID;
}

MUX_RESULT CQueryControlProxy::Connect(const UTF8 *pServer, const UTF8 *pDatabase, const UTF8 *pUser, const UTF8 *pPassword)
{
    MUX_RESULT mr = MUX_S_OK;

    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    Marshal_PutUInt32(&qiFrame, 3);
    Marshal_PutString(&qiFrame, pServer);
    Marshal_PutString(&qiFrame, pDatabase);
    Marshal_PutString(&qiFrame, pUser);
    Marshal_PutString(&qiFrame, pPassword);

    mr = Pipe_SendCallPacketAndWait(m_nChannel, &qiFrame);

    if (MUX_SUCCEEDED(mr))
    {
        MUX_RESULT mrResult;
        if (Marshal_GetInt(&qiFrame, &mrResult))
        {
            mr = mrResult;
        }
        else
        {
            mr = MUX_E_FAIL;
        }
    }

    Pipe_EmptyQueue(&qiFrame);
    return mr;
}

MUX_RESULT CQueryControlProxy::Advise(mux_IQuerySink *pIQuerySink)
{
    MUX_RESULT mr = MUX_S_OK;

    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    Marshal_PutUInt32(&qiFrame, 4);

    mr = mux_MarshalInterface(&qiFrame, IID_IQuerySink, pIQuerySink, CrossProcess);
    if (MUX_SUCCEEDED(mr))
    {
        mr = Pipe_SendCallPacketAndWait(m_nChannel, &qiFrame);

        if (MUX_SUCCEEDED(mr))
        {
            MUX_RESULT mrResult;
            if (Marshal_GetInt(&qiFrame, &mrResult))
            {
                mr = mrResult;
            }
            else
            {
                mr = MUX_E_FAIL;
            }
        }
    }

    Pipe_EmptyQueue(&qiFrame);
    return mr;
}

MUX_RESULT CQueryControlProxy::Query(uint32_t iQueryHandle, const UTF8 *pDatabaseName, const UTF8 *pQuery)
{
    MUX_RESULT mr = MUX_S_OK;

    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    Marshal_PutUInt32(&qiFrame, 5);
    Marshal_PutUInt32(&qiFrame, iQueryHandle);
    Marshal_PutString(&qiFrame, pDatabaseName);
    Marshal_PutString(&qiFrame, pQuery);

    mr = Pipe_SendMsgPacket(m_nChannel, &qiFrame);

    Pipe_EmptyQueue(&qiFrame);
    return mr;
}

// CQueryControlStub: server-side stub for mux_IQueryControl.
//
class CQueryControlStub : public mux_IRpcStubBuffer
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t     AddRef(void);
    virtual uint32_t     Release(void);

    virtual MUX_RESULT Connect(mux_IUnknown *pUnknownServer);
    virtual void       Disconnect(void);
    virtual MUX_RESULT Invoke(QUEUE_INFO *pqi);
    virtual MUX_RESULT IsSupported(MUX_IID riid);
    virtual uint32_t     CountRefs(void);

    CQueryControlStub(void);
    virtual ~CQueryControlStub();

private:
    uint32_t              m_cRef;
    mux_IQueryControl    *m_pIQueryControl;
};

CQueryControlStub::CQueryControlStub(void) : m_cRef(1), m_pIQueryControl(nullptr)
{
}

CQueryControlStub::~CQueryControlStub()
{
    if (nullptr != m_pIQueryControl)
    {
        m_pIQueryControl->Release();
        m_pIQueryControl = nullptr;
    }
}

MUX_RESULT CQueryControlStub::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IRpcStubBuffer *>(this);
    }
    else if (mux_IID_IRpcStubBuffer == iid)
    {
        *ppv = static_cast<mux_IRpcStubBuffer *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CQueryControlStub::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CQueryControlStub::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CQueryControlStub::Connect(mux_IUnknown *pUnknownServer)
{
    if (nullptr != m_pIQueryControl)
    {
        m_pIQueryControl->Release();
        m_pIQueryControl = nullptr;
    }

    MUX_RESULT mr = MUX_E_INVALIDARG;
    if (nullptr != pUnknownServer)
    {
        mr = pUnknownServer->QueryInterface(IID_IQueryControl, (void **)&m_pIQueryControl);
    }
    return mr;
}

void CQueryControlStub::Disconnect(void)
{
    if (nullptr != m_pIQueryControl)
    {
        m_pIQueryControl->Release();
        m_pIQueryControl = nullptr;
    }
}

MUX_RESULT CQueryControlStub::Invoke(QUEUE_INFO *pqi)
{
    if (nullptr == m_pIQueryControl)
    {
        return MUX_E_NOINTERFACE;
    }

    uint32_t iMethod;
    if (!Marshal_GetUInt32(pqi, &iMethod))
    {
        return MUX_E_INVALIDARG;
    }

    switch (iMethod)
    {
    case 3: // MUX_RESULT Connect(const UTF8 *pServer, const UTF8 *pDatabase, const UTF8 *pUser, const UTF8 *pPassword)
        {
            const UTF8 *pServer = nullptr;
            const UTF8 *pDatabase = nullptr;
            const UTF8 *pUser = nullptr;
            const UTF8 *pPassword = nullptr;
            UTF8 bufServer[1024];
            UTF8 bufDatabase[1024];
            UTF8 bufUser[256];
            UTF8 bufPassword[256];

            MUX_RESULT mrReturn;
            if (  !Marshal_GetString(pqi, bufServer, sizeof(bufServer), &pServer)
               || !Marshal_GetString(pqi, bufDatabase, sizeof(bufDatabase), &pDatabase)
               || !Marshal_GetString(pqi, bufUser, sizeof(bufUser), &pUser)
               || !Marshal_GetString(pqi, bufPassword, sizeof(bufPassword), &pPassword))
            {
                mrReturn = MUX_E_INVALIDARG;
            }
            else
            {
                mrReturn = m_pIQueryControl->Connect(pServer, pDatabase, pUser, pPassword);
            }

            Pipe_EmptyQueue(pqi);
            Marshal_PutInt(pqi, mrReturn);
            return MUX_S_OK;
        }
        break;

    case 4: // MUX_RESULT Advise(mux_IQuerySink *pIQuerySink)
        {
            mux_IQuerySink *pIQuerySink = nullptr;
            MUX_RESULT mrReturn = mux_UnmarshalInterface(pqi, IID_IQuerySink, (void **)&pIQuerySink);

            if (MUX_SUCCEEDED(mrReturn))
            {
                mrReturn = m_pIQueryControl->Advise(pIQuerySink);
            }

            Pipe_EmptyQueue(pqi);
            Marshal_PutInt(pqi, mrReturn);
            return MUX_S_OK;
        }
        break;

    case 5: // MUX_RESULT Query(uint32_t iQueryHandle, const UTF8 *pDatabaseName, const UTF8 *pQuery)
        {
            uint32_t iQueryHandle;
            const UTF8 *pDatabaseName = nullptr;
            const UTF8 *pQuery = nullptr;
            UTF8 bufDatabaseName[1024];
            UTF8 bufQuery[8192];

            MUX_RESULT mrReturn;
            if (  !Marshal_GetUInt32(pqi, &iQueryHandle)
               || !Marshal_GetString(pqi, bufDatabaseName, sizeof(bufDatabaseName), &pDatabaseName)
               || !Marshal_GetString(pqi, bufQuery, sizeof(bufQuery), &pQuery))
            {
                mrReturn = MUX_E_INVALIDARG;
            }
            else
            {
                mrReturn = m_pIQueryControl->Query(iQueryHandle, pDatabaseName, pQuery);
            }

            Pipe_EmptyQueue(pqi);
            Marshal_PutInt(pqi, mrReturn);
            return MUX_S_OK;
        }
        break;
    }
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryControlStub::IsSupported(MUX_IID riid)
{
    if (IID_IQueryControl == riid)
    {
        return MUX_S_OK;
    }
    return MUX_S_FALSE;
}

uint32_t CQueryControlStub::CountRefs(void)
{
    return (nullptr != m_pIQueryControl) ? 1 : 0;
}

// CQueryControlPSFactory: proxy-stub factory for mux_IQueryControl.
//
class CQueryControlPSFactory : public mux_IPSFactoryBuffer, public mux_IClassFactory
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t     AddRef(void);
    virtual uint32_t     Release(void);

    virtual MUX_RESULT CreateProxy(mux_IUnknown *pUnknownOuter, MUX_IID riid, mux_IRpcProxyBuffer **ppProxy, void **ppv);
    virtual MUX_RESULT CreateStub(MUX_IID riid, mux_IUnknown *pUnknownOuter, mux_IRpcStubBuffer **ppStub);

    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);

    CQueryControlPSFactory(void);
    virtual ~CQueryControlPSFactory();

private:
    uint32_t m_cRef;
};

CQueryControlPSFactory::CQueryControlPSFactory(void) : m_cRef(1)
{
}

CQueryControlPSFactory::~CQueryControlPSFactory()
{
}

MUX_RESULT CQueryControlPSFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IPSFactoryBuffer *>(this);
    }
    else if (mux_IID_IPSFactoryBuffer == iid)
    {
        *ppv = static_cast<mux_IPSFactoryBuffer *>(this);
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

uint32_t CQueryControlPSFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CQueryControlPSFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CQueryControlPSFactory::CreateProxy(mux_IUnknown *pUnknownOuter, MUX_IID riid, mux_IRpcProxyBuffer **ppProxy, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    if (IID_IQueryControl != riid)
    {
        return MUX_E_NOINTERFACE;
    }

    CQueryControlProxy *pProxy = nullptr;
    try
    {
        pProxy = new CQueryControlProxy;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pProxy)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pProxy->QueryInterface(mux_IID_IRpcProxyBuffer, (void **)ppProxy);
    if (MUX_SUCCEEDED(mr))
    {
        mr = pProxy->QueryInterface(riid, ppv);
    }
    pProxy->Release();
    return mr;
}

MUX_RESULT CQueryControlPSFactory::CreateStub(MUX_IID riid, mux_IUnknown *pUnknownOuter, mux_IRpcStubBuffer **ppStub)
{
    UNUSED_PARAMETER(pUnknownOuter);

    if (IID_IQueryControl != riid)
    {
        return MUX_E_NOINTERFACE;
    }

    CQueryControlStub *pStub = nullptr;
    try
    {
        pStub = new CQueryControlStub;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pStub)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pStub->QueryInterface(mux_IID_IRpcStubBuffer, (void **)ppStub);
    pStub->Release();
    return mr;
}

MUX_RESULT CQueryControlPSFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);
    return QueryInterface(iid, ppv);
}

MUX_RESULT CQueryControlPSFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// libmux-internal class object factory.
//
extern "C" MUX_RESULT DCL_API libmux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    if (CID_SlaveControlPSFactory == cid)
    {
        CSlaveControlPSFactory *pFactory = nullptr;
        try
        {
            pFactory = new CSlaveControlPSFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pFactory->QueryInterface(iid, ppv);
        pFactory->Release();
    }
    else if (CID_QuerySinkPSFactory == cid)
    {
        CQuerySinkPSFactory *pFactory = nullptr;
        try
        {
            pFactory = new CQuerySinkPSFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pFactory->QueryInterface(iid, ppv);
        pFactory->Release();
    }
    else if (CID_QueryControlPSFactory == cid)
    {
        CQueryControlPSFactory *pFactory = nullptr;
        try
        {
            pFactory = new CQueryControlPSFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pFactory->QueryInterface(iid, ppv);
        pFactory->Release();
    }
    return mr;
}

// ---------------------------------------------------------------------------
// Crash diagnostic global — set by engine during command processing.
// ---------------------------------------------------------------------------

LIBMUX_API const UTF8 *g_debug_cmd = T("< init >");

// ---------------------------------------------------------------------------
// AssertionFailed / OutOfMemory — libmux versions.
//
// These are intentionally simple: log to stderr and abort.  The engine
// has richer versions (using mudstate, restart logic) but any code in
// libmux.so or the driver that hits an assertion should crash hard.
// ---------------------------------------------------------------------------

DCL_EXPORT void OutOfMemory(const UTF8 *SourceFile, unsigned int LineNo)
{
    fprintf(stderr, "%s(%u): Out of memory.\n",
        reinterpret_cast<const char *>(SourceFile), LineNo);
    fflush(stderr);
    abort();
}

DCL_EXPORT bool AssertionFailed(const UTF8 *SourceFile, unsigned int LineNo)
{
    fprintf(stderr, "%s(%u): Assertion failed.\n",
        reinterpret_cast<const char *>(SourceFile), LineNo);
    fflush(stderr);
    abort();
    return false;
}
