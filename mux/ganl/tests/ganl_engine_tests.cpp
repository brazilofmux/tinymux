// GANL engine regression harness.
//
// Scripted scenarios run against each engine available on this platform,
// locking in the fixes from the 2026-07 engine hardening pass.
//
// POSIX (Linux: epoll + select; macOS/BSD: kqueue + select) — these engines
// support socketpair + adoptConnection, so connections are injected directly:
//
//   #942  epoll immediate-connect must emit ConnectSuccess
//   #943  write readiness past the maxEvents budget must re-fire, not stall
//   #946  select must reject fds >= FD_SETSIZE (ENOBUFS), not FD_SET OOB
//   #947  closeConnection must no-op on a not-found fd (fd-reuse safety)
//   #953  IoBuffer::ensureWritable must reject wrapping sizes (length_error)
//   EMFILE accept failure must emit a listener Error event (NET/LERR path)
//
// Windows (wselect + iocp) — these engines do NOT support adoptConnection or
// initiateConnect (base-class ENOTSUP), so scenarios obtain a connection via
// the accept path (loopback listener + client connect + Accept event):
//
//   #962  Read event must carry the exact IoBuffer handed to postRead()
//   #947  closeConnection must be idempotent (double-close stays healthy)
//   #953  IoBuffer::ensureWritable must reject wrapping sizes (shared)
//
// Zero dependencies: plain main(), TAP-ish output, nonzero exit on failure.
// Build/run: POSIX `make -C mux/ganl/tests check`; Windows via ganl_tests.vcxproj.
//
// Engines print their debug logging to stderr in non-NDEBUG builds; the
// `check` target redirects stderr to ganl_tests.err and shows stdout only.

// Winsock must be included before anything that might pull in <windows.h>.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <network_engine.h>
#include <network_types.h>
#include <io_buffer.h>

#if defined(_WIN32)
#include <wselect_network_engine.h>
#include <iocp_network_engine.h>
#else
#include <select_network_engine.h>
#if defined(__linux__)
#include <epoll_network_engine.h>
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <kqueue_network_engine.h>
#endif
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ganl;

