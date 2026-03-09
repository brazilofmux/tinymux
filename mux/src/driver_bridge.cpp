/*! \file driver_bridge.cpp
 * \brief Driver-side bridges to engine COM interfaces.
 *
 * These functions provide the same signatures that driver files have
 * always called, but delegate through COM interfaces into the engine.
 * Engine files have their own implementations; these are for netmux
 * (driver) only.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "modules.h"
#include "driverstate.h"

// notify_check — driver-side bridge through mux_INotify.
//
void notify_check(dbref target, dbref sender, const UTF8 *msg, int key)
{
    if (g_pINotify)
    {
        g_pINotify->NotifyCheck(target, sender, msg, key);
    }
}
