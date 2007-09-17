/*! \file libmux.cpp
 * \brief Base-level module support
 *
 * $Id$
 *
 */

#include "copyright.h"
#include "autoconf.h"
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

#ifdef WIN32
typedef HINSTANCE MODULE_HANDLE;
#define MOD_OPEN(m)  LoadLibrary(m)
#define MOD_SYM(h,s) GetProcAddress(h,s)
#define MOD_CLOSE(h) FreeLibrary(h)
#else
typedef void     *MODULE_HANDLE;
#define MOD_OPEN(m)  dlopen((char *)m, RTLD_LAZY)
#define MOD_SYM(h,s) dlsym(h,s)
#define MOD_CLOSE(h) dlclose(h)
#endif

typedef struct mod_info
{
    struct mod_info  *pNext;
    FPGETCLASSOBJECT *fpGetClassObject;
    FPCANUNLOADNOW   *fpCanUnloadNow;
    FPREGISTER       *fpRegister;
    FPUNREGISTER     *fpUnregister;
    MODULE_HANDLE    hInst;
    UTF8             *pModuleName;
#ifdef WIN32
    UTF16            *pFileName;
#else
    UTF8             *pFileName;
#endif
    bool             bLoaded;
} MODULE_INFO;

typedef struct
{
    CLASS_INFO    ci;
    MODULE_INFO  *pModule;
} CLASS_INFO_PRIVATE;

static MODULE_INFO *g_pModuleList = NULL;
static MODULE_INFO *g_pModuleLast = NULL;

static MODULE_INFO  g_MainModule =
{
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    false
};

static int                  g_nClasses = 0;
static int                  g_nClassesAllocated = 0;
static CLASS_INFO_PRIVATE  *g_pClasses = NULL;

static MODULE_INFO *g_pModule = NULL;

static int                  g_nInterfaces = 0;
static int                  g_nInterfacesAllocated = 0;
static INTERFACE_INFO      *g_pInterfaces = NULL;

PipePump   *g_fpPipePump = NULL;
QUEUE_INFO *g_pQueue_In  = NULL;
QUEUE_INFO *g_pQueue_Out = NULL;

CHANNEL_INFO *aChannels = NULL;
size_t        nChannels = 0;
size_t        nChannelsAllocated = 0;

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
    UTF8 *p = NULL;

    try
    {
        p = new UTF8[n+1];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL != p)
    {
        memcpy(p, pString, n+1);
    }
    return p;
}

#ifdef WIN32
static UTF16 *CopyUTF16(const UTF16 *pString)
{
    size_t n = wcslen(pString);
    UTF16 *p = NULL;

    try
    {
        p = new UTF16[n+1];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL != p)
    {
        memcpy(p, pString, (n+1) * sizeof(UTF16));
    }
    return p;
}
#endif // WIN32

/*! \brief Find the first class ID not less than the requested class id.
 *
 * The return value may be beyond the end of the array, so callers should check bounds.
 *
 * \param  cid  Class ID.
 * \return      Index into g_pClasses.
 */

static int ClassFind(MUX_CID cid)
{
    // Binary search for the class id.
    //
    int lo = 0;
    int mid;
    int hi = g_nClasses - 1;
    while (lo <= hi)
    {
        mid = ((hi - lo) >> 1) + lo;
        if (cid < g_pClasses[mid].ci.cid)
        {
            hi = mid - 1;
        }
        else if (g_pClasses[mid].ci.cid < cid)
        {
            lo = mid + 1;
        }
        else // (g_pClasses[mid].ci.cid == cid)
        {
            return mid;
        }
    }
    return lo;
}

/*! \brief Find which module implements a particular class id.
 *
 * Note that callers may need to test for MainModule and cannot assume the
 * returned module record is implemented in a module.
 *
 * \param  cid  Class ID.
 * \return      Pointer to module.
 */

static MODULE_INFO *ModuleFindFromCID(MUX_CID cid)
{
    int i = ClassFind(cid);
    if (  i < g_nClasses
       && g_pClasses[i].ci.cid == cid)
   {
        return g_pClasses[i].pModule;
    }
    return NULL;
}

/*! \brief Find module given its module name.
 *
 * Note that it is not possible to find the special-case module for the main
 * program (netmux or stubslave) this way.
 *
 * \param  UTF8[]    Module name.
 * \return           Corresponding module record or NULL if not found.
 */

static MODULE_INFO *ModuleFindFromName(const UTF8 aModuleName[])
{
    MODULE_INFO *pModule = g_pModuleList;
    while (NULL != pModule)
    {
        if (strcmp((const char *)aModuleName, (const char *)pModule->pModuleName) == 0)
        {
            return pModule;
        }
        pModule = pModule->pNext;
    }
    return NULL;
}

