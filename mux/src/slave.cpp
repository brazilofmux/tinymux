/*! \file slave.cpp
 * \brief This slave does iptoname conversions.
 *
 * The philosophy is to keep this program as simple/small as possible.  It
 * routinely performs non-vfork forks()s, so the conventional wisdom is that
 * the smaller it is, the faster it goes.  However, with modern memory
 * management support (including copy on reference paging), size is probably
 * not the issue it once was.
 */

#include "autoconf.h"
#include "config.h"

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif // HAVE_NETDB_H

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif // HAVE_NETINET_IN_H

#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif // HAVE_SYS_FILE_H

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif // HAVE_SYS_IOCTL_H

#include <signal.h>
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif // HAVE_ARPA_INET_H

#ifdef _SGI_SOURCE
#define CAST_SIGNAL_FUNC (SIG_PF)
#else
#define CAST_SIGNAL_FUNC
#endif

pid_t parent_pid;

#define MAX_STRING 1000

//
// copy a string, returning pointer to the null terminator of dest
//
char *mux_stpcpy(char *dest, const char *src)
{
    while ((*dest = *src))
    {
        ++dest;
        ++src;
    }
    return (dest);
}

void child_timeout_signal(int iSig)
{
    // _exit(), not exit(): exit() runs atexit handlers and flushes stdio,
    // neither of which is async-signal-safe.
    //
    _exit(1);
}

int query(char *ip)
{
    // The response format is one record per line, "ip ' ' hostname '\n'", and
    // ip is copied verbatim below.  The hostname is sanitized (see #801), but
    // ip was not: a separator or newline inside it splits one record into two
    // on the parent side, which is the same forgery primitive #801 closed for
    // the hostname.  The caller now frames requests on newlines so this cannot
    // happen, but state the invariant here so a future change to the input
    // path cannot quietly reopen it.
    //
    for (const char *s = ip; '\0' != *s; s++)
    {
        if (  '\n' == *s
           || '\r' == *s
           || ' '  == *s)
        {
            return -1;
        }
    }

    const char *pHName = ip;

#if defined(HAVE_GETADDRINFO) && defined(HAVE_GETNAMEINFO)

    // Let getaddrinfo() fill out the sockinfo structure for us.
    //
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_ADDRCONFIG;

    struct addrinfo *servinfo;
    char host[MAX_STRING];
    if (0 == getaddrinfo(ip, nullptr, &hints, &servinfo))
    {
        for (struct addrinfo *p = servinfo; nullptr != p; p = p->ai_next)
        {
            // NI_NAMEREQD: fail instead of returning the numeric
            // address when there is no PTR record, so a numeric form
            // can never pose as the hostname below.
            //
            if (0 == getnameinfo(p->ai_addr, p->ai_addrlen, host, sizeof(host), nullptr, 0, NI_NUMERICSERV | NI_NAMEREQD))
            {
                pHName = host;
                break;
            }
        }
        freeaddrinfo(servinfo);
    }

#else

#ifndef INADDR_NONE
#define INADDR_NONE ((in_addr_t)-1)
#endif

    in_addr_t addr = inet_addr(ip);
    if (INADDR_NONE == addr)
    {
        return -1;
    }

#if defined(HAVE_GETHOSTBYADDR)
    struct hostent *hp = gethostbyaddr( reinterpret_cast<char *>(&addr), sizeof(addr), AF_INET);
    if (  nullptr != hp
       && strlen(hp->h_name) < MAX_STRING)
    {
        pHName = hp->h_name;
    }
#endif
#endif

    // Layout: ip ' ' pHName '\n' '\0'. Both ip and pHName can be
    // up to MAX_STRING - 1 bytes (999), so the maximum write is
    // 2 * (MAX_STRING - 1) + 3 = 2001 bytes. The previous
    // buf[MAX_STRING * 2] sized the buffer at 2000 and overran by
    // one byte in the worst case. Add three bytes of headroom for
    // the separator, newline, and terminator.
    //
    // No reverse name resolved (pHName still points at the numeric
    // address, either because getaddrinfo/getnameinfo failed or because
    // NI_NAMEREQD found no PTR record).  Send an empty hostname so the
    // parent keeps the numeric form: the LDH sanitizer below strips ':',
    // so a numeric IPv6 address allowed to pose as a hostname would be
    // mangled ("2001:db8::1" -> "2001db81") and then published to WHO,
    // the logs, and A_LASTSITE (#801).
    //
    if (0 == strcmp(pHName, ip))
    {
        pHName = "";
    }

    char buf[MAX_STRING * 2 + 3];
    char *p = mux_stpcpy(buf, ip);
    *p++ = ' ';

    // Sanitize the reverse-DNS hostname before it crosses the slave->parent
    // pipe (#801).  A client controls its own PTR record, so this name is
    // attacker-influenced.  Restrict it to the LDH-plus-dot/underscore host
    // charset, dropping every other byte and capping the length.  This strips
    // control characters, ANSI escapes, and spaces before they can reach the
    // parent's logs / A_LASTSITE attribute / WHO display, and -- critically --
    // removes any embedded newline, which would otherwise split one slave
    // response into multiple parsed records on the parent side and let an
    // attacker forge another connection's audit entry.
    size_t hostLen = 0;
    for (const char *s = pHName; '\0' != *s && hostLen < 255; s++)
    {
        unsigned char c = static_cast<unsigned char>(*s);
        if (  (c >= 'a' && c <= 'z')
           || (c >= 'A' && c <= 'Z')
           || (c >= '0' && c <= '9')
           || '.' == c || '-' == c || '_' == c)
        {
            *p++ = static_cast<char>(c);
            hostLen++;
        }
    }

    *p++ = '\n';
    *p++ = '\0';

    size_t len = strlen(buf);
    ssize_t written = write(1, buf, len);
    if (  written < 0
       || len != static_cast<size_t>(written))
    {
        return (-1);
    }
    return 0;
}

