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

## Platform Module Implementations

### platform_unix.cpp

Registers as `CID_Platform`. Implements all methods:

- `RegisterSignalHandler`: installs POSIX `sigaction()` handlers that
  translate signals into `PlatformSignal` enum values and call the
  registered callback.
- `BootHelperProcess`: the existing fork/socketpair/dup2/exec code from
  `ganl_adapter.cpp:2820–2867`, moved here.
- `ReapChild`: `waitpid(0, &status, WNOHANG)` with WIFEXITED/WIFSIGNALED
  decoding.
- `MaximizeFileDescriptors`: the existing `init_rlimit()` code from
  `driver.cpp:600–621`.
- `PanicRestart`: the existing fork+dump+exec code from
  `signals.cpp:577–608`.
- `GetProcessId`: `getpid()`.
- `GetSignalNametab`: the existing `build_signal_names_table()`.

### platform_win32.cpp

Registers as `CID_Platform`. Returns not-implemented for Unix-only
operations:

- `RegisterSignalHandler`: installs `SetConsoleCtrlHandler` for
  CTRL_C_EVENT (→ PLATSIG_SHUTDOWN), CTRL_CLOSE_EVENT (→ PLATSIG_SHUTDOWN).
  Installs structured exception handler for access violations
  (→ PLATSIG_PANIC).
- `BootHelperProcess`: returns `MUX_E_NOTIMPLEMENTED`.
- `ReapChild`: returns `MUX_E_NOTIMPLEMENTED`.
- `MaximizeFileDescriptors`: returns `MUX_E_NOTIMPLEMENTED` (Windows
  doesn't have the Unix fd limit concept).
- `PanicRestart`: calls `abort()`. Could potentially use
  `CreateProcess()` for restart in the future.
- `GetProcessId`: `GetCurrentProcessId()`.
- `GetSignalNametab`: returns minimal table (SIGINT, SIGTERM, SIGABRT).

## Build Integration

### Unix (Makefile.am / configure)

`platform_unix.cpp` is compiled as part of netmux (same as other driver
files). No separate shared library — it's in-process, registered via
`mux_RegisterClassObjects` during `init_modules()`.

### Windows (netmux.vcxproj)

`platform_win32.cpp` is compiled as part of netmux. Same pattern.

Both files are in `mux/src/`. Only one compiles per platform, selected
by the build system (Makefile.am on Unix, vcxproj on Windows).

## What Moves

| Current Location | Lines | Destination |
|-----------------|-------|-------------|
| `ganl_adapter.cpp:2800–2870` (boot_stubslave) | ~70 | `platform_unix.cpp::BootHelperProcess` |
| `driver.cpp:600–621` (init_rlimit) | ~22 | `platform_unix.cpp::MaximizeFileDescriptors` |
| `signals.cpp:649–680` (signal registration) | ~32 | `platform_{unix,win32}.cpp::RegisterSignalHandler` |
| `signals.cpp:571–608` (panic restart) | ~38 | `platform_{unix,win32}.cpp::PanicRestart` |
| `signals.cpp:426–478` (SIGCHLD handler) | ~53 | `platform_unix.cpp` internal + `ReapChild` |
| `signals.cpp:345–390` (signal name table) | ~46 | `platform_{unix,win32}.cpp::GetSignalNametab` |

**Total moved:** ~260 lines out of driver/signals/ganl_adapter into
platform modules.

**Total #ifdef blocks eliminated from driver code:** All platform-
conditional blocks in driver.cpp, signals.cpp. The driver becomes
purely platform-neutral.

## What Stays

- `main()` — in `driver.cpp`, unchanged
- `modules.cpp` — module registration, unchanged
- `mux_CreateInstance(CID_Platform, ...)` — the one new line in driver.cpp
- Signal callback (`driver_signal_callback`) — platform-neutral flag
  setting, stays in driver code
- GANL networking — already abstracted, untouched

## Implementation Order

1. **modules.h:** Add `CID_Platform`, `IID_IPlatform`, `PlatformSignal`
   enum, `PLATFORM_SIGNAL_HANDLER` typedef, `mux_IPlatform` interface.
2. **platform_unix.cpp:** Move fork/signal/rlimit code, implement
   interface. Register in `init_modules()`.
3. **platform_win32.cpp:** Implement interface with not-implemented
   stubs and Windows equivalents. Register in `init_modules()`.
4. **driver.cpp:** Replace `#ifdef` blocks with `g_pIPlatform->` calls.
5. **signals.cpp:** Replace signal registration and handler dispatch
   with platform callback.
6. **ganl_adapter.cpp:** Replace `boot_stubslave()` with
   `attach_stubslave(readFd, writeFd, pid)`.
7. **Build:** Add platform files to Makefile.am and vcxproj.
8. **Testing:** Build and smoke test on both platforms.

## Risk Assessment

- **Low risk to engine:** The engine doesn't call any of this code
  directly. It interacts with the driver through `mux_IDriverControl`
  and `mux_IConnectionManager`, which are unchanged.
- **Moderate risk to signal handling:** Signal handlers are delicate
  (async-signal-safe requirements). The platform module must preserve
  the same signal-safety guarantees as the current inline code.
- **Low risk to Windows:** Most methods return `MUX_E_NOTIMPLEMENTED`,
  which the driver already handles gracefully. The Windows platform
  module is mostly stubs.
- **Build system:** Two new .cpp files, one per platform. Straightforward
  Makefile.am and vcxproj additions.