namespace {

// ---------------------------------------------------------------------------
// Result plumbing
// ---------------------------------------------------------------------------

enum class Outcome { Pass, Fail, Skip };

struct Result {
    Outcome outcome;
    std::string detail;
};

Result pass(const std::string& d = "") { return {Outcome::Pass, d}; }
Result fail(const std::string& d)      { return {Outcome::Fail, d}; }
Result skip(const std::string& d)      { return {Outcome::Skip, d}; }

using EngineFactory = std::function<std::unique_ptr<NetworkEngine>()>;

struct EngineUnderTest {
    std::string name;
    EngineFactory make;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Poll until an event matching `want` arrives (or timeout).  Returns true and
// fills `out` on match; keeps count of all events seen in `seen` if non-null.
bool pollFor(NetworkEngine& eng, int totalMs, int budget,
             const std::function<bool(const IoEvent&)>& want,
             IoEvent* out = nullptr, int* seen = nullptr) {
    IoEvent events[32];
    if (budget > 32) budget = 32;
    int waited = 0;
    while (waited < totalMs) {
        int n = eng.processEvents(100, events, budget);
        waited += 100;
        for (int i = 0; i < n; i++) {
            if (seen) (*seen)++;
            if (want(events[i])) {
                if (out) *out = events[i];
                return true;
            }
        }
    }
    return false;
}

#if defined(_WIN32)

// ---------------------------------------------------------------------------
// Windows helpers (accept path)
//
// wselect and iocp do not support adoptConnection()/initiateConnect() — those
// return ENOTSUP on the base class — so connections cannot be injected via a
// socketpair as the POSIX scenarios do. Instead we obtain a real connection by
// listening on loopback, connecting a client, and pumping the engine until the
// accepted ConnectionHandle arrives. Both engines return the underlying SOCKET
// as the Listener/Connection handle.
// ---------------------------------------------------------------------------

uint16_t listenerPort(ListenerHandle lh) {
    sockaddr_in addr{};
    int alen = static_cast<int>(sizeof(addr));
    if (getsockname(static_cast<SOCKET>(lh),
                    reinterpret_cast<sockaddr*>(&addr), &alen) != 0) {
        return 0;
    }
    return ntohs(addr.sin_port);
}

SOCKET connectLoopback(uint16_t port) {
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

struct AcceptedConn {
    ListenerHandle lh = InvalidListenerHandle;
    SOCKET client = INVALID_SOCKET;
    ConnectionHandle conn = InvalidConnectionHandle;
};

// Fill `out` by listening, connecting a client, and accepting. Returns "" on
// success or an error detail string.
std::string setupAccepted(NetworkEngine& eng, AcceptedConn& out) {
    ErrorCode err = 0;
    out.lh = eng.createListener("127.0.0.1", 0, err);
    if (out.lh == InvalidListenerHandle) return "createListener failed";
    if (!eng.startListening(out.lh, nullptr, err)) return "startListening failed";
    uint16_t port = listenerPort(out.lh);
    if (port == 0) return "getsockname failed";
    out.client = connectLoopback(port);
    if (out.client == INVALID_SOCKET) return "client connect failed";
    IoEvent ev{};
    if (!pollFor(eng, 3000, 16, [&](const IoEvent& e) {
            return e.type == IoEventType::Accept &&
                   e.connection != InvalidConnectionHandle;
        }, &ev)) {
        return "Accept event never emitted";
    }
    out.conn = ev.connection;
    return "";
}

void teardownAccepted(NetworkEngine& eng, AcceptedConn& ac) {
    if (ac.conn != InvalidConnectionHandle) eng.closeConnection(ac.conn);
    if (ac.lh != InvalidListenerHandle) eng.closeListener(ac.lh);
    if (ac.client != INVALID_SOCKET) closesocket(ac.client);
    ac.conn = InvalidConnectionHandle;
    ac.lh = InvalidListenerHandle;
    ac.client = INVALID_SOCKET;
}

// ---------------------------------------------------------------------------
// Windows scenarios
// ---------------------------------------------------------------------------

// #962: a Read event must carry the exact IoBuffer handed to postRead().
// wselect saved the buffer, nulled its slot, then tested the nulled slot — so
// ev.buffer was never assigned and kept a stale pointer from the reused
// events[] slot. Assert ev.buffer points at the posted buffer.
Result scenarioAcceptReadEvBuffer(const EngineUnderTest& eut) {
    auto eng = eut.make();
    if (!eng->initialize()) return fail("engine init failed");

    AcceptedConn ac;
    std::string setupErr = setupAccepted(*eng, ac);
    if (!setupErr.empty()) { teardownAccepted(*eng, ac); eng->shutdown(); return fail(setupErr); }

    IoBuffer readBuf(4096);
    ErrorCode err = 0;
    if (!eng->postRead(ac.conn, readBuf, err)) {
        teardownAccepted(*eng, ac); eng->shutdown();
        return fail("postRead failed (errno " + std::to_string(err) + ")");
    }

    const char msg[] = "hello-ganl";
    int sent = ::send(ac.client, msg, static_cast<int>(sizeof(msg) - 1), 0);
    if (sent != static_cast<int>(sizeof(msg) - 1)) {
        teardownAccepted(*eng, ac); eng->shutdown();
        return fail("client send failed");
    }

    IoEvent ev{};
    bool got = pollFor(*eng, 3000, 16, [&](const IoEvent& e) {
        return e.type == IoEventType::Read && e.connection == ac.conn;
    }, &ev);

    Result r;
    if (!got) {
        r = fail("Read event never emitted");
    } else if (ev.buffer != &readBuf) {
        r = fail("Read event ev.buffer != posted buffer (#962 stale-buffer regression)");
    } else {
        r = pass();
    }
    teardownAccepted(*eng, ac);
    eng->shutdown();
    return r;
}

// A posted write must produce a Write event (write readiness for wselect,
// write completion for iocp).
Result scenarioWriteArmsAndFires(const EngineUnderTest& eut) {
    auto eng = eut.make();
    if (!eng->initialize()) return fail("engine init failed");

    AcceptedConn ac;
    std::string setupErr = setupAccepted(*eng, ac);
    if (!setupErr.empty()) { teardownAccepted(*eng, ac); eng->shutdown(); return fail(setupErr); }

    ErrorCode err = 0;
    if (!eng->postWrite(ac.conn, "hello", 5, err)) {
        teardownAccepted(*eng, ac); eng->shutdown();
        return fail("postWrite failed (errno " + std::to_string(err) + ")");
    }

    bool got = pollFor(*eng, 3000, 16, [&](const IoEvent& e) {
        return e.type == IoEventType::Write && e.connection == ac.conn;
    });
    Result r = got ? pass() : fail("Write event never emitted");
    teardownAccepted(*eng, ac);
    eng->shutdown();
    return r;
}

// #947: closeConnection on an already-closed handle must no-op, never a second
// shutdown()/closesocket() on a since-reused socket. Close twice and confirm
// the engine stays healthy (still pumps events, no crash/double-free).
Result scenarioDoubleCloseIdempotent(const EngineUnderTest& eut) {
    auto eng = eut.make();
    if (!eng->initialize()) return fail("engine init failed");

    AcceptedConn ac;
    std::string setupErr = setupAccepted(*eng, ac);
    if (!setupErr.empty()) { teardownAccepted(*eng, ac); eng->shutdown(); return fail(setupErr); }

    eng->closeConnection(ac.conn);   // legitimate close
    eng->closeConnection(ac.conn);   // handleClose-style re-close: must no-op
    ac.conn = InvalidConnectionHandle;

    // Engine must remain usable after the redundant close.
    IoEvent events[8];
    for (int i = 0; i < 3; i++) eng->processEvents(50, events, 8);

    teardownAccepted(*eng, ac);   // closes listener + client
    eng->shutdown();
    return pass();
}

#else // !defined(_WIN32)

struct TcpListener {
    int fd = -1;
    uint16_t port = 0;
    bool open() {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return false;
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            listen(fd, 8) != 0) {
            ::close(fd);
            fd = -1;
            return false;
        }
        socklen_t alen = sizeof(addr);
        getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &alen);
        port = ntohs(addr.sin_port);
        return true;
    }
    ~TcpListener() { if (fd >= 0) ::close(fd); }
};

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

// #942: an outbound Unix connect that completes immediately must still emit
// ConnectSuccess on a later poll (epoll used to hang forever).
Result scenarioImmediateUnixConnect(const EngineUnderTest& eut) {
    auto eng = eut.make();
    if (!eng->initialize()) return fail("engine init failed");

    char path[] = "/tmp/ganltestXXXXXX";
    int tmp = mkstemp(path);
    if (tmp < 0) return fail("mkstemp failed");
    ::close(tmp);
    unlink(path);

    int lfd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(lfd, 8) != 0) {
        ::close(lfd);
        return fail("unix bind/listen failed");
    }