/*! \brief Find module given its filename.
 *
 * Note that it is not possible to find the special-case module for the main
 * program (netmux or stubslave) this way.
 *
 * \param  UTF8[]    File name.
 * \return           Corresponding module record or NULL if not found.
 */

#ifdef WIN32
static MODULE_INFO *ModuleFindFromFileName(const UTF16 aFileName[])
#else
static MODULE_INFO *ModuleFindFromFileName(const UTF8 aFileName[])
#endif
{
    MODULE_INFO *pModule = g_pModuleList;
    while (NULL != pModule)
    {
        if (strcmp((const char *)aFileName, (const char *)pModule->pFileName) == 0)
        {
            return pModule;
        }
        pModule = pModule->pNext;
    }
    return NULL;
}

#define MINIMUM_SIZE 8

static int GrowByFactor(int i)
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

/*! \brief Adds (class id, module) in table while maintain its order.
 *
 * This routine assumes the array is large enough to hold the addition.
 *
 * \param pci        Class-related attributes including cid.
 * \param pModule    Module that implements it.
 * \return           None.
 */

static void ClassAdd(CLASS_INFO *pci, MODULE_INFO *pModule)
{
    int i = ClassFind(pci->cid);
    if (  i < g_nClasses
       && g_pClasses[i].ci.cid == pci->cid)
    {
        return;
    }

    if (i != g_nClasses)
    {
        memmove( g_pClasses + i + 1,
                 g_pClasses + i,
                 (g_nClasses - i) * sizeof(CLASS_INFO_PRIVATE));
    }
    g_nClasses++;

    g_pClasses[i].ci = *pci;
    g_pClasses[i].pModule = pModule;
}

/*! \brief Removes a class id from the table while maintaining order.
 *
 * \param cid       Class ID
 * \return          None.
 */

static void ClassRemove(MUX_CID cid)
{
    int i = ClassFind(cid);
    if (  i < g_nClasses
       && g_pClasses[i].ci.cid == cid)
    {
        g_nClasses--;
        if (i != g_nClasses)
        {
            memmove( g_pClasses + i,
                     g_pClasses + i + 1,
                     (g_nClasses - i) * sizeof(CLASS_INFO_PRIVATE));
        }
    }
}

static int InterfaceFind(MUX_IID iid)
{
    // Binary search for the interface id.
    //
    int lo = 0;
    int mid;
    int hi = g_nInterfaces - 1;
    while (lo <= hi)
    {
        mid = ((hi - lo) >> 1) + lo;
        if (iid < g_pInterfaces[mid].iid)
        {
            hi = mid - 1;
        }
        else if (g_pInterfaces[mid].iid < iid)
        {
            lo = mid + 1;
        }
        else // (g_pInterfaces[mid].iid == iid)
        {
            return mid;
        }
    }
    return lo;
}

static void InterfaceAdd(INTERFACE_INFO *pii)
{
    int i = InterfaceFind(pii->iid);
    if (  i < g_nInterfaces
       && g_pInterfaces[i].iid == pii->iid)
    {
        return;
    }

    if (i != g_nInterfaces)
    {
        memmove( g_pInterfaces + i + 1,
                 g_pInterfaces + i,
                 (g_nInterfaces - i) * sizeof(INTERFACE_INFO));
    }
    g_nInterfaces++;

    g_pInterfaces[i] = *pii;
}

static void InterfaceRemove(MUX_IID iid)
{
    int i = InterfaceFind(iid);
    if (  i < g_nInterfaces
       && g_pInterfaces[i].iid == iid)
    {
        g_nInterfaces--;
        if (i != g_nInterfaces)
        {
            memmove( g_pInterfaces + i,
                     g_pInterfaces + i + 1,
                     (g_nInterfaces - i) * sizeof(INTERFACE_INFO));
        }
    }
}

/*! \brief Adds a module.
 *
 * \param aModuleName[]  Filename of Module
 * \return               Module context record, NULL if out of memory or
 *                       duplicate found.
 */

