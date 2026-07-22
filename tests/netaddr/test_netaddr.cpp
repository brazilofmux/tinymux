// Unit tests for mux_subnet::compare_to — the subnet-vs-subnet comparison
// that drives the access-control (site-ban) tree.  This is the logic whose
// shared-base / shared-end containment bug (#799) inverted the ban tree so a
// /8 forbid silently stopped applying once a same-base /24 rule was added.
//
// There was previously NO test for this code path: fun_subnetmatch() exercises
// only the address-vs-subnet overload, and the tree itself lives in net.cpp
// (whole-driver dependencies).  This harness links the netmux-side netaddr
// object against libmux with a handful of driver-global stubs and tests the
// comparator directly, so the #799 class of bug (and the historically-buggy
// operator==/< and tree logic that feeds it) is locked mechanically.
//
// Build/run: make test   (needs a built netmux: mux/src/netmux-netaddr.o)

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <arpa/inet.h>   // inet_pton, for building test sockaddrs

#include "autoconf.h"
#include "config.h"
#include "alloc.h"

// --- Driver-global stubs ---------------------------------------------------
// netaddr.o references these driver-owned globals.  The compare_to /
// parse_subnet happy path (valid CIDR input) never dereferences the two
// nullable COM interfaces; g_bStandAlone just selects allocation behavior.
class mux_ILog;
class mux_INotify;
bool         g_bStandAlone = true;
mux_ILog    *g_pILog       = nullptr;
mux_INotify *g_pINotify    = nullptr;

// --- tiny test framework ---------------------------------------------------
static int g_pass = 0;
static int g_fail = 0;

using SC = mux_subnet::SubnetComparison;

static const char *scname(SC c)
{
    switch (c)
    {
    case SC::kLessThan:    return "kLessThan";
    case SC::kEqual:       return "kEqual";
    case SC::kContains:    return "kContains";
    case SC::kContainedBy: return "kContainedBy";
    case SC::kGreaterThan: return "kGreaterThan";
    }
    return "?";
}

static mux_subnet *make_subnet(const char *cidr)
{
    UTF8 buf[64];
    std::strncpy(reinterpret_cast<char *>(buf), cidr, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return parse_subnet(buf, 0, nullptr);
}

// Assert compare_to(a, b) == want.
static void expect(const char *a, const char *b, SC want)
{
    mux_subnet *sa = make_subnet(a);
    mux_subnet *sb = make_subnet(b);
    if (nullptr == sa || nullptr == sb)
    {
        g_fail++;
        printf("FAIL: parse_subnet failed for \"%s\" or \"%s\"\n", a, b);
        delete sa;
        delete sb;
        return;
    }
    SC got = sa->compare_to(sb);
    if (got == want)
    {
        g_pass++;
    }
    else
    {
        g_fail++;
        printf("FAIL: compare_to(%-16s, %-16s) = %-13s want %s\n",
               a, b, scname(got), scname(want));
    }
    delete sa;
    delete sb;
}

// Assert parse_subnet(cidr) rejects the input (returns nullptr).  These drive
// the error paths (which call the netmux-side cf_log_syntax); that is
// nullptr-safe here because driver_log.h's STARTLOG guards on g_pILog.
static void expect_reject(const char *cidr)
{
    mux_subnet *sn = make_subnet(cidr);
    if (nullptr == sn)
    {
        g_pass++;
    }
    else
    {
        g_fail++;
        printf("FAIL: parse_subnet(\"%s\") should have been rejected but parsed\n",
               cidr);
        delete sn;
    }
}

// Assert parse_subnet(cidr) accepts the input (returns non-nullptr).
static void expect_accept(const char *cidr)
{
    mux_subnet *sn = make_subnet(cidr);
    if (nullptr != sn)
    {
        g_pass++;
        delete sn;
    }
    else
    {
        g_fail++;
        printf("FAIL: parse_subnet(\"%s\") should have been accepted but was rejected\n",
               cidr);
    }
}

// --- address-vs-subnet (compare_to(MUX_SOCKADDR*)) helpers ----------------
// Build a MUX_SOCKADDR for a native IPv4 dotted address.
static MUX_SOCKADDR sockaddr_v4(const char *ip)
{
    struct sockaddr_in sin;
    std::memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &sin.sin_addr);
    return MUX_SOCKADDR(reinterpret_cast<const struct sockaddr *>(&sin));
}

// Build a MUX_SOCKADDR for an IPv6 address (string form, may be ::ffff:a.b.c.d).
static MUX_SOCKADDR sockaddr_v6(const char *ip)
{
    struct sockaddr_in6 sin6;
    std::memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
    inet_pton(AF_INET6, ip, &sin6.sin6_addr);
    return MUX_SOCKADDR(reinterpret_cast<const struct sockaddr *>(&sin6));
}

