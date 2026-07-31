// GANL ConnectionBase regression harness (#1858).
//
// Dependency-injected fakes for NetworkEngine / SecureTransport /
// ProtocolHandler / SessionManager drive ReadinessConnection without a
// real epoll loop for the close-drain path, and with a socketpair +
// SelectNetworkEngine for the ingress high-water path.
//
//   #1855  write/error during close-with-drain must abort teardown
//   #1856  encrypted ingress high-water closes a non-consuming TLS peer
//
// Build/run: POSIX `make -C mux/ganl/tests check` (runs after engine tests);
// Windows: `run-msvc.bat` builds ganl_connection_tests.vcxproj too.

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#endif

#include <connection.h>
#include <network_engine.h>
#include <protocol_handler.h>
#include <secure_transport.h>
#include <session_manager.h>
#include <io_buffer.h>

#if !defined(_WIN32)
#include <select_network_engine.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace ganl;

namespace {

enum class Outcome { Pass, Fail, Skip };
struct Result {
    Outcome outcome;
    std::string detail;
};
Result pass(const std::string& d = "") { return {Outcome::Pass, d}; }
Result fail(const std::string& d) { return {Outcome::Fail, d}; }
Result skip(const std::string& d) { return {Outcome::Skip, d}; }

// ---------------------------------------------------------------------------
// Fakes
// ---------------------------------------------------------------------------

class FakeEngine : public NetworkEngine {
public:
    IoModel getIoModelType() const override { return IoModel::Readiness; }
    bool initialize() override { return true; }
    void shutdown() override {}

    ListenerHandle createListener(const std::string&, uint16_t,
                                  ErrorCode& error) override {
        error = ENOTSUP;
        return InvalidListenerHandle;
    }
    bool startListening(ListenerHandle, void*, ErrorCode& error) override {
        error = ENOTSUP;
        return false;
    }
    void closeListener(ListenerHandle) override {}

    bool associateContext(ConnectionHandle conn, void* context,
                          ErrorCode& error) override {
        (void)conn;
        context_ = context;
        error = 0;
        associateCalls_++;
        return true;
    }

    void closeConnection(ConnectionHandle conn) override {
        (void)conn;
        closeCalls_++;
    }

    bool postRead(ConnectionHandle, IoBuffer&, ErrorCode& error) override {
        error = 0;
        postReadCalls_++;
        return postReadOk_;
    }

    bool postWrite(ConnectionHandle, const char*, size_t, void*,
                   ErrorCode& error) override {
        error = 0;
        postWriteCalls_++;
        return postWriteOk_;
    }

    int processEvents(int, IoEvent*, int) override { return 0; }

    std::string getRemoteAddress(ConnectionHandle) override {
        return "127.0.0.1:0";
    }
    NetworkAddress getRemoteNetworkAddress(ConnectionHandle) override {
        return NetworkAddress();
    }
    std::string getErrorString(ErrorCode e) override {
        return "fake-error-" + std::to_string(e);
    }

    void* context_{nullptr};
    int associateCalls_{0};
    int closeCalls_{0};
    int postReadCalls_{0};
    int postWriteCalls_{0};
    bool postReadOk_{true};
    bool postWriteOk_{true};
};

class FakeProtocol : public ProtocolHandler {
public:
    bool createProtocolContext(ConnectionHandle) override { return true; }
    void destroyProtocolContext(ConnectionHandle) override { destroyCalls_++; }
    void startNegotiation(ConnectionHandle, IoBuffer&) override {}
    bool processInput(ConnectionHandle, IoBuffer& decrypted_in,
                      IoBuffer& app_data_out, IoBuffer& /*telnet*/,
                      bool consumeInput) override {
        size_t n = decrypted_in.readableBytes();
        if (n > 0) {
            app_data_out.append(decrypted_in.readPtr(), n);
            bytesToApp_ += n;
            if (consumeInput) {
                decrypted_in.consumeRead(n);
            }
        }
        return true;
    }
    bool formatOutput(ConnectionHandle, IoBuffer& app_data_in,
                      IoBuffer& formatted_out, bool consumeInput) override {
        size_t n = app_data_in.readableBytes();
        if (n > 0) {
            formatted_out.append(app_data_in.readPtr(), n);
            if (consumeInput) {
                app_data_in.consumeRead(n);
            }
        }
        return true;
    }
    NegotiationStatus getNegotiationStatus(ConnectionHandle) override {
        return NegotiationStatus::Completed;
    }
    bool consumeStateChanges(ConnectionHandle, ProtocolState&,
                             ProtocolStateChangeFlags&) override {
        return false;
    }
    bool setEncoding(ConnectionHandle, EncodingType) override { return true; }
    EncodingType getEncoding(ConnectionHandle) override {
        return EncodingType::Utf8;
    }
    ProtocolState getProtocolState(ConnectionHandle) override {
        return ProtocolState{};
    }
    void updateWidth(ConnectionHandle, uint16_t) override {}
    void updateHeight(ConnectionHandle, uint16_t) override {}
    std::string getLastProtocolErrorString(ConnectionHandle) override {
        return {};
    }

