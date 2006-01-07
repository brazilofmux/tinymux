/*! \file bsd.cpp
 * File for most TCP socket-related code. Some socket-related code also exists
 * in netcommon.cpp, but most of it is here.
 *
 * $Id: bsd.cpp,v 1.77 2006-01-07 04:33:41 jake Exp $
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#ifndef WIN32
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#endif // !WIN32

#include <signal.h>

#include "attrs.h"
#include "command.h"
#include "file_c.h"
#include "slave.h"

#ifdef SOLARIS
extern const int _sys_nsig;
#define NSIG _sys_nsig
#endif // SOLARIS

PortInfo aMainGamePorts[MAX_LISTEN_PORTS];
int      nMainGamePorts = 0;

unsigned int ndescriptors = 0;
DESC *descriptor_list = NULL;
bool bDescriptorListInit = false;

#ifdef WIN32
int game_pid;
#else // WIN32
int maxd = 0;
pid_t slave_pid = 0;
int slave_socket = INVALID_SOCKET;
pid_t game_pid;
#ifdef QUERY_SLAVE
pid_t sqlslave_pid = 0;
int sqlslave_socket = INVALID_SOCKET;
#endif // QUERY_SLAVE
#endif // WIN32

DESC *initializesock(SOCKET, struct sockaddr_in *);
DESC *new_connection(PortInfo *Port, int *piError);
bool process_input(DESC *);
void SiteMonSend(int, const char *, DESC *, const char *);

#ifdef WIN32

// First version of Windows NT TCP/IP routines written by Nick Gammon
// <nick@gammon.com.au>, and were throughly reviewed, re-written and debugged
// by Stephen Dennis <brazilofmux@gmail.com>.
//
HANDLE hGameProcess = INVALID_HANDLE_VALUE;
FCANCELIO *fpCancelIo = NULL;
FGETPROCESSTIMES *fpGetProcessTimes = NULL;
HANDLE CompletionPort;    // IOs are queued up on this port
void __cdecl MUDListenThread(void * pVoid);  // the listening thread
DWORD platform;   // which version of Windows are we using?
OVERLAPPED lpo_aborted; // special to indicate a player has finished TCP IOs
OVERLAPPED lpo_aborted_final; // Finally free the descriptor.
OVERLAPPED lpo_shutdown; // special to indicate a player should do a shutdown
OVERLAPPED lpo_welcome; // special to indicate a player has -just- connected.
OVERLAPPED lpo_wakeup;  // special to indicate that the loop should wakeup and return.
void ProcessWindowsTCP(DWORD dwTimeout);  // handle NT-style IOs
CRITICAL_SECTION csDescriptorList;      // for thread synchronization
extern void Task_DeferredClose(void *arg_voidptr, int arg_Integer);

typedef struct
{
    int                port_in;
    struct sockaddr_in sa_in;
} SLAVE_REQUEST;

static HANDLE hSlaveRequestStackSemaphore;
#define SLAVE_REQUEST_STACK_SIZE 50
static SLAVE_REQUEST SlaveRequests[SLAVE_REQUEST_STACK_SIZE];
static int iSlaveRequest = 0;
#define MAX_STRING 514
typedef struct
{
    char host[MAX_STRING];
    char token[MAX_STRING];
    char ident[MAX_STRING];
} SLAVE_RESULT;

static HANDLE hSlaveResultStackSemaphore;
#define SLAVE_RESULT_STACK_SIZE 50
static SLAVE_RESULT SlaveResults[SLAVE_RESULT_STACK_SIZE];
static volatile int iSlaveResult = 0;

#define NUM_SLAVE_THREADS 5
typedef struct tagSlaveThreadsInfo
{
    DWORD iDoing;
    DWORD iError;
    DWORD hThreadId;
} SLAVETHREADINFO;
static SLAVETHREADINFO SlaveThreadInfo[NUM_SLAVE_THREADS];
static HANDLE hSlaveThreadsSemaphore;

DWORD WINAPI SlaveProc(LPVOID lpParameter)
{
    SLAVE_REQUEST req;
    unsigned long addr;
    struct hostent *hp;
    DWORD iSlave = (DWORD)lpParameter;

    if (NUM_SLAVE_THREADS <= iSlave) return 1;

    SlaveThreadInfo[iSlave].iDoing = __LINE__;
    for (;;)
    {
        // Go to sleep until there's something useful to do.
        //
        SlaveThreadInfo[iSlave].iDoing = __LINE__;
        DWORD dwReason = WaitForSingleObject(hSlaveThreadsSemaphore,
            30000UL*NUM_SLAVE_THREADS);
        switch (dwReason)
        {
        case WAIT_TIMEOUT:
        case WAIT_OBJECT_0:

            // Either the main game thread rang, or 60 seconds has past,
            // and it's probably a good idea to check the stack anyway.
            //
            break;

        default:

            // Either the main game thread has terminated, in which case
            // we want to, too, or the function itself has failed, in which
            // case: calling it again won't do much good.
            //
            SlaveThreadInfo[iSlave].iError = __LINE__;
            return 1;
        }

        SlaveThreadInfo[iSlave].iDoing = __LINE__;
        for (;;)
        {
            // Go take the request off the stack, but not if it takes more
            // than 5 seconds to do it. Go back to sleep if we time out. The
            // request can wait: either another thread will pick it up, or
            // we'll wakeup in 60 seconds anyway.
            //
            SlaveThreadInfo[iSlave].iDoing = __LINE__;
            if (WAIT_OBJECT_0 != WaitForSingleObject(hSlaveRequestStackSemaphore, 5000))
            {
                SlaveThreadInfo[iSlave].iError = __LINE__;
                break;
            }

            SlaveThreadInfo[iSlave].iDoing = __LINE__;

            // We have control of the stack.
            //
            if (iSlaveRequest <= 0)
            {
                // The stack is empty. Release control and go back to sleep.
                //
                SlaveThreadInfo[iSlave].iDoing = __LINE__;
                ReleaseSemaphore(hSlaveRequestStackSemaphore, 1, NULL);
                SlaveThreadInfo[iSlave].iDoing = __LINE__;
                break;
            }

            // Remove the request from the stack.
            //
            iSlaveRequest--;
            req = SlaveRequests[iSlaveRequest];

            SlaveThreadInfo[iSlave].iDoing = __LINE__;
            ReleaseSemaphore(hSlaveRequestStackSemaphore, 1, NULL);
            SlaveThreadInfo[iSlave].iDoing = __LINE__;

            // Ok, we have complete control of this address, now, so let's
            // do the host/ident thing.
            //

            // Take note of what time it is.
            //
#define IDENT_PROTOCOL_TIMEOUT 5*60 // 5 minutes expressed in seconds.
            CLinearTimeAbsolute ltaTimeoutOrigin;
            ltaTimeoutOrigin.GetUTC();
            CLinearTimeDelta ltdTimeout;
            ltdTimeout.SetSeconds(IDENT_PROTOCOL_TIMEOUT);
            CLinearTimeAbsolute ltaTimeoutForward(ltaTimeoutOrigin, ltdTimeout);
            ltdTimeout.SetSeconds(-IDENT_PROTOCOL_TIMEOUT);
            CLinearTimeAbsolute ltaTimeoutBackward(ltaTimeoutOrigin, ltdTimeout);

            addr = req.sa_in.sin_addr.S_un.S_addr;
            hp = gethostbyaddr((char *)&addr, sizeof(addr), AF_INET);

            if (  hp
               && strlen(hp->h_name) < MAX_STRING)
            {
                SlaveThreadInfo[iSlave].iDoing = __LINE__;

                char host[MAX_STRING];
                char token[MAX_STRING];
                char szIdent[MAX_STRING];
                struct sockaddr_in sin;
                memset(&sin, 0, sizeof(sin));
                SOCKET s;

                // We have a host name.
                //
                strcpy(host, inet_ntoa(req.sa_in.sin_addr));
                strcpy(token, hp->h_name);

                // Setup ident port.
                //
                sin.sin_family = hp->h_addrtype;
                memcpy(&sin.sin_addr, hp->h_addr, hp->h_length);
                sin.sin_port = htons(113);

                szIdent[0] = 0;
                s = socket(hp->h_addrtype, SOCK_STREAM, 0);
                SlaveThreadInfo[iSlave].iDoing = __LINE__;
                if (s != INVALID_SOCKET)
                {
                    SlaveThreadInfo[iSlave].iDoing = __LINE__;

                    DebugTotalSockets++;
                    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) == SOCKET_ERROR)
                    {
                        SlaveThreadInfo[iSlave].iDoing = __LINE__;
                        shutdown(s, SD_BOTH);
                        SlaveThreadInfo[iSlave].iDoing = __LINE__;
                        if (closesocket(s) == 0)
                        {
                            DebugTotalSockets--;
                        }
                        s = INVALID_SOCKET;
                    }
                    else
                    {
                        int TurnOn = 1;
                        setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char *)&TurnOn, sizeof(TurnOn));

                        SlaveThreadInfo[iSlave].iDoing = __LINE__;
                        char szPortPair[128];
                        sprintf(szPortPair, "%d, %d\r\n",
                            ntohs(req.sa_in.sin_port), req.port_in);
                        SlaveThreadInfo[iSlave].iDoing = __LINE__;
                        int nPortPair = strlen(szPortPair);

                        CLinearTimeAbsolute ltaCurrent;
                        ltaCurrent.GetUTC();
                        if (  ltaTimeoutBackward < ltaCurrent
                           &&  ltaCurrent < ltaTimeoutForward
                           && send(s, szPortPair, nPortPair, 0) != SOCKET_ERROR)
                        {
                            SlaveThreadInfo[iSlave].iDoing = __LINE__;
                            int nIdent = 0;
                            int cc;

                            char szIdentBuffer[MAX_STRING];
                            szIdentBuffer[0] = 0;
                            bool bAllDone = false;

                            ltaCurrent.GetUTC();
                            while (  !bAllDone
                                  && nIdent < sizeof(szIdent)-1
                                  && ltaTimeoutBackward < ltaCurrent
                                  && ltaCurrent < ltaTimeoutForward
                                  && (cc = recv(s, szIdentBuffer, sizeof(szIdentBuffer)-1, 0)) != SOCKET_ERROR
                                  && cc != 0)
                            {
                                SlaveThreadInfo[iSlave].iDoing = __LINE__;

                                int nIdentBuffer = cc;
                                szIdentBuffer[nIdentBuffer] = 0;

                                char *p = szIdentBuffer;
                                for (; nIdent < sizeof(szIdent)-1;)
                                {
                                    if (  *p == '\0'
                                       || *p == '\r'
                                       || *p == '\n')
                                    {
                                        bAllDone = true;
                                        break;
                                    }
                                    if (mux_isprint(*p))
                                    {
                                        szIdent[nIdent++] = *p;
                                    }
                                    p++;
                                }
                                szIdent[nIdent] = '\0';

                                ltaCurrent.GetUTC();
                            }
                        }
                        SlaveThreadInfo[iSlave].iDoing = __LINE__;
                        shutdown(s, SD_BOTH);
                        SlaveThreadInfo[iSlave].iDoing = __LINE__;
                        if (closesocket(s) == 0)
                        {
                            DebugTotalSockets--;
                        }
                        s = INVALID_SOCKET;
                    }
                }

                SlaveThreadInfo[iSlave].iDoing = __LINE__;
                if (WAIT_OBJECT_0 == WaitForSingleObject(hSlaveResultStackSemaphore, INFINITE))
                {
                    SlaveThreadInfo[iSlave].iDoing = __LINE__;
                    if (iSlaveResult < SLAVE_RESULT_STACK_SIZE)
                    {
                        SlaveThreadInfo[iSlave].iDoing = __LINE__;
                        strcpy(SlaveResults[iSlaveResult].host, host);
                        strcpy(SlaveResults[iSlaveResult].token, token);
                        strcpy(SlaveResults[iSlaveResult].ident, szIdent);
                        iSlaveResult++;
                    }
                    else
                    {
                        // The result stack is full, so we just toss
                        // the info and act like it never happened.
                        //
                        SlaveThreadInfo[iSlave].iError = __LINE__;
                    }
                    SlaveThreadInfo[iSlave].iDoing = __LINE__;
                    ReleaseSemaphore(hSlaveResultStackSemaphore, 1, NULL);
                    SlaveThreadInfo[iSlave].iDoing = __LINE__;
                }
                else
                {
                    // The main game thread terminated or the function itself failed,
                    // There isn't much left to do except terminate ourselves.
                    //
                    SlaveThreadInfo[iSlave].iError = __LINE__;
                    return 1;
                }
            }
        }
    }
    SlaveThreadInfo[iSlave].iDoing = __LINE__;
    return 1;
}

static bool bSlaveBooted = false;
void boot_slave(dbref executor, dbref caller, dbref enactor, int)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);

    int iSlave;

    if (bSlaveBooted) return;

    hSlaveThreadsSemaphore = CreateSemaphore(NULL, 0, NUM_SLAVE_THREADS, NULL);
    hSlaveRequestStackSemaphore = CreateSemaphore(NULL, 1, 1, NULL);
    hSlaveResultStackSemaphore = CreateSemaphore(NULL, 1, 1, NULL);
    DebugTotalSemaphores += 3;
    for (iSlave = 0; iSlave < NUM_SLAVE_THREADS; iSlave++)
    {
        SlaveThreadInfo[iSlave].iDoing = 0;
        SlaveThreadInfo[iSlave].iError = 0;
        CreateThread(NULL, 0, SlaveProc, (LPVOID)iSlave, 0, &SlaveThreadInfo[iSlave].hThreadId);
        DebugTotalThreads++;
    }
    bSlaveBooted = true;
}


static int get_slave_result(void)
{
    char host[MAX_STRING];
    char token[MAX_STRING];
    char ident[MAX_STRING];
    char os[MAX_STRING];
    char userid[MAX_STRING];
    DESC *d;
    int local_port, remote_port;

    // Go take the result off the stack, but not if it takes more
    // than 5 seconds to do it. Skip it if we time out.
    //
    if (WAIT_OBJECT_0 != WaitForSingleObject(hSlaveResultStackSemaphore, 5000))
    {
        return 1;
    }

    // We have control of the stack. Go back to sleep if the stack is empty.
    //
    if (iSlaveResult <= 0)
    {
        ReleaseSemaphore(hSlaveResultStackSemaphore, 1, NULL);
        return 1;
    }
    iSlaveResult--;
    strcpy(host, SlaveResults[iSlaveResult].host);
    strcpy(token, SlaveResults[iSlaveResult].token);
    strcpy(ident, SlaveResults[iSlaveResult].ident);
    ReleaseSemaphore(hSlaveResultStackSemaphore, 1, NULL);

    // At this point, we have a host name on our own stack.
    //
    if (!mudconf.use_hostname)
    {
        return 1;
    }
    for (d = descriptor_list; d; d = d->next)
    {
        if (strcmp(d->addr, host))
        {
            continue;
        }

        strncpy(d->addr, token, 50);
        d->addr[50] = '\0';
        if (d->player != 0)
        {
            if (d->username[0])
            {
                atr_add_raw(d->player, A_LASTSITE, tprintf("%s@%s", d->username, d->addr));
            }
            else
            {
                atr_add_raw(d->player, A_LASTSITE, d->addr);
            }
            atr_add_raw(d->player, A_LASTIP, inet_ntoa((d->address).sin_addr));
        }
    }

    if (sscanf(ident, "%d , %d : %s : %s : %s", &remote_port, &local_port, token, os, userid) != 5)
    {
        return 1;
    }
    for (d = descriptor_list; d; d = d->next)
    {
        if (ntohs((d->address).sin_port) != remote_port)
        {
            continue;
        }

        strncpy(d->username, userid, 10);
        d->username[10] = '\0';
        if (d->player != 0)
        {
            atr_add_raw(d->player, A_LASTSITE, tprintf("%s@%s", d->username, d->addr));
        }
    }
    return 1;
}

#else // WIN32

extern int make_nonblocking(SOCKET s);

void CleanUpSlaveSocket(void)
{
    if (!IS_INVALID_SOCKET(slave_socket))
    {
        shutdown(slave_socket, SD_BOTH);
        if (close(slave_socket) == 0)
        {
            DebugTotalSockets--;
        }
        slave_socket = INVALID_SOCKET;
    }
}

void CleanUpSlaveProcess(void)
{
    if (slave_pid > 0)
    {
        kill(slave_pid, SIGKILL);
        waitpid(slave_pid, NULL, 0);
    }
    slave_pid = 0;
}

#ifdef QUERY_SLAVE
void CleanUpSQLSlaveSocket(void)
{
    if (!IS_INVALID_SOCKET(sqlslave_socket))
    {
        shutdown(sqlslave_socket, SD_BOTH);
        if (close(sqlslave_socket) == 0)
        {
            DebugTotalSockets--;
        }
        sqlslave_socket = INVALID_SOCKET;
    }
}

void CleanUpSQLSlaveProcess(void)
{
    if (sqlslave_pid > 0)
    {
        kill(sqlslave_pid, SIGKILL);
        waitpid(sqlslave_pid, NULL, 0);
    }
    sqlslave_pid = 0;
}

/*! \brief Lauch query slave process.
 *
 * This spawns the query slave process and creates a socket-oriented,
 * bi-directional communiocation path between that process and this
 * process. Any existing slave process is killed.
 *
 * \param executor dbref of Executor.
 * \param caller   dbref of Caller.
 * \param enactor  dbref of Enactor.
 * \return         None.
 */

