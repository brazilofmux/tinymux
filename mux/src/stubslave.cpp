/*! \file stubslave.cpp
 * \brief This slave hosts modules in a separate process.
 *
 * $Id$
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"

#if defined(HAVE_DLOPEN) || defined(WIN32)

#include "libmux.h"
#include "modules.h"

#define MAX_STRING 1000

int main(int argc, char *argv[])
{
    pid_t parent_pid = getppid();
    if (parent_pid == 1)
    {
        // Our real parent process is gone, and we have been inherited by the
        // init process.
        //
        return 1;
    }

    // TODO: Should we pass in a pipepump function here or not?
    //
    mux_InitModuleLibrary(IsSlaveProcess, NULL);
    for (;;)
    {
        char arg[MAX_STRING];
        int len = read(0, arg, sizeof(arg)-1);
        if (len == 0)
        {
            break;
        }

        if (len < 0)
        {
            if (EINTR == errno)
            {
                errno = 0;
                continue;
            }
            break;
        }

        arg[len] = '\0';

        write(1, "OK", 2);
    }
    mux_FinalizeModuleLibrary();
    return 0;
}

#endif
