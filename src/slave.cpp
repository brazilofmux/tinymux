//
// This slave does iptoname conversions, and identquery lookups.
// 
// The philosophy is to keep this program as simple/small as possible.
// It does normal fork()s, so the smaller it is, the faster it goes.
// 
// $Id: slave.cpp,v 1.7 2001-02-09 18:13:28 sdennis Exp $
//
#include "autoconf.h"

#include <netdb.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <signal.h>
#include "slave.h"
#include <arpa/inet.h>

#ifdef _SGI_SOURCE
#define CAST_SIGNAL_FUNC (SIG_PF)
#else
#define CAST_SIGNAL_FUNC
#endif

pid_t parent_pid;

#define MAX_STRING 8000

#define NO_PRINTF
#ifdef NO_PRINTF
const char Digits100[201] =
"001020304050607080900111213141516171819102122232425262728292\
031323334353637383930414243444546474849405152535455565758595\
061626364656667686960717273747576777879708182838485868788898\
09192939495969798999";

int Tiny_ltoa(long val, char *buf)
{
    char *p = buf;
    
    if (val < 0)
    {
        *p++ = '-';
        val = -val;
    }
    unsigned int uval = (unsigned int)val;
    
    char *q = p;
    
    const char *z;
    while (uval > 99)
    {
        z = Digits100 + ((uval % 100) << 1);
        uval /= 100;
        *p++ = *z;
        *p++ = *(z+1);
    }
    z = Digits100 + (uval << 1);
    *p++ = *z;
    if (uval > 9)
    {
        *p++ = *(z+1);
    }

    int nLength = p - buf;
    *p-- = '\0';

    // The digits are in reverse order with a possible leading '-'
    // if the value was negative. q points to the first digit,
    // and p points to the last digit.
    //
    while (q < p)
    {
        // Swap characters are *p and *q
        //
        char temp = *p;
        *p = *q;
        *q = temp;

        // Move p and first digit towards the middle.
        //
        --p;
        ++q;

        // Stop when we reach or pass the middle.
        //
    }
    return nLength;
}
#endif // NO_PRINTF

RETSIGTYPE child_timeout_signal(int iSig)
{
    exit(1);
}