void alarm_signal(int iSig)
{
    struct itimerval itime;
    struct timeval interval;

    if (getppid() != parent_pid)
    {
        // _exit(): see child_timeout_signal().
        //
        _exit(1);
    }

    signal(SIGALRM, CAST_SIGNAL_FUNC alarm_signal);
    interval.tv_sec = 120;  // 2 minutes.
    interval.tv_usec = 0;
    itime.it_interval = interval;
    itime.it_value = interval;
    setitimer(ITIMER_REAL, &itime, 0);
}

#define MAX_CHILDREN 20
volatile sig_atomic_t nChildrenStarted = 0;
volatile sig_atomic_t nChildrenEndedSIGCHLD = 0;
volatile sig_atomic_t nChildrenEndedMain = 0;

void child_signal(int iSig)
{
    // Collect the children.
    //
    while (waitpid(0, nullptr, WNOHANG) > 0)
    {
        int nChildren = nChildrenStarted - nChildrenEndedSIGCHLD
            - nChildrenEndedMain;
        if (0 < nChildren)
        {
            nChildrenEndedSIGCHLD++;
        }
    }

    signal(SIGCHLD, CAST_SIGNAL_FUNC child_signal);
}

// Fork a child to resolve one address.  Returns false only if fork() failed.
//
static bool spawn_query(char *arg)
{
    pid_t child = fork();
    if (-1 == child)
    {
        return false;
    }

    if (0 == child)
    {
        // We don't want to try this for more than 5 minutes.
        //
        struct itimerval itime;
        struct timeval interval;

        interval.tv_sec = 300;  // 5 minutes.
        interval.tv_usec = 0;
        itime.it_interval = interval;
        itime.it_value = interval;
        signal(SIGALRM, CAST_SIGNAL_FUNC child_timeout_signal);
        setitimer(ITIMER_REAL, &itime, 0);

        _exit(query(arg) != 0);
    }

    nChildrenStarted++;

    int nChildren = nChildrenStarted - nChildrenEndedSIGCHLD
        - nChildrenEndedMain;

    // Collect the children.
    //
    while (waitpid(0, nullptr, (nChildren < MAX_CHILDREN) ? WNOHANG : 0) > 0)
    {
        if (0 < nChildren)
        {
            nChildrenEndedMain++;
        }
    }
    return true;
}

int main(int argc, char *argv[])
{
    // Requests arrive as newline-delimited addresses, and the pipe does not
    // preserve the parent's write boundaries: one read() can return several
    // queued requests, and can split one across two reads.  Treating each
    // read() as exactly one address (as this loop used to) meant that a burst
    // of connections -- the case that matters, since that is when the parent
    // queues lookups faster than the slave forks -- handed the concatenation
    // to the resolver.
    //
    // On the getaddrinfo path that lookup simply fails and both records are
    // discarded by the parent, so use_hostname silently degraded to numeric
    // addresses under load.  On the legacy gethostbyaddr path it is worse:
    // inet_addr("A\nB") parses as A and ignores the rest, so the slave
    // resolved A's PTR and emitted "A\nB <A's hostname>", which the parent
    // splits into a dropped line and "B <A's hostname>" -- attributing one
    // connection's hostname to another in WHO, the logs, and A_LASTSITE.
    //
    // Frame on newlines, exactly as the parent does when reading responses.
    //
    char buf[MAX_STRING * 2];
    size_t nBuf = 0;
    bool bDiscard = false;   // dropping an over-long line until its delimiter

    parent_pid = getppid();
    if (parent_pid == 1)
    {
        // Our real parent process is gone, and we have been inherited by the
        // init process.
        //
        exit(1);
    }

    alarm_signal(SIGALRM);
    signal(SIGCHLD, CAST_SIGNAL_FUNC child_signal);
    signal(SIGPIPE, SIG_DFL);

    for (;;)
    {
        ssize_t got = read(0, buf + nBuf, sizeof(buf) - nBuf - 1);
        if (0 == got)
        {
            break;
        }

        if (got < 0)
        {
            if (errno == EINTR)
            {
                errno = 0;
                continue;
            }
            break;
        }
        nBuf += static_cast<size_t>(got);

        // Dispatch every complete request in the buffer, leaving any partial
        // tail for the next read().
        //
        size_t start = 0;
        for (;;)
        {
            char *nl = static_cast<char *>(memchr(buf + start, '\n', nBuf - start));
            if (nullptr == nl)
            {
                break;
            }

            size_t next = static_cast<size_t>(nl - buf) + 1;
            if (bDiscard)
            {
                // Tail of an over-long line; its delimiter ends the drop.
                //
                bDiscard = false;
            }
            else
            {
                char *line = buf + start;
                *nl = '\0';

                size_t nLine = static_cast<size_t>(nl - line);
                while (  0 < nLine
                      && (  '\r' == line[nLine-1]
                         || ' '  == line[nLine-1]))
                {
                    line[--nLine] = '\0';
                }

                if (  0 < nLine
                   && !spawn_query(line))
                {
                    _exit(1);
                }
            }
            start = next;
        }

        // Keep the partial tail.
        //
        if (0 < start)
        {
            memmove(buf, buf + start, nBuf - start);
            nBuf -= start;
        }

        // A request with no delimiter that has filled the buffer cannot be a
        // real address (the parent sends at most a numeric address plus a
        // newline).  Drop it and resynchronize at the next delimiter rather
        // than resolving a truncated prefix or wedging on a full buffer.
        //
        if (nBuf >= sizeof(buf) - 1)
        {
            nBuf = 0;
            bDiscard = true;
        }
    }
    exit(0);
}