    ErrorCode err = 0;
    ConnectionHandle ch = eng->initiateUnixConnect(path, nullptr, err);
    Result r = pass();
    if (ch == InvalidConnectionHandle) {
        r = (err == ENOTSUP) ? skip("initiateUnixConnect not supported")
                             : fail("initiateUnixConnect errno " + std::to_string(err));
    } else if (!pollFor(*eng, 3000, 16, [&](const IoEvent& ev) {
                   return ev.type == IoEventType::ConnectSuccess && ev.connection == ch;
               })) {
        r = fail("ConnectSuccess never emitted (#942 hang)");
    }
    ::close(lfd);
    unlink(path);
    eng->shutdown();
    return r;
}

// #942 companion: TCP loopback connect (immediate or EINPROGRESS) must emit
// ConnectSuccess.
Result scenarioTcpLoopbackConnect(const EngineUnderTest& eut) {
    auto eng = eut.make();
    if (!eng->initialize()) return fail("engine init failed");
    TcpListener l;
    if (!l.open()) return fail("listener setup failed");

    ErrorCode err = 0;
    ConnectionHandle ch = eng->initiateConnect("127.0.0.1", l.port, nullptr, err);
    Result r = pass();
    if (ch == InvalidConnectionHandle) {
        r = (err == ENOTSUP) ? skip("initiateConnect not supported")
                             : fail("initiateConnect errno " + std::to_string(err));
    } else if (!pollFor(*eng, 3000, 16, [&](const IoEvent& ev) {
                   return ev.type == IoEventType::ConnectSuccess && ev.connection == ch;
               })) {
        r = fail("ConnectSuccess never emitted");
    }
    eng->shutdown();
    return r;
}