void boot_sqlslave(dbref executor, dbref caller, dbref enactor, int)
{
    char *pFailedFunc = 0;
    int sv[2];
    int i;
    int maxfds;

#ifdef HAVE_GETDTABLESIZE
    maxfds = getdtablesize();
#else // HAVE_GETDTABLESIZE
    maxfds = sysconf(_SC_OPEN_MAX);
#endif // HAVE_GETDTABLESIZE

    CleanUpSQLSlaveSocket();
    CleanUpSQLSlaveProcess();

    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0)
    {
        pFailedFunc = "socketpair() error: ";
        goto failure;
    }

    // Set to nonblocking.
    //
    if (make_nonblocking(sv[0]) < 0)
    {
        pFailedFunc = "make_nonblocking() error: ";
        close(sv[0]);
        close(sv[1]);
        goto failure;
    }

    sqlslave_pid = fork();
    switch (sqlslave_pid)
    {
    case -1:

        pFailedFunc = "fork() error: ";
        close(sv[0]);
        close(sv[1]);
        goto failure;

    case 0:

        // If we don't clear this alarm, the child will eventually receive a
        // SIG_PROF.
        //
        MuxAlarm.Clear();

        // Child.  The following calls to dup2() assume only the minimal
        // dup2() functionality.  That is, the destination descriptor is
        // always available for it, and sv[1] is never that descriptor.
        // It is likely that the standard defined behavior of dup2()
        // would handle the job by itself more directly, but a little
        // extra code is low-cost insurance.
        //
        close(sv[0]);
        if (sv[1] != 0)
        {
            close(0);
            if (dup2(sv[1], 0) == -1)
            {
                _exit(1);
            }
        }
        if (sv[1] != 1)
        {
            close(1);
            if (dup2(sv[1], 1) == -1)
            {
                _exit(1);
            }
        }
        for (i = 3; i < maxfds; i++)
        {
            close(i);
        }
        execlp("bin/sqlslave", "sqlslave", NULL);
        _exit(1);
    }
    close(sv[1]);

    sqlslave_socket = sv[0];
    DebugTotalSockets++;
    if (make_nonblocking(sqlslave_socket) < 0)
    {
        pFailedFunc = "make_nonblocking() error: ";
        CleanUpSQLSlaveSocket();
        goto failure;
    }
    if (maxd <= sqlslave_socket)
    {
        maxd = sqlslave_socket + 1;
    }

    STARTLOG(LOG_ALWAYS, "NET", "QUERY");
    log_text("SQL slave started on fd ");
    log_number(sqlslave_socket);
    ENDLOG;

    write(sqlslave_socket, "PING", 4);
    return;

failure:

    CleanUpSQLSlaveProcess();
    STARTLOG(LOG_ALWAYS, "NET", "SQL");
    log_text(pFailedFunc);
    log_number(errno);
    ENDLOG;
}
#endif // QUERY_SLAVE

/*! \brief Lauch reverse-DNS slave process.
 *
 * This spawns the reverse-DNS slave process and creates a socket-oriented,
 * bi-directional communiocation path between that process and this
 * process. Any existing slave process is killed.
 *
 * \param executor dbref of Executor.
 * \param caller   dbref of Caller.
 * \param enactor  dbref of Enactor.
 * \return         None.
 */

void boot_slave(dbref executor, dbref caller, dbref enactor, int)
{
    char *pFailedFunc = 0;
    int sv[2];
    int i;
    int maxfds;

#ifdef HAVE_GETDTABLESIZE
    maxfds = getdtablesize();
#else // HAVE_GETDTABLESIZE
    maxfds = sysconf(_SC_OPEN_MAX);
#endif // HAVE_GETDTABLESIZE

    CleanUpSlaveSocket();
    CleanUpSlaveProcess();

    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0)
    {
        pFailedFunc = "socketpair() error: ";
        goto failure;
    }

    // Set to nonblocking.
    //
    if (make_nonblocking(sv[0]) < 0)
    {
        pFailedFunc = "make_nonblocking() error: ";
        close(sv[0]);
        close(sv[1]);
        goto failure;
    }
    slave_pid = fork();
    switch (slave_pid)
    {
    case -1:

        pFailedFunc = "fork() error: ";
        close(sv[0]);
        close(sv[1]);
        goto failure;

    case 0:

        // If we don't clear this alarm, the child will eventually receive a
        // SIG_PROF.
        //
        MuxAlarm.Clear();

        // Child.  The following calls to dup2() assume only the minimal
        // dup2() functionality.  That is, the destination descriptor is
        // always available for it, and sv[1] is never that descriptor.
        // It is likely that the standard defined behavior of dup2()
        // would handle the job by itself more directly, but a little
        // extra code is low-cost insurance.
        //
        close(sv[0]);
        if (sv[1] != 0)
        {
            close(0);
            if (dup2(sv[1], 0) == -1)
            {
                _exit(1);
            }
        }
        if (sv[1] != 1)
        {
            close(1);
            if (dup2(sv[1], 1) == -1)
            {
                _exit(1);
            }
        }
        for (i = 3; i < maxfds; i++)
        {
            close(i);
        }
        execlp("bin/slave", "slave", NULL);
        _exit(1);
    }
    close(sv[1]);

    slave_socket = sv[0];
    DebugTotalSockets++;
    if (make_nonblocking(slave_socket) < 0)
    {
        pFailedFunc = "make_nonblocking() error: ";
        CleanUpSlaveSocket();
        goto failure;
    }
    if (maxd <= slave_socket)
    {
        maxd = slave_socket + 1;
    }

    STARTLOG(LOG_ALWAYS, "NET", "SLAVE");
    log_text("DNS lookup slave started on fd ");
    log_number(slave_socket);
    ENDLOG;
    return;

failure:

    CleanUpSlaveProcess();
    STARTLOG(LOG_ALWAYS, "NET", "SLAVE");
    log_text(pFailedFunc);
    log_number(errno);
    ENDLOG;
}

#ifdef QUERY_SLAVE

/*! \brief Get results from the SQL query slave.
 *
 * Any communication from the SQL query slave is logged.
 *
 * \return         -1 for failure and 0 for success.
 */

static int get_sqlslave_result(void)
{
    char buf[LBUF_SIZE];

    int len = read(sqlslave_socket, buf, sizeof(buf)-1);
    if (len < 0)
    {
        int iSocketError = SOCKET_LAST_ERROR;
        if (  iSocketError == SOCKET_EAGAIN
           || iSocketError == SOCKET_EWOULDBLOCK)
        {
            return -1;
        }
        CleanUpSQLSlaveSocket();
        CleanUpSQLSlaveProcess();

        STARTLOG(LOG_ALWAYS, "NET", "QUERY");
        log_text("read() of query slave failed. Query Slave stopped.");
        ENDLOG;

        return -1;
    }
    else if (len == 0)
    {
        return -1;
    }
    buf[len] = '\0';

    STARTLOG(LOG_ALWAYS, "NET", "QUERY");
    log_text(buf);
    ENDLOG;

    return 0;
}

#endif // QUERY_SLAVE

// Get a result from the slave
//
static int get_slave_result(void)
{
    int local_port, remote_port;
    DESC *d;

    char *buf = alloc_lbuf("slave_buf");

    int len = read(slave_socket, buf, LBUF_SIZE-1);
    if (len < 0)
    {
        int iSocketError = SOCKET_LAST_ERROR;
        if (  iSocketError == SOCKET_EAGAIN
           || iSocketError == SOCKET_EWOULDBLOCK)
        {
            free_lbuf(buf);
            return -1;
        }
        CleanUpSlaveSocket();
        CleanUpSlaveProcess();
        free_lbuf(buf);

        STARTLOG(LOG_ALWAYS, "NET", "SLAVE");
        log_text("read() of slave result failed. Slave stopped.");
        ENDLOG;

        return -1;
    }
    else if (len == 0)
    {
        free_lbuf(buf);
        return -1;
    }
    buf[len] = '\0';

    char *token = alloc_lbuf("slave_token");
    char *os = alloc_lbuf("slave_os");
    char *userid = alloc_lbuf("slave_userid");
    char *host = alloc_lbuf("slave_host");
    char *p;
    if (sscanf(buf, "%s %s", host, token) != 2)
    {
        goto Done;
    }
    p = strchr(buf, '\n');
    *p = '\0';
    if (mudconf.use_hostname)
    {
        for (d = descriptor_list; d; d = d->next)
        {
            if (strcmp(d->addr, host) != 0)
            {
                continue;
            }

            strncpy(d->addr, token, 50);
            d->addr[50] = '\0';
            if (d->player != 0)
            {
                if (d->username[0])
                {
                    atr_add_raw(d->player, A_LASTSITE, tprintf("%s@%s",
                        d->username, d->addr));
                }
                else
                {
                    atr_add_raw(d->player, A_LASTSITE, d->addr);
                }
                atr_add_raw(d->player, A_LASTIP, inet_ntoa((d->address).sin_addr));
            }
        }
    }

    if (sscanf(p + 1, "%s %d , %d : %s : %s : %s",
           host,
           &remote_port, &local_port,
           token, os, userid) != 6)
    {
        goto Done;
    }
    for (d = descriptor_list; d; d = d->next)
    {
        if (ntohs((d->address).sin_port) != remote_port)
            continue;
        strncpy(d->username, userid, 10);
        d->username[10] = '\0';
        if (d->player != 0)
        {
            atr_add_raw(d->player, A_LASTSITE, tprintf("%s@%s",
                             d->username, d->addr));
        }
    }
Done:
    free_lbuf(buf);
    free_lbuf(token);
    free_lbuf(os);
    free_lbuf(userid);
    free_lbuf(host);
    return 0;
}
#endif // WIN32

void make_socket(PortInfo *Port)
{
    SOCKET s;
    struct sockaddr_in server;
    int opt = 1;

#ifdef WIN32

    // If we are running Windows NT we must create a completion port,
    // and start up a listening thread for new connections
    //
    if (platform == VER_PLATFORM_WIN32_NT)
    {
        int nRet;

        // create initial IO completion port, so threads have something to wait on
        //
        CompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);

        if (!CompletionPort)
        {
            Log.tinyprintf("Error %ld on CreateIoCompletionPort" ENDLINE,  GetLastError());
            WSACleanup();     // clean up
            exit(1);
        }

        // Initialize the critical section
        //
        if (!bDescriptorListInit)
        {
            InitializeCriticalSection(&csDescriptorList);
            bDescriptorListInit = true;
        }

        // Create a TCP/IP stream socket
        //
        s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
        {
            log_perror("NET", "FAIL", NULL, "creating master socket");
            exit(3);
        }
        DebugTotalSockets++;
        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
        {
            log_perror("NET", "FAIL", NULL, "setsockopt");
        }

        // Fill in the the address structure
        //
        server.sin_port = htons((unsigned short)(Port->port));
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = INADDR_ANY;

        // bind our name to the socket
        //
        nRet = bind(s, (LPSOCKADDR) &server, sizeof server);

        if (nRet == SOCKET_ERROR)
        {
            Log.tinyprintf("Error %ld on Win32: bind" ENDLINE, SOCKET_LAST_ERROR);
            if (closesocket(s) == 0)
            {
                DebugTotalSockets--;
            }
            s = INVALID_SOCKET;
            WSACleanup();     // clean up
            exit(1);
        }

        // Set the socket to listen
        //
        nRet = listen(s, SOMAXCONN);

        if (nRet)
        {
            Log.tinyprintf("Error %ld on Win32: listen" ENDLINE, SOCKET_LAST_ERROR);
            WSACleanup();
            exit(1);
        }

        // Create the MUD listening thread
        //
        if (_beginthread(MUDListenThread, 0, (void *) Port) == (unsigned)(-1))
        {
            log_perror("NET", "FAIL", "_beginthread", "setsockopt");
            WSACleanup();
            exit(1);
        }

        Port->socket = s;
        Log.tinyprintf("Listening (NT-style) on port %d" ENDLINE, Port->port);
        return;
    }
#endif // WIN32

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (IS_INVALID_SOCKET(s))
    {
        log_perror("NET", "FAIL", NULL, "creating master socket");
#ifdef WIN32
        WSACleanup();
#endif // WIN32
        exit(3);
    }
    DebugTotalSockets++;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
    {
        log_perror("NET", "FAIL", NULL, "setsockopt");
    }
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons((unsigned short)(Port->port));
    int cc  = bind(s, (struct sockaddr *)&server, sizeof(server));
    if (IS_SOCKET_ERROR(cc))
    {
        log_perror("NET", "FAIL", NULL, "bind");
        if (SOCKET_CLOSE(s) == 0)
        {
            DebugTotalSockets--;
        }
        s = INVALID_SOCKET;
#ifdef WIN32
        WSACleanup();
#endif // WIN32
        exit(4);
    }
    listen(s, SOMAXCONN);
    Port->socket = s;
    Log.tinyprintf("Listening on port %d" ENDLINE, Port->port);
}

void SetupPorts(int *pnPorts, PortInfo aPorts[], IntArray *pia)
{
    // Any existing open port which does not appear in the requested set
    // should be closed.
    //
    int i, j, k;
    bool bFound;
    for (i = 0; i < *pnPorts; i++)
    {
        bFound = false;
        for (j = 0; j < pia->n; j++)
        {
            if (aPorts[i].port == pia->pi[j])
            {
                bFound = true;
                break;
            }
        }
        if (!bFound)
        {
            if (SOCKET_CLOSE(aPorts[i].socket) == 0)
            {
                DebugTotalSockets--;
                (*pnPorts)--;
                k = *pnPorts;
                if (i != k)
                {
                    aPorts[i] = aPorts[k];
                }
                aPorts[k].port = 0;
                aPorts[k].socket = INVALID_SOCKET;
            }
        }
    }

    // Any requested port which does not appear in the existing open set
    // of ports should be opened.
    //
    for (j = 0; j < pia->n; j++)
    {
        bFound = false;
        for (i = 0; i < *pnPorts; i++)
        {
            if (aPorts[i].port == pia->pi[j])
            {
                bFound = true;
                break;
            }
        }
        if (!bFound)
        {
            k = *pnPorts;
            (*pnPorts)++;
            aPorts[k].port = pia->pi[j];
            make_socket(aPorts+k);
        }
    }

#ifndef WIN32
    for (i = 0; i < *pnPorts; i++)
    {
        if (maxd <= aPorts[i].socket)
        {
            maxd = aPorts[i].socket + 1;
        }
    }
#endif
}

#ifdef WIN32
// Private version of FD_ISSET:
//
// The following routine is only used on Win9x. Ordinarily, FD_ISSET
// maps to a __WSAFDIsSet call, however, the Intel compiler encounters
// an internal error at link time when some of the higher-order
// optimizations are requested (-Qipo). Including this function is a
// workaround.
//
DCL_INLINE bool FD_ISSET_priv(SOCKET fd, fd_set *set)
{
    unsigned int i;
    for (i = 0; i < set->fd_count; i++)
    {
        if (set->fd_array[i] == fd)
        {
            return true;
        }
    }
    return false;
}