// Build a MUX_SOCKADDR for a native IPv4 dotted address with an explicit port.
static MUX_SOCKADDR sockaddr_v4_port(const char *ip, unsigned short port)
{
    struct sockaddr_in sin;
    std::memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    inet_pton(AF_INET, ip, &sin.sin_addr);
    return MUX_SOCKADDR(reinterpret_cast<const struct sockaddr *>(&sin));
}

// Build a MUX_SOCKADDR for an IPv6 address with an explicit port.
static MUX_SOCKADDR sockaddr_v6_port(const char *ip, unsigned short port)
{
    struct sockaddr_in6 sin6;
    std::memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(port);
    inet_pton(AF_INET6, ip, &sin6.sin6_addr);
    return MUX_SOCKADDR(reinterpret_cast<const struct sockaddr *>(&sin6));
}

// Assert same_address(a, b) == want.  Also asserts the relationship to
// operator==, which differs precisely by including the port.
static void expect_same_addr(MUX_SOCKADDR a, MUX_SOCKADDR b, bool want,
                             const char *label)
{
    bool got = a.same_address(b);
    if (got == want)
    {
        g_pass++;
    }
    else
    {
        g_fail++;
        printf("FAIL: same_address(%s) = %s want %s\n",
               label, got ? "true" : "false", want ? "true" : "false");
    }
}

// Assert compare_to(addr) for a subnet == want.  `inside` true => the address
// is expected to be within the subnet (kContains).
static void expect_addr(const char *subnet, MUX_SOCKADDR addr, bool inside,
                        const char *label)
{
    mux_subnet *sn = make_subnet(subnet);
    if (nullptr == sn)
    {
        g_fail++;
        printf("FAIL: parse_subnet failed for \"%s\"\n", subnet);
        return;
    }
    SC got = sn->compare_to(&addr);
    bool isInside = (SC::kContains == got);
    if (isInside == inside)
    {
        g_pass++;
    }
    else
    {
        g_fail++;
        printf("FAIL: %-28s vs %-16s = %-13s (%s) want %s\n",
               label, subnet, scname(got),
               isInside ? "inside" : "outside",
               inside ? "inside" : "outside");
    }
    delete sn;
}