#ifdef WIN32
static MODULE_INFO *ModuleAdd(const UTF8 aModuleName[], const UTF16 aFileName[])
#else
static MODULE_INFO *ModuleAdd(const UTF8 aModuleName[], const UTF8 aFileName[])
#endif
{
    // If the module name or file name is already being used, we won't add it
    // again.  This does not handle file-system links, but that will be caught
    // when the module tries to register its class ids.
    //
    MODULE_INFO *pModuleFromMN = ModuleFindFromName(aModuleName);
    MODULE_INFO *pModuleFromFN = ModuleFindFromFileName(aFileName);
    if (  NULL == pModuleFromMN
       && NULL == pModuleFromFN)
    {
        // Ensure that enough room is available to append a new MODULE_INFO.
        //
        MODULE_INFO *pModule = NULL;
        try
        {
            pModule = new MODULE_INFO;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pModule)
        {
            return NULL;
        }

        // Fill in new MODULE_INFO
        //
        pModule->fpGetClassObject = NULL;
        pModule->fpCanUnloadNow = NULL;
        pModule->fpRegister = NULL;
        pModule->fpUnregister = NULL;
        pModule->hInst = NULL;
        pModule->pModuleName = CopyUTF8(aModuleName);
#ifdef WIN32
        pModule->pFileName = CopyUTF16(aFileName);
#else
        pModule->pFileName = CopyUTF8(aFileName);
#endif // WIN32
        pModule->bLoaded = false;

        if (  NULL != pModule->pModuleName
           && NULL != pModule->pFileName)
        {
            // Add new MODULE_INFO to the end of the list.
            //
            pModule->pNext = NULL;
            if (NULL == g_pModuleLast)
            {
                g_pModuleList = pModule;
            }
            else
            {
                g_pModuleLast->pNext = pModule;
                g_pModuleLast = pModule;
            }
            return pModule;
        }
        else
        {
            // Clean up after failing to copy string.
            //
            if (NULL != pModule->pModuleName)
            {
                delete [] pModule->pModuleName;
                pModule->pModuleName = NULL;
            }

            if (NULL != pModule->pFileName)
            {
                delete [] pModule->pFileName;
                pModule->pFileName = NULL;
            }

            delete [] pModule;
        }
    }
    return NULL;
}

/*! \brief Removes a module from the module table.
 *
 * \param pModule      Module context record to remove and destroy.
 */

static void ModuleRemove(MODULE_INFO *pModule)
{
    MODULE_INFO *p = g_pModuleList;
    MODULE_INFO *q = NULL;

    while (NULL != p)
    {
        if (pModule == p)
        {
            // Unlink from list.
            //
            if (NULL == q)
            {
                g_pModuleList = p->pNext;
            }
            else
            {
                q->pNext = p->pNext;
            }

            // As a precaution, remove any any references in the class id
            // table.  This should have been done when we asked the module to
            // revoke its class ids.
            //
            int i;
            for (i = 0; i < g_nClasses; i++)
            {
                if (g_pClasses[i].pModule == pModule)
                {
                    ClassRemove(g_pClasses[i].ci.cid);
                }
            }

            // Free associated memory.
            //
            if (NULL != p->pModuleName)
            {
                delete [] p->pModuleName;
                p->pModuleName = NULL;
            }

            if (NULL != p->pFileName)
            {
                delete [] p->pFileName;
                p->pFileName = NULL;
            }

            delete p;
            return;
        }

        q = p;
        p = p->pNext;
    }
}

/*! \brief Loads a known module.
 *
 * \param pModule   Module context record.
 */

static void ModuleLoad(MODULE_INFO *pModule)
{
    if (pModule->bLoaded)
    {
        // Module is already in loaded state.
        //
        return;
    }

    pModule->hInst = MOD_OPEN(pModule->pFileName);
    if (NULL != pModule->hInst)
    {
        pModule->fpGetClassObject = (FPGETCLASSOBJECT *)MOD_SYM(pModule->hInst, "mux_GetClassObject");
        pModule->fpCanUnloadNow   = (FPCANUNLOADNOW *)MOD_SYM(pModule->hInst, "mux_CanUnloadNow");
        pModule->fpRegister       = (FPREGISTER *)MOD_SYM(pModule->hInst, "mux_Register");
        pModule->fpUnregister     = (FPUNREGISTER *)MOD_SYM(pModule->hInst, "mux_Unregister");
        if (  NULL != pModule->fpGetClassObject
           && NULL != pModule->fpCanUnloadNow
           && NULL != pModule->fpRegister
           && NULL != pModule->fpUnregister)
        {
            pModule->bLoaded = true;
        }
        else
        {
            MOD_CLOSE(pModule->hInst);
        }
    }
}

/*! \brief Unloads a known module.
 *
 * \param pModule   Module context record.
 */