int query(int nIP, char *pIP, int nLine2, char *pLine2)
{
    struct sockaddr_in sin;
    int s;

    long addr = inet_addr(pIP);
    if (addr == -1)
    {
        return -1;
    }
    char *pHostName = pIP;
    int   nHostName = nIP;
    struct hostent *hp = gethostbyaddr((char *)&addr, sizeof(addr), AF_INET);
    if (hp)
    {
        pHostName = hp->h_name;
        nHostName = strlen(pHostName);
    }
    char buf[MAX_STRING];
    char *p = buf;
    memcpy(p, pIP, nIP);
    p += nIP;
    *p++ = ' ';
    memcpy(p, pHostName, nHostName);
    p += nHostName;
    *p++ = '\n';
    *p   = '\0';
    size_t nbuf = p - buf;

    int i = nLine2;
    for (int j = 0; j < 2; j++)
    {
        for (i--; 0 <= i && pLine2[i] != ','; i--)
        {
            ; // Nothing
        }
        if (i < 0)
        {
            return -1;
        }
    }
    char *pPortPair = pLine2 + i;
    *pPortPair++ = '\0';
    size_t nPortPair = nLine2 - i - 1;
    nLine2 = i;

    hp = gethostbyname(pLine2);
    if (hp == NULL)
    {
        static struct hostent def;
        static struct in_addr defaddr;
        static char *alist[1];
        static char namebuf[128];

        defaddr.s_addr = inet_addr(pLine2);
        if ((long)defaddr.s_addr == -1)
        {
            return -1;
        }
        memcpy(namebuf, pLine2, nLine2);
        def.h_name = namebuf;
        def.h_addr_list = alist;
        def.h_addr = (char *)&defaddr;
        def.h_length = sizeof(struct in_addr);

        def.h_addrtype = AF_INET;
        def.h_aliases = 0;
        hp = &def;
    }
    sin.sin_family = hp->h_addrtype;
    memcpy((char *)&sin.sin_addr, hp->h_addr, hp->h_length);
    sin.sin_port = htons(113); // ident port
    s = socket(hp->h_addrtype, SOCK_STREAM, 0);
    if (s < 0)
    {
        return -1;
    }
    char buf2[MAX_STRING];
    int  nbuf2 = 0;
    buf2[0] = '\0';
    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
    {
        if (   errno != ECONNREFUSED
            && errno != ETIMEDOUT
            && errno != ENETUNREACH
            && errno != EHOSTUNREACH)
        {
            close(s);
            return -1;
        }
    }
    else
    {
        int c;
        if (write(s, pPortPair, nPortPair) != nPortPair)
        {
            close(s);
            return -1;
        }
        if (write(s, "\r\n", 2) != 2)
        {
            close(s);
            return -1;
        }
        FILE *f = fdopen(s, "r");
        char pResult[MAX_STRING];
        p = pResult;
        while ((c = fgetc(f)) != EOF)
        {
            if (c == '\n')
            {
                break;
            }
            if (0x20 <= c && c <= 0x7E)
            {
                *p++ = c;
                if (p - pResult == MAX_STRING - 1)
                {
                    break;
                }
            }
        }
        *p = '\0';
        size_t nResult = p - pResult;
        fclose(f);
        p = buf2;
#ifdef NO_PRINTF
        p += Tiny_ltoa((addr >> 24) & 0xFF, p);
        *p++ = '.';
        p += Tiny_ltoa((addr >> 16) & 0xFF, p);
        *p++ = '.';
        p += Tiny_ltoa((addr >> 8) & 0xFF, p);
        *p++ = '.';
        p += Tiny_ltoa(addr & 0xFF, p);
#else
        sprintf(p, "%ld.%ld.%ld.%ld", (addr >> 24) & 0xFF,
            (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF);
        p += strlen(p);
#endif
        *p++ = ' ';
        memcpy(p, pResult, nResult);
        p += nResult;
        *p++ = '\n';
        *p   = '\0';
        nbuf2 = p - buf2;
    }
    char buf3[MAX_STRING*2];
    p = buf3;
    memcpy(p, buf, nbuf);
    p += nbuf;
    memcpy(p, buf2, nbuf2);
    p += nbuf2;
    size_t nbuf3 = p - buf3;
    write(1, buf3, nbuf3);
    return 0;
}

RETSIGTYPE child_signal(int iSig)
{
    // Collect any children.
    //

#ifdef NEXT
    while (wait3(NULL, WNOHANG, NULL) > 0)
    {
        ; // Nothing.
    }
#else
    while (waitpid(0, NULL, WNOHANG) > 0)
    {
        ; // Nothing.
    }
#endif
    signal(SIGCHLD, CAST_SIGNAL_FUNC child_signal);
}

RETSIGTYPE alarm_signal(int iSig)
{
    struct itimerval itime;
    struct timeval interval;

    if (getppid() != parent_pid)
    {
        exit(1);
    }
    signal(SIGALRM, CAST_SIGNAL_FUNC alarm_signal);
    interval.tv_sec = 120;  // 2 minutes.
    interval.tv_usec = 0;
    itime.it_interval = interval;
    itime.it_value = interval;
    setitimer(ITIMER_REAL, &itime, 0);
}


int main(int argc, char *argv[])
{
    char aIP[MAX_STRING + 1];

    parent_pid = getppid();
    if (parent_pid == 1)
    {
        exit(1);
    }
    alarm_signal(SIGALRM);
    signal(SIGCHLD, CAST_SIGNAL_FUNC child_signal);
    signal(SIGPIPE, SIG_DFL);

    for (;;)
    {
        int nRead = read(0, aIP, MAX_STRING);
        if (nRead == 0)
        {
            break;
        }
        if (nRead < 0)
        {
            if (errno == EINTR)
            {
                errno = 0;
                continue;
            }
            break;
        }
        if (aIP[nRead-1] == '\n')
        {
            nRead--;
        }
        aIP[nRead] = '\0';

        int nIP = nRead;
        int nLine2 = 0;
        char *pLine2 = strchr(aIP, '\n');
        if (pLine2)
        {
            nIP    = pLine2 - aIP;
            nLine2 = nRead  - nIP;
            *pLine2++ = '\0';
        }
        switch (fork())
        {
        case -1:
            exit(1);

        case 0: // child.
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
            }
            exit(query(nIP, aIP, nLine2, pLine2) != 0);
        }

        // collect any children 
        //
#ifdef NEXT
        while (wait3(NULL, WNOHANG, NULL) > 0)
        {
            ; // Nothing.
        }
#else
        while (waitpid(0, NULL, WNOHANG) > 0)
        {
            ; // Nothing.
        }
#endif
    }
    exit(0);
}
