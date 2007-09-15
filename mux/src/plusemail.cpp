// plusmail.cpp -- Quicky module implementing Firan-like @email function.
//
// $Id$
//
// Rachel Blackman <sparks@noderunner.net>
//
// Config options:
//     mail_server               SMTP server to use (mail.bar.com)
//     mail_ehlo                 ehlo server (my.baz.com)
//     mail_sendaddr             return address (foo@bar.com)
//     mail_sendname             RFC822 'name' (FooMUSH)
//     mail_subject              Defaults subject link (FooMUSH Mail)
//

#include "autoconf.h"
#include "config.h"
#include "_build.h"

#ifndef WIN32
#include <netdb.h>
#endif

#include "externs.h"

// Encode mail body such that CRLF.CRLF only appears at the end of the
// message.  Here are some example transforms.
//
//   <CR><LF>.<anytext><CR><LF> --> <CR><LF>..<CR><LF>
//   <BOM>.<anytext><CR><LF>    --> <BOM>..<anytext><CR><LF>
//   <anytext><EOM>             --> <anytext><CR><LF>.<CR><LF>
//   <anytext><CR><LF><EOM>     --> <anytext><CR><LF>.<CR><LF>
//
// <CR> and <LF> are only allowed to occur as the <CR><LF> pair -- never alone.
//
// Code 0 - Any byte.
// Code 1 - NUL  (0x00)
// Code 2 - LF   (0x0A)
// Code 3 - CR   (0x0D)
// Code 4 - '.'  (0x2E)
// Code 5 - non-ASCII
//
static const unsigned char BodyClasses[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 3, 0, 0, // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 3
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 6
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 7
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, // 8
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, // 9
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, // A
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, // B
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, // C
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, // D
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, // E
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5  // F
};

#define STATE_NOTHING          0
#define STATE_BOM              1
#define STATE_HAVE_CR          2
#define STATE_HAVE_CRLF        3
#define STATE_HAVE_CRLF_DOT    4
#define STATE_HAVE_CRLF_DOT_CR 5
#define STATE_HAVE_DOT         6
#define STATE_HAVE_DOT_CR      7

// Action  0 - Remain in current state.
// Action  1 - Emit ch and remain in current state.
// Action  2 - Emit ch and move to STATE_NOTHING.
// Action  3 - Emit CRLF.CRLF and terminate.
// Action  4 - Move to STATE_HAVE_CR.
// Action  5 - Move to STATE_HAVE_DOT.
// Action  6 - Move to STATE_HAVE_CRLF.
// Action  7 - Emit CRLF+ch and move to STATE_NOTHING.
// Action  8 - Emit CRLF and move to STATE_HAVE_CR.
// Action  9 - Move to STATE_HAVE_CRLF_DOT.
// Action 10 - Emit CRLF..+ch and move to STATE_NOTHING.
// Action 11 - Emit CRLF..CRLF.CRLF and terminate.
// Action 12 - Move to STATE_HAVE_CRLF_DOT_CR.
// Action 13 - Emit CRLF.. and move to STATE_HAVE_CRLF.
// Action 14 - Emit ..+ch and move to STATE_NOTHING.
// Action 15 - Emit ..CRLF.CRLF and terminate.
// Action 16 - Move to STATE_HAVE_DOT_CR.
// Action 17 - Emit .. and move to STATE_HAVE_CRLF.
//

static const int BodyActions[8][6] =
{
//    Any  '\0' LF   CR   '.'  Non
    {  1,   3,   0,   4,   1,   0}, // STATE_NOTHING
    {  2,   3,   0,   4,   5,   0}, // STATE_BOM
    {  2,   3,   6,   0,   5,   0}, // STATE_HAVE_CR
    {  7,   3,   0,   8,   9,   0}, // STATE_HAVE_CRLF
    { 10,  11,   0,  12,  10,   0}, // STATE_HAVE_CRLF_DOT
    { 10,  11,  13,   0,  10,   0}, // STATE_HAVE_CRLF_DOT_CR
    { 14,  15,   0,  16,  14,   0}, // STATE_HAVE_DOT
    { 14,  15,  17,   0,  14,   0}  // STATE_HAVE_DOT_CR
};

