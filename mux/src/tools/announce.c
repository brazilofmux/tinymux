/*
 *      announce - sits listening on a port, and whenever anyone connects
 *                 announces a message and disconnects them
 *
 *      Usage:  announce [port] < message_file
 *
 *      Author: Lawrence Brown <lpb@cs.adfa.oz.au>      Aug 90
 *
 *      Bits of code are adapted from the Berkeley telnetd sources
 */

#define PORT    2860

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>

char   *Name;        // name of this program for error messages.
char    msg[8192];
size_t  nmsg;

int main(int argc, char *argv[])
{
    int    s;
    int    ns;
    int    foo;
    static struct sockaddr_in sin = {AF_INET};
    char   *host;
    char   *inet_ntoa();
    long   ct;
    int    ch;
    char   *p;
    int    opt;

    // Save program name for error messages.
    //
    Name = argv[0];

    // Assume PORT, but let command-line override.
    //
    sin.sin_port = htons((u_short) PORT);
    argc--;
    argv++;
    if (argc > 0)
    {
        // unless specified on command-line.
        //
        sin.sin_port = atoi(*argv);
        sin.sin_port = htons((u_short) sin.sin_port);
    }

    // Read in message and translate CRLF/NL to something reasonable.
    //
    p = msg;
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
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        perror("announce: socket");
        exit(1);
    }
    opt = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
    {   
        perror("setsockopt");
    }
    if (bind(s, (struct sockaddr *)&sin, sizeof sin) < 0)
    {
        perror("bind");
        exit(1);
    }
    if ((foo = fork()) != 0)
    {
        fprintf(stderr, "announce: pid %d running on port %d\n", foo,
                ntohs((u_short) sin.sin_port));
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
    foo = sizeof sin;
    for (;;)
    {
        // loop forever, accepting requests & printing msg.
        //
        ns = accept(s, (struct sockaddr *)&sin, &foo);
        if (ns < 0)
        {
            perror("announce: accept");
            _exit(1);
        }
        host = inet_ntoa(sin.sin_addr);
        ct = time(0L);
        fprintf(stderr, "CONNECTION made from %s at %s",
                host, ctime(&ct));
        write(ns, msg, nmsg);
        sleep(5);
        close(ns);
    }
}

