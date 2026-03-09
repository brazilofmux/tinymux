/*! \file sitemon.cpp
 * \brief Site monitoring.
 *
 * Functions for site connection monitoring.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "interface.h"
#include "driverstate.h"
#include "driver_bridge.h"

void site_mon_send(const SOCKET port, const UTF8 *address, DESC *d, const UTF8 *msg)
{
    int host_info = 0;
    if (nullptr != d)
    {
        host_info = g_access_list.check(&d->address);
    }

    // Don't do sitemon for blocked sites.
    //
    if (host_info & HI_NOSITEMON)
    {
        return;
    }

    // Build the msg.
    //
    UTF8 *send_msg;
    const bool suspect = (0 != (host_info & HI_SUSPECT));
    if (IS_INVALID_SOCKET(port))
    {
        send_msg = tprintf(T("SITEMON: [UNKNOWN] %s from %s.%s"), msg, address,
            suspect ? T(" (SUSPECT)"): T(""));
    }
    else
    {
        send_msg = tprintf(T("SITEMON: [%d] %s from %s.%s"), port, msg,
            address, suspect ? T(" (SUSPECT)"): T(""));
    }

    for (auto it = g_descriptors_list.begin(); it != g_descriptors_list.end(); )
    {
        DESC* nd = *it;
        ++it;
        if (nd->flags & DS_CONNECTED)
        {
            if (drv_Flags(nd->player, FLAG_WORD3) & SITEMON)
            {
                queue_string(nd, send_msg);
                queue_write_LEN(nd, T("\r\n"), 2);
                process_output(nd, false);
            }
        }
    }
}