// #943: Write readiness that lands past the per-poll budget must be emitted
// on a later poll.  Two write-armed connections plus a readable filler, with
// a budget of 1: whatever the kernel's delivery order, at least two of the
// three ready events land past the first poll's budget — pre-fix (epoll
// over-fetch + edge drop) at least one armed Write is lost forever; post-fix
// every armed Write must still arrive.
Result scenarioDeferredWriteCrossFd(const EngineUnderTest& eut) {
    auto eng = eut.make();
    if (!eng->initialize()) return fail("engine init failed");

    int bPair[2], a1Pair[2], a2Pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, bPair) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, a1Pair) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, a2Pair) != 0)
        return fail("socketpair failed");

    ErrorCode err = 0;
    ConnectionHandle chB  = eng->adoptConnection(bPair[0], nullptr, err);
    ConnectionHandle chA1 = eng->adoptConnection(a1Pair[0], nullptr, err);
    ConnectionHandle chA2 = eng->adoptConnection(a2Pair[0], nullptr, err);
    if (chB == InvalidConnectionHandle || chA1 == InvalidConnectionHandle ||
        chA2 == InvalidConnectionHandle) {
        ::close(bPair[0]);  ::close(bPair[1]);
        ::close(a1Pair[0]); ::close(a1Pair[1]);
        ::close(a2Pair[0]); ::close(a2Pair[1]);
        return (err == ENOTSUP) ? skip("adoptConnection not supported")
                                : fail("adoptConnection failed");
    }

    if (write(bPair[1], "x", 1) != 1) return fail("filler write failed");
    if (!eng->postWrite(chA1, "hello", 5, err)) return fail("postWrite A1 failed");
    if (!eng->postWrite(chA2, "hello", 5, err)) return fail("postWrite A2 failed");

    // One budget-1 poll to force the overflow, then generous polls: BOTH
    // armed Writes must arrive.
    bool w1 = false, w2 = false;
    IoEvent events[8];
    int n = eng->processEvents(200, events, 1);
    for (int i = 0; i < n; i++) {
        if (events[i].type == IoEventType::Write && events[i].connection == chA1) w1 = true;
        if (events[i].type == IoEventType::Write && events[i].connection == chA2) w2 = true;
    }
    int waited = 0;
    while (waited < 3000 && !(w1 && w2)) {
        n = eng->processEvents(100, events, 8);
        waited += 100;
        for (int i = 0; i < n; i++) {
            if (events[i].type == IoEventType::Write && events[i].connection == chA1) w1 = true;
            if (events[i].type == IoEventType::Write && events[i].connection == chA2) w2 = true;
        }
    }
    Result r = (w1 && w2)
        ? pass()
        : fail(std::string("deferred Write lost (#943 stall): A1=") +
               (w1 ? "ok" : "LOST") + " A2=" + (w2 ? "ok" : "LOST"));
    ::close(bPair[1]);
    ::close(a1Pair[1]);
    ::close(a2Pair[1]);
    eng->shutdown();
    return r;
}