char *EncodeBody(char *pBody)
{
    static char buf[2*LBUF_SIZE];
    char *bp = buf;

    int iState = STATE_BOM;
    for (;;)
    {
        char ch = *pBody++;
        int iAction = BodyActions[iState][BodyClasses[(unsigned char)ch]];
        switch (iAction)
        {
        case 0:
            // Action 0 - Remain in current state.
            //
            break;

        case 1:
            // Action 1 - Emit ch and remain in current state.
            //
            *bp++ = ch;
            break;

        case 2:
            // Action 2 - Emit ch and move to STATE_NOTHING.
            //
            *bp++ = ch;
            iState = STATE_NOTHING;
            break;

        case 3:
            // Action 3 - Emit CRLF.CRLF and terminate.
            //
            *bp++ = '\r';
            *bp++ = '\n';
            *bp++ = '.';
            *bp++ = '\r';
            *bp++ = '\n';
            *bp++ = '\0';
            return buf;
            break;

        case 4:
            // Action 4 - Move to STATE_HAVE_CR.
            //
            iState = STATE_HAVE_CR;
            break;

        case 5:
            // Action 5 - Move to STATE_HAVE_DOT.
            //
            iState = STATE_HAVE_DOT;
            break;

        case 6:
            // Action 6 - Move to STATE_HAVE_CRLF.
            //
            iState = STATE_HAVE_CRLF;
            break;

        case 7:
            // Action 7 - Emit CRLF+ch and move to STATE_NOTHING.
            //
            *bp++ = '\r';
            *bp++ = '\n';
            *bp++ = ch;
            iState = STATE_NOTHING;
            break;

        case 8:
            // Action 8 - Emit CRLF and move to STATE_HAVE_CR.
            //
            *bp++ = '\r';
            *bp++ = '\n';
            iState = STATE_HAVE_CR;
            break;

        case 9:
            // Action 9 - Move to STATE_HAVE_CRLF_DOT.
            //
            iState = STATE_HAVE_CRLF_DOT;
            break;

        case 10:
            // Action 10 - Emit CRLF..+ch and move to STATE_NOTHING.
            //
            *bp++ = '\r';
            *bp++ = '\n';
            *bp++ = '.';
            *bp++ = '.';
            *bp++ = ch;
            iState = STATE_NOTHING;
            break;

        case 11:
            // Action 11 - Emit CRLF..CRLF.CRLF and terminate.
            //
            *bp++ = '\r';
            *bp++ = '\n';
            *bp++ = '.';
            *bp++ = '.';
            *bp++ = '\r';
            *bp++ = '\n';
            *bp++ = '.';
            *bp++ = '\r';
            *bp++ = '\n';
            *bp++ = '\0';
            return buf;
            break;

        case 12:
            // Action 12 - Move to STATE_HAVE_CRLF_DOT_CR.
            //
            iState = STATE_HAVE_CRLF_DOT_CR;
            break;

        case 13:
            // Action 13 - Emit CRLF.. and move to STATE_HAVE_CRLF.
            //
            *bp++ = '\r';
            *bp++ = '\n';
            *bp++ = '.';
            *bp++ = '.';
            iState = STATE_HAVE_CRLF;
            break;

        case 14:
            // Action 14 - Emit ..+ch and move to STATE_NOTHING.
            //
            *bp++ = '.';
            *bp++ = '.';
            *bp++ = ch;
            iState = STATE_NOTHING;
            break;

        case 15:
            // Action 15 - Emit ..CRLF.CRLF and terminate.
            //
            *bp++ = '.';
            *bp++ = '.';
            *bp++ = '\r';
            *bp++ = '\n';
            *bp++ = '.';
            *bp++ = '\r';
            *bp++ = '\n';
            *bp++ = '\0';
            return buf;
            break;

        case 16:
            // Action 16 - Move to STATE_HAVE_DOT_CR.
            //
            iState = STATE_HAVE_DOT_CR;
            break;

        case 17:
            // Action 17 - Emit .. and move to STATE_HAVE_CRLF.
            //
            *bp++ = '.';
            *bp++ = '.';
            iState = STATE_HAVE_CRLF;
            break;
        }
    }
}

