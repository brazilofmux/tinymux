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
    typedef MUX_RESULT FPGETCLASSOBJECT(UINT64 cid, UINT64 iid, void **ppv);
    typedef MUX_RESULT FPCANUNLOADNOW(void);
    typedef MUX_RESULT FPREGISTERSERVER(void);
    typedef MUX_RESULT FPUNREGISTERSERVER(void);
};

#ifdef WIN32
typedef HINSTANCE MODULE_HANDLE;
#else
typedef void     *MODULE_HANDLE;
#endif

typedef struct
{
    FPGETCLASSOBJECT *fpGetClassObject;
    FPCANUNLOADNOW   *fpCanUnloadNow;
    MODULE_HANDLE    hInst;
    UTF8            *pFilename;
    bool             bLoaded;
} MODULE_INFO;

typedef struct
{
    UINT64           cid;
    MODULE_INFO     *pModule;
} CLASS_INFO;

int          nModules = 0;
MODULE_INFO *pModules = NULL;

int          nClasses = 0;
CLASS_INFO  *pClasses = NULL;

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
    pModule->hInst = LoadLibrary(pModule->pFilename);
    if (NULL != pModule->hInst)
    {
        pModule->fpGetClassObject = (FPGETCLASSOBJECT *)GetProcAddress(pModule->hInst, "mux_GetClassObject");
        pModule->fpCanUnloadNow   = (FPCANUNLOADNOW *)GetProcAddress(pModule->hInst, "mux_CanUnloadNow");
        if (  NULL != pModule->fpGetClassObject
           && NULL != pModule->fpCanUnloadNow)
        {
            pModule->bLoaded = true;
        }
        else
        {
            FreeLibrary(pModule->hInst);
        }
    }
#else // !WIN32
#ifdef HAVE_DLOPEN
    pModule->hInst = dlopen((char *)pModule->pFilename, RTLD_LAZY);
    if (NULL != pModule->hInst)
    {
        pModule->fpGetClassObject = (FPGETCLASSOBJECT *)dlsym(pModule->hInst, "mux_GetClassObject");
        pModule->fpCanUnloadNow = (FPCANUNLOADNOW *)dlsym(pModule->hInst, "mux_CanUnloadNow");
        if (  NULL != pModule->fpGetClassObject
           && NULL != pModule->fpCanUnloadNow)
        {
            pModule->bLoaded = true;
        }
        else
        {
            dlclose(pModule->hInst);
        }
    }
#endif // HAVE_DLOPEN
#endif // !WIN32
}

/*! \brief Unloads a known module.
 *
 * \param pModule   Module context record.
 */

static void ModuleUnload(MODULE_INFO *pModule)
{
    if (!pModule->bLoaded)
    {
        // Module is already in unloaded state.
        //
        return;
    }

#ifdef WIN32
    FreeLibrary(pModule->hInst);
#else
#ifdef HAVE_DLOPEN
    dlclose(pModule->hInst);
#endif
#endif
    pModule->hInst = NULL;
    pModule->fpGetClassObject = NULL;
    pModule->fpCanUnloadNow = NULL;
    pModule->bLoaded = false;
}

/*! \brief Find which module implements a particular class id.
 *
 * \param  UINT64   Class ID.
 * \return          Pointer to module.
 */

static MODULE_INFO *ModuleFindFromCID(UINT64 cid)
{
    int i;
    for (i = 0; i < nClasses; i++)
    {
        if (pClasses->cid == cid)
        {
            return pClasses->pModule;
        }
    }
    return NULL;
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
        if (!pModule->bLoaded)
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

/*! \brief Register component ids and factory implemented by the process binary.
 *
 * Modules must pass NULL for pFactory, but netmux must pass a non-NULL
 * pFactory.  For modules, the factory is obtained by using the
 * mux_GetClassObject export.
 *
 * \param int                   Number of component ids to register
 * \param UINT64[]              Component ID table.
 * \param mux_IClassFactory     Pointer to Factory capable of creating
 *                              instances of the given components.
 * \return                      MUX_RESULT
 */

extern "C" DCL_EXPORT MUX_RESULT mux_RegisterClassObjects(int ncid, UINT64 acid[], mux_IClassFactory *pFactory)
{
    return MUX_E_NOTIMPLEMENTED;
}

/*! \brief Register component ids and factory implemented by the process binary.
 *
 * \param int                   Number of component ids to register
 * \param UINT64[]              Component ID table.
 * \return                      MUX_RESULT
 */

extern "C" DCL_EXPORT MUX_RESULT mux_RevokeClassObjects(int ncid, UINT64 acid[])
{
    return MUX_E_NOTIMPLEMENTED;
}

/*! \brief Add module to the set of available modules.
 *
 * Modules do not use this.
 *
 * \param UTF8     Filename of dynamic module.
 * \return         MUX_RESULT
 */

extern "C" DCL_EXPORT MUX_RESULT mux_AddModule(UTF8 aModuleFileName[])
{
    return MUX_E_NOTIMPLEMENTED;
}

/*! \brief Remove module from the set of available modules.
 *
 * Modules do not use this.
 *
 * \param UTF8     Filename of dynamic module to remove.
 * \return         MUX_RESULT
 */

extern "C" DCL_EXPORT MUX_RESULT mux_RemoveModule(UTF8 aModuleFileName[])
{
    return MUX_E_NOTIMPLEMENTED;
}

/*! \brief Return information about a particular module.
 *
 * Modules do not use this.
 *
 * \param UTF8     Filename of dynamic module to remove.
 * \param void **  External module info structure.
 * \return         MUX_RESULT
 */

extern "C" DCL_EXPORT MUX_RESULT mux_ModuleInfo(int iModule, MUX_MODULE_INFO *pModuleInfo)
{
    if (  0 <= iModule
       && iModule < nModules
       && NULL != pModuleInfo)
    {
        pModuleInfo->bLoaded = pModules[iModule].bLoaded;
        pModuleInfo->pName   = pModules[iModule].pFilename;
        return MUX_S_OK;
    }
    return MUX_E_FAIL;
}

/*! \brief Periodic service tick for modules.
 *
 * Modules do not use this.
 *
 * \return         MUX_RESULT
 */

extern "C" DCL_EXPORT MUX_RESULT mux_ModuleTick(void)
{
    // We can query each loaded module and unload the ones that are unloadable.
    //
    int i;
    for (i = 0; i < nModules; i++)
    {
        MODULE_INFO *p = &pModules[i];
        if (  p->bLoaded
           && p->fpCanUnloadNow())
        {
            ModuleUnload(p);
        }
    }
    return MUX_S_OK;
}