    size_t bytesToApp_{0};
    int destroyCalls_{0};
};

class FakeSession : public SessionManager {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    SessionId onConnectionOpen(ConnectionHandle, const std::string&) override {
        openCalls_++;
        return nextId_++;
    }
    void onDataReceived(SessionId, const std::string& data) override {
        received_ += data;
    }
    void onConnectionClose(SessionId, DisconnectReason reason) override {
        closeCalls_++;
        lastReason_ = reason;
    }
    bool sendToSession(SessionId, const std::string&) override { return true; }
    bool broadcastMessage(const std::string&, SessionId) override { return true; }
    bool disconnectSession(SessionId, DisconnectReason) override { return true; }
    bool authenticateSession(SessionId, ConnectionHandle, const std::string&,
                             const std::string&) override {
        return false;
    }
    void onAuthenticationSuccess(SessionId, int) override {}
    int getPlayerId(SessionId) override { return -1; }
    SessionState getSessionState(SessionId) override {
        return SessionState::Connected;
    }
    SessionStats getSessionStats(SessionId) override { return SessionStats{}; }
    ConnectionHandle getConnectionHandle(SessionId) override {
        return InvalidConnectionHandle;
    }
    bool isAddressAllowed(const std::string&) override { return true; }
    bool isAddressRegistered(const std::string&) override { return false; }
    bool isAddressForbidden(const std::string&) override { return false; }
    bool isAddressSuspect(const std::string&) override { return false; }
    std::string getLastSessionErrorString(SessionId) override { return {}; }

    SessionId nextId_{1};
    int openCalls_{0};
    int closeCalls_{0};
    DisconnectReason lastReason_{DisconnectReason::Unknown};
    std::string received_;
};

// TLS that never establishes and never consumes ciphertext — forces
// encryptedInput_ to grow under a sustained writer (#1856).
//
class StickyTls : public SecureTransport {
public:
    bool initialize(const TlsConfig&) override { return true; }
    void shutdown() override {}
    bool createSessionContext(ConnectionHandle, bool) override { return true; }
    void destroySessionContext(ConnectionHandle) override { destroyCalls_++; }
    TlsResult processIncoming(ConnectionHandle, IoBuffer& /*encrypted_in*/,
                              IoBuffer&, IoBuffer&, bool) override {
        // Leave encrypted_in intact so the ingress high-water can trip.
        //
        return TlsResult::WantRead;
    }
    TlsResult processOutgoing(ConnectionHandle, IoBuffer&, IoBuffer&,
                              bool) override {
        return TlsResult::WantWrite;
    }
    TlsResult shutdownSession(ConnectionHandle, IoBuffer& out) override {
        // Produce a close_notify-shaped byte so close() defers for drain.
        //
        const char token = 'X';
        out.append(&token, 1);
        return TlsResult::WantWrite;
    }
    bool isEstablished(ConnectionHandle) override { return established_; }
    bool needsNetworkRead(ConnectionHandle) override { return true; }
    bool needsNetworkWrite(ConnectionHandle) override { return false; }
    std::string getLastTlsErrorString(ConnectionHandle) override {
        return "sticky-tls";
    }

    bool established_{false};
    int destroyCalls_{0};
};

// ---------------------------------------------------------------------------
// #1855 — forceCloseAfterIoFailure while Closing + drain pending
// ---------------------------------------------------------------------------

Result scenarioCloseDrainWriteFailure() {
    FakeEngine eng;
    FakeProtocol proto;
    FakeSession sess;

    const ConnectionHandle h = static_cast<ConnectionHandle>(42);
    auto conn = std::make_shared<ReadinessConnection>(
        h, eng, nullptr, proto, sess);
    if (!conn->initialize(/*useTls=*/false)) {
        return fail("plaintext initialize failed");
    }
    if (conn->getState() == ConnectionState::Closed) {
        return fail("connection closed after init");
    }

    conn->sendDataToClient("queued-output-for-drain");
    if (conn->pendingOutputBytes() == 0) {
        return fail("sendDataToClient left no pending output");
    }
    if (eng.postWriteCalls_ < 1) {
        return fail("expected postWrite when queuing output");
    }

    const int closesBefore = eng.closeCalls_;
    conn->close(DisconnectReason::UserQuit);

    if (conn->getState() != ConnectionState::Closing) {
        return fail("expected Closing after close with queued output, got "
            + std::to_string(static_cast<int>(conn->getState())));
    }
    if (conn->pendingOutputBytes() == 0) {
        return fail("output vanished before drain failure injection");
    }

    // Simulate a terminal write/error while still Closing (#1855).
    //
    IoEvent errEv{};
    errEv.type = IoEventType::Error;
    errEv.connection = h;
    errEv.context = conn.get();
    errEv.error = ECONNRESET;
    conn->handleNetworkEvent(errEv);

    if (eng.closeCalls_ <= closesBefore) {
        return fail("forceCloseAfterIoFailure did not call closeConnection "
            "(#1855 sticky Closing)");
    }
    if (proto.destroyCalls_ < 1) {
        return fail("protocol context not destroyed after abort-drain");
    }
    if (sess.closeCalls_ < 1) {
        return fail("session onConnectionClose not invoked after abort-drain");
    }
    if (sess.lastReason_ != DisconnectReason::UserQuit) {
        return fail("disconnect reason not preserved (got "
            + std::to_string(static_cast<int>(sess.lastReason_)) + ")");
    }

    return pass("drain abort + teardown");
}

// ---------------------------------------------------------------------------
// #1856 — TLS incomplete-record flood hits encrypted ingress high-water
// ---------------------------------------------------------------------------

#if !defined(_WIN32)
Result scenarioIngressHighWaterTls() {
    SelectNetworkEngine eng;
    if (!eng.initialize()) {
        return fail("select engine init failed");
    }

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        eng.shutdown();
        return fail("socketpair failed");
    }
    // Non-blocking peer side not required for the writer flood.
    //
    ErrorCode err = 0;
    ConnectionHandle ch = eng.adoptConnection(sv[0], nullptr, err);
    if (ch == InvalidConnectionHandle) {
        ::close(sv[0]);
        ::close(sv[1]);
        eng.shutdown();
        return fail("adoptConnection failed errno " + std::to_string(err));
    }

