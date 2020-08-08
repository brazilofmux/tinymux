/*! \file announce.c
 * \brief Port announcer.
 *
 * \verbatim
 *      announce - sits listening on a port, and whenever anyone connects
 *                 announces a message and disconnects them
 *
 *      Usage:  announce [port] < message_file
 *
 *
 * \endverbatim
 */

#include "autoconf.h"

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#include <sys/resource.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define PORT    "2860"

char    msg[8192];
size_t  nmsg;
char host_address[1024];

void child(int s)
{
    setpriority(PRIO_PROCESS, getpid(), 10);

    if (listen(s, SOMAXCONN) < 0)
    {
        perror("announce: listen");
        exit(1);
    }

    for (;;)
    {
        // loop forever, accepting requests & printing msg.
        //
#if defined(HAVE_SOCKADDR_IN6)
        struct sockaddr_in6 sai;
#else
        struct sockaddr_in sai;
#endif
        socklen_t bar = sizeof(sai);
        int ns = accept(s, (struct sockaddr *)&sai, &bar);
        if (ns < 0)
        {
            perror("announce: accept");
            _exit(1);
        }

        if (0 == getnameinfo((struct sockaddr *)&sai, bar, host_address, sizeof(host_address), NULL, 0, NI_NUMERICHOST|NI_NUMERICSERV))
        {
            time_t ct = time(0L);
            fprintf(stderr, "CONNECTION made from %s at %s", host_address, ctime(&ct));
            write(ns, msg, nmsg);
            sleep(5);
            close(ns);
        }
    }
}

int main(int argc, char *argv[])
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
#if defined(HAVE_SOCKADDR_IN6)
    hints.ai_family = AF_UNSPEC;
#else
    hints.ai_family = AF_INET;
#endif
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // Assume PORT, but let command-line override.
    //
    const char *sPort = PORT;
    argc--;
    argv++;
    if (argc > 0)
    {
        // unless specified on command-line.
        //
        sPort = argv[0];
    }

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

    struct addrinfo *ai;
    struct addrinfo *servinfo;
    if (0 == getaddrinfo(NULL, sPort, &hints, &servinfo))
    {
        for (ai = servinfo; NULL != ai; ai = ai->ai_next)
        {
            int s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (s < 0)
            {
                perror("announce: socket");
                exit(1);
            }

            int opt = 1;
            if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
            {
                perror("SO_REUSEADDR");
            }

#if defined(HAVE_SOCKADDR_IN6)
            if (AF_INET6 == ai->ai_family)
            {
                opt = 1;
                if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&opt, sizeof(opt)) < 0)
                {
                    perror("IPV6_V6ONLY");
                }
            }
#endif

            if (bind(s, ai->ai_addr, ai->ai_addrlen) < 0)
            {
                perror("bind");
                exit(1);
            }

            int foo = fork();
            if (foo == -1)
            {
                perror("fork");
                exit(1);
            }
            else if (0 == foo)
            {
                freeaddrinfo(servinfo);
                child(s);
            }
            fprintf(stderr, "announce: pid %d running on port %s\n", foo, sPort);
            close(s);
        }
        freeaddrinfo(servinfo);
    }
}
