/*! \file modules.cpp
 * \brief Base-level module support
 *
 * $Id$
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#ifdef HAVE_DLOPEN
#include <dlfcn.h>
#endif // HAVE_DLOPEN

#include "modules.h"

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
    UTF8            *pModuleName;
    UTF8            *pFileName;
    bool             bLoaded;
} MODULE_INFO;

typedef struct
{
    UINT64           cid;
    MODULE_INFO     *pModule;
} CLASS_INFO;

static MODULE_INFO *g_pModuleList = NULL;
static MODULE_INFO *g_pModuleLast = NULL;

static MODULE_INFO  g_NetmuxModule =
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

static int          g_nClasses = 0;
static int          g_nClassesAllocated = 0;
static CLASS_INFO  *g_pClasses = NULL;

static MODULE_INFO *g_pModule = NULL;

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

static UTF8 *CopyString(const UTF8 *pString)
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

/*! \brief Find the first class ID not less than the requested class id.
 *
 * The return value may be beyond the end of the array, so callers should check bounds.
 *
 * \param  UINT64   Class ID.
 * \return          Index into g_pClasses.
 */

static int ClassFind(UINT64 cid)
{
    // Binary search for the class id.
    //
    int lo = 0;
    int mid;
    int hi = g_nClasses - 1;
    while (lo <= hi)
    {
        mid = ((hi - lo) >> 1) + lo;
        if (cid < g_pClasses[mid].cid)
        {
            hi = mid - 1;
        }
        else if (g_pClasses[mid].cid < cid)
        {
            lo = mid + 1;
        }
        else // (g_pClasses[mid].cid == cid)
        {
            return mid;
        }
    }
    return lo;

}

/*! \brief Find which module implements a particular class id.
 *
 * Note that callers may need to test for NetmuxModule and cannot assume the
 * returned module record is implemented in a module.
 *
 * \param  UINT64   Class ID.
 * \return          Pointer to module.
 */

static MODULE_INFO *ModuleFindFromCID(UINT64 cid)
{
    int i = ClassFind(cid);
    if (  i < g_nClasses
       && g_pClasses[i].cid == cid)
   {
        return g_pClasses[i].pModule;
    }
    return NULL;
}

/*! \brief Find module given its module name.
 *
 * Note that it is not possible to find the 'netmux' module this way.
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
 * Note that it is not possible to find the 'netmux' module this way.
 *
 * \param  UTF8[]    File name.
 * \return           Corresponding module record or NULL if not found.
 */

static MODULE_INFO *ModuleFindFromFileName(const UTF8 aFileName[])
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
 * \param UINT64         Class ID
 * \param pModule        Module that implements it.
 * \return               None.
 */

static void ClassAdd(UINT64 cid, MODULE_INFO *pModule)
{
    int i = ClassFind(cid);
    if (  i < g_nClasses
       && g_pClasses[i].cid == cid)
    {
        return;
    }

    if (i != g_nClasses)
    {
        memmove( g_pClasses + i + 1,
                 g_pClasses + i,
                 (g_nClasses - i) * sizeof(CLASS_INFO));
    }
    g_nClasses++;

    g_pClasses[i].cid = cid;
    g_pClasses[i].pModule = pModule;
}

/*! \brief Removes a class id from the table while maintaining order.
 *
 * \param UINT64         Class ID
 * \param pModule        Module that implements it.
 * \return               None.
 */

static void ClassRemove(UINT64 cid)
{
    int i = ClassFind(cid);
    if (  i < g_nClasses
       && g_pClasses[i].cid == cid)
    {
        g_nClasses--;
        if (i != g_nClasses)
        {
            memmove( g_pClasses + i,
                     g_pClasses + i + 1,
                     (g_nClasses - i) * sizeof(CLASS_INFO));
        }
    }
}

/*! \brief Adds a module.
 *
 * \param aModuleName[]  Filename of Module
 * \return               Module context record, NULL if out of memory or
 *                       duplicate found.
 */

static MODULE_INFO *ModuleAdd(const UTF8 aModuleName[], const UTF8 aFileName[])
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
        pModule->pModuleName = CopyString(aModuleName);
        pModule->pFileName = CopyString(aFileName);
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
                    ClassRemove(g_pClasses[i].cid);
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

#ifdef WIN32
    size_t   nExternalName;
    wchar_t *pExternalName = ConvertFromUTF8ToUTF16(pModule->pFileName, &nExternalName);
#else
    char *pExternalName = pModule->pFileName;
#endif
    pModule->hInst = MOD_OPEN(pExternalName);
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
 * \param  UINT64    Class ID
 * \param  UINT64    Interface ID
 * \return           MUX_RESULT
 */