// Transform CRLF runs to a space.
//
char *ConvertCRLFtoSpace(const char *pString)
{
    static char buf[LBUF_SIZE];
    char *bp = buf;

    // Skip any leading CRLF as well as non-ASCII.
    //
    while (  '\r' == *pString
          || '\n' == *pString
          || (0x80 & *pString) == 0x80)
    {
        pString++;
    }

    bool bFirst = true;
    while (*pString)
    {
        if (!bFirst)
        {
            safe_chr(' ', buf, &bp);
        }
        else
        {
            bFirst = false;
        }

        while (  *pString
              && '\r' != *pString
              && '\n' != *pString
              && (0x80 & *pString) == 0x00)
        {
            safe_chr(*pString, buf, &bp);
            pString++;
        }

        // Skip any CRLF.
        //
        while (  '\r' == *pString
              || '\n' == *pString
              || 0x80 == (0x80 & *pString))
        {
            pString++;
        }
    }
    *bp = '\0';
    return buf;
}

// Write a formatted string to a socket.
//
static int DCL_CDECL mod_email_sock_printf(SOCKET sock, const char *format, ...)
{
    va_list vargs;
    char mybuf[2*LBUF_SIZE];

    if (IS_INVALID_SOCKET(sock))
    {
        return 0;
    }

    va_start(vargs, format);
    mux_vsnprintf(mybuf, sizeof(mybuf), format, vargs);
    va_end(vargs);

    return SOCKET_WRITE(sock, &mybuf[0], strlen(mybuf), 0);
}

// Read a line of input from the socket.
//
static int mod_email_sock_readline(SOCKET sock, char *buffer, int maxlen)
{
    buffer[0] = '\0';

    if (IS_INVALID_SOCKET(sock))
    {
        return 0;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);

    // Wait up to 1 second.
    //
    struct timeval tv;
    tv.tv_sec  = 1;
    tv.tv_usec = 0;

    // Check for data before giving up.
    //
    if (IS_SOCKET_ERROR(select(sock+1, &read_fds, NULL, NULL, &tv)))
    {
        return 0;
    }

    if (!FD_ISSET(sock, &read_fds))
    {
        return 0;
    }

    bool done = false;
    bool possible_close = false;
    int  pos = 0;

    while (  !done
          && pos < maxlen)
    {
        char getme[2];

        int numread = SOCKET_READ(sock, &getme[0], 1, 0);
        if (  IS_SOCKET_ERROR(numread)
           || 0 == numread)
        {
            if (possible_close)
            {
                done = true;
            }
            else
            {
                FD_ZERO(&read_fds);
                FD_SET(sock, &read_fds);

                // Wait up to 1 second.
                //
                tv.tv_sec  = 1;
                tv.tv_usec = 0;

                // Check for data before giving up.
                //
                if (IS_SOCKET_ERROR(select(sock+1, &read_fds, NULL, NULL, &tv)))
                {
                    done = true;
                }

                if (FD_ISSET(sock, &read_fds))
                {
                    possible_close = true;
                }
            }
        }
        else
        {
            possible_close = false;
            if (getme[0] != '\n')
            {
                buffer[pos++] = getme[0];
            }
            else
            {
                done = true;
            }
        }
    }
    buffer[pos] = '\0';

    return pos;
}

