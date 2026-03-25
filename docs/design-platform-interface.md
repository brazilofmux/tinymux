# Design: Platform Interface Module (mux_IPlatform)

## Problem

The driver (`driver.cpp`, `signals.cpp`, `ganl_adapter.cpp`) contains
platform-specific code guarded by `#ifdef`:

- `HAVE_WORKING_FORK && STUB_SLAVE` — stubslave child process (fork,
  socketpair, dup2, exec, waitpid)
- `HAVE_SETRLIMIT && RLIMIT_NOFILE` — file descriptor limits
- `UNIX_SIGNALS` — SIGCHLD, SIGHUP, SIGUSR1/2, SIGPIPE, plus panic
  restart via fork+exec
- `WINDOWS_PROCESSES` — panic via abort()

The driver is supposed to be a thin host: `main()`, module loading, and
the event loop. Everything else should be behind COM interfaces, as
`IID_ILog`, `IID_IGameEngine`, `IID_IPlayerSession`, `IID_IDriverControl`,
and `IID_IConnectionManager` already are. The platform-specific code is
the last set of stragglers.

## Approach

Create a `mux_IPlatform` COM interface that abstracts platform
**operations**, not individual syscalls. The interface methods represent
what the driver wants to *do*: "boot a helper process", "maximize file
descriptors", "handle a panic". The platform module implements these
using OS-native mechanisms, or returns `MUX_E_NOTIMPLEMENTED` when the
operation doesn't apply.

Two implementations ship:
- `platform_unix.cpp` — compiled into the Unix build, uses fork/exec,
  POSIX signals, setrlimit
- `platform_win32.cpp` — compiled into the Windows build, returns
  not-implemented for fork-based operations, uses Windows event/process
  APIs where applicable

The driver adapts at runtime based on return codes, not at compile time
based on `#ifdef`.

## Interface Design

```cpp
const MUX_CID CID_Platform              = UINT64_C(0x00000002...);
const MUX_IID IID_IPlatform             = UINT64_C(0x00000002...);

// Callback signature for lifecycle signals.
// The platform module calls this when a signal/event fires.
// signal_type values defined below.
typedef void (*PLATFORM_SIGNAL_HANDLER)(int signal_type, void *context);

// Signal types (platform-neutral).
enum PlatformSignal {
    PLATSIG_SHUTDOWN       = 1,  // graceful shutdown (SIGTERM, SIGQUIT)
    PLATSIG_DUMP           = 2,  // database checkpoint (SIGHUP)
    PLATSIG_RESTART        = 3,  // restart server (SIGUSR1)
    PLATSIG_CHILD_EXIT     = 4,  // child process exited (SIGCHLD)
    PLATSIG_PANIC          = 5,  // fatal signal (SIGSEGV, SIGILL, etc.)
};

interface mux_IPlatform : public mux_IUnknown
{
public:
    // ---- Lifecycle signal registration ----

    // Register a callback for platform lifecycle events.  The platform
    // module translates OS signals/events into PlatformSignal values
    // and invokes the callback.
    //
    // On Unix: installs signal handlers for SIGTERM, SIGHUP, SIGUSR1,
    //   SIGCHLD, SIGSEGV, SIGILL, etc.
    // On Windows: installs console control handler, structured exception
    //   handler.
    //
    virtual MUX_RESULT RegisterSignalHandler(
        PLATFORM_SIGNAL_HANDLER pfHandler, void *context) = 0;

    // Unregister and restore default signal handling.
    //
    virtual MUX_RESULT UnregisterSignalHandler(void) = 0;

    // ---- Helper process management ----

    // Boot an external helper process (stubslave).  The platform module
    // creates the child process and IPC channel.  On success, ppReadFd
    // and ppWriteFd are set to file descriptors for communication.
    //
    // On Unix: fork() + socketpair() + exec("bin/stubslave")
    // On Windows: returns MUX_E_NOTIMPLEMENTED (stubslave runs in-process)
    //
    virtual MUX_RESULT BootHelperProcess(
        const UTF8 *path,       // e.g. "bin/stubslave"
        int *pReadFd,           // parent reads from child
        int *pWriteFd,          // parent writes to child
        int *pChildPid) = 0;

    // Wait for a child process to exit (non-blocking).
    // Returns child PID and exit status, or 0 if no child exited.
    //
    // On Unix: waitpid(0, &status, WNOHANG)
    // On Windows: returns MUX_E_NOTIMPLEMENTED
    //
    virtual MUX_RESULT ReapChild(int *pPid, int *pExitStatus,
        bool *pSignaled) = 0;

    // ---- Resource limits ----

    // Maximize the number of file descriptors available to the process.
    // Returns the new limit in *pLimit.
    //
    // On Unix: setrlimit(RLIMIT_NOFILE, hard_limit)
    // On Windows: returns MUX_E_NOTIMPLEMENTED (no fd limit concept)
    //
    virtual MUX_RESULT MaximizeFileDescriptors(int *pLimit) = 0;

    // ---- Panic recovery ----

    // Attempt to restart the server after a fatal error.  Called from
    // the signal/exception handler context — must be signal-safe.
    //
    // On Unix: fork() a child to dump core, then exec() to restart.
    //   Returns MUX_S_OK if exec succeeds (never returns in practice).
    // On Windows: calls abort(). Returns MUX_E_NOTIMPLEMENTED if restart
    //   is not possible.
    //
    virtual MUX_RESULT PanicRestart(
        const UTF8 *execPath,       // e.g. "bin/netmux"
        const UTF8 *const *argv,    // saved command-line arguments
        int argc) = 0;

    // ---- Process information ----

    // Get the current process ID.
    //
    virtual MUX_RESULT GetProcessId(int *pPid) = 0;

    // ---- Signal name table ----

    // Build the platform-specific signal name table for @list signals.
    //
    // On Unix: populates SIGCHLD, SIGHUP, SIGUSR1, etc.
    // On Windows: populates SIGINT, SIGTERM, etc.
    //
    virtual MUX_RESULT GetSignalNametab(NAMETAB **ppTable) = 0;
};
```

