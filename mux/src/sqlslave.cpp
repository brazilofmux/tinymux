// sqlslave.cpp -- This slave does SQL queries.
//
// $Id: sqlslave.cpp,v 1.2 2005-11-24 20:07:06 sdennis Exp $
//
#include "autoconf.h"
#include "config.h"

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
            if (errno == EINTR)
            {
                errno = 0;
                continue;
            }
            break;
        }

        arg[len] = '\0';

        write(1, "OK", 2);
    }
    return 0;
}