static void ModuleUnload(MODULE_INFO *pModule)
{
    if (pModule->bLoaded)
    {
        MOD_CLOSE(pModule->hInst);
        pModule->hInst = NULL;
        pModule->fpGetClassObject = NULL;
        pModule->fpCanUnloadNow = NULL;
        pModule->fpRegister = NULL;
        pModule->fpUnregister = NULL;
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
    MUX_RESULT mr = MUX_S_OK;

    if (  (UseSameProcess & ctx)
       || (  g_ProcessContext == IsMainProcess
          && (UseMainProcess & ctx))
       || (  g_ProcessContext == IsSlaveProcess
          && (UseSlaveProcess & ctx)))
    {
        MODULE_INFO *pModule = ModuleFindFromCID(cid);
        if (NULL != pModule)
        {
            if (pModule == &g_MainModule)
            {
                if (NULL == pModule->fpGetClassObject)
                {
                    mr = MUX_E_CLASSNOTAVAILABLE;
                }
            }
            else if (!pModule->bLoaded)
            {
                ModuleLoad(pModule);
                if (!pModule->bLoaded)
                {
                    mr =  MUX_E_CLASSNOTAVAILABLE;
                }
            }

            if (MUX_SUCCEEDED(mr))
            {
                mux_IClassFactory *pIClassFactory = NULL;
                MUX_RESULT mr = pModule->fpGetClassObject(cid, mux_IID_IClassFactory, (void **)&pIClassFactory);
                if (  MUX_SUCCEEDED(mr)
                   && NULL != pIClassFactory)
                {
                    mr = pIClassFactory->CreateInstance(pUnknownOuter, iid, ppv);
                    pIClassFactory->Release();
                }
            }
        }
    }
    else if (NULL != g_fpPipePump)
    {
        // Out-of-Proc.
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
            MUX_CID cidProxy = 0;
            size_t nWanted = sizeof(cidProxy);
            if (  Pipe_GetBytes(&qiFrame, &nWanted, (UINT8*)(&cidProxy))
               && sizeof(cidProxy) == nWanted)
            {
                // Open an IMarshal interface on the given proxy and pass it the marshal packet.
                //
                mux_IMarshal *pIMarshal = NULL;
                mr = mux_CreateInstance(cidProxy, NULL, UseSameProcess, mux_IID_IMarshal, (void **)&pIMarshal);
                if (MUX_SUCCEEDED(mr))
                {
                    mr = pIMarshal->UnmarshalInterface(&qiFrame, iid, ppv);
                    pIMarshal->Release();
                }
            }
            else
            {
                mr =  MUX_E_CLASSNOTAVAILABLE;
            }
        }
        Pipe_EmptyQueue(&qiFrame);
    }
    return mr;
}