void shovechars9x(int nPorts, PortInfo aPorts[])
{
    fd_set input_set, output_set;
    int found;
    DESC *d, *dnext, *newd;

#define CheckInput(x)   FD_ISSET_priv(x, &input_set)
#define CheckOutput(x)  FD_ISSET_priv(x, &output_set)

    mudstate.debug_cmd = "< shovechars >";

    CLinearTimeAbsolute ltaLastSlice;
    ltaLastSlice.GetUTC();

    while (mudstate.shutdown_flag == 0)
    {
        CLinearTimeAbsolute ltaCurrent;
        ltaCurrent.GetUTC();
        update_quotas(ltaLastSlice, ltaCurrent);

        // Before processing a possible QUIT command, be sure to give the slave
        // a chance to report it's findings.
        //
        if (iSlaveResult) get_slave_result();

        // Check the scheduler. Run a little ahead into the future so that
        // we tend to sleep longer.
        //
        scheduler.RunTasks(ltaCurrent);
        CLinearTimeAbsolute ltaWakeUp;
        if (!scheduler.WhenNext(&ltaWakeUp))
        {
            CLinearTimeDelta ltd = time_30m;
            ltaWakeUp = ltaCurrent + ltd;
        }
        else if (ltaWakeUp < ltaCurrent)
        {
            ltaWakeUp = ltaCurrent;
        }

        if (mudstate.shutdown_flag)
        {
            break;
        }

        FD_ZERO(&input_set);
        FD_ZERO(&output_set);

        // Listen for new connections.
        //
        int i;
        for (i = 0; i < nPorts; i++)
        {
            FD_SET(aPorts[i].socket, &input_set);
        }

        // Mark sockets that we want to test for change in status.
        //
        DESC_ITER_ALL(d)
        {
            if (!d->input_head)
                FD_SET(d->descriptor, &input_set);
            if (d->output_head)
                FD_SET(d->descriptor, &output_set);
        }

        // Wait for something to happen
        //
        struct timeval timeout;
        CLinearTimeDelta ltdTimeout = ltaWakeUp - ltaCurrent;
        ltdTimeout.ReturnTimeValueStruct(&timeout);
        found = select(0, &input_set, &output_set, (fd_set *) NULL, &timeout);

        switch (found)
        {
        case SOCKET_ERROR:
            {
                STARTLOG(LOG_NET, "NET", "CONN");
                log_text("shovechars: Socket error.");
                ENDLOG;
            }

        case 0:
            continue;
        }

        // Check for new connection requests.
        //
        for (i = 0; i < nPorts; i++)
        {
            if (CheckInput(aPorts[i].socket))
            {
                int iSocketError;
                newd = new_connection(aPorts+i, &iSocketError);
                if (!newd)
                {
                    if (  iSocketError
                       && iSocketError != SOCKET_EINTR)
                    {
                        log_perror("NET", "FAIL", NULL, "new_connection");
                    }
                }
            }
        }

        // Check for activity on user sockets
        //
        DESC_SAFEITER_ALL(d, dnext)
        {
            // Process input from sockets with pending input
            //
            if (CheckInput(d->descriptor))
            {
                // Undo autodark
                //
                if (d->flags & DS_AUTODARK)
                {
                    // Clear the DS_AUTODARK on every related session.
                    //
                    DESC *d1;
                    DESC_ITER_PLAYER(d->player, d1)
                    {
                        d1->flags &= ~DS_AUTODARK;
                    }
                    db[d->player].fs.word[FLAG_WORD1] &= ~DARK;
                }

                // Process received data
                //
                if (!process_input(d))
                {
                    shutdownsock(d, R_SOCKDIED);
                    continue;
                }
            }

            // Process output for sockets with pending output
            //
            if (CheckOutput(d->descriptor))
            {
                process_output9x(d, true);
            }
        }
    }
}

LRESULT WINAPI mux_WindowProc
(
    HWND   hWin,
    UINT   msg,
    WPARAM wParam,
    LPARAM lParam
)
{
    switch (msg)
    {
    case WM_CLOSE:
        mudstate.shutdown_flag = true;
        PostQueuedCompletionStatus(CompletionPort, 0, 0, &lpo_wakeup);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWin, msg, wParam, lParam);
}

const char szApp[] = "MUX2";

DWORD WINAPI ListenForCloseProc(LPVOID lpParameter)
{
    UNUSED_PARAMETER(lpParameter);

    WNDCLASS wc;

    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = mux_WindowProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = 0;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = szApp;

    RegisterClass(&wc);

    HWND hWnd = CreateWindow(szApp, szApp, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, 0, NULL);

    ShowWindow(hWnd, SW_HIDE);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        DispatchMessage(&msg);
    }
    mudstate.shutdown_flag = true;
    PostQueuedCompletionStatus(CompletionPort, 0, 0, &lpo_wakeup);
    return 1;
}

void shovecharsNT(int nPorts, PortInfo aPorts[])
{
    UNUSED_PARAMETER(nPorts);
    UNUSED_PARAMETER(aPorts);

    mudstate.debug_cmd = "< shovechars >";

    CreateThread(NULL, 0, ListenForCloseProc, NULL, 0, NULL);

    CLinearTimeAbsolute ltaLastSlice;
    ltaLastSlice.GetUTC();

    for (;;)
    {
        CLinearTimeAbsolute ltaCurrent;
        ltaCurrent.GetUTC();
        update_quotas(ltaLastSlice, ltaCurrent);

        // Before processing a possible QUIT command, be sure to give the slave
        // a chance to report it's findings.
        //
        if (iSlaveResult) get_slave_result();

        // Check the scheduler. Run a little ahead into the future so that
        // we tend to sleep longer.
        //
        scheduler.RunTasks(ltaCurrent);
        CLinearTimeAbsolute ltaWakeUp;
        if (!scheduler.WhenNext(&ltaWakeUp))
        {
            CLinearTimeDelta ltd = time_30m;
            ltaWakeUp = ltaCurrent + ltd;
        }
        else if (ltaWakeUp < ltaCurrent)
        {
            ltaWakeUp = ltaCurrent;
        }

        // The following gets Asyncronous writes to the sockets going
        // if they are not already going. Doing it this way is better
        // than:
        //
        //   1) starting an asyncronous write after a single addition
        //      to the socket's output queue,
        //
        //   2) scheduling a task to do it (because we would need to
        //      either maintain the task's uniqueness in the
        //      scheduler's queue, or endure many redudant calls to
        //      process_output for the same descriptor.
        //
        DESC *d, *dnext;
        DESC_SAFEITER_ALL(d, dnext)
        {
            if (d->bCallProcessOutputLater)
            {
                d->bCallProcessOutputLater = false;
                process_outputNT(d, false);
            }
        }

        if (mudstate.shutdown_flag)
            break;

        CLinearTimeDelta ltdTimeOut = ltaWakeUp - ltaCurrent;
        unsigned int iTimeout = ltdTimeOut.ReturnMilliseconds();
        ProcessWindowsTCP(iTimeout);
    }
}

#else // WIN32

bool ValidSocket(SOCKET s)
{
    struct stat fstatbuf;
    if (fstat(s, &fstatbuf) < 0)
    {
        return false;
    }
    return true;
}

void shovechars(int nPorts, PortInfo aPorts[])
{
    fd_set input_set, output_set;
    int found;
    DESC *d, *dnext, *newd;
    unsigned int avail_descriptors;
    int maxfds;
    int i;

#define CheckInput(x)     FD_ISSET(x, &input_set)
#define CheckOutput(x)    FD_ISSET(x, &output_set)

    mudstate.debug_cmd = "< shovechars >";

    CLinearTimeAbsolute ltaLastSlice;
    ltaLastSlice.GetUTC();

#ifdef HAVE_GETDTABLESIZE
    maxfds = getdtablesize();
#else // HAVE_GETDTABLESIZE
    maxfds = sysconf(_SC_OPEN_MAX);
#endif // HAVE_GETDTABLESIZE

    avail_descriptors = maxfds - 7;

    while (mudstate.shutdown_flag == 0)
    {
        CLinearTimeAbsolute ltaCurrent;
        ltaCurrent.GetUTC();
        update_quotas(ltaLastSlice, ltaCurrent);

        // Check the scheduler.
        //
        scheduler.RunTasks(ltaCurrent);
        CLinearTimeAbsolute ltaWakeUp;
        if (scheduler.WhenNext(&ltaWakeUp))
        {
            if (ltaWakeUp < ltaCurrent)
            {
                ltaWakeUp = ltaCurrent;
            }
        }
        else
        {
            CLinearTimeDelta ltd = time_30m;
            ltaWakeUp = ltaCurrent + ltd;
        }

        if (mudstate.shutdown_flag)
        {
            break;
        }

        FD_ZERO(&input_set);
        FD_ZERO(&output_set);

        // Listen for new connections if there are free descriptors.
        //
        if (ndescriptors < avail_descriptors)
        {
            for (i = 0; i < nPorts; i++)
            {
                FD_SET(aPorts[i].socket, &input_set);
            }
        }

        // Listen for replies from the slave socket.
        //
        if (!IS_INVALID_SOCKET(slave_socket))
        {
            FD_SET(slave_socket, &input_set);
        }

#ifdef QUERY_SLAVE
        // Listen for replies from the sqlslave socket.
        //
        if (!IS_INVALID_SOCKET(sqlslave_socket))
        {
            FD_SET(sqlslave_socket, &input_set);
        }
#endif // QUERY_SLAVE

        // Mark sockets that we want to test for change in status.
        //
        DESC_ITER_ALL(d)
        {
            if (!d->input_head)
            {
                FD_SET(d->descriptor, &input_set);
            }
            if (d->output_head)
            {
                FD_SET(d->descriptor, &output_set);
            }
        }

        // Wait for something to happen.
        //
        struct timeval timeout;
        CLinearTimeDelta ltdTimeout = ltaWakeUp - ltaCurrent;
        ltdTimeout.ReturnTimeValueStruct(&timeout);
        found = select(maxd, &input_set, &output_set, (fd_set *) NULL,
                   &timeout);

        if (IS_SOCKET_ERROR(found))
        {
            int iSocketError = SOCKET_LAST_ERROR;
            if (iSocketError == SOCKET_EBADF)
            {
                // This one is bad, as it results in a spiral of
                // doom, unless we can figure out what the bad file
                // descriptor is and get rid of it.
                //
                log_perror("NET", "FAIL", "checking for activity", "select");

                // Search for a bad socket amoungst the players.
                //
                DESC_ITER_ALL(d)
                {
                    if (!ValidSocket(d->descriptor))
                    {
                        STARTLOG(LOG_PROBLEMS, "ERR", "EBADF");
                        log_text("Bad descriptor ");
                        log_number(d->descriptor);
                        ENDLOG;
                        shutdownsock(d, R_SOCKDIED);
                    }
                }
                if (  !IS_INVALID_SOCKET(slave_socket)
                   && !ValidSocket(slave_socket))
                {
                    // Try to restart the slave, since it presumably
                    // died.
                    //
                    STARTLOG(LOG_PROBLEMS, "ERR", "EBADF");
                    log_text("Bad slave descriptor ");
                    log_number(slave_socket);
                    ENDLOG;
                    boot_slave(GOD, GOD, GOD, 0);
                }

#ifdef QUERY_SLAVE
                if (  !IS_INVALID_SOCKET(sqlslave_socket)
                   && !ValidSocket(sqlslave_socket))
                {
                    CleanUpSQLSlaveSocket();
                }
#endif // QUERY_SLAVE

                for (i = 0; i < nPorts; i++)
                {
                    if (!ValidSocket(aPorts[i].socket))
                    {
                        // That's it. Game over.
                        //
                        STARTLOG(LOG_PROBLEMS, "ERR", "EBADF");
                        log_text("Bad game port descriptor ");
                        log_number(aPorts[i].socket);
                        ENDLOG;
                        return;
                    }
                }
            }
            else if (iSocketError != SOCKET_EINTR)
            {
                log_perror("NET", "FAIL", "checking for activity", "select");
            }
            continue;
        }

        // Get usernames and hostnames.
        //
        if (  !IS_INVALID_SOCKET(slave_socket)
           && CheckInput(slave_socket))
        {
            while (get_slave_result() == 0)
            {
                ; // Nothing.
            }
        }

#ifdef QUERY_SLAVE
        // Get result sets from sqlslave.
        //
        if (  !IS_INVALID_SOCKET(sqlslave_socket)
           && CheckInput(sqlslave_socket))
        {
            while (get_sqlslave_result() == 0)
            {
                ; // Nothing.
            }
        }
#endif // QUERY_SLAVE

        // Check for new connection requests.
        //
        for (i = 0; i < nPorts; i++)
        {
            if (CheckInput(aPorts[i].socket))
            {
                int iSocketError;
                newd = new_connection(aPorts+i, &iSocketError);
                if (!newd)
                {
                    if (  iSocketError
                       && iSocketError != SOCKET_EINTR)
                    {
                        log_perror("NET", "FAIL", NULL, "new_connection");
                    }
                }
                else if (maxd <= newd->descriptor)
                {
                    maxd = newd->descriptor + 1;
                }
            }
        }

        // Check for activity on user sockets.
        //
        DESC_SAFEITER_ALL(d, dnext)
        {
            // Process input from sockets with pending input.
            //
            if (CheckInput(d->descriptor))
            {
                // Undo autodark
                //
                if (d->flags & DS_AUTODARK)
                {
                    // Clear the DS_AUTODARK on every related session.
                    //
                    DESC *d1;
                    DESC_ITER_PLAYER(d->player, d1)
                    {
                        d1->flags &= ~DS_AUTODARK;
                    }
                    db[d->player].fs.word[FLAG_WORD1] &= ~DARK;
                }

                // Process received data.
                //
                if (!process_input(d))
                {
                    shutdownsock(d, R_SOCKDIED);
                    continue;
                }
            }

            // Process output for sockets with pending output.
            //
            if (CheckOutput(d->descriptor))
            {
                process_output(d, true);
            }
        }
    }
}

#endif // WIN32

void TelnetSetup(DESC *d);

DESC *new_connection(PortInfo *Port, int *piSocketError)
{
    DESC *d;
    struct sockaddr_in addr;
#ifdef SOCKLEN_T_DCL
    socklen_t addr_len;
#else // SOCKLEN_T_DCL
    int addr_len;
#endif // SOCKLEN_T_DCL
#ifndef WIN32
    int len;
#endif // !WIN32

    char *cmdsave = mudstate.debug_cmd;
    mudstate.debug_cmd = "< new_connection >";
    addr_len = sizeof(struct sockaddr);

    SOCKET newsock = accept(Port->socket, (struct sockaddr *)&addr, &addr_len);

    if (IS_INVALID_SOCKET(newsock))
    {
        *piSocketError = SOCKET_LAST_ERROR;
        return 0;
    }

    char *pBuffM2 = alloc_mbuf("new_connection.address");
    strcpy(pBuffM2, inet_ntoa(addr.sin_addr));
    unsigned short usPort = ntohs(addr.sin_port);

    DebugTotalSockets++;
    if (site_check(addr.sin_addr, mudstate.access_list) == H_FORBIDDEN)
    {
        STARTLOG(LOG_NET | LOG_SECURITY, "NET", "SITE");
        char *pBuffM1  = alloc_mbuf("new_connection.LOG.badsite");
        sprintf(pBuffM1, "[%d/%s] Connection refused.  (Remote port %d)",
            newsock, pBuffM2, usPort);
        log_text(pBuffM1);
        free_mbuf(pBuffM1);
        ENDLOG;

        // Report site monitor information.
        //
        SiteMonSend(newsock, pBuffM2, NULL, "Connection refused");

        fcache_rawdump(newsock, FC_CONN_SITE);
        shutdown(newsock, SD_BOTH);
        if (SOCKET_CLOSE(newsock) == 0)
        {
            DebugTotalSockets--;
        }
        newsock = INVALID_SOCKET;
        errno = 0;
        d = NULL;
    }
    else
    {
#ifdef WIN32
        // Make slave request
        //
        // Go take control of the stack, but don't bother if it takes
        // longer than 5 seconds to do it.
        //
        if (  bSlaveBooted
           && WAIT_OBJECT_0 == WaitForSingleObject(hSlaveRequestStackSemaphore, 5000))
        {
            // We have control of the stack. Skip the request if the stack is full.
            //
            if (iSlaveRequest < SLAVE_REQUEST_STACK_SIZE)
            {
                // There is room on the stack, so make the request.
                //
                SlaveRequests[iSlaveRequest].sa_in = addr;
                SlaveRequests[iSlaveRequest].port_in = Port->port;
                iSlaveRequest++;
                ReleaseSemaphore(hSlaveRequestStackSemaphore, 1, NULL);

                // Wake up a single slave thread. Event automatically resets itself.
                //
                ReleaseSemaphore(hSlaveThreadsSemaphore, 1, NULL);
            }
            else
            {
                // No room on the stack, so skip it.
                //
                ReleaseSemaphore(hSlaveRequestStackSemaphore, 1, NULL);
            }
        }
#else // WIN32
        // Make slave request
        //
        if (  !IS_INVALID_SOCKET(slave_socket)
           && mudconf.use_hostname)
        {
            char *pBuffL1 = alloc_lbuf("new_connection.write");
            sprintf(pBuffL1, "%s\n%s,%d,%d\n", pBuffM2, pBuffM2, usPort,
                Port->port);
            len = strlen(pBuffL1);
            if (write(slave_socket, pBuffL1, len) < 0)
            {
                CleanUpSlaveSocket();
                CleanUpSlaveProcess();

                STARTLOG(LOG_ALWAYS, "NET", "SLAVE");
                log_text("write() of slave request failed. Slave stopped.");
                ENDLOG;
            }
            free_lbuf(pBuffL1);
        }
#endif // WIN32

        STARTLOG(LOG_NET, "NET", "CONN");
        char *pBuffM3 = alloc_mbuf("new_connection.LOG.open");
        sprintf(pBuffM3, "[%d/%s] Connection opened (remote port %d)", newsock,
            pBuffM2, usPort);
        log_text(pBuffM3);
        free_mbuf(pBuffM3);
        ENDLOG;

        d = initializesock(newsock, &addr);
        TelnetSetup(d);

        // Initalize everything before sending the sitemon info, so that we
        // can pass the descriptor, d.
        //
        SiteMonSend(newsock, pBuffM2, d, "Connection");

        welcome_user(d);
        mudstate.debug_cmd = cmdsave;
    }
    free_mbuf(pBuffM2);
    mudstate.debug_cmd = cmdsave;
    *piSocketError = SOCKET_LAST_ERROR;
    return d;
}