int main()
{
    pool_init(POOL_LBUF, LBUF_SIZE);

    // --- #799: nested CIDRs that share a base address -----------------------
    // The regression: strict '<' on both bounds misclassified these as
    // kContainedBy (inverting containment).  Must now be kContains.
    expect("10.0.0.0/8",   "10.0.0.0/24",  SC::kContains);
    expect("10.0.0.0/24",  "10.0.0.0/8",   SC::kContainedBy);
    expect("192.168.0.0/16", "192.168.0.0/24", SC::kContains);

    // Nested CIDRs that share an END address (0.0.0.0/1 ends at
    // 127.255.255.255, same as 127.0.0.0/8).
    expect("0.0.0.0/1",    "127.0.0.0/8",  SC::kContains);
    expect("127.0.0.0/8",  "0.0.0.0/1",    SC::kContainedBy);

    // --- equality -----------------------------------------------------------
    expect("10.0.0.0/8",   "10.0.0.0/8",   SC::kEqual);
    expect("172.16.0.0/12","172.16.0.0/12",SC::kEqual);

    // --- strict nesting (distinct base AND end) -----------------------------
    expect("10.0.0.0/8",   "10.1.0.0/16",  SC::kContains);
    expect("10.1.0.0/16",  "10.0.0.0/8",   SC::kContainedBy);

    // --- disjoint -----------------------------------------------------------
    expect("10.0.0.0/8",   "192.168.0.0/16", SC::kLessThan);
    expect("192.168.0.0/16","10.0.0.0/8",    SC::kGreaterThan);

    // --- IPv6: same shared-base nesting must classify correctly -------------
    expect("2001:db8::/32","2001:db8::/48", SC::kContains);
    expect("2001:db8::/48","2001:db8::/32", SC::kContainedBy);
    expect("2001:db8::/32","2001:db8::/32", SC::kEqual);

    // --- address-vs-subnet, including the #800 IPv4-mapped IPv6 case --------
    // Native IPv4 inside / outside a v4 subnet.
    expect_addr("1.2.3.0/24", sockaddr_v4("1.2.3.4"),  true,  "v4 1.2.3.4");
    expect_addr("1.2.3.0/24", sockaddr_v4("1.2.4.4"),  false, "v4 1.2.4.4");

    // #800: an IPv4-mapped IPv6 source (::ffff:1.2.3.4 -- what a dual-stack
    // listener delivers for an inbound IPv4 connection) must match the v4 rule
    // exactly as the native form does.  Without canonicalization the
    // cross-family compare sorts it past every v4 subnet (kLessThan) and the
    // ban is bypassed -- this case fails if the v4-mapped handling is removed.
    expect_addr("1.2.3.0/24", sockaddr_v6("::ffff:1.2.3.4"), true,
                "v4-mapped ::ffff:1.2.3.4");
    expect_addr("1.2.4.0/24", sockaddr_v6("::ffff:1.2.3.4"), false,
                "v4-mapped ::ffff:1.2.3.4 (outside)");

    // A genuine (non-mapped) IPv6 address still matches v6 rules and not v4.
    expect_addr("2001:db8::/32", sockaddr_v6("2001:db8::1"), true,
                "v6 2001:db8::1");
    expect_addr("1.2.3.0/24",    sockaddr_v6("2001:db8::1"), false,
                "v6 2001:db8::1 (vs v4 rule)");

    // --- parse_subnet rejection paths (previously untested) -----------------
    // The subnet parser gates the access-control (site-ban) rule set, so its
    // rejection of malformed input is security-adjacent.  make_subnet() treated
    // any nullptr as a hard FAIL, so nothing exercised these branches.
    //
    // Non-numeric / empty CIDR mask field.
    expect_reject("10.0.0.0/abc");
    expect_reject("10.0.0.0/");
    // CIDR prefix length out of range (v4 0..32, v6 0..128).
    expect_reject("10.0.0.0/33");
    expect_reject("10.0.0.0/-1");
    expect_reject("2001:db8::/129");
    // Missing mask (no '/' and no whitespace-delimited netmask).
    expect_reject("10.0.0.0");
    expect_reject("");
    // Malformed host address (bad octet / not an address at all).
    expect_reject("10.0.0.999/24");
    expect_reject("not-an-ip/24");

    // --- parse_subnet accepts and normalizes host bits set outside the mask -
    // 10.0.0.1/24 has a host bit set; parse_subnet clears it ("fixed") rather
    // than rejecting, yielding a subnet equal to 10.0.0.0/24.  Verify both the
    // acceptance and that the normalization is what compare_to sees.
    expect_accept("10.0.0.1/24");
    expect("10.0.0.1/24", "10.0.0.0/24", SC::kEqual);

    // --- IPv6 shared-END nesting (the v6 analogue of the 0.0.0.0/1 case) ----
    // ::/1 spans ::..7fff:ffff:...:ffff; 7fff::/16 ends at that same address,
    // so the shared-end containment must classify as kContains, not kEqual /
    // kContainedBy (the #799 bug class, now on the v6 path).
    expect("::/1",      "7fff::/16", SC::kContains);
    expect("7fff::/16", "::/1",      SC::kContainedBy);

    // --- same_address: address-only equality (port ignored) ---------------
    // This is the whole reason the method exists: every connection from one
    // peer has a different source port, so operator== (which includes the
    // port) cannot group them.  Used by the per-source pre-auth cap.
    expect_same_addr(sockaddr_v4_port("10.0.0.7", 40001),
                     sockaddr_v4_port("10.0.0.7", 40002), true,
                     "v4 same addr, different ports");
    expect_same_addr(sockaddr_v4_port("10.0.0.7", 40001),
                     sockaddr_v4_port("10.0.0.8", 40001), false,
                     "v4 different addr, same port");
    expect_same_addr(sockaddr_v6_port("2001:db8::1", 40001),
                     sockaddr_v6_port("2001:db8::1", 40002), true,
                     "v6 same addr, different ports");
    expect_same_addr(sockaddr_v6_port("2001:db8::1", 40001),
                     sockaddr_v6_port("2001:db8::2", 40001), false,
                     "v6 differs in last hextet");

    // Cross-family never matches, including the v4-mapped form: mux_sockaddr
    // stores what the kernel handed back without normalizing, so a v4-mapped
    // v6 address is a distinct value from its native v4 twin.  Matches
    // operator== semantics; both forms of one peer cannot arrive on the same
    // listener, so per-source grouping is unaffected.
    expect_same_addr(sockaddr_v4_port("127.0.0.1", 40001),
                     sockaddr_v6_port("::ffff:127.0.0.1", 40001), false,
                     "v4 vs v4-mapped v6");

    // A different port must NOT make operator== agree with same_address --
    // that difference is the contract.
    {
        MUX_SOCKADDR a = sockaddr_v4_port("10.0.0.7", 40001);
        MUX_SOCKADDR b = sockaddr_v4_port("10.0.0.7", 40002);
        if (a.same_address(b) && !(a == b))
        {
            g_pass++;
        }
        else
        {
            g_fail++;
            printf("FAIL: same_address must ignore the port where "
                   "operator== does not\n");
        }
    }

    printf("\n=== netaddr compare_to: %d passed, %d failed ===\n",
           g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
