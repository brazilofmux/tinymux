/*! \file sitemon.cpp
 * \brief Site monitoring and system resource listing.
 *
 * Functions for site connection monitoring and reporting system resource usage.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "interface.h"

void list_system_resources(dbref player)
{
    UTF8 buffer[80];

    int nTotal = 0;
    notify(player, T("System Resources"));

    mux_sprintf(buffer, sizeof(buffer), T("Total Open Files: %ld"), DebugTotalFiles);
    notify(player, buffer);
    nTotal += DebugTotalFiles;

    mux_sprintf(buffer, sizeof(buffer), T("Total Sockets: %ld"), DebugTotalSockets);
    notify(player, buffer);
    nTotal += DebugTotalSockets;

    mux_sprintf(buffer, sizeof(buffer), T("Total Handles (sum of above): %d"), nTotal);
    notify(player, buffer);
}


void site_mon_send(const SOCKET port, const UTF8 *address, DESC *d, const UTF8 *msg)
{
    int host_info = 0;
    if (nullptr != d)
    {
        host_info = mudstate.access_list.check(&d->address);
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

    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); )
    {
        DESC* nd = *it;
        ++it;
        if (nd->flags & DS_CONNECTED)
        {
            if (SiteMon(nd->player))
            {
                queue_string(nd, send_msg);
                queue_write_LEN(nd, T("\r\n"), 2);
                process_output(nd, false);
            }
        }
    }
}
