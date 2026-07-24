/*! \file websocket_test.cpp
 * \brief Standalone unit test for the WebSocket frame parser.
 *
 * Links against the real compiled ws_process_input (netmux-websocket.o) and
 * stubs the handful of netmux-layer callbacks it references, so it exercises
 * the actual parser rather than a copy.  Driven by websocket_test.sh.
 *
 * Focus: a zero-length payload frame (CLOSE / empty PING / empty TEXT) whose
 * frame ends exactly at a read boundary must still be dispatched.  With a
 * plain `while (p < end)` parse loop it was deferred to the next read, which
 * never comes for a CLOSE — the connection stalled.  See the comment at the
 * top of ws_process_input.
 */
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "interface.h"
#include "websocket.h"

#include <cstdio>
#include <string>
#include <vector>

// ---- stubs for netmux-layer symbols referenced by websocket.o ----
static std::vector<std::string> g_messages;
static int g_closed = 0;

void save_command(descriptor_data *, const UTF8 *cmd, size_t n) {
    g_messages.emplace_back(reinterpret_cast<const char *>(cmd), n);
}
void process_output(descriptor_data *, int) {}
void ganl_close_connection(descriptor_data *, int) { g_closed++; }
void welcome_user(descriptor_data *) {}
void queue_write_LEN(descriptor_data *, const UTF8 *, size_t) {}

// #1083 path in ws_queue_frame reads g_dc.output_limit and may log via
// g_pILog.  Provide definitions so the unit test links without the full
// driver/engine (libmux may not export these to this link line).
//
DRIVER_CONFIG g_dc = {};
mux_ILog *g_pILog = nullptr;
UTF8 *pool_alloc_lbuf(const UTF8 *, const UTF8 *, int) { return nullptr; }
void pool_free_lbuf(UTF8 *, const UTF8 *, int) {}
void LogName(dbref) {}

// descriptor_data embeds a mux_sockaddr; the parser never touches it, but the
// aggregate's default construction needs the ctor.  Stub it to avoid pulling
// in netaddr.o (which drags in global server state).
mux_sockaddr::mux_sockaddr() {}

// ---- test harness ----
static int g_pass = 0, g_fail = 0;
#define CHECK(desc, cond) do { \
    if (cond) g_pass++; else { g_fail++; printf("  FAIL: %s\n", desc); } } while (0)

static void reset(descriptor_data &d, ws_state &ws) {
    g_messages.clear(); g_closed = 0;
    d.output_queue.clear(); d.output_size = 0;
    ws = ws_state();
    d.ws = &ws;
}

// Feed bytes to the real parser, optionally split into fixed-size reads.
static void feed(descriptor_data &d, const std::vector<uint8_t> &b,
                 size_t chunk = 0) {
    if (chunk == 0) chunk = b.size() ? b.size() : 1;
    for (size_t off = 0; off < b.size(); off += chunk) {
        size_t n = (off + chunk <= b.size()) ? chunk : b.size() - off;
        ws_process_input(&d, reinterpret_cast<const char *>(b.data() + off), n);
        if (g_closed) break;  // mirror the real caller: stop after a close
    }
}

static int last_opcode(descriptor_data &d) {
    if (d.output_queue.empty()) return -1;
    return static_cast<uint8_t>(d.output_queue.back()[0]) & 0x0F;
}

int main() {
    descriptor_data d{};
    ws_state ws;

    // Client->server frames MUST be masked; mask = 00 00 00 00 leaves the
    // payload unchanged.  b1 = 0x80 | len.
    const uint8_t M0 = 0, M1 = 0, M2 = 0, M3 = 0;

    // 1. The bug: zero-payload CLOSE, whole frame in one read ending exactly
    //    at the boundary.  Must echo + close, not stall.
    reset(d, ws);
    feed(d, {0x88, 0x80, M0, M1, M2, M3});
    CHECK("zero CLOSE at boundary: closed", g_closed == 1);
    CHECK("zero CLOSE at boundary: echo queued", !d.output_queue.empty());
    CHECK("zero CLOSE at boundary: echo opcode CLOSE", last_opcode(d) == 0x8);

    // 2. Same CLOSE delivered as one 6-byte read (header+mask, no payload).
    reset(d, ws);
    feed(d, {0x88, 0x80, M0, M1, M2, M3}, 6);
    CHECK("zero CLOSE one read: closed", g_closed == 1);

    // 3. Empty PING at boundary must produce a PONG, not stall.
    reset(d, ws);
    feed(d, {0x89, 0x80, M0, M1, M2, M3});
    CHECK("empty PING at boundary: pong queued", last_opcode(d) == 0xA);
    CHECK("empty PING at boundary: not closed", g_closed == 0);

    // 4. Regression: normal masked TEXT "hi" delivered whole.
    reset(d, ws);
    feed(d, {0x81, 0x82, M0, M1, M2, M3, 'h', 'i'});
    CHECK("TEXT hi: one message", g_messages.size() == 1);
    CHECK("TEXT hi: payload", g_messages.size() == 1 && g_messages[0] == "hi");

    // 5. Regression: TEXT "hi" split byte-by-byte (the completing byte
    //    dispatches in the same pass — non-zero payloads were never affected).
    reset(d, ws);
    feed(d, {0x81, 0x82, M0, M1, M2, M3, 'h', 'i'}, 1);
    CHECK("TEXT hi byte-split: one message", g_messages.size() == 1);
    CHECK("TEXT hi byte-split: payload", g_messages.size() == 1 && g_messages[0] == "hi");

    // 6. Empty TEXT message at boundary -> dispatch an empty command.
    reset(d, ws);
    feed(d, {0x81, 0x80, M0, M1, M2, M3});
    CHECK("empty TEXT at boundary: one message", g_messages.size() == 1);
    CHECK("empty TEXT at boundary: empty payload",
          g_messages.size() == 1 && g_messages[0].empty());

    // 7. #1081: unmasked client frames must fail the connection (RFC 6455 §5.1).
    //    b1 without 0x80 mask bit: empty TEXT, unmasked.
    reset(d, ws);
    feed(d, {0x81, 0x00});  // FIN+TEXT, unmasked, len 0
    CHECK("unmasked TEXT: closed", g_closed == 1);
    CHECK("unmasked TEXT: no message", g_messages.empty());

    printf("\nws parser: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
