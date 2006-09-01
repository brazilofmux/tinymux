// sqlslave.cpp -- This slave does SQL queries.
//
// $Id: sqlslave.cpp,v 1.3 2006/01/02 07:34:42 sdennis Exp $
//
#include "autoconf.h"
#include "config.h"
#include <dbi/dbi.h>

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

    dbi_conn conn;
    dbi_initialize(NULL);

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

    dbi_shutdown();
    return 0;
}