// Disconnect reasons that get written to the logfile
//
static const char *disc_reasons[] =
{
    "Unspecified",
    "Quit",
    "Inactivity Timeout",
    "Booted",
    "Remote Close or Net Failure",
    "Game Shutdown",
    "Login Retry Limit",
    "Logins Disabled",
    "Logout (Connection Not Dropped)",
    "Too Many Connected Players"
};

// Disconnect reasons that get fed to A_ADISCONNECT via announce_disconnect
//
static const char *disc_messages[] =
{
    "Unknown",
    "Quit",
    "Timeout",
    "Boot",
    "Netfailure",
    "Shutdown",
    "BadLogin",
    "NoLogins",
    "Logout"
};

void shutdownsock(DESC *d, int reason)
{
    char *buff, *buff2;
    int i, num;
    DESC *dtemp;

    if (  (reason == R_LOGOUT)
       && (site_check((d->address).sin_addr, mudstate.access_list) == H_FORBIDDEN))
    {
        reason = R_QUIT;
    }

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();

    if (d->flags & DS_CONNECTED)
    {
        // Added by D.Piper (del@doofer.org) 1997 & 2000-APR
        //

        // Reason: attribute (disconnect reason)
        //
        atr_add_raw(d->player, A_REASON, (char *)disc_messages[reason]);

        // Update the A_CONNINFO attribute.
        //
        long anFields[4];
        fetch_ConnectionInfoFields(d->player, anFields);

        // One of the active sessions is going away. It doesn't matter which
        // one.
        //
        anFields[CIF_NUMCONNECTS]++;

        // What are the two longest sessions?
        //
        DESC *dOldest[2];
        find_oldest(d->player, dOldest);

        CLinearTimeDelta ltdFull;
        ltdFull = ltaNow - dOldest[0]->connected_at;
        long tFull = ltdFull.ReturnSeconds();
        if (dOldest[0] == d)
        {
            // We are dropping the oldest connection.
            //
            CLinearTimeDelta ltdPart;
            if (dOldest[1])
            {
                // There is another (more recently made) connection.
                //
                ltdPart = dOldest[1]->connected_at - dOldest[0]->connected_at;
            }
            else
            {
                // There is only one connection.
                //
                ltdPart = ltdFull;
            }
            long tPart = ltdPart.ReturnSeconds();

            anFields[CIF_TOTALTIME] += tPart;
            if (anFields[CIF_LONGESTCONNECT] < tFull)
            {
                anFields[CIF_LONGESTCONNECT] = tFull;
            }
        }
        anFields[CIF_LASTCONNECT] = tFull;

        put_ConnectionInfoFields(d->player, anFields, ltaNow);

        // If we are doing a LOGOUT, keep the connection open so that the
        // player can connect to a different character. Otherwise, we
        // do the normal disconnect stuff.
        //
        if (reason == R_LOGOUT)
        {
            STARTLOG(LOG_NET | LOG_LOGIN, "NET", "LOGO")
            buff = alloc_mbuf("shutdownsock.LOG.logout");
            sprintf(buff, "[%d/%s] Logout by ", d->descriptor, d->addr);
            log_text(buff);
            log_name(d->player);
            sprintf(buff, " <Reason: %s>", disc_reasons[reason]);
            log_text(buff);
            free_mbuf(buff);
            ENDLOG;
        }
        else
        {
            fcache_dump(d, FC_QUIT);
            STARTLOG(LOG_NET | LOG_LOGIN, "NET", "DISC")
            buff = alloc_mbuf("shutdownsock.LOG.disconn");
            sprintf(buff, "[%d/%s] Logout by ", d->descriptor, d->addr);
            log_text(buff);
            log_name(d->player);
            sprintf(buff, " <Reason: %s>", disc_reasons[reason]);
            log_text(buff);
            free_mbuf(buff);
            ENDLOG;
            SiteMonSend(d->descriptor, d->addr, d, "Disconnection");
        }

        // If requested, write an accounting record of the form:
        // Plyr# Flags Cmds ConnTime Loc Money [Site] <DiscRsn> Name
        //
        STARTLOG(LOG_ACCOUNTING, "DIS", "ACCT");
        CLinearTimeDelta ltd = ltaNow - d->connected_at;
        int Seconds = ltd.ReturnSeconds();
        buff = alloc_lbuf("shutdownsock.LOG.accnt");
        buff2 = decode_flags(GOD, &(db[d->player].fs));
        sprintf(buff, "%d %s %d %d %d %d [%s] <%s> %s", d->player, buff2, d->command_count,
                Seconds, Location(d->player), Pennies(d->player), d->addr, disc_reasons[reason],
                Name(d->player));
        log_text(buff);
        free_lbuf(buff);
        free_sbuf(buff2);
        ENDLOG;
        announce_disconnect(d->player, d, disc_messages[reason]);
    }
    else
    {
        if (reason == R_LOGOUT)
        {
            reason = R_QUIT;
        }
        STARTLOG(LOG_SECURITY | LOG_NET, "NET", "DISC");
        buff = alloc_mbuf("shutdownsock.LOG.neverconn");
        sprintf(buff, "[%d/%s] Connection closed, never connected. <Reason: %s>", d->descriptor, d->addr, disc_reasons[reason]);
        log_text(buff);
        free_mbuf(buff);
        ENDLOG;
        SiteMonSend(d->descriptor, d->addr, d, "N/C Connection Closed");
    }

    process_output(d, false);
    clearstrings(d);

    d->flags &= ~DS_CONNECTED;

    // Is this desc still in interactive mode?
    //
    if (d->program_data != NULL)
    {
        num = 0;
        DESC_ITER_PLAYER(d->player, dtemp) num++;

        if (num == 0)
        {
            for (i = 0; i < MAX_GLOBAL_REGS; i++)
            {
                if (d->program_data->wait_regs[i])
                {
                    free_lbuf(d->program_data->wait_regs[i]);
                    d->program_data->wait_regs[i] = NULL;
                }
            }
            MEMFREE(d->program_data);
            d->program_data = NULL;
            atr_clr(d->player, A_PROGCMD);
        }
    }
    if (reason == R_LOGOUT)
    {
        d->connected_at.GetUTC();
        d->retries_left = mudconf.retry_limit;
        d->command_count = 0;
        d->timeout = mudconf.idle_timeout;
        d->player = 0;
        d->doing[0] = '\0';
        d->quota = mudconf.cmd_quota_max;
        d->last_time = d->connected_at;
        d->host_info = site_check((d->address).sin_addr, mudstate.access_list)
                     | site_check((d->address).sin_addr, mudstate.suspect_list);
        d->input_tot = d->input_size;
        d->output_tot = 0;
        welcome_user(d);
    }
    else
    {
        // Cancel any scheduled processing on this descriptor.
        //
        scheduler.CancelTask(Task_ProcessCommand, d, 0);

#ifdef WIN32
        if (platform == VER_PLATFORM_WIN32_NT)
        {
            // Don't close down the socket twice.
            //
            if (!d->bConnectionShutdown)
            {
                // Make sure we don't try to initiate or process any
                // outstanding IOs
                //
                d->bConnectionShutdown = true;

                // Protect removing the descriptor from our linked list from
                // any interference from the listening thread.
                //
                EnterCriticalSection(&csDescriptorList);
                *d->prev = d->next;
                if (d->next)
                {
                    d->next->prev = d->prev;
                }
                LeaveCriticalSection(&csDescriptorList);

                // This descriptor may hang around awhile, clear out the links.
                //
                d->next = 0;
                d->prev = 0;

                // Close the connection in 5 seconds.
                //
                scheduler.DeferTask(ltaNow + FACTOR_100NS_PER_SECOND*5,
                    PRIORITY_SYSTEM, Task_DeferredClose, d, 0);
            }
            return;
        }
#endif

        shutdown(d->descriptor, SD_BOTH);
        if (SOCKET_CLOSE(d->descriptor) == 0)
        {
            DebugTotalSockets--;
        }
        d->descriptor = INVALID_SOCKET;

        *d->prev = d->next;
        if (d->next)
        {
            d->next->prev = d->prev;
        }

        // This descriptor may hang around awhile, clear out the links.
        //
        d->next = 0;
        d->prev = 0;

        // If we don't have queued IOs, then we can free these, now.
        //
        freeqs(d);
        free_desc(d);
        ndescriptors--;
    }
}

#ifdef WIN32
void shutdownsock_brief(DESC *d)
{
    // don't close down the socket twice
    //
    if (d->bConnectionShutdown)
    {
        return;
    }

    // make sure we don't try to initiate or process any outstanding IOs
    //
    d->bConnectionShutdown = true;
    d->bConnectionDropped = true;


    // cancel any pending reads or writes on this socket
    //
    if (!fpCancelIo((HANDLE) d->descriptor))
    {
        Log.tinyprintf("Error %ld on CancelIo" ENDLINE, GetLastError());
    }

    shutdown(d->descriptor, SD_BOTH);
    if (closesocket(d->descriptor) == 0)
    {
        DebugTotalSockets--;
    }
    d->descriptor = INVALID_SOCKET;

    // protect removing the descriptor from our linked list from
    // any interference from the listening thread
    //
    EnterCriticalSection(&csDescriptorList);

    *d->prev = d->next;
    if (d->next)
    {
        d->next->prev = d->prev;
    }

    d->next = 0;
    d->prev = 0;

    // safe to allow the listening thread to continue now
    LeaveCriticalSection(&csDescriptorList);

    // post a notification that it is safe to free the descriptor
    // we can't free the descriptor here (below) as there may be some
    // queued completed IOs that will crash when they refer to a descriptor
    // (d) that has been freed.
    //
    if (!PostQueuedCompletionStatus(CompletionPort, 0, (DWORD) d, &lpo_aborted))
    {
        Log.tinyprintf("Error %ld on PostQueuedCompletionStatus in shutdownsock" ENDLINE, GetLastError());
    }

}
#endif // WIN32

int make_nonblocking(SOCKET s)
{
#ifdef WIN32
    unsigned long on = 1;
    if (IS_SOCKET_ERROR(ioctlsocket(s, FIONBIO, &on)))
    {
        log_perror("NET", "FAIL", "make_nonblocking", "ioctlsocket");
        return -1;
    }
#else // WIN32
#if defined(O_NONBLOCK)
    if (fcntl(s, F_SETFL, O_NONBLOCK) < 0)
    {
        log_perror("NET", "FAIL", "make_nonblocking", "fcntl");
        return -1;
    }
#elif defined(FNDELAY)
    if (fcntl(s, F_SETFL, FNDELAY) < 0)
    {
        log_perror("NET", "FAIL", "make_nonblocking", "fcntl");
        return -1;
    }
#elif defined(O_NDELAY)
    if (fcntl(s, F_SETFL, O_NDELAY) < 0)
    {
        log_perror("NET", "FAIL", "make_nonblocking", "fcntl");
        return -1;
    }
#elif defined(FIONBIO)
    unsigned long on = 1;
    if (ioctl(s, FIONBIO, &on) < 0)
    {
        log_perror("NET", "FAIL", "make_nonblocking", "ioctl");
        return -1;
    }
#endif // O_NONBLOCK, FNDELAY, O_NDELAY, FIONBIO
#endif // WIN32
    return 0;
}

void make_nolinger(SOCKET s)
{
#if defined(HAVE_LINGER)
    struct linger ling;
    ling.l_onoff = 0;
    ling.l_linger = 0;
    if (IS_SOCKET_ERROR(setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&ling, sizeof(ling))))
    {
        log_perror("NET", "FAIL", "linger", "setsockopt");
    }
#endif // HAVE_LINGER
}

void config_socket(SOCKET s)
{
    make_nonblocking(s);
    make_nolinger(s);
}

// This function must be thread safe WinNT
//
DESC *initializesock(SOCKET s, struct sockaddr_in *a)
{
    DESC *d;

#ifdef WIN32
    // protect adding the descriptor from the linked list from
    // any interference from socket shutdowns
    //
    if (platform == VER_PLATFORM_WIN32_NT)
    {
        EnterCriticalSection(&csDescriptorList);
    }
#endif // WIN32

    d = alloc_desc("init_sock");

#ifdef WIN32
    if (platform == VER_PLATFORM_WIN32_NT)
    {
        LeaveCriticalSection(&csDescriptorList);
    }
#endif // WIN32

    d->descriptor = s;
    d->flags = 0;
    d->connected_at.GetUTC();
    d->last_time = d->connected_at;
    d->retries_left = mudconf.retry_limit;
    d->command_count = 0;
    d->timeout = mudconf.idle_timeout;
    d->host_info = site_check((*a).sin_addr, mudstate.access_list)
                 | site_check((*a).sin_addr, mudstate.suspect_list);

    // Be sure #0 isn't wizard. Shouldn't be.
    //
    d->player = 0;

    d->addr[0] = '\0';
    d->doing[0] = '\0';
    d->username[0] = '\0';
    config_socket(s);
    d->output_prefix = NULL;
    d->output_suffix = NULL;
    d->output_size = 0;
    d->output_tot = 0;
    d->output_lost = 0;
    d->output_head = NULL;
    d->output_tail = NULL;
    d->input_head = NULL;
    d->input_tail = NULL;
    d->input_size = 0;
    d->input_tot = 0;
    d->input_lost = 0;
    d->raw_input = NULL;
    d->raw_input_at = NULL;
    d->nOption = 0;
    d->raw_input_state = NVT_IS_NORMAL;
    d->nvt_sga_him_state = OPTION_NO;
    d->nvt_sga_us_state = OPTION_NO;
    d->nvt_eor_him_state = OPTION_NO;
    d->nvt_eor_us_state = OPTION_NO;
    d->nvt_naws_him_state = OPTION_NO;
    d->nvt_naws_us_state = OPTION_NO;
    d->height = 24;
    d->width = 78;
    d->quota = mudconf.cmd_quota_max;
    d->program_data = NULL;
    d->address = *a;
    strncpy(d->addr, inet_ntoa(a->sin_addr), 50);
    d->addr[50] = '\0';

#ifdef WIN32
    // protect adding the descriptor from the linked list from
    // any interference from socket shutdowns
    //
    if (platform == VER_PLATFORM_WIN32_NT)
    {
        EnterCriticalSection (&csDescriptorList);
    }
#endif // WIN32

    ndescriptors++;

    if (descriptor_list)
    {
        descriptor_list->prev = &d->next;
    }
    d->hashnext = NULL;
    d->next = descriptor_list;
    d->prev = &descriptor_list;
    descriptor_list = d;

#ifdef WIN32
    // ok to continue now
    //
    if (platform == VER_PLATFORM_WIN32_NT)
    {
        LeaveCriticalSection (&csDescriptorList);

        d->OutboundOverlapped.hEvent = NULL;
        d->InboundOverlapped.hEvent = NULL;
        d->InboundOverlapped.Offset = 0;
        d->InboundOverlapped.OffsetHigh = 0;
        d->bWritePending = false;   // no write pending yet
        d->bConnectionShutdown = false; // not shutdown yet
        d->bConnectionDropped = false; // not dropped yet
        d->bCallProcessOutputLater = false;
    }
#endif // WIN32
    return d;
}

#ifdef WIN32
FTASK *process_output = 0;