/*! \brief Register class ids and factory implemented by the process binary.
 *
 * Modules must pass NULL for pfGetClassObject, but the main program (netmux
 * or stubslave) must pass a non-NULL pfGetClassObject.  For modules, the
 * class factory is obtained by using the mux_GetClassObject export.
 *
 * \param nci                  Number of components to register.
 * \param aci                  Table of component-related info.
 * \param fpGetClassObject     Pointer to Factory capable of creating
 *                             instances of the given components.
 * \return                     MUX_RESULT
 */

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RegisterClassObjects(int nci, CLASS_INFO aci[], FPGETCLASSOBJECT *fpGetClassObject)
{
    if (  nci <= 0
       || NULL == aci)
    {
        return MUX_E_INVALIDARG;
    }

    // Modules export a mux_GetClassObject handler, but the main program
    // (netmux or stubslave) must pass its handler in here. Also, it doesn't
    // make sense to load and unload netmux.  But, we want to allow the main
    // program to provide module interfaces, so some special-casing is done to
    // allow that.
    //
    if (  (  NULL != g_pModule
          && NULL != fpGetClassObject)
       || (  NULL == g_pModule
          && NULL == fpGetClassObject))
    {
        return MUX_E_INVALIDARG;
    }

    // Verify that the requested class ids are not already registered.
    //
    MODULE_INFO *pModule = NULL;
    int i;
    for (i = 0; i < nci; i++)
    {
        pModule = ModuleFindFromCID(aci[i].cid);
        if (NULL != pModule)
        {
            return MUX_E_INVALIDARG;
        }
    }

    // Find corresponding MODULE_INFO. Since we're the one that requested the
    // module to register its classes, we know which module is registering.
    //
    pModule = g_pModule;
    if (NULL == pModule)
    {
        // These classes are implemented in the main program (netmux or
        // stubslave).
        //
        pModule = &g_MainModule;
        if (NULL != pModule->fpGetClassObject)
        {
            // The main program is attempting to register another handler.
            //
            return MUX_E_FAIL;
        }
    }

    // Make sure there is enough room in the class table for additional class
    // ids.
    //
    if (g_nClassesAllocated < g_nClasses + nci)
    {
        int nAllocate = GrowByFactor(g_nClasses + nci);

        CLASS_INFO_PRIVATE *pNewClasses = NULL;
        try
        {
            pNewClasses = new CLASS_INFO_PRIVATE[nAllocate];
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pNewClasses)
        {
            return MUX_E_OUTOFMEMORY;
        }

        if (NULL != g_pClasses)
        {
            int j;
            for (j = 0; j < g_nClasses; j++)
            {
                pNewClasses[j] = g_pClasses[j];
            }

            delete [] g_pClasses;
            g_pClasses = NULL;
        }

        g_pClasses = pNewClasses;
        g_nClassesAllocated = nAllocate;
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
        ClassAdd(&(aci[i]), pModule);
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

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RevokeClassObjects(int nci, CLASS_INFO aci[])
{
    if (  nci <= 0
       || NULL == aci)
    {
        return MUX_E_INVALIDARG;
    }

    // Verify that all class ids in this request are handled by the same module.
    //
    MODULE_INFO *pModule = NULL;
    int i;
    for (i = 0; i < nci; i++)
    {
        MODULE_INFO *q = ModuleFindFromCID(aci[i].cid);
        if (NULL == q)
        {
            // Attempt to revoke a class ids which were never registered.
            //
            return MUX_E_INVALIDARG;
        }
        else if (NULL == pModule)
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
        pModule->fpGetClassObject = NULL;
    }

    // Remove the requested class ids.
    //
    for (i = 0; i < nci; i++)
    {
        ClassRemove(aci[i].cid);
    }
    return MUX_S_OK;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RegisterInterfaces(int nii, INTERFACE_INFO aii[])
{
    if (  nii <= 0
       || NULL == aii)
    {
        return MUX_E_INVALIDARG;
    }

    // Make sure there is enough room in the interface table.
    //
    if (g_nInterfacesAllocated < g_nInterfaces + nii)
    {
        int nAllocate = GrowByFactor(g_nInterfaces + nii);

        INTERFACE_INFO *pNewInterfaces = NULL;
        try
        {
            pNewInterfaces = new INTERFACE_INFO[nAllocate];
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pNewInterfaces)
        {
            return MUX_E_OUTOFMEMORY;
        }

        if (NULL != g_pInterfaces)
        {
            int j;
            for (j = 0; j < g_nInterfaces; j++)
            {
                pNewInterfaces[j] = g_pInterfaces[j];
            }

            delete [] g_pInterfaces;
            g_pInterfaces = NULL;
        }

        g_pInterfaces = pNewInterfaces;
        g_nInterfacesAllocated = nAllocate;
    }

    for (int i = 0; i < nii; i++)
    {
        InterfaceAdd(&aii[i]);
    }
    return MUX_S_OK;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RevokeInterfaces(int nii, INTERFACE_INFO aii[])
{
    if (  nii <= 0
       || NULL == aii)
    {
        return MUX_E_INVALIDARG;
    }

    for (int i = 0; i < nii; i++)
    {
        InterfaceRemove(aii[i].iid);
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

#ifdef WIN32
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_AddModule(const UTF8 aModuleName[], const UTF16 aFileName[])
#else
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_AddModule(const UTF8 aModuleName[], const UTF8 aFileName[])
#endif // WIN32
{
    MUX_RESULT mr;
    if (NULL == g_pModule)
    {
        // Create new MODULE_INFO.
        //
        MODULE_INFO *pModule = ModuleAdd(aModuleName, aFileName);
        if (NULL != pModule)
        {
            // Ask module to register its classes.
            //
            ModuleLoad(pModule);
            if (pModule->bLoaded)
            {
                g_pModule = pModule;
                mr = pModule->fpRegister();
                g_pModule = NULL;
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

/*! \brief Remove module from the set of available modules.
 *
 * Modules do not use this.
 *
 * \param UTF8     Filename of dynamic module to remove.
 * \return         MUX_RESULT
 */

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_RemoveModule(const UTF8 aModuleName[])
{
    MUX_RESULT mr;
    if (NULL == g_pModule)
    {
        MODULE_INFO *pModule = ModuleFindFromName(aModuleName);
        if (NULL != pModule)
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
                g_pModule = pModule;
                mr = pModule->fpUnregister();
                g_pModule = NULL;

                if (MUX_SUCCEEDED(mr))
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
                mr = MUX_E_FAIL;
            }
        }
        else
        {
            mr = MUX_E_NOTFOUND;
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
    if (iModule < 0)
    {
        return MUX_E_INVALIDARG;
    }

    MODULE_INFO *pModule = g_pModuleList;
    while (NULL != pModule)
    {
        if (0 == iModule)
        {
            pModuleInfo->bLoaded = pModule->bLoaded;
            pModuleInfo->pName   = pModule->pModuleName;
            return MUX_S_OK;
        }
        iModule--;
        pModule = pModule->pNext;
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
    // We can query each loaded module and unload the ones that are unloadable.
    //
    MODULE_INFO *pModule = g_pModuleList;
    while (NULL != pModule)
    {
        if (pModule->bLoaded)
        {
            MUX_RESULT mr = pModule->fpCanUnloadNow();
            if (  MUX_SUCCEEDED(mr)
               && MUX_S_FALSE != mr)
            {
                ModuleUnload(pModule);
            }
        }
        pModule = pModule->pNext;
    }
    return MUX_S_OK;
}

static bool GrowChannels(void);

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_InitModuleLibrary(process_context ctx, PipePump *fpPipePump, QUEUE_INFO *pQueue_In, QUEUE_INFO *pQueue_Out)
{
    if (IsUninitialized == g_ProcessContext)
    {
        g_ProcessContext = ctx;
        if (  NULL != fpPipePump
           && NULL != pQueue_In
           && NULL != pQueue_Out
           && GrowChannels())
        {
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
        }
        else
        {
            g_fpPipePump = NULL;
            g_pQueue_In  = NULL;
            g_pQueue_Out = NULL;
        }
        return MUX_S_OK;
    }
    else
    {
        return MUX_E_FAIL;
    }
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_FinalizeModuleLibrary(void)
{
    g_ProcessContext = IsUninitialized;
    return MUX_S_OK;
}

static MUX_RESULT Channel0_Call(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
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
        mux_IMarshal *pIMarshal = NULL;
        mr = mux_CreateInstance(CallFrame.cid, NULL, UseSameProcess, mux_IID_IMarshal, (void **)&pIMarshal);
        if (MUX_SUCCEEDED(mr))
        {
            MUX_CID cidProxy = 0;
            mr = pIMarshal->GetUnmarshalClass(CallFrame.iid, CrossProcess, &cidProxy);
            if (MUX_SUCCEEDED(mr))
            {
                Pipe_AppendBytes(pqi, sizeof(cidProxy), &cidProxy);
                mr = pIMarshal->MarshalInterface(pqi, CallFrame.iid, CrossProcess);
            }
            pIMarshal->Release();
        }
    }
    else
    {
       Pipe_EmptyQueue(pqi);
       mr = MUX_E_INVALIDARG;
    }
    return mr;
}

extern "C" void DCL_EXPORT DCL_API Pipe_InitializeQueueInfo(QUEUE_INFO *pqi)
{
    pqi->pHead = NULL;
    pqi->pTail = NULL;
    pqi->nBytes = 0;
}

static void FreeChannel(UINT32 nChannel)
{
    aChannels[nChannel].bAllocated = false;
    aChannels[nChannel].nChannel   = nChannel;
    aChannels[nChannel].pfCall     = NULL;
    aChannels[nChannel].pfMsg      = NULL;
    aChannels[nChannel].pfDisc     = NULL;
    aChannels[nChannel].pInterface = NULL;
}

static bool GrowChannels(void)
{
    int nNew = GrowByFactor(nChannels+1);
    CHANNEL_INFO *pNew = NULL;
    try
    {
        pNew = new CHANNEL_INFO[nNew];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL != pNew)
    {
        int i;
        if (NULL != aChannels)
        {
            for (i = 0; i < nChannels; i++)
            {
                pNew[i] = aChannels[i];
            }
            delete aChannels;
            aChannels = NULL;
        }
        else
        {
            // Initialized Channel 0 as always allocated.
            //
            pNew[0].bAllocated = true;
            pNew[0].nChannel   = 0;
            pNew[0].pfCall     = Channel0_Call;
            pNew[0].pfMsg      = NULL;
            pNew[0].pfDisc     = NULL;
            pNew[0].pInterface = NULL;
            nChannels = 1;
        }

        aChannels = pNew;
        pNew = NULL;
        for (i = nChannels; i < nNew; i++)
        {
            FreeChannel(i);
        }
        nChannels = nNew;
        return true;
    }
    else
    {
        return false;
    }
}

static UINT32 AllocateChannel(void)
{
    if (  NULL == aChannels
       && !GrowChannels())
    {
        return CHANNEL_INVALID;
    }

    for (;;)
    {
        for (int i = 0; i < nChannels; i++)
        {
            if (!aChannels[i].bAllocated)
            {
                aChannels[i].bAllocated = true;
                return i;
            }
        }

        if (!GrowChannels())
        {
            return CHANNEL_INVALID;
        }
    }
}

extern "C" PCHANNEL_INFO DCL_EXPORT DCL_API Pipe_AllocateChannel(FCALL *pfCall, FMSG *pfMsg, FDISC *pfDisc)
{
    UINT32 n = AllocateChannel();

    aChannels[n].pfCall     = pfCall;
    aChannels[n].pfMsg      = pfMsg;
    aChannels[n].pfDisc     = pfDisc;
    aChannels[n].pInterface = NULL;

    return &aChannels[n];
}

extern "C" void DCL_EXPORT DCL_API Pipe_FreeChannel(CHANNEL_INFO *pci)
{
    UINT32 n;
    if (  NULL != pci
       && pci == &aChannels[n = pci->nChannel]
       && n != 0
       && aChannels[n].bAllocated)
    {
        FreeChannel(n);
    }
}

extern "C" void DCL_EXPORT DCL_API Pipe_AppendBytes(QUEUE_INFO *pqi, size_t n, const void *p)
{
    if (  0 != n
       && NULL != p)
    {
        // Continue copying data to the end of the queue until it is all consumed.
        //
        QUEUE_BLOCK *pBlock = NULL;
        while (0 < n)
        {
            // We need an empty or partially filled QUEUE_BLOCK.
            //
            if (  NULL == pqi->pTail
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

                if (NULL != pBlock)
                {
                    pBlock->pNext   = NULL;
                    pBlock->pPrev   = NULL;
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
                if (NULL == pqi->pTail)
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
    if (  NULL != pqiOut
       && NULL != pqiIn)
    {
        QUEUE_BLOCK *pBlock = pqiIn->pHead;
        while (NULL != pBlock)
        {
            Pipe_AppendBytes(pqiOut, pBlock->nBuffer, pBlock->pBuffer);

            QUEUE_BLOCK *qBlock = pBlock->pNext;
            delete pBlock;
            pBlock = qBlock;
        }

        pqiIn->pHead = NULL;
        pqiIn->pTail = NULL;
        pqiIn->nBytes = 0;
    }
}

extern "C" void DCL_EXPORT DCL_API Pipe_EmptyQueue(QUEUE_INFO *pqi)
{
    if (NULL != pqi)
    {
        QUEUE_BLOCK *pBlock = pqi->pHead;

        // Free all the QUEUE_BLOCKs finally the owning QUEUE_INFO structure.
        //
        while (NULL != pBlock)
        {
            QUEUE_BLOCK *qBlock = pBlock->pNext;
            delete pBlock;
            pBlock = qBlock;
        }

        pqi->pHead = NULL;
        pqi->pTail = NULL;
        pqi->nBytes = 0;
    }
}

extern "C" bool DCL_EXPORT DCL_API Pipe_GetByte(QUEUE_INFO *pqi, UINT8 ach[1])
{
    QUEUE_BLOCK *pBlock;

    if (  NULL != pqi
       && NULL != (pBlock = pqi->pHead))
    {
        // Advance over empty blocks.
        //
        while (  NULL != pBlock
              && 0 == pBlock->nBuffer)
        {
            pqi->pHead = pBlock->pNext;
            if (NULL == pqi->pHead)
            {
                pqi->pTail = NULL;
            }
            delete pBlock;
            pBlock = pqi->pHead;
        }

        // If there is a block left on the list, it will have something.
        //
        if (NULL != pBlock)
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

    if (  NULL != pqi
       && NULL != pn)
    {
        size_t nWantedBytes = *pn;
        pBlock = pqi->pHead;
        while (  NULL != pBlock
              && 0 < nWantedBytes)
        {
            // Advance over empty blocks.
            //
            while (  NULL != pBlock
                  && 0 == pBlock->nBuffer)
            {
                pqi->pHead = pBlock->pNext;
                if (NULL == pqi->pHead)
                {
                    pqi->pTail = NULL;
                }
                delete pBlock;
                pBlock = pqi->pHead;
            }

            // If there is a block left on the list, it will have something.
            //
            if (NULL != pBlock)
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
    if (NULL != pqi)
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
int           iState = 0;
FrameType     eType = eUnknown;
union LENGTH
{
    UINT32 n;
    UINT8 ch[4];
} Length = { 0 };
UINT32        nChannel = 0;
size_t        nLengthRemaining = 0;

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
extern "C" bool DCL_EXPORT DCL_API Pipe_DecodeFrames(UINT32 nReturnChannel, QUEUE_INFO *pqiFrame)
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

            if (  !Pipe_GetBytes(g_pQueue_In, &nWanted, buffer)
               || 0 == nWanted)
            {
                return false;
            }
            Pipe_AppendBytes(pqiFrame, nWanted, buffer);
            nLengthRemaining -= nWanted;
        }
    }

    UINT8 ch;
    while (Pipe_GetByte(g_pQueue_In, &ch))
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
            nLengthRemaining = Length.n;

            // We've been told how long to expect the packet to be.
            //
            while (0 < nLengthRemaining)
            {
                size_t nWanted = nLengthRemaining;
                if (QUEUE_BLOCK_SIZE < nWanted)
                {
                    nWanted = QUEUE_BLOCK_SIZE;
                }

                if (  !Pipe_GetBytes(g_pQueue_In, &nWanted, buffer)
                   || 0 == nWanted)
                {
                    // We'll leave the state machine and try to pick up again at the same place later.
                    //
                    return false;
                }
                Pipe_AppendBytes(pqiFrame, nWanted, buffer);
                nLengthRemaining -= nWanted;
            }
            break;

        case 12: // Cleanup

            // Something went wrong. Re-initialize all the decoding variables.
            //
            eType   = eUnknown;
            Length.n = 0;
            nChannel = 0;
            Pipe_EmptyQueue(pqiFrame);
            break;

        case 13: // Accept
            if (4 <= Length.n)
            {
                size_t nWanted = sizeof(nChannel);
                if (  Pipe_GetBytes(pqiFrame, &nWanted, &nChannel)
                   && nWanted == sizeof(nChannel))
                {
                    if (eReturn == eType)
                    {
                        if (nChannel == nReturnChannel)
                        {
                            eType    = eUnknown;
                            Length.n = 0;
                            nChannel = 0;
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
                        if (  nChannel < nChannels
                           && aChannels[nChannel].bAllocated)
                        {
                            switch (eType)
                            {
                            case eCall:
                                if (NULL != aChannels[nChannel].pfCall)
                                {
                                    aChannels[nChannel].pfCall(&aChannels[nChannel], pqiFrame);

                                    // Send Queue_Frame back to sender.
                                    //
                                    UINT32 nReturn = sizeof(nChannel) + Pipe_QueueLength(pqiFrame);
    
                                    Pipe_AppendBytes(g_pQueue_Out, sizeof(ReturnMagic), ReturnMagic);
                                    Pipe_AppendBytes(g_pQueue_Out, sizeof(nReturn), &nReturn);
                                    Pipe_AppendBytes(g_pQueue_Out, sizeof(nChannel), &nChannel);
                                    Pipe_AppendQueue(g_pQueue_Out, pqiFrame);
                                    Pipe_AppendBytes(g_pQueue_Out, sizeof(EndMagic), EndMagic);
                                }
                                break;
    
                            case eMessage:
                                if (NULL != aChannels[nChannel].pfMsg)
                                {
                                    aChannels[nChannel].pfMsg(&aChannels[nChannel], pqiFrame);
                                }
                                break;
    
                            case eDisconnect:
                                if (NULL != aChannels[nChannel].pfDisc)
                                {
                                    aChannels[nChannel].pfDisc(&aChannels[nChannel], pqiFrame);
                                }
                                break;
                            }
                        }
                    }
                }
            }

            // The packet was too short to contain a channel number, the
            // channel did not exist, or the call completed successfully.
            //
            eType    = eUnknown;
            Length.n = 0;
            nChannel = 0;
            Pipe_EmptyQueue(pqiFrame);
            break;
        }
    }
    return false;
}

static void Pipe_SendReceive(UINT32 nChannel, QUEUE_INFO *pqi)
{
    for (;;)
    {
        g_fpPipePump();
        if (Pipe_DecodeFrames(nChannel, pqi))
        {
            break;
        }
    }
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API Pipe_SendCallPacketAndWait(UINT32 nChannel, QUEUE_INFO *pqiFrame)
{
    UINT32 nLength = sizeof(nChannel) + Pipe_QueueLength(pqiFrame);
    Pipe_AppendBytes(g_pQueue_Out, sizeof(CallMagic), CallMagic);
    Pipe_AppendBytes(g_pQueue_Out, sizeof(nLength), &nLength);
    Pipe_AppendBytes(g_pQueue_Out, sizeof(nChannel), &nChannel);
    Pipe_AppendQueue(g_pQueue_Out, pqiFrame);
    Pipe_AppendBytes(g_pQueue_Out, sizeof(EndMagic), EndMagic);
    Pipe_SendReceive(nChannel, pqiFrame);
    return MUX_S_OK;
}
