/*! \file announce.c
 * \brief Port announcer.
 *
 * $Id$
 *
 * \verbatim
 *      announce - sits listening on a port, and whenever anyone connects
 *                 announces a message and disconnects them
 *
 *      Usage:  announce [port] < message_file
 *
 *      Author: Lawrence Brown <lpb@cs.adfa.oz.au>      Aug 90
 *
 *      Bits of code are adapted from the Berkeley telnetd sources
 *
 * \endverbatim
 */

#define PORT    2860

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

char   *Name;        // name of this program for error messages.
char    msg[8192];
size_t  nmsg;

int main(int argc, char *argv[])
{
    static struct sockaddr_in sin = {AF_INET};

    // Save program name for error messages.
    //
    Name = argv[0];

    // Assume PORT, but let command-line override.
    //
    unsigned short usPort = PORT;
    argc--;
    argv++;
    if (argc > 0)
    {
        // unless specified on command-line.
        //
        int iPort = atoi(*argv);
        if (0 <= iPort && iPort <= 65535)
        {
            usPort = (unsigned short)iPort;
        }
    }
    sin.sin_port = htons(usPort);

    // Read in message and translate CRLF/NL to something reasonable.
    //
    int ch;
    char *p = msg;
    while (  (ch = getchar()) != EOF
          && p + 2 < msg + sizeof(msg))
    {
        if (ch != '\r')
        {
            if (ch == '\n')
            {
                *p++ = '\r';
            }
            *p++ = ch;
        }
    }
    *p = '\0';
    nmsg = p - msg;

    signal(SIGHUP, SIG_IGN);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        perror("announce: socket");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
    {
        perror("setsockopt");
    }

    if (bind(s, (struct sockaddr *)&sin, sizeof sin) < 0)
    {
        perror("bind");
        exit(1);
    }

    int foo;
    if ((foo = fork()) != 0)
    {
        fprintf(stderr, "announce: pid %d running on port %d\n", foo, usPort);
        _exit(0);
    }
    else
    {
        setpriority(PRIO_PROCESS, getpid(), 10);
    }

    if (listen(s, 1) < 0)
    {
        // start listening on port.
        //
        perror("announce: listen");
        _exit(1);
    }

    for (;;)
    {
        // loop forever, accepting requests & printing msg.
        //
        socklen_t bar = sizeof(sin);
        int ns = accept(s, (struct sockaddr *)&sin, &bar);
        if (ns < 0)
        {
            perror("announce: accept");
            _exit(1);
        }
        char *host = inet_ntoa(sin.sin_addr);
        long ct = time(0L);
        fprintf(stderr, "CONNECTION made from %s at %s", host, ctime(&ct));
        write(ns, msg, nmsg);
        sleep(5);
        close(ns);
    }
}
