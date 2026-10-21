/*! \file static_modules.cpp
 * \brief The modules a static build links into the program.
 *
 * Built only with MUX_STATIC_MODULES, into netmux and muxscript alike.  Each
 * module is partially linked into one object (static/Makefile) in which
 * everything but its four entry points is local, so its hidden symbols stay
 * as private to it as they are in its .so, and the entry points are renamed
 * <module>_mux_GetClassObject and so on, so several modules can share a link.
 * This table tells libmux which modules are here and where their entry
 * points are; mux_AddModule then finds them by the file name a configuration
 * gives, exactly as it would load them.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "libmux.h"

#if defined(MUX_STATIC_MODULES)

#define STATIC_MODULE_ENTRIES(m)                                              \
    extern "C" MUX_RESULT DCL_API m##_mux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv); \
    extern "C" MUX_RESULT DCL_API m##_mux_CanUnloadNow(void);                 \
    extern "C" MUX_RESULT DCL_API m##_mux_Register(void);                     \
    extern "C" MUX_RESULT DCL_API m##_mux_Unregister(void);

#define STATIC_MODULE(m)                                                      \
    { #m, m##_mux_GetClassObject, m##_mux_CanUnloadNow, m##_mux_Register, m##_mux_Unregister }

STATIC_MODULE_ENTRIES(engine)
STATIC_MODULE_ENTRIES(comsys_mod)
STATIC_MODULE_ENTRIES(mail_mod)
STATIC_MODULE_ENTRIES(exp3)

static const MUX_STATIC_MODULE s_aStaticModules[] =
{
    STATIC_MODULE(engine),
    STATIC_MODULE(comsys_mod),
    STATIC_MODULE(mail_mod),
    STATIC_MODULE(exp3),
    { nullptr, nullptr, nullptr, nullptr, nullptr }
};

// Registered before main runs, so the table is in place before anything can
// ask for a module.  libmux's side is a single pointer with no constructor,
// so the order of static initialization between the two does not matter.
//
static struct StaticModuleRegistrar
{
    StaticModuleRegistrar()
    {
        mux_RegisterStaticModules(s_aStaticModules);
    }
} s_StaticModuleRegistrar;

#endif // MUX_STATIC_MODULES