## Driver Changes

### driver.cpp

Replace the three `#ifdef` blocks:

**Before (line 384):**
```cpp
// TODO: Create platform interface
```

**After:**
```cpp
mr = mux_CreateInstance(CID_Platform, nullptr, UseSameProcess,
                        IID_IPlatform,
                        reinterpret_cast<void **>(&g_pIPlatform));
if (MUX_SUCCEEDED(mr) && g_pIPlatform) {
    int fdLimit;
    g_pIPlatform->MaximizeFileDescriptors(&fdLimit);
    // fdLimit == 0 or failure means platform doesn't support it — fine.
}
```

**Before (fork stubslave):**
```cpp
#if defined(HAVE_WORKING_FORK) && defined(STUB_SLAVE)
    g_GanlAdapter.boot_stubslave();
    init_stubslave();
#endif
```

**After:**
```cpp
int readFd, writeFd, childPid;
MUX_RESULT mr = g_pIPlatform->BootHelperProcess(
    T("bin/stubslave"), &readFd, &writeFd, &childPid);
if (MUX_SUCCEEDED(mr)) {
    g_GanlAdapter.attach_stubslave(readFd, writeFd, childPid);
} else {
    // In-process fallback (Windows, or Unix without fork)
}
```

**Before (init_rlimit):**
```cpp
#if defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE)
    init_rlimit();
#endif
```

**After:**
```cpp
int fdLimit;
g_pIPlatform->MaximizeFileDescriptors(&fdLimit);
```

### signals.cpp

The signal handler becomes a thin dispatcher that calls back into the
platform module's registered callback:

```cpp
void driver_signal_callback(int signal_type, void *context) {
    switch (signal_type) {
    case PLATSIG_SHUTDOWN:
        // set shutdown flag
        break;
    case PLATSIG_DUMP:
        // set dump flag
        break;
    case PLATSIG_RESTART:
        // set restart flag
        break;
    case PLATSIG_CHILD_EXIT:
        // reap children via g_pIPlatform->ReapChild()
        break;
    case PLATSIG_PANIC:
        // dump + g_pIPlatform->PanicRestart()
        break;
    }
}
```

Registration:
```cpp
g_pIPlatform->RegisterSignalHandler(driver_signal_callback, nullptr);
```

The driver callback code is platform-neutral — it just sets flags and
calls COM methods. All the `#ifdef UNIX_SIGNALS` / `#ifdef WINDOWS_SIGNALS`
code moves into the platform module implementations.

### ganl_adapter.cpp

The `boot_stubslave()` method (~70 lines of fork/socketpair/dup2/exec)
moves into `platform_unix.cpp::BootHelperProcess()`. The adapter gets
a new `attach_stubslave(readFd, writeFd, childPid)` that takes the
already-created IPC channel from the platform module.

