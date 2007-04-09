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

// TODO:
//
//  - Start lists for bookeeping modules for something like @list modules.
//  - Add export for all the mux_CanUnloadNow() for every module to be called
//    during @dbck.
//  - When module is loaded, call export to get its component id list and add
//    them to the master list.

extern "C"
{
    typedef MUX_RESULT FPGETCLASSOBJECT(UINT64 cid, UINT64 iid, void **ppv);
};

/*! \brief Creates an instance of the given class with the given interface.
 *
 * \param UINT64    Class ID
 * \param UINT64    Interface ID
 * \return          MUX_RESULT
 */

extern "C" DCL_EXPORT MUX_RESULT mux_CreateInstance(UINT64 cid, UINT64 iid, void **ppv)
{
#ifdef WIN32
    HINSTANCE hInst = LoadLibrary(L"bin\\sample.dll");
    if (hInst)
    {
        FPGETCLASSOBJECT *fpGetClassObject = (FPGETCLASSOBJECT *)GetProcAddress(hInst, "mux_GetClassObject");
        if (NULL != fpGetClassObject)
        {
            mux_IClassFactory *pIClassFactory = NULL;
            MUX_RESULT mr = fpGetClassObject(cid, mux_IID_IClassFactory, (void **)&pIClassFactory);
            if (  MUX_SUCCEEDED(mr)
               && NULL != pIClassFactory)
            {
                mr = pIClassFactory->CreateInstance(iid, ppv);
                pIClassFactory->Release();
            }
            return mr;
        }
    }
#else // !WIN32
#ifdef HAVE_DLOPEN
    void *mh = dlopen("bin/sample.so", RTLD_LAZY);
    if (NULL != mh)
    {
        FPGETCLASSOBJECT *fpGetClassObject = (FPGETCLASSOBJECT *)dlsym(mh, "mux_GetClassObject");
        if (NULL != fpGetClassObject)
        {
            mux_IClassFactory *pIClassFactory = NULL;
            MUX_RESULT mr = fpGetClassObject(cid, mux_IID_IClassFactory, (void **)&pIClassFactory);
            if (  MUX_SUCCEEDED(mr)
               && NULL != pIClassFactory)
            {
                mr = pIClassFactory->CreateInstance(iid, ppv);
                pIClassFactory->Release();
            }
            return mr;
        }
    }
#endif // HAVE_DLOPEN
#endif // !WIN32
    return MUX_E_CLASSNOTAVAILABLE;
}

/*! \brief Register component ids and factory implemented by the process binary.
 *
 * Modules do not use this. There is a separate mechanism for obtaining and their
 * component ids which occurs when the module is loaded.
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
 * Modules do not use this. There is a separate mechanism for obtaining their component ids.
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
 * \param void **  This will eventually be a module information structure.
 * \return         MUX_RESULT
 */

extern "C" DCL_EXPORT MUX_RESULT mux_ModuleInfo(int iModule, void **pv)
{
    return MUX_E_NOTIMPLEMENTED;
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
    return MUX_S_OK;
}