    StickyTls tls;
    FakeProtocol proto;
    FakeSession sess;
    auto conn = std::make_shared<ReadinessConnection>(
        ch, eng, &tls, proto, sess);
    if (!conn->initialize(/*useTls=*/true)) {
        ::close(sv[1]);
        eng.shutdown();
        return fail("TLS connection initialize failed");
    }

    // Non-blocking writer so we cannot deadlock the socketpair when the
    // peer has not yet drained (blocking write would hang the test).
    //
    int fl = fcntl(sv[1], F_GETFL, 0);
    fcntl(sv[1], F_SETFL, fl | O_NONBLOCK);

    std::vector<char> chunk(16 * 1024, 'Z');
    size_t written = 0;
    const size_t target = 512 * 1024;
    bool closed = false;

    // Interleave flood writes with processEvents so the connection
    // actually sees the data and the high-water can trip.
    //
    for (int round = 0; round < 200 && !closed && written < target; ++round) {
        while (written < target) {
            ssize_t n = ::write(sv[1], chunk.data(), chunk.size());
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                break;
            }
            written += static_cast<size_t>(n);
        }

        IoEvent events[16];
        int n = eng.processEvents(20, events, 16);
        if (n < 0) {
            ::close(sv[1]);
            eng.shutdown();
            return fail("processEvents error");
        }
        for (int j = 0; j < n; ++j) {
            // adoptConnection stored the Connection* via initialize's
            // associateContext; use event context if set, else our conn.
            //
            if (events[j].context == nullptr) {
                events[j].context = conn.get();
            }
            if (events[j].connection == ch
                || events[j].context == conn.get()) {
                conn->handleNetworkEvent(events[j]);
            }
        }
        if (conn->getState() == ConnectionState::Closing
            || conn->getState() == ConnectionState::Closed
            || conn->isTearingDown()
            || sess.closeCalls_ > 0) {
            closed = true;
        }
    }

    ::close(sv[1]);
    eng.shutdown();

    if (!closed) {
        return fail("TLS incomplete-record flood did not trip ingress high-water "
            "(#1856); wrote " + std::to_string(written) + " bytes, state="
            + std::to_string(static_cast<int>(conn->getState())));
    }
    return pass("ingress high-water closed after flood ("
        + std::to_string(written) + " bytes written)");
}
#else
Result scenarioIngressHighWaterTls() {
    return skip("socketpair readiness flood covered on POSIX");
}
#endif

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

struct Scenario {
    const char* name;
    Result (*run)();
};

const Scenario kScenarios[] = {
    {"close-drain-write-failure", scenarioCloseDrainWriteFailure},
    {"ingress-high-water-tls",    scenarioIngressHighWaterTls},
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

    int total = static_cast<int>(sizeof(kScenarios) / sizeof(kScenarios[0]));
    int failed = 0;
    int skipped = 0;
    int passed = 0;
    int n = 0;

    printf("# GANL ConnectionBase harness (#1858)\n");
    for (const auto& s : kScenarios) {
        ++n;
        Result r = s.run();
        if (r.outcome == Outcome::Pass) {
            ++passed;
            printf("ok %d - %s", n, s.name);
            if (!r.detail.empty()) {
                printf("  # %s", r.detail.c_str());
            }
            printf("\n");
        } else if (r.outcome == Outcome::Skip) {
            ++skipped;
            printf("ok %d - %s  # SKIP %s\n", n, s.name, r.detail.c_str());
        } else {
            ++failed;
            printf("not ok %d - %s  # %s\n", n, s.name, r.detail.c_str());
        }
    }
    printf("1..%d\n", total);
    printf("# %d passed, %d failed, %d skipped\n", passed, failed, skipped);

#if defined(_WIN32)
    WSACleanup();
#endif
    return failed == 0 ? 0 : 1;
}