void process_output9x(void *dvoid, int bHandleShutdown)
{
    DESC *d = (DESC *)dvoid;
    int cnt;

    char *cmdsave = mudstate.debug_cmd;
    mudstate.debug_cmd = "< process_output >";

    TBLOCK *tb = d->output_head;

    while (tb != NULL)
    {
        while (tb->hdr.nchars > 0)
        {
            cnt = SOCKET_WRITE(d->descriptor, tb->hdr.start, tb->hdr.nchars, 0);
            if (IS_SOCKET_ERROR(cnt))
            {
                int iSocketError = SOCKET_LAST_ERROR;
                if (  iSocketError != SOCKET_EWOULDBLOCK
#ifdef SOCKET_EAGAIN
                   && iSocketError != SOCKET_EAGAIN
#endif // SOCKET_EAGAIN
                   && bHandleShutdown)
                {
                    shutdownsock(d, R_SOCKDIED);
                }
                mudstate.debug_cmd = cmdsave;
                return;
            }
            d->output_size -= cnt;
            tb->hdr.nchars -= cnt;
            tb->hdr.start += cnt;
        }
        TBLOCK *save = tb;
        tb = tb->hdr.nxt;
        MEMFREE(save);
        save = NULL;
        d->output_head = tb;
        if (tb == NULL)
        {
            d->output_tail = NULL;
        }
    }
    mudstate.debug_cmd = cmdsave;
}

int AsyncSend(DESC *d, char *buf, int len)
{
    DWORD nBytes;

    // Move data from one buffer to another.
    //
    if (len <= SIZEOF_OVERLAPPED_BUFFERS)
    {
        // We can consume this buffer.
        //
        nBytes = len;
    }
    else
    {
        // Use the entire bufer and leave the remaining data in the queue.
        //
        nBytes = SIZEOF_OVERLAPPED_BUFFERS;
    }
    memcpy(d->output_buffer, buf, nBytes);

    d->OutboundOverlapped.Offset = 0;
    d->OutboundOverlapped.OffsetHigh = 0;

    BOOL bResult = WriteFile((HANDLE) d->descriptor, d->output_buffer, nBytes, NULL, &d->OutboundOverlapped);

    d->bWritePending = false;

    if (!bResult)
    {
        DWORD dwLastError = GetLastError();
        if (dwLastError == ERROR_IO_PENDING)
        {
            d->bWritePending = true;
        }
        else
        {
            if (!(d->bConnectionDropped))
            {
                // Do no more writes and post a notification that the descriptor should be shutdown.
                //
                d->bConnectionDropped = true;
                Log.tinyprintf("AsyncSend(%d) failed with error %ld. Requesting port shutdown." ENDLINE, d->descriptor, dwLastError);
                if (!PostQueuedCompletionStatus(CompletionPort, 0, (DWORD) d, &lpo_shutdown))
                {
                    Log.tinyprintf("Error %ld on PostQueuedCompletionStatus in AsyncSend" ENDLINE, GetLastError());
                }
            }
            return 0;
        }
    }
    return nBytes;
}

void process_outputNT(void *dvoid, int bHandleShutdown)
{
    UNUSED_PARAMETER(bHandleShutdown);

    DESC *d = (DESC *)dvoid;

    // Don't write if connection dropped or a write is pending.
    //
    if (d->bConnectionDropped || d->bWritePending)
    {
        return;
    }

    char *cmdsave = mudstate.debug_cmd;
    mudstate.debug_cmd = "< process_output >";

    TBLOCK *tb = d->output_head;
    TBLOCK *save;
    int cnt;

    if (tb != NULL)
    {
        while (tb->hdr.nchars == 0)
        {
            save = tb;
            tb = tb->hdr.nxt;
            MEMFREE(save);
            save = NULL;
            d->output_head = tb;
            if (tb == NULL)
            {
                d->output_tail = NULL;
                break;
            }
        }

        if (tb != NULL)
        {
            if (tb->hdr.nchars > 0)
            {
                cnt = AsyncSend(d, tb->hdr.start, tb->hdr.nchars);
                if (cnt <= 0)
                {
                    mudstate.debug_cmd = cmdsave;
                    return;
                }
                d->output_size -= cnt;
                tb->hdr.nchars -= cnt;
                tb->hdr.start += cnt;
            }
            if (tb->hdr.nchars <= 0)
            {
                save = tb;
                tb = tb->hdr.nxt;
                MEMFREE(save);
                save = NULL;
                d->output_head = tb;
                if (tb == NULL)
                {
                    d->output_tail = NULL;
                }
            }
        }
    }
    mudstate.debug_cmd = cmdsave;
}

#else // WIN32

void process_output(void *dvoid, int bHandleShutdown)
{
    DESC *d = (DESC *)dvoid;

    char *cmdsave = mudstate.debug_cmd;
    mudstate.debug_cmd = "< process_output >";

    TBLOCK *tb = d->output_head;
    while (tb != NULL)
    {
        while (tb->hdr.nchars > 0)
        {
            int cnt = SOCKET_WRITE(d->descriptor, tb->hdr.start, tb->hdr.nchars, 0);
            if (IS_SOCKET_ERROR(cnt))
            {
                int iSocketError = SOCKET_LAST_ERROR;
                mudstate.debug_cmd = cmdsave;
                if (  iSocketError != SOCKET_EWOULDBLOCK
#ifdef SOCKET_EAGAIN
                   && iSocketError != SOCKET_EAGAIN
#endif // SOCKET_EAGAIN
                   && bHandleShutdown)
                {
                    shutdownsock(d, R_SOCKDIED);
                }
                return;
            }
            d->output_size -= cnt;
            tb->hdr.nchars -= cnt;
            tb->hdr.start += cnt;
        }
        TBLOCK *save = tb;
        tb = tb->hdr.nxt;
        MEMFREE(save);
        save = NULL;
        d->output_head = tb;
        if (tb == NULL)
        {
            d->output_tail = NULL;
        }
    }

    mudstate.debug_cmd = cmdsave;
}
#endif // WIN32

/*! \brief Table to quickly classify characters recieved from the wire with
 * their Telnet meaning.
 *
 * The use of this table reduces the size of the state table.
 *
 * Class  0 - Any byte.    Class  5 - BRK  (0xF3)  Class 10 - WONT (0xFC)
 * Class  1 - BS   (0x08)  Class  5 - IP   (0xF4)  Class 11 - DO   (0xFD)
 * Class  2 - LF   (0x0A)  Class  5 - AO   (0xF5)  Class 12 - DONT (0xFE)
 * Class  3 - CR   (0x0D)  Class  6 - AYT  (0xF6)  Class 13 - IAC  (0xFF)
 * Class  1 - DEL  (0x7F)  Class  7 - EC   (0xF7)
 * Class  5 - EOR  (0xEF)  Class  5 - EL   (0xF8)
 * Class  4 - SE   (0xF0)  Class  5 - GA   (0xF9)
 * Class  5 - NOP  (0xF1)  Class  8 - SB   (0xFA)
 * Class  5 - DM   (0xF2)  Class  9 - WILL (0xFB)
 */

static const unsigned char nvt_input_xlat_table[256] =
{
//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
//
    0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  2,  0,  0,  3,  0,  0,  // 0
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 1
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 2
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 3
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 4
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 5
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 6
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  // 7

    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 8
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 9
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // A
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // B
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // C
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // D
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  5,  // E
    4,  5,  5,  5,  5,  5,  6,  7,  5,  5,  8,  9, 10, 11, 12, 13   // F
};

/*! \brief Table to map current telnet parsing state state and input to
 * specific actions and state changes.
 *
 * Action  0 - Nothing.
 * Action  1 - Accept CHR(X) (and transition to Normal state).
 * Action  2 - Erase Character.
 * Action  3 - Accept Line.
 * Action  4 - Transition to the Normal state.
 * Action  5 - Transition to Have_IAC state.
 * Action  6 - Transition to the Have_IAC_WILL state.
 * Action  7 - Transition to the Have_IAC_DONT state.
 * Action  8 - Transition to the Have_IAC_DO state.
 * Action  9 - Transition to the Have_IAC_WONT state.
 * Action 10 - Transition to the Have_IAC_SB state.
 * Action 11 - Transition to the Have_IAC_SB_IAC state.
 * Action 12 - Respond to IAC AYT and return to the Normal state.
 * Action 13 - Respond to IAC WILL X
 * Action 14 - Respond to IAC DONT X
 * Action 15 - Respond to IAC DO X
 * Action 16 - Respond to IAC WONT X
 * Action 17 - Accept CHR(X) for Sub-Option (and transition to Have_IAC_SB state).
 * Action 18 - Accept Completed Sub-option and transition to Normal state.
 */

static const int nvt_input_action_table[8][14] =
{
//    Any   BS   LF   CR   SE  NOP  AYT   EC   SB WILL DONT   DO WONT  IAC
    {   1,   2,   3,   0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   5  }, // Normal
    {   4,   4,   4,   4,   4,   4,  12,   2,  10,   6,   7,   8,   9,   1  }, // Have_IAC
    {  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,  13,   4  }, // Have_IAC_WILL
    {  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,  14,   4  }, // Have_IAC_DONT
    {  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,   4  }, // Have_IAC_DO
    {  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,  16,   4  }, // Have_IAC_WONT
    {  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  17,  11  }, // Have_IAC_SB
    {   0,   0,   0,   0,  18,   0,   0,   0,   0,   0,   0,   0,   0,  17  }, // Have_IAC_SB_IAC
};

/*! \brief Return the other side's negotiation state.
 *
 * The negotiation of each optional feature of telnet can be in one of six
 * states (defined in interface.h): OPTION_NO, OPTION_YES,
 * OPTION_WANTNO_EMPTY, OPTION_WANTNO_OPPOSITE, OPTION_WANTYES_EMPTY, and
 * OPTION_WANTYES_OPPOSITE.
 *
 * An option is only enabled when it is in the OPTION_YES state.
 *
 * \param d        Player connection context.
 * \param chOption Telnet Option
 * \return         One of six states.
 */

int HimState(DESC *d, unsigned char chOption)
{
    if (TELNET_NAWS == chOption)
    {
        return d->nvt_naws_him_state;
    }
    else if (TELNET_EOR == chOption)
    {
        return d->nvt_eor_him_state;
    }
    else if (TELNET_SGA == chOption)
    {
        return d->nvt_sga_him_state;
    }
    return OPTION_NO;
}

/*! \brief Return our side's negotiation state.
 *
 * The negotiation of each optional feature of telnet can be in one of six
 * states (defined in interface.h): OPTION_NO, OPTION_YES,
 * OPTION_WANTNO_EMPTY, OPTION_WANTNO_OPPOSITE, OPTION_WANTYES_EMPTY, and
 * OPTION_WANTYES_OPPOSITE.
 *
 * An option is only enabled when it is in the OPTION_YES state.
 *
 * \param d        Player connection context.
 * \param chOption Telnet Option
 * \return         One of six states.
 */

int UsState(DESC *d, unsigned char chOption)
{
    if (TELNET_NAWS == chOption)
    {
        return d->nvt_naws_us_state;
    }
    else if (TELNET_EOR == chOption)
    {
        return d->nvt_eor_us_state;
    }
    else if (TELNET_SGA == chOption)
    {
        return d->nvt_sga_us_state;
    }
    return OPTION_NO;
}

/*! \brief Change the other side's negotiation state.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option
 * \param iHimState One of the six option negotiation states.
 * \return          None.
 */

void SetHimState(DESC *d, unsigned char chOption, int iHimState)
{
    if (TELNET_NAWS == chOption)
    {
        d->nvt_naws_him_state = iHimState;
    }
    else if (TELNET_EOR == chOption)
    {
        d->nvt_eor_him_state = iHimState;
    }
    else if (TELNET_SGA == chOption)
    {
        d->nvt_sga_him_state = iHimState;
    }
}

/*! \brief Change our side's negotiation state.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \param iHimState One of the six option negotiation states.
 * \return          None.
 */

void SetUsState(DESC *d, unsigned char chOption, int iUsState)
{
    if (TELNET_NAWS == chOption)
    {
        d->nvt_naws_us_state = iUsState;
    }
    else if (TELNET_EOR == chOption)
    {
        d->nvt_eor_us_state = iUsState;
        if (OPTION_YES == iUsState)
        {
            EnableUs(d, TELNET_SGA);
        }
        else if (OPTION_NO == iUsState)
        {
            DisableUs(d, TELNET_SGA);
        }
    }
    else if (TELNET_SGA == chOption)
    {
        d->nvt_sga_us_state = iUsState;
    }
}

/*! \brief Transmit a Telnet WILL sequence for the given option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \return          None.
 */

void SendWill(DESC *d, unsigned char chOption)
{
    char aWill[3] = { NVT_IAC, NVT_WILL, 0 };
    aWill[2] = chOption;
    queue_write_LEN(d, aWill, sizeof(aWill));
}

/*! \brief Transmit a Telnet DONT sequence for the given option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \return          None.
 */

void SendDont(DESC *d, unsigned char chOption)
{
    char aDont[3] = { NVT_IAC, NVT_DONT, 0 };
    aDont[2] = chOption;
    queue_write_LEN(d, aDont, sizeof(aDont));
}

/*! \brief Transmit a Telnet DO sequence for the given option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \return          None.
 */

void SendDo(DESC *d, unsigned char chOption)
{
    char aDo[3]   = { NVT_IAC, NVT_DO,   0 };
    aDo[2] = chOption;
    queue_write_LEN(d, aDo, sizeof(aDo));
}

/*! \brief Transmit a Telnet WONT sequence for the given option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \return          None.
 */

void SendWont(DESC *d, unsigned char chOption)
{
    char aWont[3] = { NVT_IAC, NVT_WONT, 0 };
    aWont[2] = chOption;
    queue_write_LEN(d, aWont, sizeof(aWont));
}

/*! \brief Determine whether we want a particular option on his side of the
 * link to be enabled.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \return          Yes if we want it enabled.
 */

bool DesiredHimOption(DESC *d, unsigned char chOption)
{
    UNUSED_PARAMETER(d);

    if (  TELNET_NAWS == chOption
       || TELNET_EOR  == chOption
       || TELNET_SGA  == chOption)
    {
        return true;
    }
    return false;
}

/*! \brief Determine whether we want a particular option on our side of the
 * link to be enabled.
 *
 * It doesn't make sense for NAWS to be enabled on the server side, and we
 * only negotiate SGA on our side if we have already successfully negotiated
 * the EOR option.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \return          Yes if we want it enabled.
 */

bool DesiredUsOption(DESC *d, unsigned char chOption)
{
    if (  TELNET_EOR  == chOption
       || (  TELNET_SGA == chOption
          && OPTION_YES == UsState(d, TELNET_EOR)))
    {
        return true;
    }
    return false;
}

/*! \brief Start the process of negotiating the enablement of an option on
 * his side.
 *
 * Whether we actually send anything across the wire to enable this depends
 * on the negotiation state. The option could potentially already be enabled.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \return          None.
 */

void EnableHim(DESC *d, unsigned char chOption)
{
    switch (HimState(d, chOption))
    {
    case OPTION_NO:
        SetHimState(d, chOption, OPTION_WANTYES_EMPTY);
        SendDo(d, chOption);
        break;

    case OPTION_WANTNO_EMPTY:
        SetHimState(d, chOption, OPTION_WANTNO_OPPOSITE);
        break;

    case OPTION_WANTYES_OPPOSITE:
        SetHimState(d, chOption, OPTION_WANTYES_EMPTY);
        break;
    }
}

/*! \brief Start the process of negotiating the disablement of an option on
 * his side.
 *
 * Whether we actually send anything across the wire to disable this depends
 * on the negotiation state. The option could potentially already be disabled.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \return          None.
 */

void DisableHim(DESC *d, unsigned char chOption)
{
    switch (HimState(d, chOption))
    {
    case OPTION_YES:
        SetHimState(d, chOption, OPTION_WANTNO_EMPTY);
        SendDont(d, chOption);
        break;

    case OPTION_WANTNO_OPPOSITE:
        SetHimState(d, chOption, OPTION_WANTNO_EMPTY);
        break;

    case OPTION_WANTYES_EMPTY:
        SetHimState(d, chOption, OPTION_WANTYES_OPPOSITE);
        break;
    }
}

/*! \brief Start the process of negotiating the enablement of an option on
 * our side.
 *
 * Whether we actually send anything across the wire to enable this depends
 * on the negotiation state. The option could potentially already be enabled.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \return          None.
 */

void EnableUs(DESC *d, unsigned char chOption)
{
    switch (HimState(d, chOption))
    {
    case OPTION_NO:
        SetUsState(d, chOption, OPTION_WANTYES_EMPTY);
        SendWill(d, chOption);
        break;

    case OPTION_WANTNO_EMPTY:
        SetUsState(d, chOption, OPTION_WANTNO_OPPOSITE);
        break;

    case OPTION_WANTYES_OPPOSITE:
        SetUsState(d, chOption, OPTION_WANTYES_EMPTY);
        break;
    }
}