// #943 same-fd variant: Read and Write readiness in one poll with budget 1 —
// the Read takes the slot; the Write must re-fire on a later poll.
Result scenarioDeferredWriteSameFd(const EngineUnderTest& eut) {
    auto eng = eut.make();
    if (!eng->initialize()) return fail("engine init failed");

    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return fail("socketpair failed");

    ErrorCode err = 0;
    ConnectionHandle ch = eng->adoptConnection(pair[0], nullptr, err);
    if (ch == InvalidConnectionHandle) {
        ::close(pair[0]); ::close(pair[1]);
        return (err == ENOTSUP) ? skip("adoptConnection not supported")
                                : fail("adoptConnection failed");
    }
    if (write(pair[1], "x", 1) != 1) return fail("peer write failed");
    if (!eng->postWrite(ch, "hello", 5, err)) return fail("postWrite failed");

    bool writeSeen = false, readSeen = false;
    IoEvent events[4];
    // First poll, budget 1.
    int n = eng->processEvents(200, events, 1);
    for (int i = 0; i < n; i++) {
        if (events[i].type == IoEventType::Read)  readSeen = true;
        if (events[i].type == IoEventType::Write) writeSeen = true;
    }
    Result r;
    if (!readSeen && writeSeen) {
        r = pass("write arrived first (order inconclusive for deferral)");
    } else if (writeSeen || pollFor(*eng, 2000, 8, [&](const IoEvent& ev) {
                   return ev.type == IoEventType::Write && ev.connection == ch;
               })) {
        r = pass();
    } else {
        r = fail("same-fd deferred Write never re-fired (#943 stall)");
    }
    ::close(pair[1]);
    eng->shutdown();
    return r;
}

// #947: closeConnection on a handle whose fd is gone must no-op — the fd
// number may have been reused by an innocent descriptor.
Result scenarioDoubleCloseFdReuse(const EngineUnderTest& eut) {
    auto eng = eut.make();
    if (!eng->initialize()) return fail("engine init failed");

    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return fail("socketpair failed");
    ErrorCode err = 0;
    ConnectionHandle ch = eng->adoptConnection(pair[0], nullptr, err);
    if (ch == InvalidConnectionHandle) {
        ::close(pair[0]); ::close(pair[1]);
        return (err == ENOTSUP) ? skip("adoptConnection not supported")
                                : fail("adoptConnection failed");
    }

    eng->closeConnection(ch);          // legitimate close — engine owns pair[0]
    int reused = open("/dev/null", O_RDONLY);
    if (reused != pair[0]) {
        if (reused >= 0) ::close(reused);
        ::close(pair[1]);
        eng->shutdown();
        return skip("fd number not reused; inconclusive");
    }
    eng->closeConnection(ch);          // handleClose-style re-close: must no-op

    bool alive = (fcntl(reused, F_GETFD) != -1);
    if (alive) ::close(reused);
    ::close(pair[1]);
    eng->shutdown();
    return alive ? pass() : fail("innocent reused fd was closed (#947)");
}

// EMFILE: a real accept() failure must surface as a listener Error event
// (the adapter logs it as NET/LERR), not just a stderr line.
Result scenarioEmfileListenerError(const EngineUnderTest& eut) {
    auto eng = eut.make();
    if (!eng->initialize()) return fail("engine init failed");

    ErrorCode err = 0;
    ListenerHandle lh = eng->createListener("127.0.0.1", 0, err);
    if (lh == InvalidListenerHandle) return fail("createListener failed");
    if (!eng->startListening(lh, nullptr, err)) return fail("startListening failed");
    sockaddr_in addr{};
    socklen_t alen = sizeof(addr);
    getsockname(static_cast<int>(lh), reinterpret_cast<sockaddr*>(&addr), &alen);

    int client = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port = addr.sin_port;
    if (connect(client, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) != 0) {
        ::close(client);
        return fail("client connect failed");
    }

    // Clamp the soft fd limit just above current usage and burn the rest, so
    // accept() fails EMFILE with the connection pending.
    rlimit saved{};
    getrlimit(RLIMIT_NOFILE, &saved);
    int probe = open("/dev/null", O_RDONLY);
    if (probe < 0) { ::close(client); return fail("probe open failed"); }
    rlimit lowered = saved;
    lowered.rlim_cur = probe + 3;
    if (setrlimit(RLIMIT_NOFILE, &lowered) != 0) {
        ::close(probe); ::close(client);
        return skip("cannot lower RLIMIT_NOFILE");
    }
    std::vector<int> burned{probe};
    for (;;) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) break;
        burned.push_back(fd);
        if (burned.size() > 64) break;
    }

    IoEvent got{};
    bool errorEmitted = pollFor(*eng, 1000, 8, [&](const IoEvent& ev) {
        return ev.type == IoEventType::Error && ev.listener == lh;
    }, &got);

    for (int fd : burned) ::close(fd);
    setrlimit(RLIMIT_NOFILE, &saved);
    ::close(client);
    eng->shutdown();

    if (!errorEmitted) return fail("fd exhaustion produced no listener Error event");
    if (got.error != EMFILE)
        return pass("Error event emitted (errno " + std::to_string(got.error) + ", expected EMFILE)");
    return pass();
}

