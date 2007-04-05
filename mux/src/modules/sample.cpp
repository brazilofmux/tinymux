#include "../modules.h"

extern "C" MUX_RESULT mux_GetClassObject(UINT64 cid, UINT64 iid, void **ppv)
{
    return MUX_E_FAIL;
}

extern "C" MUX_RESULT mux_CanUnloadNow(void)
{
    return MUX_S_OK;
}