/*! \brief Start the process of negotiating the disablement of an option on
 * our side.
 *
 * Whether we actually send anything across the wire to disable this depends
 * on the negotiation state. The option could potentially already be disabled.
 *
 * \param d         Player connection context.
 * \param chOption  Telnet Option.
 * \return          None.
 */

void DisableUs(DESC *d, unsigned char chOption)
{
    switch (HimState(d, chOption))
    {
    case OPTION_YES:
        SetUsState(d, chOption, OPTION_WANTNO_EMPTY);
        SendWont(d, chOption);
        break;

    case OPTION_WANTNO_OPPOSITE:
        SetUsState(d, chOption, OPTION_WANTNO_EMPTY);
        break;

    case OPTION_WANTYES_EMPTY:
        SetUsState(d, chOption, OPTION_WANTYES_OPPOSITE);
        break;
    }
}

/*! \brief Begin initial telnet negotiations on a socket.
 *
 * The two sides of the connection may not agree on the following set of
 * options, and keep in mind that the successful negotiation of a particular
 * option may cause the negotiation of another option.
 *
 * Without this function, we are only react to client requests.
 *
 * \param d        Player connection on which the input arrived.
 * \return         None.
 */

void TelnetSetup(DESC *d)
{
    // Attempt negotation of EOR so we can use that, and if that succeeds,
    // code elsewhere will attempt the negotation of SGA for our side as well.
    //
    EnableUs(d, TELNET_EOR);
    EnableHim(d, TELNET_EOR);
    EnableHim(d, TELNET_SGA);
    EnableHim(d, TELNET_NAWS);
}

/*! \brief Parse raw data from network connection into command lines and
 * Telnet indications.
 *
 * Once input has been received from a particular socket, it is given to this
 * function for initial parsing. While most clients do line editing on their
 * side, a raw telnet client is still capable of sending backspace (BS) and
 * Delete (DEL) to the server, so we perform basic editing on our side.
 *
 * TinyMUX only allows printable characters through, imposes a maximum line
 * length, and breaks lines at CRLF.
 *
 * \param d        Player connection on which the input arrived.
 * \param pBytes   Point to received bytes.
 * \param nBytes   Number of received bytes in above buffer.
 * \return         None.
 */