extern "C" DCL_EXPORT MUX_RESULT mux_CreateInstance(UINT64 cid, UINT64 iid, void **ppv)
{
    MODULE_INFO *pModule = ModuleFindFromCID(cid);
    if (NULL != pModule)
    {
        if (pModule == &g_NetmuxModule)
        {
            if (NULL == pModule->fpGetClassObject)
            {
                return MUX_E_CLASSNOTAVAILABLE;
            }
        }
        else if (!pModule->bLoaded)
        {
            ModuleLoad(pModule);
            if (!pModule->bLoaded)
            {
                return MUX_E_CLASSNOTAVAILABLE;
            }
        }

        mux_IClassFactory *pIClassFactory = NULL;
        MUX_RESULT mr = pModule->fpGetClassObject(cid, mux_IID_IClassFactory, (void **)&pIClassFactory);
        if (  MUX_SUCCEEDED(mr)
           && NULL != pIClassFactory)
        {
            mr = pIClassFactory->CreateInstance(iid, ppv);
            pIClassFactory->Release();
        }
        return mr;
    }
    return MUX_E_CLASSNOTAVAILABLE;
}

/*! \brief Register class ids and factory implemented by the process binary.
 *
 * Modules must pass NULL for pfGetClassObject, but netmux must pass a
 * non-NULL pfGetClassObject.  For modules, the class factory is obtained by
 * using the mux_GetClassObject export.
 *
 * \param int                   Number of class ids to register
 * \param UINT64[]              Class ID table.
 * \param mux_IClassFactory     Pointer to Factory capable of creating
 *                              instances of the given components.
 * \return                      MUX_RESULT
 */

extern "C" DCL_EXPORT MUX_RESULT mux_RegisterClassObjects(int ncid, UINT64 acid[], FPGETCLASSOBJECT *fpGetClassObject)
{
    if (ncid <= 0)
    {
        return MUX_E_INVALIDARG;
    }

    // Modules export a mux_GetClassObject handler, but netmux must pass its
    // handler in here. Also, it doesn't make sense to load and unload netmux.
    // But, we want to allow netmux to provide module interfaces, so some
    // special-casing is done to allow that.
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
    for (i = 0; i < ncid; i++)
    {
        pModule = ModuleFindFromCID(acid[i]);
        if (NULL != pModule)
        {
            return MUX_E_INVALIDARG;
        }
    }

    // Find corresponding MODULE_INFO. Since we're the ones that requested the
    // module to register its classes, we know which module is registering.
    //
    pModule = g_pModule;
    if (NULL == pModule)
    {
        // These classes are implemented in netmux.
        //
        pModule = &g_NetmuxModule;
        if (NULL != pModule->fpGetClassObject)
        {
            // Netmux is attempting to register another handler.
            //
            return MUX_E_FAIL;
        }
    }

    // Make sure there is enough room in the class table for additional class
    // ids.
    //
    if (g_nClassesAllocated < g_nClasses + ncid)
    {
        int nAllocate = GrowByFactor(g_nClasses + ncid);

        CLASS_INFO *pNewClasses = NULL;
        try
        {
            pNewClasses = new CLASS_INFO[nAllocate];
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

    for (i = 0; i < ncid; i++)
    {
        ClassAdd(acid[i], pModule);
    }
    return MUX_S_OK;
}

/*! \brief De-register class ids and possibly the handler implemented by the
 *         process binary.
 *
 * \param int                   Number of component ids to register
 * \param UINT64[]              Component ID table.
 * \return                      MUX_RESULT
 */

extern "C" DCL_EXPORT MUX_RESULT mux_RevokeClassObjects(int ncid, UINT64 acid[])
{
    if (ncid <= 0)
    {
        return MUX_E_INVALIDARG;
    }

    // Verify that all class ids in this request are handled by the same module.
    //
    MODULE_INFO *pModule = NULL;
    int i;
    for (i = 0; i < ncid; i++)
    {
        MODULE_INFO *q = ModuleFindFromCID(acid[i]);
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

    // If these classes are implemented by netmux, we need to clear the
    // handler as well.
    //
    if (pModule == &g_NetmuxModule)
    {
        pModule->fpGetClassObject = NULL;
    }

    // Remove the requested class ids.
    //
    for (i = 0; i < ncid; i++)
    {
        ClassRemove(acid[i]);
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

extern "C" DCL_EXPORT MUX_RESULT mux_AddModule(const UTF8 aModuleName[], const UTF8 aFileName[])
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

extern "C" DCL_EXPORT MUX_RESULT mux_RemoveModule(const UTF8 aModuleName[])
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
 * Modules do not use this.  Notice that the 'netmux' module is not included.
 *
 * \param UTF8     Filename of dynamic module to remove.
 * \param void **  External module info structure.
 * \return         MUX_S_OK if found, MUX_S_FALSE if at end of list,
 *                 MUX_E_INVALIDARG for invalid arguments.
 */

extern "C" DCL_EXPORT MUX_RESULT mux_ModuleInfo(int iModule, MUX_MODULE_INFO *pModuleInfo)
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
 * Modules do not use this.  Notice that the 'netmux' module is not unloaded.
 *
 * \return         MUX_RESULT
 */

extern "C" DCL_EXPORT MUX_RESULT mux_ModuleTick(void)
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