// #946 (select only): fds >= FD_SETSIZE must be rejected with ENOBUFS at
// every registration path, never handed to FD_SET.
Result scenarioFdSetsizeReject(const EngineUnderTest& eut) {
    if (eut.name != "select") return skip("select-specific");

    rlimit saved{};
    getrlimit(RLIMIT_NOFILE, &saved);
    rlimit raised = saved;
    rlim_t want = FD_SETSIZE + 512;
    if (raised.rlim_cur < want) {
        raised.rlim_cur = (saved.rlim_max == RLIM_INFINITY || saved.rlim_max > want)
                              ? want : saved.rlim_max;
        setrlimit(RLIMIT_NOFILE, &raised);
        getrlimit(RLIMIT_NOFILE, &raised);
        if (raised.rlim_cur < want) {
            setrlimit(RLIMIT_NOFILE, &saved);
            return skip("cannot raise RLIMIT_NOFILE past FD_SETSIZE");
        }
    }

    auto eng = eut.make();
    if (!eng->initialize()) { setrlimit(RLIMIT_NOFILE, &saved); return fail("engine init failed"); }

    // Burn fds past FD_SETSIZE.
    std::vector<int> burned;
    int fd;
    do {
        fd = open("/dev/null", O_RDONLY);
        if (fd < 0) { setrlimit(RLIMIT_NOFILE, &saved); return fail("burn failed"); }
        burned.push_back(fd);
    } while (fd < FD_SETSIZE + 8);

    Result r = pass();
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
        r = fail("socketpair failed");
    } else {
        ErrorCode err = 0;
        ConnectionHandle ch = eng->adoptConnection(pair[0], nullptr, err);
        if (ch != InvalidConnectionHandle) {
            r = fail("oversized fd ACCEPTED — FD_SET OOB reachable (#946)");
        } else if (err != ENOBUFS) {
            r = fail("rejected but errno " + std::to_string(err) + ", expected ENOBUFS");
        }
        ::close(pair[0]);
        ::close(pair[1]);
    }

    for (int b : burned) ::close(b);
    eng->shutdown();
    setrlimit(RLIMIT_NOFILE, &saved);
    return r;
}

// Peer sends data then closes: the engine must deliver events (Read and/or
// Close per its documented semantics) and never leave the connection silent.
Result scenarioHupWithData(const EngineUnderTest& eut) {
    auto eng = eut.make();
    if (!eng->initialize()) return fail("engine init failed");

    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return fail("socketpair failed");
    ErrorCode err = 0;
    ConnectionHandle ch = eng->adoptConnection(pair[0], nullptr, err);
    if (ch == InvalidConnectionHandle) {
        ::close(pair[0]); ::close(pair[1]);
        return (err == ENOTSUP) ? skip("adoptConnection not supported")
                                : fail("adoptConnection failed");
    }

    // data + FIN, potentially coalesced into one wakeup.
    if (write(pair[1], "QUIT\n", 5) != 5) return fail("peer write failed");
    ::close(pair[1]);

    // Act as the consumer: drain on Read (required by the ET contract);
    // accept either a Close/Error event or consumer-level EOF as terminal.
    bool terminal = false;
    IoEvent events[8];
    int waited = 0;
    while (waited < 3000 && !terminal) {
        int n = eng->processEvents(100, events, 8);
        waited += 100;
        for (int i = 0; i < n && !terminal; i++) {
            if (events[i].connection != ch) continue;
            if (events[i].type == IoEventType::Close ||
                events[i].type == IoEventType::Error) {
                terminal = true;
            } else if (events[i].type == IoEventType::Read) {
                char buf[64];
                ssize_t r;
                while ((r = read(pair[0], buf, sizeof(buf))) > 0) {}
                if (r == 0) terminal = true;   // consumer-level EOF
            }
        }
    }
    Result r = terminal ? pass()
                        : fail("send-then-close produced no terminal signal (hang)");
    eng->closeConnection(ch);
    eng->shutdown();
    return r;
}

