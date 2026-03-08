/*! \file sqlproxy.cpp
 * \brief SQLProxy Module
 *
 * With standard marshaling, the IQueryControl proxy/stub lives in libmux.
 * This module is retained as an empty shell for backward compatibility
 * with configurations that load it.
 */

#include "../autoconf.h"
#include "../config.h"
#include "../libmux.h"
#include "../modules.h"

static int32_t g_cServerLocks = 0;

// The following four functions are for access by dlopen.
//
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_CanUnloadNow(void)
{
    if (0 == g_cServerLocks)
    {
        return MUX_S_OK;
    }
    else
    {
        return MUX_S_FALSE;
    }
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(cid);
    UNUSED_PARAMETER(iid);
    UNUSED_PARAMETER(ppv);

    return MUX_E_CLASSNOTAVAILABLE;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Register(void)
{
    return MUX_S_OK;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Unregister(void)
{
    return MUX_S_OK;
}
