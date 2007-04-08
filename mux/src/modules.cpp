/*! \file modules.cpp
 * \brief Module support
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
//  - Add export for netmux to pass in it's component ids and its factory
//    method.
//  - Start lists for bookeeping modules for something like @list modules.
//  - Add export for all the mux_CanUnloadNow() for every module to be called
//    during @dbck.
//  - When module is loaded, call export to get its component id list and add
//    them to the master list.
//  - When module is unloaded, remove its component ids from the master list.

extern "C"
{
    typedef MUX_RESULT FPGETCLASSOBJECT(UINT64 cid, UINT64 iid, void **ppv);
};

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