// #947 harmonization: after a terminal Close/Error event the fd must still be
// OPEN — close ownership belongs to the application (handleError/handleClose
// -> closeConnection), never the engine.  select historically self-closed
// errored fds inside processEvents, freeing the fd number while the caller
// still held a live handle to it (the fd-reuse collision class).  Trigger per
// engine:
//   - select: TCP urgent data (MSG_OOB) — select flags exceptfds -> the
//     engine's error branch -> Error event.
//   - epoll/kqueue: abortive close (SO_LINGER 0) -> RST ->
//     EPOLLERR|EPOLLHUP / EV_EOF-with-error -> Close event.
Result scenarioConnErrorDeferClose(const EngineUnderTest& eut) {
    auto eng = eut.make();
    if (!eng->initialize()) return fail("engine init failed");

    // Build a real TCP loopback pair outside the engine (OOB needs TCP; a
    // socketpair(AF_UNIX) has no urgent-data path).
    int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) return fail("socket failed");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(lfd, 1) != 0) {
        ::close(lfd);
        return fail("bind/listen failed");
    }
    socklen_t alen = sizeof(addr);
    if (::getsockname(lfd, reinterpret_cast<sockaddr*>(&addr), &alen) != 0) {
        ::close(lfd);
        return fail("getsockname failed");
    }
    int peer = ::socket(AF_INET, SOCK_STREAM, 0);
    if (peer < 0) { ::close(lfd); return fail("socket failed"); }
    if (::connect(peer, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(peer); ::close(lfd);
        return fail("connect failed");
    }
    int sfd = ::accept(lfd, nullptr, nullptr);
    ::close(lfd);
    if (sfd < 0) { ::close(peer); return fail("accept failed"); }

    ErrorCode err = 0;
    ConnectionHandle ch = eng->adoptConnection(sfd, nullptr, err);
    if (ch == InvalidConnectionHandle) {
        ::close(sfd); ::close(peer);
        return (err == ENOTSUP) ? skip("adoptConnection not supported")
                                : fail("adoptConnection failed");
    }

    if (eut.name == "select") {
        // Urgent data drives select's exceptfds -> its error branch.
        if (::send(peer, "!", 1, MSG_OOB) != 1) {
            ::close(peer);
            return fail("send MSG_OOB failed");
        }
    } else {
        // Abortive close: RST reaches the engine as an error/HUP condition.
        struct linger lg;
        lg.l_onoff = 1;
        lg.l_linger = 0;
        setsockopt(peer, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
        ::close(peer);
        peer = -1;
    }

    IoEvent got{};
    bool terminal = pollFor(*eng, 3000, 8, [&](const IoEvent& e) {
        return e.connection == ch &&
               (e.type == IoEventType::Close || e.type == IoEventType::Error);
    }, &got);

    Result r = pass();
    if (!terminal) {
        r = fail("no terminal Close/Error event for errored connection");
    } else if (::fcntl(sfd, F_GETFD) == -1) {
        r = fail("engine closed the fd itself before closeConnection "
                 "(close-ownership contract)");
    } else {
        eng->closeConnection(ch);
        if (::fcntl(sfd, F_GETFD) != -1) {
            r = fail("closeConnection did not close the fd");
        }
    }
    if (peer >= 0) ::close(peer);
    if (r.outcome == Outcome::Fail && ::fcntl(sfd, F_GETFD) != -1) ::close(sfd);
    eng->shutdown();
    return r;
}

#endif // !defined(_WIN32)

// #953: IoBuffer::ensureWritable must reject a size that wraps
// writePos_ + required instead of silently under-allocating.
// Engine-independent — runs on every platform.
Result scenarioEnsureWritableCap(const EngineUnderTest&) {
    IoBuffer b(64);
    b.ensureWritable(16);
    b.commitWrite(16);
    const size_t huge = static_cast<size_t>(-1) - 8;
    try {
        b.ensureWritable(huge);
        return fail("wrapping size accepted — silent under-allocation (#953)");
    } catch (const std::length_error&) {
        return pass();
    } catch (const std::exception& e) {
        return pass(std::string("rejected via ") + e.what());
    }
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

struct Scenario {
    std::string name;
    Result (*run)(const EngineUnderTest&);
    bool perEngine;   // false = engine-independent, run once
};

const Scenario kScenarios[] = {
#if defined(_WIN32)
    {"accept-read-ev-buffer",    scenarioAcceptReadEvBuffer,    true},
    {"write-arms-and-fires",     scenarioWriteArmsAndFires,     true},
    {"double-close-idempotent",  scenarioDoubleCloseIdempotent, true},
#else
    {"immediate-unix-connect",   scenarioImmediateUnixConnect, true},
    {"tcp-loopback-connect",     scenarioTcpLoopbackConnect,   true},
    {"deferred-write-cross-fd",  scenarioDeferredWriteCrossFd, true},
    {"deferred-write-same-fd",   scenarioDeferredWriteSameFd,  true},
    {"double-close-fd-reuse",    scenarioDoubleCloseFdReuse,   true},
    {"emfile-listener-error",    scenarioEmfileListenerError,  true},
    {"fd-setsize-reject",        scenarioFdSetsizeReject,      true},
    {"hup-with-data",            scenarioHupWithData,          true},
    {"conn-error-defer-close",   scenarioConnErrorDeferClose,  true},
#endif
    {"ensure-writable-cap",      scenarioEnsureWritableCap,    false},
};

} // namespace

int main() {
#if defined(_WIN32)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Bail out! WSAStartup failed\n");
        return 1;
    }
#endif

    std::vector<EngineUnderTest> engines;
#if defined(_WIN32)
    engines.push_back({"wselect", [] {
        return std::unique_ptr<NetworkEngine>(new WSelectNetworkEngine());
    }});
    engines.push_back({"iocp", [] {
        return std::unique_ptr<NetworkEngine>(new IocpNetworkEngine());
    }});
#else
#if defined(__linux__)
    engines.push_back({"epoll", [] {
        return std::unique_ptr<NetworkEngine>(new EpollNetworkEngine());
    }});
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    engines.push_back({"kqueue", [] {
        return std::unique_ptr<NetworkEngine>(new KqueueNetworkEngine());
    }});
#endif
    engines.push_back({"select", [] {
        return std::unique_ptr<NetworkEngine>(new SelectNetworkEngine());
    }});
#endif

    int testNum = 0, failures = 0, skips = 0;
    auto report = [&](const std::string& label, const Result& r) {
        testNum++;
        switch (r.outcome) {
            case Outcome::Pass:
                printf("ok %d - %s%s%s\n", testNum, label.c_str(),
                       r.detail.empty() ? "" : "  # ", r.detail.c_str());
                break;
            case Outcome::Skip:
                skips++;
                printf("ok %d - %s  # SKIP %s\n", testNum, label.c_str(), r.detail.c_str());
                break;
            case Outcome::Fail:
                failures++;
                printf("not ok %d - %s  # %s\n", testNum, label.c_str(), r.detail.c_str());
                break;
        }
        fflush(stdout);
    };

    for (const Scenario& s : kScenarios) {
        if (s.perEngine) {
            for (const EngineUnderTest& eut : engines) {
                report(eut.name + ": " + s.name, s.run(eut));
            }
        } else {
            report(s.name, s.run(engines.front()));
        }
    }

    printf("1..%d\n", testNum);
    printf("# %d passed, %d failed, %d skipped\n",
           testNum - failures - skips, failures, skips);

#if defined(_WIN32)
    WSACleanup();
#endif
    return failures == 0 ? 0 : 1;
}