## Implementation: CPlatform in modules.cpp

`CPlatform` is **not** a separately loadable module. It is compiled
directly into `netmux` (the driver binary), registered in
`driver_classes[]` alongside `CDriverControl` and `CConnectionManager`,
and created via `mux_CreateInstance(CID_Platform, ...)` at startup.

This is the same pattern as every other driver-side COM class. The
platform interface needs to be available before engine.so loads
(MaximizeFileDescriptors runs before config, BootHelperProcess before
engine creation), so it cannot be in a separate .so/.dll — there is
no config to tell the driver which module to load at that point.

The `#ifdef` blocks that were scattered across driver.cpp, signals.cpp,
and ganl_adapter.cpp are now concentrated in `CPlatform` method bodies
in `modules.cpp`. One class, one file, one place to find all platform
conditionals. The driver code that calls the interface is `#ifdef`-free.

### Method implementations

| Method | Unix (`HAVE_WORKING_FORK` etc.) | Windows |
|--------|--------------------------------|---------|
| `BootHelperProcess` | fork + socketpair + exec | `MUX_E_NOTIMPLEMENTED` |
| `ReapChild` | waitpid(WNOHANG) + WIFEXITED/WIFSIGNALED | `MUX_E_NOTIMPLEMENTED` |
| `MaximizeFileDescriptors` | getrlimit + setrlimit | `MUX_E_NOTIMPLEMENTED` |
| `PanicRestart` | fork (dump core) + execl (restart) | `abort()` |
| `GetProcessId` | `mux_getpid()` | `mux_getpid()` |
| `RegisterSignalHandler` | Placeholder (signal registration stays in signals.cpp for now) | Placeholder |
| `GetSignalNametab` | Placeholder | Placeholder |

## What Changed

| File | Before | After |
|------|--------|-------|
| `driver.cpp` | `#ifdef HAVE_SETRLIMIT` block, `#ifdef HAVE_WORKING_FORK && STUB_SLAVE` block | `g_pIPlatform->MaximizeFileDescriptors()`, `g_pIPlatform->BootHelperProcess()` |
| `signals.cpp` | SIGCHLD: nested `#ifdef` with waitpid/STUB_SLAVE/HAVE_WORKING_FORK. Panic: `#ifdef WINDOWS_PROCESSES` / `#ifdef UNIX_PROCESSES` fork+exec vs abort | `g_pIPlatform->ReapChild()` loop, `g_pIPlatform->PanicRestart()` |
| `ganl_adapter.cpp` | `boot_stubslave()` contains fork+socketpair+dup2+exec | `attach_stubslave()` takes pre-created IPC fds (BootHelperProcess will eventually supply them) |
| `modules.cpp` | No platform class | `CPlatform` + `CPlatformFactory`, `#ifdef` blocks concentrated here |

## What Stays

- `main()` — in `driver.cpp`, unchanged
- `modules.cpp` — module registration, `CPlatform` class
- `set_signals()` — still in `signals.cpp` with `#ifdef UNIX_SIGNALS`
  for the extended signal set (candidate for `RegisterSignalHandler`
  in a future pass)
- `sighandler()` — stays in `signals.cpp`, calls platform methods
  for ReapChild and PanicRestart
- GANL networking — already abstracted, untouched

## Remaining Work

- `set_signals()` still has `#ifdef UNIX_SIGNALS` — move into
  `CPlatform::RegisterSignalHandler` (low priority, straightforward)
- `boot_stubslave()` body should eventually move into
  `CPlatform::BootHelperProcess` on Unix (currently BootHelperProcess
  returns `MUX_E_NOTIMPLEMENTED` on both platforms)
- `GetSignalNametab` — not yet wired
- `build_signal_names_table()` — candidate for `GetSignalNametab`

## Risk Assessment

- **Low risk to engine:** The engine doesn't call any of this code
  directly. It interacts with the driver through `mux_IDriverControl`
  and `mux_IConnectionManager`, which are unchanged.
- **Moderate risk to signal handling:** Signal handlers are delicate
  (async-signal-safe requirements). `ReapChild` and `PanicRestart`
  preserve the same signal-safety guarantees — waitpid and fork/exec
  are async-signal-safe.
- **No build system changes:** `CPlatform` lives in `modules.cpp`,
  which is already compiled on both platforms. No new files to add
  to Makefile.am or vcxproj.
