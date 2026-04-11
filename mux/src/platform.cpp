/*! \file platform.cpp
 * \brief CPlatform — platform abstraction for OS-specific driver operations.
 *
 * Signal handling, helper process management, file descriptor limits,
 * panic restart, and process identity.  All platform-specific code
 * (#ifdef UNIX_SIGNALS, HAVE_WORKING_FORK, etc.) lives here.
 *
 * On Windows: most methods return MUX_E_NOTIMPLEMENTED (no fork, no
 * setrlimit).  Signal handling uses SetConsoleCtrlHandler.
 * On Unix: fork/exec, socketpair, setrlimit, waitpid.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "driver_log.h"
#include "interface.h"
#include "driverstate.h"

#include <vector>

DEFINE_FACTORY(CPlatformFactory)

// ---------------------------------------------------------------------------
// CPlatform class
// ---------------------------------------------------------------------------

class CPlatform : public mux_IPlatform
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT RegisterSignalHandler(
        PLATFORM_SIGNAL_HANDLER pfHandler, void *context);
    virtual MUX_RESULT UnregisterSignalHandler(void);
    virtual MUX_RESULT BootHelperProcess(
        const UTF8 *path, int *pReadFd, int *pWriteFd, int *pChildPid);
    virtual MUX_RESULT ReapChild(int *pPid, int *pExitStatus, bool *pSignaled);
    virtual MUX_RESULT MaximizeFileDescriptors(int *pLimit);
    virtual MUX_RESULT PanicRestart(
        const UTF8 *execPath, const UTF8 *const *argv, int argc);
    virtual MUX_RESULT GetProcessId(int *pPid);
    virtual MUX_RESULT GetSignalNametab(NAMETAB **ppTable);

    CPlatform(void);
    virtual ~CPlatform();

private:
    uint32_t m_cRef;
};

CPlatform::CPlatform(void) : m_cRef(1)
{
}

CPlatform::~CPlatform()
{
}

MUX_RESULT CPlatform::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IPlatform *>(this);
    }
    else if (IID_IPlatform == iid)
    {
        *ppv = static_cast<mux_IPlatform *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    AddRef();
    return MUX_S_OK;
}

uint32_t CPlatform::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CPlatform::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

// Static storage for the registered callback.
static PLATFORM_SIGNAL_HANDLER s_pfSignalHandler = nullptr;
static void *s_pSignalContext = nullptr;

MUX_RESULT CPlatform::RegisterSignalHandler(
    PLATFORM_SIGNAL_HANDLER pfHandler, void *context)
{
    s_pfSignalHandler = pfHandler;
    s_pSignalContext = context;

    // Install OS-specific signal/event handlers that call the callback.
    //
#if defined(WINDOWS_SIGNALS)
    // TODO: SetConsoleCtrlHandler that maps events to PlatformSignal.
#endif
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGABRT, SIG_DFL);
#if defined(UNIX_SIGNALS)
    signal(SIGCHLD, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGPIPE, SIG_IGN);
#endif
    return MUX_S_OK;
}

MUX_RESULT CPlatform::UnregisterSignalHandler(void)
{
    s_pfSignalHandler = nullptr;
    s_pSignalContext = nullptr;
    return MUX_S_OK;
}

MUX_RESULT CPlatform::BootHelperProcess(
    const UTF8 *path, int *pReadFd, int *pWriteFd, int *pChildPid)
{
#if defined(HAVE_WORKING_FORK) && defined(STUB_SLAVE)
    if (nullptr == path || '\0' == *path)
    {
        return MUX_E_INVALIDARG;
    }

    if (pReadFd)
    {
        *pReadFd = -1;
    }
    if (pWriteFd)
    {
        *pWriteFd = -1;
    }
    if (pChildPid)
    {
        *pChildPid = 0;
    }

    const char *pFailedFunc = nullptr;
    int sv[2] = {-1, -1};
    pid_t childPid = 0;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
    {
        pFailedFunc = "socketpair() error: ";
        goto failure;
    }

    {
        int flags = fcntl(sv[0], F_GETFL, 0);
        if (flags < 0 || fcntl(sv[0], F_SETFL, flags | O_NONBLOCK) < 0)
        {
            pFailedFunc = "fcntl(O_NONBLOCK) error: ";
            goto failure;
        }
    }

    childPid = fork();
    switch (childPid)
    {
    case -1:
        pFailedFunc = "fork() error: ";
        goto failure;

    case 0:
        alarm_clock.clear();
        mux_close(sv[0]);
        if (sv[1] != 0)
        {
            mux_close(0);
            if (dup2(sv[1], 0) == -1)
            {
                _exit(1);
            }
        }
        if (sv[1] != 1)
        {
            mux_close(1);
            if (dup2(sv[1], 1) == -1)
            {
                _exit(1);
            }
        }
        if (sv[1] > 1)
        {
            mux_close(sv[1]);
        }

        // Close every inherited fd from 3 upward before exec'ing
        // the helper. The old linear `for (i = 3; i < maxfds; i++)`
        // loop was O(rlim) — on systems where ulimit -n has been
        // raised to 1M+, each BootHelperProcess call burned several
        // seconds of wall clock time closing nonexistent fds and
        // thrashed the file-table lock. Prefer close_range(2)
        // (Linux 5.9+, wrapped by glibc ≥ 2.34) or closefrom(3)
        // (BSDs) which do the right thing in a single syscall, and
        // keep the linear fallback for systems without either.
        //
#if defined(__linux__) && defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 34)
        (void)close_range(3, ~0U, 0);
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) \
   || defined(__DragonFly__) || defined(__sun)
        closefrom(3);
#else
        {
            int maxfds = 256;
#ifdef HAVE_GETDTABLESIZE
            maxfds = getdtablesize();
#else
            maxfds = sysconf(_SC_OPEN_MAX);
#endif
            if (maxfds < 3)
            {
                maxfds = 256;
            }
            for (int i = 3; i < maxfds; i++)
            {
                mux_close(i);
            }
        }
#endif

        execlp(reinterpret_cast<const char *>(path),
               "stubslave",
               static_cast<char *>(nullptr));
        _exit(1);
    }

    mux_close(sv[1]);
    if (pReadFd)
    {
        *pReadFd = sv[0];
    }
    if (pWriteFd)
    {
        *pWriteFd = sv[0];
    }
    if (pChildPid)
    {
        *pChildPid = static_cast<int>(childPid);
    }
    return MUX_S_OK;

failure:
    if (sv[0] >= 0)
    {
        mux_close(sv[0]);
    }
    if (sv[1] >= 0)
    {
        mux_close(sv[1]);
    }
    if (childPid > 0)
    {
        waitpid(childPid, nullptr, 0);
    }
    STARTLOG(LOG_ALWAYS, "NET", "STUB");
    g_pILog->log_text(T(pFailedFunc ? pFailedFunc : "BootHelperProcess() error: "));
    g_pILog->log_number(errno);
    ENDLOG;
    return MUX_E_FAIL;
#else
    UNUSED_PARAMETER(path);
    UNUSED_PARAMETER(pReadFd);
    UNUSED_PARAMETER(pWriteFd);
    UNUSED_PARAMETER(pChildPid);
    return MUX_E_NOTIMPLEMENTED;
#endif
}

MUX_RESULT CPlatform::ReapChild(int *pPid, int *pExitStatus, bool *pSignaled)
{
#if defined(HAVE_WORKING_FORK)
    int stat_buf;
    pid_t child = waitpid(0, &stat_buf, WNOHANG);
    if (child <= 0)
    {
        if (pPid) *pPid = 0;
        return MUX_S_FALSE;  // no child exited
    }
    if (pPid) *pPid = static_cast<int>(child);
    if (pExitStatus)
    {
        *pExitStatus = WIFEXITED(stat_buf) ? WEXITSTATUS(stat_buf) : 0;
    }
    if (pSignaled)
    {
        *pSignaled = WIFSIGNALED(stat_buf);
    }
    return MUX_S_OK;
#else
    UNUSED_PARAMETER(pPid);
    UNUSED_PARAMETER(pExitStatus);
    UNUSED_PARAMETER(pSignaled);
    return MUX_E_NOTIMPLEMENTED;
#endif
}

MUX_RESULT CPlatform::MaximizeFileDescriptors(int *pLimit)
{
#if defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE)
    struct rlimit rlp;
    if (getrlimit(RLIMIT_NOFILE, &rlp) != 0)
    {
        return MUX_E_FAIL;
    }

    // Try to raise the soft limit to the hard limit. Ignoring the
    // setrlimit return value would have let the caller believe the
    // ceiling was raised when it wasn't — select()/poll()/epoll
    // sizing would then outrun the real descriptor limit. Re-read
    // the current value after the set (succeeded or not) so we
    // report what the kernel actually enforces, which also handles
    // partial success where the kernel silently clamps rlim_cur.
    //
    rlp.rlim_cur = rlp.rlim_max;
    (void)setrlimit(RLIMIT_NOFILE, &rlp);
    if (getrlimit(RLIMIT_NOFILE, &rlp) != 0)
    {
        return MUX_E_FAIL;
    }

    if (pLimit)
    {
        // Saturate to INT_MAX: on systems with an unlimited or
        // very large nofile ceiling, rlim_cur can exceed the
        // int return type.
        //
        *pLimit = (rlp.rlim_cur > static_cast<rlim_t>(INT_MAX))
                  ? INT_MAX
                  : static_cast<int>(rlp.rlim_cur);
    }
    return MUX_S_OK;
#else
    UNUSED_PARAMETER(pLimit);
    return MUX_E_NOTIMPLEMENTED;
#endif
}

MUX_RESULT CPlatform::PanicRestart(
    const UTF8 *execPath, const UTF8 *const *argv, int argc)
{
#if defined(HAVE_WORKING_FORK)
    // Bound argc defensively — this runs from a crash-signal
    // handler, so we must not read past the caller's array. A
    // handful of slots is plenty for every current call site
    // (signals.cpp passes 7).
    //
    constexpr int kMaxPanicArgv = 16;
    if (!execPath || !argv || argc <= 0 || argc > kMaxPanicArgv)
    {
        return MUX_E_FAIL;
    }

    // Fork a child to dump core, then exec to restart.
    //
    if (!fork())
    {
        // We are the broken parent. Die and leave a core.
        //
        signal(SIGABRT, SIG_DFL);
        abort();
    }

    // We are the reproduced child with a slightly better chance.
    // The caller (signals.cpp) has already done presync and cleanup.
    //
    // Build a NULL-terminated local argv bounded by the declared
    // argc. The previous implementation read argv[0..6] verbatim
    // regardless of argc, silently reading uninitialized or OOB
    // memory from any caller passing fewer than 7 slots — exactly
    // what we cannot afford during crash recovery. Stack allocation
    // and pointer assignment are async-signal-safe, and so is
    // execv (POSIX.1-2008 async-signal-safe list). std::vector is
    // NOT async-signal-safe, so we deliberately avoid it here.
    //
    const char *localArgv[kMaxPanicArgv + 1];
    for (int i = 0; i < argc; i++)
    {
        if (!argv[i])
        {
            return MUX_E_FAIL;
        }
        localArgv[i] = reinterpret_cast<const char *>(argv[i]);
    }
    localArgv[argc] = nullptr;

    execv(reinterpret_cast<const char *>(execPath),
          const_cast<char *const *>(localArgv));

    // execv failed — fall through to caller's exit path.
    return MUX_E_FAIL;

#elif defined(WINDOWS_PROCESSES)
    UNUSED_PARAMETER(execPath);
    UNUSED_PARAMETER(argv);
    UNUSED_PARAMETER(argc);
    abort();
    return MUX_E_FAIL;  // unreachable
#else
    UNUSED_PARAMETER(execPath);
    UNUSED_PARAMETER(argv);
    UNUSED_PARAMETER(argc);
    return MUX_E_NOTIMPLEMENTED;
#endif
}

MUX_RESULT CPlatform::GetProcessId(int *pPid)
{
    if (nullptr == pPid)
    {
        return MUX_E_INVALIDARG;
    }
    *pPid = mux_getpid();
    return MUX_S_OK;
}

MUX_RESULT CPlatform::GetSignalNametab(NAMETAB **ppTable)
{
    if (nullptr == ppTable)
    {
        return MUX_E_INVALIDARG;
    }
    // The existing build_signal_names_table() populates signames_tab.
    // For now, return nullptr until we move that table here.
    *ppTable = nullptr;
    return MUX_E_NOTIMPLEMENTED;
}

// ---------------------------------------------------------------------------
// CPlatformFactory
// ---------------------------------------------------------------------------

CPlatformFactory::CPlatformFactory(void) : m_cRef(1)
{
}

CPlatformFactory::~CPlatformFactory()
{
}

MUX_RESULT CPlatformFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else if (mux_IID_IClassFactory == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    AddRef();
    return MUX_S_OK;
}

uint32_t CPlatformFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CPlatformFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CPlatformFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    CPlatform *pPlatform = nullptr;
    try
    {
        pPlatform = new CPlatform;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pPlatform)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pPlatform->QueryInterface(iid, ppv);
    pPlatform->Release();
    return mr;
}

MUX_RESULT CPlatformFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}