// Open a socket to a specific host/port.
//
static int mod_email_sock_open(const char *conhostname, int port, SOCKET *sock)
{
    struct hostent *conhost;
    struct sockaddr_in name;
    int addr_len;

    conhost = gethostbyname(conhostname);
    if (0 == conhost)
    {
        return -1;
    }

    name.sin_port = htons(port);
    name.sin_family = AF_INET;
    memcpy((char *)&name.sin_addr, (char *)conhost->h_addr, conhost->h_length);
    SOCKET mysock = socket(AF_INET, SOCK_STREAM, 0);
    addr_len = sizeof(name);

    if (connect(mysock, (struct sockaddr *)&name, addr_len) == -1)
    {
        return -2;
    }

    *sock = mysock;

    return 0;
}

static int mod_email_sock_close(SOCKET sock)
{
    return SOCKET_CLOSE(sock);
}

void do_plusemail(dbref executor, dbref cause, dbref enactor, int key,
                 int nfargs, char *arg1, char *arg2)
{
    UNUSED_PARAMETER(cause);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nfargs);

    char inputline[LBUF_SIZE];

    if ('\0' == mudconf.mail_server[0])
    {
        notify(executor, "@email: Not configured");
        return;
    }

    if (!arg1 || !*arg1)
    {
        notify(executor, "@email: I don't know who you want to e-mail!");
        return;
    }

    if (!arg2 || !*arg2)
    {
        notify(executor, "@email: Not sending an empty e-mail!");
        return;
    }

    char *addy = alloc_lbuf("mod_email_do_email.headers");
    char *bp = addy;
    safe_str(arg1, addy, &bp);
    *bp = '\0';

    char *subject = strchr(addy, '/');
    if (subject)
    {
        *subject = '\0';
        subject++;
    }
    else
    {
        subject = mudconf.mail_subject;
    }

    char *pMailServer = ConvertCRLFtoSpace(mudconf.mail_server);
    SOCKET mailsock = INVALID_SOCKET;
    int result = mod_email_sock_open(pMailServer, 25, &mailsock);

    if (-1 == result)
    {
        notify(executor, tprintf("@email: Unable to resolve hostname %s!",
            pMailServer));
        free_lbuf(addy);
        return;
    }
    else if (-2 == result)
    {
        // Periodically, we get a failed connect, for reasons which elude me.
        // In almost every case, an immediate retry works.  Therefore, we give
        // it one more shot, before we give up.
        //
        result = mod_email_sock_open(pMailServer, 25, &mailsock);
        if (0 != result)
        {
            notify(executor, "@email: Unable to connect to mailserver, aborting!");
            free_lbuf(addy);
            return;
        }
    }

    char *body = alloc_lbuf("mod_email_do_email.body");
    char *bodyptr = body;
    char *bodysrc = arg2;
    mux_exec(body, &bodyptr, executor, executor, executor,
        EV_TOP | EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &bodysrc, NULL, 0);
    *bodyptr = '\0';

    do
    {
        result = mod_email_sock_readline(mailsock, inputline, LBUF_SIZE - 1);
    } while (  0 == result
            || (  3 < result
               && '-' == inputline[3]));

    if (-1 == result)
    {
        mod_email_sock_close(mailsock);
        notify(executor, "@email: Connection to mailserver lost.");
        free_lbuf(body);
        free_lbuf(addy);
        return;
    }

    if ('2' != inputline[0])
    {
        mod_email_sock_close(mailsock);
        notify(executor, tprintf("@email: Invalid mailserver greeting (%s)",
            inputline));
    }

    mod_email_sock_printf(mailsock, "EHLO %s\r\n", ConvertCRLFtoSpace(mudconf.mail_ehlo));

    do
    {
        result = mod_email_sock_readline(mailsock, inputline, LBUF_SIZE - 1);
    } while (  0 == result
            || (  3 < result
               && '-' == inputline[3]));

    if (-1 == result)
    {
        mod_email_sock_close(mailsock);
        notify(executor, "@email: Connection to mailserver lost.");
        free_lbuf(body);
        free_lbuf(addy);
        return;
    }

    if ('2' != inputline[0])
    {
        notify(executor, tprintf("@email: Error response on EHLO (%s)",
            inputline));
    }

    mod_email_sock_printf(mailsock, "MAIL FROM:<%s>\r\n", ConvertCRLFtoSpace(mudconf.mail_sendaddr));

    do
    {
        result = mod_email_sock_readline(mailsock, inputline, LBUF_SIZE - 1);
    } while (  0 == result
            || (  3 < result
               && '-' == inputline[3]));

    if (-1 == result)
    {
        mod_email_sock_close(mailsock);
        notify(executor, "@email: Connection to mailserver lost.");
        free_lbuf(body);
        free_lbuf(addy);
        return;
    }

    if ('2' != inputline[0])
    {
        notify(executor, tprintf("@email: Error response on MAIL FROM (%s)",
            inputline));
    }

    mod_email_sock_printf(mailsock, "RCPT TO:<%s>\r\n", ConvertCRLFtoSpace(addy));

    do
    {
        result = mod_email_sock_readline(mailsock, inputline, LBUF_SIZE - 1);
    } while (  0 == result
            || (  3 < result
               && '-' == inputline[3]));

    if (-1 == result)
    {
        mod_email_sock_close(mailsock);
        notify(executor, "@email: Connection to mailserver lost.");
        free_lbuf(body);
        free_lbuf(addy);
        return;
    }

    if ('2' != inputline[0])
    {
        notify(executor, tprintf("@email: Error response on RCPT TO (%s)",
            inputline));
        free_lbuf(body);
        free_lbuf(addy);
        return;
    }

    mod_email_sock_printf(mailsock, "DATA\r\n");

    do
    {
        result = mod_email_sock_readline(mailsock, inputline, LBUF_SIZE - 1);
    } while (  0 == result
            || (  3 < result
               && '-' == inputline[3]));

    if (-1 == result)
    {
        mod_email_sock_close(mailsock);
        notify(executor, "@email: Connection to mailserver lost.");
        free_lbuf(body);
        free_lbuf(addy);
        return;
    }

    if ('3' != inputline[0])
    {
        notify(executor, tprintf("@email: Error response on DATA (%s)",
            inputline));
        free_lbuf(body);
        free_lbuf(addy);
        return;
    }

    char *pSendName = StringClone(ConvertCRLFtoSpace(mudconf.mail_sendname));
    mod_email_sock_printf(mailsock, "From: %s <%s>\r\n",  pSendName, ConvertCRLFtoSpace(mudconf.mail_sendaddr));
    MEMFREE(pSendName);

    mod_email_sock_printf(mailsock, "To: %s\r\n", ConvertCRLFtoSpace(addy));
    mod_email_sock_printf(mailsock, "X-Mailer: TinyMUX %s\r\n", mudstate.short_ver);
    mod_email_sock_printf(mailsock, "Subject: %s\r\n\r\n", ConvertCRLFtoSpace(subject));

    // The body is encoded to include the CRLF.CRLF at the end.
    //
    mod_email_sock_printf(mailsock, "%s", EncodeBody(body));

    do
    {
        result = mod_email_sock_readline(mailsock, inputline, LBUF_SIZE - 1);

        // Remove trailing CR and LF characters.
        //
        while (  0 < result
              && (  '\n' == inputline[result-1]
                 || '\r' == inputline[result-1]))
        {
            result--;
            inputline[result] = '\0';
        }
    } while (  0 == result
            || (  3 < result
               && '-' == inputline[3]));

    if (-1 == result)
    {
        mod_email_sock_close(mailsock);
        notify(executor, "@email: Connection to mailserver lost.");
        free_lbuf(body);
        free_lbuf(addy);
        return;
    }

    if ('2' != inputline[0])
    {
        notify(executor, tprintf("@email: Message rejected (%s)",inputline));
    }
    else
    {
        notify(executor, tprintf("@email: Mail sent to %s (%s)", ConvertCRLFtoSpace(addy), &inputline[4]));
    }

    mod_email_sock_printf(mailsock, "QUIT\n");
    mod_email_sock_close(mailsock);

    free_lbuf(body);
    free_lbuf(addy);
}