void process_input_helper(DESC *d, char *pBytes, int nBytes)
{
    if (!d->raw_input)
    {
        d->raw_input = (CBLK *) alloc_lbuf("process_input.raw");
        d->raw_input_at = d->raw_input->cmd;
    }

    int nInputBytes = 0;
    int nLostBytes  = 0;

    char *p    = d->raw_input_at;
    char *pend = d->raw_input->cmd + (LBUF_SIZE - sizeof(CBLKHDR) - 1);

    unsigned char *q    = d->aOption + d->nOption;
    unsigned char *qend = d->aOption + SBUF_SIZE - 1;

    int n = nBytes;
    while (n--)
    {
        unsigned char ch = (unsigned char)*pBytes;
        int iAction = nvt_input_action_table[d->raw_input_state][nvt_input_xlat_table[ch]];
        switch (iAction)
        {
        case 1:
            // Action 1 - Accept CHR(X).
            //
            if (mux_isprint(ch))
            {
                if (p < pend)
                {
                    *p++ = ch;
                    nInputBytes++;
                }
                else
                {
                    nLostBytes++;
                }
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 0:
            // Action 0 - Nothing.
            //
            break;

        case 2:
            // Action 2 - Erase Character.
            //
            if (NVT_DEL == ch)
            {
                queue_string(d, "\b \b");
            }
            else
            {
                queue_string(d, " \b");
            }

            if (p > d->raw_input->cmd)
            {
                // The character we took back.
                //
                nInputBytes -= 1;
                p--;
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 3:
            // Action  3 - Accept Line.
            //
            *p = '\0';
            if (d->raw_input->cmd < p)
            {
                save_command(d, d->raw_input);
                d->raw_input = (CBLK *) alloc_lbuf("process_input.raw");

                p = d->raw_input_at = d->raw_input->cmd;
                pend = d->raw_input->cmd + (LBUF_SIZE - sizeof(CBLKHDR) - 1);
            }
            break;

        case 4:
            // Action 4 - Transition to the Normal state.
            //
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 5:
            // Action  5 - Transition to Have_IAC state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC;
            break;

        case 6:
            // Action 6 - Transition to the Have_IAC_WILL state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_WILL;
            break;

        case 7:
            // Action  7 - Transition to the Have_IAC_DONT state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_DONT;
            break;

        case 8:
            // Action  8 - Transition to the Have_IAC_DO state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_DO;
            break;

        case 9:
            // Action  9 - Transition to the Have_IAC_WONT state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_WONT;
            break;

        case 10:
            // Action 10 - Transition to the Have_IAC_SB state.
            //
            q = d->aOption;
            d->raw_input_state = NVT_IS_HAVE_IAC_SB;
            break;

        case 11:
            // Action 11 - Transition to the Have_IAC_SB_IAC state.
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_SB_IAC;
            break;

        case 12:
            // Action 12 - Respond to IAC AYT and return to the Normal state.
            //
            queue_string(d, "\r\n[Yes]\r\n");
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 13:
            // Action 13 - Respond to IAC WILL X
            //
            switch (HimState(d, ch))
            {
            case OPTION_NO:
                if (DesiredHimOption(d, ch))
                {
                    SetHimState(d, ch, OPTION_YES);
                    SendDo(d, ch);
                }
                else
                {
                    SendDont(d, ch);
                }
                break;

            case OPTION_WANTNO_EMPTY:
                SetHimState(d, ch, OPTION_NO);
                break;

            case OPTION_WANTYES_OPPOSITE:
                SetHimState(d, ch, OPTION_WANTNO_EMPTY);
                SendDont(d, ch);
                break;

            default:
                SetHimState(d, ch, OPTION_YES);
                break;
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 14:
            // Action 14 - Respond to IAC DONT X
            //
            switch (UsState(d, ch))
            {
            case OPTION_YES:
                SetUsState(d, ch, OPTION_NO);
                SendWont(d, ch);
                break;

            case OPTION_WANTNO_OPPOSITE:
                SetUsState(d, ch, OPTION_WANTYES_EMPTY);
                SendWill(d, ch);
                break;

            default:
                SetUsState(d, ch, OPTION_NO);
                break;
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 15:
            // Action 15 - Respond to IAC DO X
            //
            switch (UsState(d, ch))
            {
            case OPTION_NO:
                if (DesiredUsOption(d, ch))
                {
                    SetUsState(d, ch, OPTION_YES);
                    SendWill(d, ch);
                }
                else
                {
                    SendWont(d, ch);
                }
                break;

            case OPTION_WANTNO_EMPTY:
                SetUsState(d, ch, OPTION_NO);
                break;

            case OPTION_WANTYES_OPPOSITE:
                SetUsState(d, ch, OPTION_WANTNO_EMPTY);
                SendWont(d, ch);
                break;

            default:
                SetUsState(d, ch, OPTION_YES);
                break;
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 16:
            // Action 16 - Respond to IAC WONT X
            //
            // Ignore.
            //
            switch (HimState(d, ch))
            {
            case OPTION_NO:
                break;

            case OPTION_YES:
                SetHimState(d, ch, OPTION_NO);
                SendDont(d, ch);
                break;

            case OPTION_WANTNO_OPPOSITE:
                SetHimState(d, ch, OPTION_WANTYES_EMPTY);
                SendDo(d, ch);
                break;

            default:
                SetHimState(d, ch, OPTION_NO);
                break;
            }
            d->raw_input_state = NVT_IS_NORMAL;
            break;

        case 17:
            // Action 17 - Accept CHR(X) for Sub-Option (and transition to Have_IAC_SB state).
            //
            d->raw_input_state = NVT_IS_HAVE_IAC_SB;
            if (  d->aOption <= q
               && q < qend)
            {
                *q++ = ch;
            }
            break;

        case 18:
            // Action 18 - Accept Completed Sub-option and transition to Normal state.
            //
            if (  d->aOption < q
               && q < qend)
            {
                size_t n = q - d->aOption;
                switch (d->aOption[0])
                {
                case TELNET_NAWS:
                    if (n == 5)
                    {
                        d->width  = (d->aOption[1] << 8 ) | d->aOption[2];
                        d->height = (d->aOption[3] << 8 ) | d->aOption[4];
                    }
                    break;
                }
            }
            q = d->aOption;
            d->raw_input_state = NVT_IS_NORMAL;
            break;
        }
        pBytes++;
    }

    if (  d->raw_input->cmd < p
       && p < pend)
    {
        d->raw_input_at = p;
    }
    else
    {
        free_lbuf(d->raw_input);
        d->raw_input = NULL;
        d->raw_input_at = NULL;
    }

    if ( d->aOption <= q
       && q < qend)
    {
        d->nOption = q - d->aOption;
    }
    else
    {
        d->nOption = 0;
    }
    d->input_tot  += nBytes;
    d->input_size += nInputBytes;
    d->input_lost += nLostBytes;
}

bool process_input(DESC *d)
{
    char *cmdsave = mudstate.debug_cmd;
    mudstate.debug_cmd = "< process_input >";

    char buf[LBUF_SIZE];
    int got = SOCKET_READ(d->descriptor, buf, sizeof(buf), 0);
    if (IS_SOCKET_ERROR(got) || got == 0)
    {
        int iSocketError = SOCKET_LAST_ERROR;
        mudstate.debug_cmd = cmdsave;
        if (  IS_SOCKET_ERROR(got)
           && (  iSocketError == SOCKET_EWOULDBLOCK
#ifdef SOCKET_EAGAIN
              || iSocketError == SOCKET_EAGAIN
#endif // SOCKET_EAGAIN
              || iSocketError == SOCKET_EINTR))
        {
            return true;
        }
        return false;
    }
    process_input_helper(d, buf, got);
    mudstate.debug_cmd = cmdsave;
    return true;
}

void close_sockets(bool emergency, char *message)
{
    DESC *d, *dnext;

    DESC_SAFEITER_ALL(d, dnext)
    {
        if (emergency)
        {
            SOCKET_WRITE(d->descriptor, message, strlen(message), 0);
            if (IS_SOCKET_ERROR(shutdown(d->descriptor, SD_BOTH)))
            {
                log_perror("NET", "FAIL", NULL, "shutdown");
            }
            if (SOCKET_CLOSE(d->descriptor) == 0)
            {
                DebugTotalSockets--;
            }
        }
        else
        {
            queue_string(d, message);
            queue_write_LEN(d, "\r\n", 2);
            shutdownsock(d, R_GOING_DOWN);
        }
    }
    for (int i = 0; i < nMainGamePorts; i++)
    {
        if (SOCKET_CLOSE(aMainGamePorts[i].socket) == 0)
        {
            DebugTotalSockets--;
        }
        aMainGamePorts[i].socket = INVALID_SOCKET;
    }
}

void emergency_shutdown(void)
{
    close_sockets(true, "Going down - Bye");
}


// ---------------------------------------------------------------------------
// Signal handling routines.
//
static void check_panicking(int sig)
{
    // If we are panicking, turn off signal catching and resignal.
    //
    if (mudstate.panicking)
    {
        for (int i = 0; i < NSIG; i++)
        {
            signal(i, SIG_DFL);
        }
#ifdef WIN32
        UNUSED_PARAMETER(sig);
        abort();
#else // WIN32
        kill(game_pid, sig);
#endif // WIN32
    }
    mudstate.panicking = true;
}

static void unset_signals(void)
{
    int i;

    for (i = 0; i < NSIG; i++)
    {
        signal(i, SIG_DFL);
    }
}

#ifdef _SGI_SOURCE
#define CAST_SIGNAL_FUNC (SIG_PF)
#else // _SGI_SOURCE
#define CAST_SIGNAL_FUNC
#endif // _SGI_SOURCE

// The purpose of the following code is support the case where sys_siglist is
// is not part of the environment. This is the case for some Unix platforms
// and also for Win32.
//
typedef struct
{
    int         iSignal;
    const char *szSignal;
} SIGNALTYPE, *PSIGNALTYPE;

const SIGNALTYPE aSigTypes[] =
{
#ifdef SIGHUP
    // Hangup detected on controlling terminal or death of controlling process.
    //
    { SIGHUP,   "SIGHUP"},
#endif // SIGHUP
#ifdef SIGINT
    // Interrupt from keyboard.
    //
    { SIGINT,   "SIGINT"},
#endif // SIGINT
#ifdef SIGQUIT
    // Quit from keyboard.
    //
    { SIGQUIT,  "SIGQUIT"},
#endif // SIGQUIT
#ifdef SIGILL
    // Illegal Instruction.
    //
    { SIGILL,   "SIGILL"},
#endif // SIGILL
#ifdef SIGTRAP
    // Trace/breakpoint trap.
    //
    { SIGTRAP,  "SIGTRAP"},
#endif // SIGTRAP
#if defined(SIGABRT)
    // Abort signal from abort(3).
    //
    { SIGABRT,  "SIGABRT"},
#elif defined(SIGIOT)
#define SIGABRT SIGIOT
    // Abort signal from abort(3).
    //
    { SIGIOT,   "SIGIOT"},
#endif // SIGABRT
#ifdef SIGEMT
    { SIGEMT,   "SIGEMT"},
#endif // SIGEMT
#ifdef SIGFPE
    // Floating-point exception.
    //
    { SIGFPE,   "SIGFPE"},
#endif // SIGFPE
#ifdef SIGKILL
    // Kill signal. Not catchable.
    //
    { SIGKILL,  "SIGKILL"},
#endif // SIGKILL
#ifdef SIGSEGV
    // Invalid memory reference.
    //
    { SIGSEGV,  "SIGSEGV"},
#endif // SIGSEGV
#ifdef SIGPIPE
    // Broken pipe: write to pipe with no readers.
    //
    { SIGPIPE,  "SIGPIPE"},
#endif // SIGPIPE
#ifdef SIGALRM
    // Timer signal from alarm(2).
    //
    { SIGALRM,  "SIGALRM"},
#endif // SIGALRM
#ifdef SIGTERM
    // Termination signal.
    //
    { SIGTERM,  "SIGTERM"},
#endif // SIGTERM
#ifdef SIGBREAK
    // Ctrl-Break.
    //
    { SIGBREAK, "SIGBREAK"},
#endif // SIGBREAK
#ifdef SIGUSR1
    // User-defined signal 1.
    //
    { SIGUSR1,  "SIGUSR1"},
#endif // SIGUSR1
#ifdef SIGUSR2
    // User-defined signal 2.
    //
    { SIGUSR2,  "SIGUSR2"},
#endif // SIGUSR2
#if defined(SIGCHLD)
    // Child stopped or terminated.
    //
    { SIGCHLD,  "SIGCHLD"},
#elif defined(SIGCLD)
#define SIGCHLD SIGCLD
    // Child stopped or terminated.
    //
    { SIGCLD,   "SIGCLD"},
#endif // SIGCHLD
#ifdef SIGCONT
    // Continue if stopped.
    //
    { SIGCONT,  "SIGCONT"},
#endif // SIGCONT
#ifdef SIGSTOP
    // Stop process. Not catchable.
    //
    { SIGSTOP,  "SIGSTOP"},
#endif // SIGSTOP
#ifdef SIGTSTP
    // Stop typed at tty
    //
    { SIGTSTP,  "SIGTSTP"},
#endif // SIGTSTP
#ifdef SIGTTIN
    // tty input for background process.
    //
    { SIGTTIN,  "SIGTTIN"},
#endif // SIGTTIN
#ifdef SIGTTOU
    // tty output for background process.
    //
    { SIGTTOU,  "SIGTTOU"},
#endif // SIGTTOU
#ifdef SIGBUS
    // Bus error (bad memory access).
    //
    { SIGBUS,   "SIGBUS"},
#endif // SIGBUS
#ifdef SIGPROF
    // Profiling timer expired.
    //
    { SIGPROF,  "SIGPROF"},
#endif // SIGPROF
#ifdef SIGSYS
    // Bad argument to routine (SVID).
    //
    { SIGSYS,   "SIGSYS"},
#endif // SIGSYS
#ifdef SIGURG
    // Urgent condition on socket (4.2 BSD).
    //
    { SIGURG,   "SIGURG"},
#endif // SIGURG
#ifdef SIGVTALRM
    // Virtual alarm clock (4.2 BSD).
    //
    { SIGVTALRM, "SIGVTALRM"},
#endif // SIGVTALRM
#ifdef SIGXCPU
    // CPU time limit exceeded (4.2 BSD).
    //
    { SIGXCPU,  "SIGXCPU"},
#endif // SIGXCPU
#ifdef SIGXFSZ
    // File size limit exceeded (4.2 BSD).
    //
    { SIGXFSZ,  "SIGXFSZ"},
#endif // SIGXFSZ
#ifdef SIGSTKFLT
    // Stack fault on coprocessor.
    //
    { SIGSTKFLT, "SIGSTKFLT"},
#endif // SIGSTKFLT
#if defined(SIGIO)
    // I/O now possible (4.2 BSD). File lock lost.
    //
    { SIGIO,    "SIGIO"},
#elif defined(SIGPOLL)
#define SIGIO SIGPOLL
    // Pollable event (Sys V).
    //
    { SIGPOLL,  "SIGPOLL"},
#endif // SIGIO
#ifdef SIGLOST
    { SIGLOST,  "SIGLOST"},
#endif // SIGLOST
#if defined(SIGPWR)
    // Power failure (System V).
    //
    { SIGPWR,   "SIGPWR"},
#elif defined(SIGINFO)
#define SIGPWR SIGINFO
    // Power failure (System V).
    //
    { SIGINFO,  "SIGINFO"},
#endif // SIGPWR
#ifdef SIGWINCH
    // Window resize signal (4.3 BSD, Sun).
    //
    { SIGWINCH, "SIGWINCH"},
#endif // SIGWINCH
    { 0,        "SIGZERO" },
    { -1, NULL }
};

typedef struct
{
    const char *pShortName;
    const char *pLongName;
} MUX_SIGNAMES;

static MUX_SIGNAMES signames[NSIG];

#if defined(HAVE_SYS_SIGNAME)
#define SysSigNames sys_signame
#elif defined(SYS_SIGLIST_DECLARED)
#define SysSigNames sys_siglist
#endif // HAVE_SYS_SIGNAME

void BuildSignalNamesTable(void)
{
    int i;
    for (i = 0; i < NSIG; i++)
    {
        signames[i].pShortName = NULL;
        signames[i].pLongName  = NULL;
    }

    const SIGNALTYPE *pst = aSigTypes;
    while (pst->szSignal)
    {
        int sig = pst->iSignal;
        if (  0 <= sig
           && sig < NSIG)
        {
            MUX_SIGNAMES *tsn = &signames[sig];
            if (tsn->pShortName == NULL)
            {
                tsn->pShortName = pst->szSignal;
#ifndef WIN32
                if (sig == SIGUSR1)
                {
                    tsn->pLongName = "Restart server";
                }
                else if (sig == SIGUSR2)
                {
                    tsn->pLongName = "Drop flatfile";
                }
#endif // WIN32
#ifdef SysSigNames
                if (  tsn->pLongName == NULL
                   && SysSigNames[sig]
                   && strcmp(tsn->pShortName, SysSigNames[sig]) != 0)
                {
                    tsn->pLongName = SysSigNames[sig];
                }
#endif // SysSigNames
            }
        }
        pst++;
    }
    for (i = 0; i < NSIG; i++)
    {
        MUX_SIGNAMES *tsn = &signames[i];
        if (tsn->pShortName == NULL)
        {
#ifdef SysSigNames
            if (SysSigNames[i])
            {
                tsn->pLongName = SysSigNames[i];
            }
#endif // SysSigNames

            // This is the only non-const memory case.
            //
            tsn->pShortName = StringClone(tprintf("SIG%03d", i));
        }
    }
}

static char *SignalDesc(int iSignal)
{
    static char buff[LBUF_SIZE];
    char *bufc = buff;
    safe_str(signames[iSignal].pShortName, buff, &bufc);
    if (signames[iSignal].pLongName)
    {
        safe_str(" (", buff, &bufc);
        safe_str(signames[iSignal].pLongName, buff, &bufc);
        safe_chr(')', buff, &bufc);
    }
    *bufc = '\0';
    return buff;
}

void log_signal(int iSignal)
{
    STARTLOG(LOG_PROBLEMS, "SIG", "CATCH");
    log_text("Caught signal ");
    log_text(SignalDesc(iSignal));
    ENDLOG;
}

void log_signal_ignore(int iSignal)
{
    STARTLOG(LOG_PROBLEMS, "SIG", "CATCH");
    log_text("Caught signal and ignored signal ");
    log_text(SignalDesc(iSignal));
    log_text(" because server just came up.");
    ENDLOG;
}

#ifndef WIN32
void LogStatBuf(int stat_buf, const char *Name)
{
    STARTLOG(LOG_ALWAYS, "NET", Name);
    if (WIFEXITED(stat_buf))
    {
        Log.tinyprintf("process exited unexpectedly with exit status %d.", WEXITSTATUS(stat_buf));
    }
    else if (WIFSIGNALED(stat_buf))
    {
        Log.tinyprintf("process was terminated with signal %s.", SignalDesc(WTERMSIG(stat_buf)));
    }
    else
    {
        log_text("process ended unexpectedly.");
    }
    ENDLOG;
}
#endif

void pcache_sync(void);

RETSIGTYPE DCL_CDECL sighandler(int sig)
{
#ifndef WIN32
    int stat_buf;
    pid_t child;
#endif // !WIN32

    switch (sig)
    {
#ifndef WIN32
    case SIGUSR1:
        if (mudstate.bCanRestart)
        {
            log_signal(sig);
            do_restart(GOD, GOD, GOD, 0);
        }
        else
        {
            log_signal_ignore(sig);
        }
        break;

    case SIGUSR2:

        // Drop a flatfile.
        //
        log_signal(sig);
        raw_broadcast(0, "Caught signal %s requesting a flatfile @dump. Please wait.", SignalDesc(sig));
        dump_database_internal(DUMP_I_SIGNAL);
        break;

    case SIGCHLD:

        // Change in child status.
        //
#ifndef SIGNAL_SIGCHLD_BRAINDAMAGE
        signal(SIGCHLD, CAST_SIGNAL_FUNC sighandler);
#endif // !SIGNAL_SIGCHLD_BRAINDAMAGE

        while ((child = waitpid(0, &stat_buf, WNOHANG)) > 0)
        {
            if (  WIFEXITED(stat_buf)
               || WIFSIGNALED(stat_buf))
            {
                if (child == slave_pid)
                {
                    // The reverse-DNS slave process ended unexpectedly.
                    //
                    CleanUpSlaveSocket();
                    slave_pid = 0;

                    LogStatBuf(stat_buf, "SLAVE");

                    continue;
                }
#ifdef QUERY_SLAVE
                else if (child == sqlslave_pid)
                {
                    // The SQL slave process ended unexpectedly.
                    //
                    CleanUpSQLSlaveSocket();
                    sqlslave_pid = 0;

                    LogStatBuf(stat_buf, "QUERY");

                    continue;
                }
#endif // QUERY_SLAVE
                else if (  mudconf.fork_dump
                        && mudstate.dumping)
                {
                    mudstate.dumped = child;
                    if (mudstate.dumper == mudstate.dumped)
                    {
                        // The dumping process finished.
                        //
                        mudstate.dumper  = 0;
                        mudstate.dumped  = 0;
                    }
                    else
                    {
                        // The dumping process finished before we could
                        // obtain its process id from fork().
                        //
                    }
                    mudstate.dumping = false;
                    local_dump_complete_signal();

                    continue;
                }
            }

            log_signal(sig);
            LogStatBuf(stat_buf, "UKNWN");

            STARTLOG(LOG_PROBLEMS, "SIG", "DEBUG");
#ifdef QUERY_SLAVE
            Log.tinyprintf("mudstate.dumper=%d, child=%d, slave_pid=%d, sqlslave_pid=%d" ENDLINE,
                mudstate.dumper, child, slave_pid, sqlslave_pid);
#else
            Log.tinyprintf("mudstate.dumper=%d, child=%d, slave_pid=%d" ENDLINE,
                mudstate.dumper, child, slave_pid);
#endif // QUERY_SLAVE
            ENDLOG;
        }
        break;

    case SIGHUP:

        // Perform a database dump.
        //
        log_signal(sig);
        extern void dispatch_DatabaseDump(void *pUnused, int iUnused);
        scheduler.CancelTask(dispatch_DatabaseDump, 0, 0);
        mudstate.dump_counter.GetUTC();
        scheduler.DeferTask(mudstate.dump_counter, PRIORITY_SYSTEM, dispatch_DatabaseDump, 0, 0);
        break;

#ifdef HAVE_SETITIMER
    case SIGPROF:

        // Softcode is running longer than is reasonable.  Apply the brakes.
        //
        log_signal(sig);
        MuxAlarm.Signal();
        break;
#endif

#endif // !WIN32

    case SIGINT:

        // Log + ignore
        //
        log_signal(sig);
        break;

#ifndef WIN32
    case SIGQUIT:
#endif // !WIN32
    case SIGTERM:
#ifdef SIGXCPU
    case SIGXCPU:
#endif // SIGXCPU
        // Time for a normal and short-winded shutdown.
        //
        check_panicking(sig);
        log_signal(sig);
        raw_broadcast(0, "GAME: Caught signal %s, exiting.", SignalDesc(sig));
        mudstate.shutdown_flag = true;
        break;

    case SIGILL:
    case SIGFPE:
    case SIGSEGV:
#ifndef WIN32
    case SIGTRAP:
#ifdef SIGXFSZ
    case SIGXFSZ:
#endif // SIGXFSZ
#ifdef SIGEMT
    case SIGEMT:
#endif // SIGEMT
#ifdef SIGBUS
    case SIGBUS:
#endif // SIGBUS
#ifdef SIGSYS
    case SIGSYS:
#endif // SIGSYS
#endif // !WIN32

        // Panic save + restart.
        //
        Log.Flush();
        check_panicking(sig);
        log_signal(sig);
        report();

        local_presync_database_sigsegv();
#ifndef MEMORY_BASED
        al_store();
#endif
        pcache_sync();
        SYNC;

        if (  mudconf.sig_action != SA_EXIT
           && mudstate.bCanRestart)
        {
            raw_broadcast
            (  0,
               "GAME: Fatal signal %s caught, restarting.",
               SignalDesc(sig)
            );

            // There is no older DB. It's a fiction. Our only choice is
            // between unamed attributes and named ones. We go with what we
            // got.
            //
            dump_database_internal(DUMP_I_RESTART);
            SYNC;
            CLOSE;
#ifdef WIN32
            unset_signals();
            signal(sig, SIG_DFL);
            WSACleanup();
            exit(12345678);
#else // WIN32
            CleanUpSlaveSocket();
            CleanUpSlaveProcess();

            // Try our best to dump a core first
            //
            if (!fork())
            {
                // We are the broken parent. Die.
                //
                unset_signals();
                exit(1);
            }

            // We are the reproduced child with a slightly better chance.
            //
            dump_restart_db();
#ifdef GAME_DOOFERMUX
            execl("bin/netmux", mudconf.mud_name, "-c", mudconf.config_file, "-p", mudconf.pid_file, NULL);
#else // GAME_DOOFERMUX
            execl("bin/netmux", "netmux", "-c", mudconf.config_file, "-p", mudconf.pid_file, NULL);
#endif // GAME_DOOFERMUX
            break;
#endif // WIN32
        }
        else
        {
#ifdef WIN32
            WSACleanup();
#endif // WIN32

            unset_signals();
            signal(sig, SIG_DFL);
            exit(1);
        }
        break;

    case SIGABRT:

        // Coredump.
        //
        check_panicking(sig);
        log_signal(sig);
        report();

#ifdef WIN32
        WSACleanup();
#endif // WIN32

        unset_signals();
        signal(sig, SIG_DFL);
        exit(1);
    }
    signal(sig, CAST_SIGNAL_FUNC sighandler);
    mudstate.panicking = 0;
}

NAMETAB sigactions_nametab[] =
{
    {"exit",        3,  0,  SA_EXIT},
    {"default",     1,  0,  SA_DFLT},
    { NULL,         0,  0,  0}
};

void set_signals(void)
{
#ifndef WIN32
    sigset_t sigs;

    // We have to reset our signal mask, because of the possibility
    // that we triggered a restart on a SIGUSR1. If we did so, then
    // the signal became blocked, and stays blocked, since control
    // never returns to the caller; i.e., further attempts to send
    // a SIGUSR1 would fail.
    //
#undef sigfillset
#undef sigprocmask
    sigfillset(&sigs);
    sigprocmask(SIG_UNBLOCK, &sigs, NULL);
#endif // !WIN32

    signal(SIGINT,  CAST_SIGNAL_FUNC sighandler);
    signal(SIGTERM, CAST_SIGNAL_FUNC sighandler);
    signal(SIGILL,  CAST_SIGNAL_FUNC sighandler);
    signal(SIGSEGV, CAST_SIGNAL_FUNC sighandler);
    signal(SIGABRT, CAST_SIGNAL_FUNC sighandler);
    signal(SIGFPE,  SIG_IGN);

#ifndef WIN32
    signal(SIGCHLD, CAST_SIGNAL_FUNC sighandler);
    signal(SIGHUP,  CAST_SIGNAL_FUNC sighandler);
    signal(SIGQUIT, CAST_SIGNAL_FUNC sighandler);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGUSR1, CAST_SIGNAL_FUNC sighandler);
    signal(SIGUSR2, CAST_SIGNAL_FUNC sighandler);
    signal(SIGTRAP, CAST_SIGNAL_FUNC sighandler);
    signal(SIGILL,  CAST_SIGNAL_FUNC sighandler);
#ifdef HAVE_SETITIMER
    signal(SIGPROF,  CAST_SIGNAL_FUNC sighandler);
#endif

#ifdef SIGXCPU
    signal(SIGXCPU, CAST_SIGNAL_FUNC sighandler);
#endif // SIGXCPU
#ifdef SIGFSZ
    signal(SIGXFSZ, CAST_SIGNAL_FUNC sighandler);
#endif // SIGFSZ
#ifdef SIGEMT
    signal(SIGEMT, CAST_SIGNAL_FUNC sighandler);
#endif // SIGEMT
#ifdef SIGBUS
    signal(SIGBUS, CAST_SIGNAL_FUNC sighandler);
#endif // SIGBUS
#ifdef SIGSYS
    signal(SIGSYS, CAST_SIGNAL_FUNC sighandler);
#endif // SIGSYS
#endif // !WIN32
}

void list_system_resources(dbref player)
{
    char buffer[80];

    int nTotal = 0;
    notify(player, "System Resources");

    sprintf(buffer, "Total Open Files: %ld", DebugTotalFiles);
    notify(player, buffer);
    nTotal += DebugTotalFiles;

    sprintf(buffer, "Total Sockets: %ld", DebugTotalSockets);
    notify(player, buffer);
    nTotal += DebugTotalSockets;

#ifdef WIN32
    sprintf(buffer, "Total Threads: %ld", DebugTotalThreads);
    notify(player, buffer);
    nTotal += DebugTotalThreads;

    sprintf(buffer, "Total Semaphores: %ld", DebugTotalSemaphores);
    notify(player, buffer);
    nTotal += DebugTotalSemaphores;
#endif // WIN32

    sprintf(buffer, "Total Handles (sum of above): %d", nTotal);
    notify(player, buffer);

#ifdef WIN32
    for (int i = 0; i < NUM_SLAVE_THREADS; i++)
    {
        sprintf(buffer, "Thread %d at line %d", i+1, SlaveThreadInfo[i].iDoing);
        notify(player, buffer);
    }
#endif // WIN32
}

#ifdef WIN32

// ---------------------------------------------------------------------------
// Thread to listen on MUD port - for Windows NT
// ---------------------------------------------------------------------------
//
void __cdecl MUDListenThread(void * pVoid)
{
    PortInfo *Port = (PortInfo *)pVoid;

    SOCKADDR_IN SockAddr;
    int         nLen;
    BOOL        b;

    struct descriptor_data * d;

    Log.tinyprintf("Starting NT-style listening on port %d" ENDLINE, Port->port);

    //
    // Loop forever accepting connections
    //
    for (;;)
    {
        //
        // Block on accept()
        //
        nLen = sizeof(SOCKADDR_IN);
        SOCKET socketClient = accept(Port->socket, (LPSOCKADDR) &SockAddr,
            &nLen);

        if (socketClient == INVALID_SOCKET)
        {
            // parent thread closes the listening socket
            // when it wants this thread to stop.
            //
            break;
        }

        DebugTotalSockets++;
        if (site_check(SockAddr.sin_addr, mudstate.access_list) == H_FORBIDDEN)
        {
            STARTLOG(LOG_NET | LOG_SECURITY, "NET", "SITE");
            Log.tinyprintf("[%d/%s] Connection refused.  (Remote port %d)",
                socketClient, inet_ntoa(SockAddr.sin_addr), ntohs(SockAddr.sin_port));
            ENDLOG;

            // The following are commented out for thread-safety, but
            // ordinarily, they would occur at this time.
            //
            //SiteMonSend(socketClient, inet_ntoa(SockAddr.sin_addr), NULL,
            //            "Connection refused");
            //fcache_rawdump(socketClient, FC_CONN_SITE);

            shutdown(socketClient, SD_BOTH);
            if (closesocket(socketClient) == 0)
            {
                DebugTotalSockets--;
            }
            continue;
        }

        // Make slave request
        //
        // Go take control of the stack, but don't bother if it takes
        // longer than 5 seconds to do it.
        //
        if (bSlaveBooted && (WAIT_OBJECT_0 == WaitForSingleObject(hSlaveRequestStackSemaphore, 5000)))
        {
            // We have control of the stack. Skip the request if the stack is full.
            //
            if (iSlaveRequest < SLAVE_REQUEST_STACK_SIZE)
            {
                // There is room on the stack, so make the request.
                //
                SlaveRequests[iSlaveRequest].sa_in = SockAddr;
                SlaveRequests[iSlaveRequest].port_in = mudconf.ports.pi[0];
                iSlaveRequest++;
                ReleaseSemaphore(hSlaveRequestStackSemaphore, 1, NULL);

                // Wake up a single slave thread. Event automatically resets itself.
                //
                ReleaseSemaphore(hSlaveThreadsSemaphore, 1, NULL);
            }
            else
            {
                // No room on the stack, so skip it.
                //
                ReleaseSemaphore(hSlaveRequestStackSemaphore, 1, NULL);
            }
        }
        d = initializesock(socketClient, &SockAddr);

        // Add this socket to the IO completion port.
        //
        CompletionPort = CreateIoCompletionPort((HANDLE)socketClient, CompletionPort, (DWORD) d, 1);

        if (!CompletionPort)
        {
            Log.tinyprintf("Error %ld on CreateIoCompletionPort for socket %ld" ENDLINE, GetLastError(), socketClient);
            shutdownsock_brief(d);
            continue;
        }

        TelnetSetup(d);

        if (!PostQueuedCompletionStatus(CompletionPort, 0, (DWORD) d, &lpo_welcome))
        {
            Log.tinyprintf("Error %ld on PostQueuedCompletionStatus in ProcessWindowsTCP (read)" ENDLINE, GetLastError());
            shutdownsock_brief(d);
            continue;
        }

        // Do the first read
        //
        b = ReadFile((HANDLE) socketClient, d->input_buffer, sizeof(d->input_buffer), NULL, &d->InboundOverlapped);

        if (!b && GetLastError() != ERROR_IO_PENDING)
        {
            // Post a notification that the descriptor should be shutdown, and do no more IO.
            //
            d->bConnectionDropped = true;
            Log.tinyprintf("ProcessWindowsTCP(%d) cannot queue read request with error %ld. Requesting port shutdown." ENDLINE, d->descriptor, GetLastError());
            if (!PostQueuedCompletionStatus(CompletionPort, 0, (DWORD) d, &lpo_shutdown))
            {
                Log.tinyprintf("Error %ld on PostQueuedCompletionStatus in ProcessWindowsTCP (initial read)" ENDLINE, GetLastError());
            }
        }
    }
    Log.tinyprintf("End of NT-style listening on port %d" ENDLINE, Port->port);
}


void Task_FreeDescriptor(void *arg_voidptr, int arg_Integer)
{
    UNUSED_PARAMETER(arg_Integer);

    DESC *d = (DESC *)arg_voidptr;
    if (d)
    {
        EnterCriticalSection(&csDescriptorList);
        ndescriptors--;
        freeqs(d);
        free_desc(d);
        LeaveCriticalSection(&csDescriptorList);
    }
}

void Task_DeferredClose(void *arg_voidptr, int arg_Integer)
{
    UNUSED_PARAMETER(arg_Integer);

    DESC *d = (DESC *)arg_voidptr;
    if (d)
    {
        d->bConnectionDropped = true;

        // Cancel any pending reads or writes on this socket
        //
        if (!fpCancelIo((HANDLE) d->descriptor))
        {
            Log.tinyprintf("Error %ld on CancelIo" ENDLINE, GetLastError());
        }

        shutdown(d->descriptor, SD_BOTH);
        if (SOCKET_CLOSE(d->descriptor) == 0)
        {
            DebugTotalSockets--;
        }
        d->descriptor = INVALID_SOCKET;

        // Post a notification that it is safe to free the descriptor
        // we can't free the descriptor here (below) as there may be some
        // queued completed IOs that will crash when they refer to a descriptor
        // (d) that has been freed.
        //
        if (!PostQueuedCompletionStatus(CompletionPort, 0, (DWORD) d, &lpo_aborted))
        {
            Log.tinyprintf("Error %ld on PostQueuedCompletionStatus in shutdownsock" ENDLINE, GetLastError());
        }
    }
}


/*
This is called from within shovechars when it needs to see if any IOs have
completed for the Windows NT version.

The 4 sorts of IO completions are:

1. Outstanding read completing (there should always be an outstanding read)
2. Outstanding write completing
3. A special "shutdown" message to tell us to shutdown the socket
4. A special "aborted" message to tell us the socket has shut down, and we
can now free the descriptor.

The latter 2 are posted by the application by PostQueuedCompletionStatus
when it is necessary to signal these "events".

The reason for posting the special messages is to shut down sockets in an
orderly way.

*/

void ProcessWindowsTCP(DWORD dwTimeout)
{
    LPOVERLAPPED lpo;
    DWORD nbytes;
    DESC *d;

    for ( ; ; dwTimeout = 0)
    {
        // pull out the next completed IO
        //
        BOOL b = GetQueuedCompletionStatus(CompletionPort, &nbytes, (LPDWORD) &d, &lpo, dwTimeout);

        if (!b)
        {
            DWORD dwLastError = GetLastError();

            // Ignore timeouts and cancelled IOs
            //
            switch (dwLastError)
            {
            case WAIT_TIMEOUT:
                //Log.WriteString("Timeout." ENDLINE);
                return;

            case ERROR_OPERATION_ABORTED:
                //Log.WriteString("Operation Aborted." ENDLINE);
                continue;

            default:
                if (!(d->bConnectionDropped))
                {
                    // bad IO - shut down this client
                    //
                    d->bConnectionDropped = true;

                    // Post a notification that the descriptor should be shutdown
                    //
                    Log.tinyprintf("ProcessWindowsTCP(%d) failed IO with error %ld. Requesting port shutdown." ENDLINE, d->descriptor, dwLastError);
                    if (!PostQueuedCompletionStatus(CompletionPort, 0, (DWORD) d, &lpo_shutdown))
                    {
                        Log.tinyprintf("Error %ld on PostQueuedCompletionStatus in ProcessWindowsTCP (write)" ENDLINE, GetLastError());
                    }
                }
            }
        }
        else if (lpo == &d->OutboundOverlapped && !d->bConnectionDropped)
        {
            //Log.tinyprintf("Write(%d bytes)." ENDLINE, nbytes);

            // Write completed
            //
            TBLOCK *tp;
            DWORD nBytes;

            bool bNothingToWrite;
            do
            {
                bNothingToWrite = true;
                tp = d->output_head;
                if (tp == NULL)
                {
                    d->bWritePending = false;
                    break;
                }
                bNothingToWrite = true;

                // Move data from one buffer to another.
                //
                if (tp->hdr.nchars <= SIZEOF_OVERLAPPED_BUFFERS)
                {
                    // We can consume this buffer.
                    //
                    nBytes = tp->hdr.nchars;
                    memcpy(d->output_buffer, tp->hdr.start, nBytes);
                    TBLOCK *save = tp;
                    tp = tp->hdr.nxt;
                    MEMFREE(save);
                    save = NULL;
                    d->output_head = tp;
                    if (tp == NULL)
                    {
                        //Log.tinyprintf("Write...%d bytes taken from a queue of %d bytes...Empty Queue, now." ENDLINE, nBytes, d->output_size);
                        d->output_tail = NULL;
                    }
                    else
                    {
                        //Log.tinyprintf("Write...%d bytes taken from a queue of %d bytes...more buffers in Queue" ENDLINE, nBytes, d->output_size);
                    }
                }
                else
                {
                    // Use the entire bufer and leave the remaining data in the queue.
                    //
                    nBytes = SIZEOF_OVERLAPPED_BUFFERS;
                    memcpy(d->output_buffer, tp->hdr.start, nBytes);
                    tp->hdr.nchars -= nBytes;
                    tp->hdr.start += nBytes;
                    //Log.tinyprintf("Write...%d bytes taken from a queue of %d bytes...buffer still has bytes" ENDLINE, nBytes, d->output_size);
                }
                d->output_size -= nBytes;

                d->OutboundOverlapped.Offset = 0;
                d->OutboundOverlapped.OffsetHigh = 0;

                // We do allow more than one complete write request in the IO
                // completion port queue. The reason is that if WriteFile
                // returns true, we -can- re-used the output_buffer -and-
                // redundant queue entries just cause us to try to write more
                // often. There is no possibility of corruption.
                //
                // It then becomes a trade off between the costs. I find that
                // keeping the TCP/IP full of data is more important.
                //
                DWORD nWritten;
                b = WriteFile((HANDLE) d->descriptor, d->output_buffer,
                    nBytes, &nWritten, &d->OutboundOverlapped);

            } while (b);

            if (bNothingToWrite)
            {
                if (d->bConnectionShutdown)
                {
                    scheduler.CancelTask(Task_DeferredClose, d, 0);
                    scheduler.DeferImmediateTask(PRIORITY_SYSTEM, Task_DeferredClose, d, 0);
                }
                continue;
            }

            d->bWritePending = true;
            DWORD dwLastError = GetLastError();
            if (dwLastError != ERROR_IO_PENDING)
            {
                // Post a notification that the descriptor should be shutdown
                //
                d->bWritePending = false;
                d->bConnectionDropped = true;
                Log.tinyprintf("ProcessWindowsTCP(%d) cannot queue write request with error %ld. Requesting port shutdown." ENDLINE, d->descriptor, dwLastError);
                if (!PostQueuedCompletionStatus(CompletionPort, 0, (DWORD) d, &lpo_shutdown))
                {
                    Log.tinyprintf("Error %ld on PostQueuedCompletionStatus in ProcessWindowsTCP (write)" ENDLINE, GetLastError());
                }
            }
        }
        else if (lpo == &d->InboundOverlapped && !d->bConnectionDropped)
        {
            //Log.tinyprintf("Read(%d bytes)." ENDLINE, nbytes);
            // The read operation completed
            //
            if (nbytes == 0)
            {
                // A zero-length IO completion means that the connection was dropped by the client.
                //

                // Post a notification that the descriptor should be shutdown
                //
                d->bConnectionDropped = true;
                Log.tinyprintf("ProcessWindowsTCP(%d) zero-length read. Requesting port shutdown." ENDLINE, d->descriptor);
                if (!PostQueuedCompletionStatus(CompletionPort, 0, (DWORD) d, &lpo_shutdown))
                {
                    Log.tinyprintf("Error %ld on PostQueuedCompletionStatus in ProcessWindowsTCP (read)" ENDLINE, GetLastError());
                }
                continue;
            }

            d->last_time.GetUTC();

            // Undo autodark
            //
            if (d->flags & DS_AUTODARK)
            {
                // Clear the DS_AUTODARK on every related session.
                //
                DESC *d1;
                DESC_ITER_PLAYER(d->player, d1)
                {
                    d1->flags &= ~DS_AUTODARK;
                }
                db[d->player].fs.word[FLAG_WORD1] &= ~DARK;
            }

            // process the player's input
            //
            process_input_helper(d, d->input_buffer, nbytes);

            // now fire off another read
            //
            b = ReadFile((HANDLE) d->descriptor, d->input_buffer, sizeof(d->input_buffer), &nbytes, &d->InboundOverlapped);

            // if ReadFile returns true, then the read completed successfully already, but it was also added to the IO
            // completion port queue, so in order to avoid having two requests in the queue for the same buffer
            // (corruption problems), we act as if the IO is still pending.
            //

            if (!b)
            {
                // ERROR_IO_PENDING is a normal way of saying, 'not done yet'. All other errors are serious errors.
                //
                DWORD dwLastError = GetLastError();
                if (dwLastError != ERROR_IO_PENDING)
                {
                    // Post a notification that the descriptor should be shutdown, and do no more IO.
                    //
                    d->bConnectionDropped = true;
                    Log.tinyprintf("ProcessWindowsTCP(%d) cannot queue read request with error %ld. Requesting port shutdown." ENDLINE, d->descriptor, dwLastError);
                    if (!PostQueuedCompletionStatus(CompletionPort, 0, (DWORD) d, &lpo_shutdown))
                    {
                        Log.tinyprintf("Error %ld on PostQueuedCompletionStatus in ProcessWindowsTCP (read)" ENDLINE, GetLastError());
                    }
                }
            }
        }
        else if (lpo == &lpo_welcome)
        {
            char *buff = alloc_mbuf("ProcessWindowsTCP.Premature");
            strcpy(buff, inet_ntoa(d->address.sin_addr));

            // If the socket is invalid, the we were unable to queue a read
            // request, and the port was shutdown while this packet was in
            // the completion port queue.
            //
            bool bInvalidSocket = IS_INVALID_SOCKET(d->descriptor);

            // Log connection.
            //
            STARTLOG(LOG_NET | LOG_LOGIN, "NET", "CONN");
            Log.tinyprintf("[%s/%s] Connection opened (remote port %d)",
                bInvalidSocket ? "UNKNOWN" : mux_ltoa_t(d->descriptor), buff,
                ntohs(d->address.sin_port));
            ENDLOG;

            SiteMonSend(d->descriptor, buff, d, "Connection");

            if (bInvalidSocket)
            {
                // Log premature disconnection.
                //
                STARTLOG(LOG_NET | LOG_LOGIN, "NET", "DISC");
                Log.tinyprintf("[UNKNOWN/%s] Connection closed prematurely (remote port %d)",
                    buff, ntohs(d->address.sin_port));
                ENDLOG;

                SiteMonSend(d->descriptor, buff, d, "Connection closed prematurely");
            }
            else
            {
                // Welcome the user.
                //
                welcome_user(d);
            }
            free_mbuf(buff);
        }
        else if (lpo == &lpo_shutdown)
        {
            //Log.WriteString("Shutdown." ENDLINE);
            // Shut this descriptor down.
            //
            shutdownsock(d, R_SOCKDIED);   // shut him down
        }
        else if (lpo == &lpo_aborted)
        {
            // Instead of freeing the descriptor immediately, we are going to put it back at the
            // end of the queue. CancelIo will still generate aborted packets. We don't want the descriptor
            // be be re-used and have a new connection be stepped on by a dead one.
            //
            if (!PostQueuedCompletionStatus(CompletionPort, 0, (DWORD) d, &lpo_aborted_final))
            {
                Log.tinyprintf("Error %ld on PostQueuedCompletionStatus in ProcessWindowsTCP (aborted)" ENDLINE, GetLastError());
            }
        }
        else if (lpo == &lpo_aborted_final)
        {
            // Now that we are fairly certain that all IO packets refering to this descriptor have been processed
            // and no further packets remain in the IO queue, schedule a task to free the descriptor. This allows
            // any tasks which might potentially refer to this descriptor to be handled before we free the
            // descriptor.
            //
            scheduler.DeferImmediateTask(PRIORITY_SYSTEM, Task_FreeDescriptor, d, 0);
        }
        else if (lpo == &lpo_wakeup)
        {
            // Just waking up is good enough.
            //
        }
    }
}

#endif // WIN32

void SiteMonSend(int port, const char *address, DESC *d, const char *msg)
{
    // Don't do sitemon for blocked sites.
    //
    if (  d != NULL
       && (d->host_info & H_NOSITEMON))
    {
        return;
    }

    // Build the msg.
    //
    char *sendMsg;
    bool bSuspect = (d != NULL) && (d->host_info & H_SUSPECT);
    if (IS_INVALID_SOCKET(port))
    {
        sendMsg = tprintf("SITEMON: [UNKNOWN] %s from %s.%s", msg, address,
            bSuspect ? " (SUSPECT)": "");
    }
    else
    {
        sendMsg = tprintf("SITEMON: [%d] %s from %s.%s", port, msg,
            address, bSuspect ? " (SUSPECT)": "");
    }

    DESC *nd;
    DESC_ITER_CONN(nd)
    {
        if (SiteMon(nd->player))
        {
            queue_string(nd, sendMsg);
            queue_write_LEN(nd, "\r\n", 2);
            process_output(nd, false);
        }
    }
}
