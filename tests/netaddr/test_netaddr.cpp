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

    printf("\n=== netaddr compare_to: %d passed, %d failed ===\n",
           g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
