/*! \file signals.cpp
 * \brief Signal handling routines.
 *
 * Signal name tables, signal handler registration, and the main signal
 * dispatcher for both Unix and Windows platforms.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "modules.h"
#include "driverstate.h"
#include "driver_log.h"
#include "driver_bridge.h"

#ifdef SOLARIS
extern const int _sys_nsig;
#define NSIG _sys_nsig
#endif // SOLARIS

// ---------------------------------------------------------------------------
// Signal handling routines.
//
#ifdef _SGI_SOURCE
#define CAST_SIGNAL_FUNC (SIG_PF)
#else // _SGI_SOURCE
#define CAST_SIGNAL_FUNC
#endif // _SGI_SOURCE

// The purpose of the following code is support the case where sys_siglist is
// is not part of the environment.  This is the case for some Unix platforms
// and also for Windows.
//
typedef struct
{
    int         iSignal;
    const UTF8 *szSignal;
} SIGNALTYPE, *PSIGNALTYPE;

const SIGNALTYPE aSigTypes[] =
{
#ifdef SIGHUP
    // Hangup detected on controlling terminal or death of controlling process.
    //
    { SIGHUP,   T("SIGHUP")},
#endif // SIGHUP
#ifdef SIGINT
    // Interrupt from keyboard.
    //
    { SIGINT,   T("SIGINT")},
#endif // SIGINT
#ifdef SIGQUIT
    // Quit from keyboard.
    //
    { SIGQUIT,  T("SIGQUIT")},
#endif // SIGQUIT
#ifdef SIGILL
    // Illegal Instruction.
    //
    { SIGILL,   T("SIGILL")},
#endif // SIGILL
#ifdef SIGTRAP
    // Trace/breakpoint trap.
    //
    { SIGTRAP,  T("SIGTRAP")},
#endif // SIGTRAP
#if defined(SIGABRT)
    // Abort signal from abort(3).
    //
    { SIGABRT,  T("SIGABRT")},
#elif defined(SIGIOT)
#define SIGABRT SIGIOT
    // Abort signal from abort(3).
    //
    { SIGIOT,   T("SIGIOT")},
#endif // SIGABRT
#ifdef SIGEMT
    { SIGEMT,   T("SIGEMT")},
#endif // SIGEMT
#ifdef SIGFPE
    // Floating-point exception.
    //
    { SIGFPE,   T("SIGFPE")},
#endif // SIGFPE
#ifdef SIGKILL
    // Kill signal. Not catchable.
    //
    { SIGKILL,  T("SIGKILL")},
#endif // SIGKILL
#ifdef SIGSEGV
    // Invalid memory reference.
    //
    { SIGSEGV,  T("SIGSEGV")},
#endif // SIGSEGV
#ifdef SIGPIPE
    // Broken pipe: write to pipe with no readers.
    //
    { SIGPIPE,  T("SIGPIPE")},
#endif // SIGPIPE
#ifdef SIGALRM
    // Timer signal from alarm(2).
    //
    { SIGALRM,  T("SIGALRM")},
#endif // SIGALRM
#ifdef SIGTERM
    // Termination signal.
    //
    { SIGTERM,  T("SIGTERM")},
#endif // SIGTERM
#ifdef SIGBREAK
    // Ctrl-Break.
    //
    { SIGBREAK, T("SIGBREAK")},
#endif // SIGBREAK
#ifdef SIGUSR1
    // User-defined signal 1.
    //
    { SIGUSR1,  T("SIGUSR1")},
#endif // SIGUSR1
#ifdef SIGUSR2
    // User-defined signal 2.
    //
    { SIGUSR2,  T("SIGUSR2")},
#endif // SIGUSR2
#if defined(SIGCHLD)
    // Child stopped or terminated.
    //
    { SIGCHLD,  T("SIGCHLD")},
#elif defined(SIGCLD)
#define SIGCHLD SIGCLD
    // Child stopped or terminated.
    //
    { SIGCLD,   T("SIGCLD")},
#endif // SIGCHLD
#ifdef SIGCONT
    // Continue if stopped.
    //
    { SIGCONT,  T("SIGCONT")},
#endif // SIGCONT
#ifdef SIGSTOP
    // Stop process. Not catchable.
    //
    { SIGSTOP,  T("SIGSTOP")},
#endif // SIGSTOP
#ifdef SIGTSTP
    // Stop typed at tty
    //
    { SIGTSTP,  T("SIGTSTP")},
#endif // SIGTSTP
#ifdef SIGTTIN
    // tty input for background process.
    //
    { SIGTTIN,  T("SIGTTIN")},
#endif // SIGTTIN
#ifdef SIGTTOU
    // tty output for background process.
    //
    { SIGTTOU,  T("SIGTTOU")},
#endif // SIGTTOU
#ifdef SIGBUS
    // Bus error (bad memory access).
    //
    { SIGBUS,   T("SIGBUS")},
#endif // SIGBUS
#ifdef SIGPROF
    // Profiling timer expired.
    //
    { SIGPROF,  T("SIGPROF")},
#endif // SIGPROF
#ifdef SIGSYS
    // Bad argument to routine (SVID).
    //
    { SIGSYS,   T("SIGSYS")},
#endif // SIGSYS
#ifdef SIGURG
    // Urgent condition on socket (4.2 BSD).
    //
    { SIGURG,   T("SIGURG")},
#endif // SIGURG
#ifdef SIGVTALRM
    // Virtual alarm clock (4.2 BSD).
    //
    { SIGVTALRM, T("SIGVTALRM")},
#endif // SIGVTALRM
#ifdef SIGXCPU
    // CPU time limit exceeded (4.2 BSD).
    //
    { SIGXCPU,  T("SIGXCPU")},
#endif // SIGXCPU
#ifdef SIGXFSZ
    // File size limit exceeded (4.2 BSD).
    //
    { SIGXFSZ,  T("SIGXFSZ")},
#endif // SIGXFSZ
#ifdef SIGSTKFLT
    // Stack fault on coprocessor.
    //
    { SIGSTKFLT, T("SIGSTKFLT")},
#endif // SIGSTKFLT
#if defined(SIGIO)
    // I/O now possible (4.2 BSD). File lock lost.
    //
    { SIGIO,    T("SIGIO")},
#elif defined(SIGPOLL)
#define SIGIO SIGPOLL
    // Pollable event (Sys V).
    //
    { SIGPOLL,  T("SIGPOLL")},
#endif // SIGIO
#ifdef SIGLOST
    { SIGLOST,  T("SIGLOST")},
#endif // SIGLOST
#if defined(SIGPWR)
    // Power failure (System V).
    //
    { SIGPWR,   T("SIGPWR")},
#elif defined(SIGINFO)
#define SIGPWR SIGINFO
    // Power failure (System V).
    //
    { SIGINFO,  T("SIGINFO")},
#endif // SIGPWR
#ifdef SIGWINCH
    // Window resize signal (4.3 BSD, Sun).
    //
    { SIGWINCH, T("SIGWINCH")},
#endif // SIGWINCH
    { 0,        T("SIGZERO") },
    { -1, nullptr }
};

typedef struct
{
    const UTF8 *pShortName;
    const UTF8 *pLongName;
} MUX_SIGNAMES;

static MUX_SIGNAMES signames[NSIG];

#if defined(HAVE_SYS_SIGNAME)
#define SysSigNames sys_signame
#elif defined(SYS_SIGLIST_DECLARED)
#define SysSigNames sys_siglist
#endif // HAVE_SYS_SIGNAME

void build_signal_names_table(void)
{
    int i;
    for (i = 0; i < NSIG; i++)
    {
        signames[i].pShortName = nullptr;
        signames[i].pLongName  = nullptr;
    }

    const SIGNALTYPE *pst = aSigTypes;
    while (pst->szSignal)
    {
        const auto sig = pst->iSignal;
        if (  0 <= sig
           && sig < NSIG)
        {
            MUX_SIGNAMES *tsn = &signames[sig];
            if (tsn->pShortName == nullptr)
            {
                tsn->pShortName = pst->szSignal;
#if defined(UNIX_SIGNALS)
                if (sig == SIGUSR1)
                {
                    tsn->pLongName = T("Restart server");
                }
                else if (sig == SIGUSR2)
                {
                    tsn->pLongName = T("Drop flatfile");
                }
#endif // UNIX_SIGNALS
#ifdef SysSigNames
                if (  tsn->pLongName == nullptr
                   && SysSigNames[sig]
                   && strcmp(reinterpret_cast<char *>(tsn->pShortName), reinterpret_cast<char *>(SysSigNames[sig])) != 0)
                {
                    tsn->pLongName = reinterpret_cast<UTF8 *>(SysSigNames[sig]);
                }
#endif // SysSigNames
            }
        }
        pst++;
    }
    for (i = 0; i < NSIG; i++)
    {
        MUX_SIGNAMES *tsn = &signames[i];
        if (tsn->pShortName == nullptr)
        {
#ifdef SysSigNames
            if (SysSigNames[i])
            {
                tsn->pLongName = reinterpret_cast<UTF8 *>(SysSigNames[i]);
            }
#endif // SysSigNames

            // This is the only non-const memory case.
            //
            tsn->pShortName = StringClone(tprintf(T("SIG%03d"), i));
        }
    }
}

static void unset_signals(void)
{
    const SIGNALTYPE *pst = aSigTypes;
    while (pst->szSignal)
    {
        int sig = pst->iSignal;
        signal(sig, SIG_DFL);
    }
}

static void check_panicking(int sig)
{
    // If we are panicking, turn off signal catching and resignal.
    //
    if (g_panicking)
    {
        unset_signals();

#ifdef WINDOWS_PROCESSES
        UNUSED_PARAMETER(sig);
        abort();
#endif // WINDOWS_PROCESSES

#ifdef UNIX_PROCESSES
        kill(game_pid, sig);
#endif // UNIX_PROCESSES
    }
    g_panicking = 1;
}

UTF8 *signal_desc(const int iSignal)
{
    static UTF8 buff[LBUF_SIZE];
    auto bufc = buff;
    safe_str(signames[iSignal].pShortName, buff, &bufc);
    if (signames[iSignal].pLongName)
    {
        safe_str(T(" ("), buff, &bufc);
        safe_str(signames[iSignal].pLongName, buff, &bufc);
        safe_chr(')', buff, &bufc);
    }
    *bufc = '\0';
    return buff;
}

void log_signal(const int iSignal)
{
    STARTLOG(LOG_PROBLEMS, T("SIG"), T("CATCH"));
    g_pILog->log_text(T("Caught signal "));
    g_pILog->log_text(signal_desc(iSignal));
    ENDLOG;
}

#if defined(UNIX_SIGNALS)

static void log_signal_ignore(int iSignal)
{
    STARTLOG(LOG_PROBLEMS, "SIG", "CATCH");
    g_pILog->log_text(T("Caught signal and ignored signal "));
    g_pILog->log_text(signal_desc(iSignal));
    g_pILog->log_text(T(" because server just came up."));
    ENDLOG;
}

#endif  // UNIX_SIGNALS

static void DCL_CDECL sighandler(int sig)
{
    // All cases below use only async-signal-safe operations: writing
    // to volatile sig_atomic_t variables and calling signal().
    //
    switch (sig)
    {
#if defined(UNIX_SIGNALS)
    case SIGUSR1:

        // Request @restart — deferred to main loop.
        //
        g_restart_flag = 1;
        break;

    case SIGUSR2:

        // With SQLite write-through, flatfile dumps from a signal handler
        // are both unnecessary and dangerous.  Ignore.
        //
        break;

    case SIGCHLD:

        // Change in child status — deferred to main loop for reaping.
        //
#ifndef SIGNAL_SIGCHLD_BRAINDAMAGE
        signal(SIGCHLD, CAST_SIGNAL_FUNC sighandler);
#endif // !SIGNAL_SIGCHLD_BRAINDAMAGE
        g_sigchld_flag = 1;
        break;

    case SIGHUP:

        // Database dump — deferred to main loop.
        //
        g_dump_flag = 1;
        break;

#endif // UNIX_SIGNALS

    case SIGINT:

        // Ignore.
        //
        break;

#if defined(UNIX_SIGNALS)
    case SIGQUIT:
#endif // UNIX_SIGNALS
    case SIGTERM:
#ifdef SIGXCPU
    case SIGXCPU:
#endif // SIGXCPU
        // Normal shutdown — deferred to main loop for broadcast/log.
        //
        g_shutdown_signal = sig;
        g_shutdown_flag = 1;
        break;

    case SIGILL:
    case SIGFPE:
    case SIGSEGV:
#if defined(UNIX_SIGNALS)
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
#endif // UNIX_SIGNALS

        // Crash handler — async-signal-safe only.
        //
        // The SQLite WAL is already durable via write-through, so we do
        // NOT call into the engine, logging, SQLite, or module teardown
        // from signal context.  All functions called here must be on the
        // POSIX async-signal-safe list: check_panicking (writes volatile
        // + calls signal/kill), fork, execl, _exit, signal, write.
        //
        check_panicking(sig);

        if (  g_dc.sig_action != SA_EXIT
           && g_bCanRestart)
        {
            // Attempt restart.  PanicRestart forks (child dumps core),
            // then the parent exec's a fresh netmux.  Both fork() and
            // execl() are async-signal-safe.
            //
            if (g_pIPlatform)
            {
                const UTF8 *argv[] = {
                    T("netmux"),
                    T("-c"), g_dc.config_file,
                    T("-p"), g_dc.pid_file,
                    T("-e"), g_dc.log_dir,
                    nullptr
                };
                g_pIPlatform->PanicRestart(T("bin/netmux"), argv, 7);
            }
            // PanicRestart returned — exec failed.
        }

        // Cannot restart or exec failed.  Reset signal to default and
        // terminate.  _exit avoids atexit handlers and stdio flushing
        // which are not safe in signal context.
        //
        unset_signals();
        signal(sig, SIG_DFL);
        _exit(1);
        break;

    case SIGABRT:

        // Let the default handler produce a core dump.
        //
        unset_signals();
        signal(SIGABRT, SIG_DFL);
        _exit(134); // 128 + SIGABRT(6)
    }
    signal(sig, CAST_SIGNAL_FUNC sighandler);
    g_panicking = 0;
}

NAMETAB sigactions_nametab[] =
{
    {T("exit"),        3,  0,  SA_EXIT},
    {T("default"),     1,  0,  SA_DFLT},
    {static_cast<UTF8 *>(nullptr), 0,  0,  0}
};

void set_signals(void)
{
#if defined(UNIX_SIGNALS)
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
    sigprocmask(SIG_UNBLOCK, &sigs, nullptr);
#endif // UNIX_SIGNALS

    signal(SIGINT,  CAST_SIGNAL_FUNC sighandler);
    signal(SIGTERM, CAST_SIGNAL_FUNC sighandler);
    signal(SIGILL,  CAST_SIGNAL_FUNC sighandler);
    signal(SIGSEGV, CAST_SIGNAL_FUNC sighandler);
    signal(SIGABRT, CAST_SIGNAL_FUNC sighandler);
    signal(SIGFPE,  SIG_IGN);

#if defined(UNIX_SIGNALS)
    signal(SIGCHLD, CAST_SIGNAL_FUNC sighandler);
    signal(SIGHUP,  CAST_SIGNAL_FUNC sighandler);
    signal(SIGQUIT, CAST_SIGNAL_FUNC sighandler);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGUSR1, CAST_SIGNAL_FUNC sighandler);
    signal(SIGUSR2, CAST_SIGNAL_FUNC sighandler);
    signal(SIGTRAP, CAST_SIGNAL_FUNC sighandler);
    signal(SIGILL,  CAST_SIGNAL_FUNC sighandler);
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
#endif // UNIX_SIGNALS
}
