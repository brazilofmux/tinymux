/*! \file netaddr.cpp
 * \brief Network address utilities.
 *
 * IPv4/IPv6 address classes, subnet parsing, and getaddrinfo/getnameinfo
 * compatibility wrappers for platforms lacking native support.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#if defined(HAVE_IN_ADDR)
typedef struct
{
    int    nShift;
    uint32_t maxValue;
    size_t maxOctLen;
    size_t maxDecLen;
    size_t maxHexLen;
} DECODEIPV4;

static bool DecodeN(const int nType, size_t len, const UTF8 *p, in_addr_t *pu32)
{
    static DECODEIPV4 decode_IPv4_table[4] =
    {
        { 8,         255UL,  3,  3, 2 },
        { 16,      65535UL,  6,  5, 4 },
        { 24,   16777215UL,  8,  8, 6 },
        { 32, 4294967295UL, 11, 10, 8 }
    };

    *pu32  = (*pu32 << decode_IPv4_table[nType].nShift) & 0xFFFFFFFFUL;
    if (len == 0)
    {
        return false;
    }
    in_addr_t ul = 0;
    in_addr_t ul2;
    if (  len >= 3
       && p[0] == '0'
       && (  'x' == p[1]
          || 'X' == p[1]))
    {
        // Hexadecimal Path
        //
        // Skip the leading zeros.
        //
        p += 2;
        len -= 2;
        while (*p == '0' && len)
        {
            p++;
            len--;
        }
        if (len > decode_IPv4_table[nType].maxHexLen)
        {
            return false;
        }
        while (len)
        {
            const auto ch = *p;
            ul2 = ul;
            ul  = (ul << 4) & 0xFFFFFFFFUL;
            if (ul < ul2)
            {
                // Overflow
                //
                return false;
            }
            if ('0' <= ch && ch <= '9')
            {
                ul |= ch - '0';
            }
            else if ('A' <= ch && ch <= 'F')
            {
                ul |= ch - 'A';
            }
            else if ('a' <= ch && ch <= 'f')
            {
                ul |= ch - 'a';
            }
            else
            {
                return false;
            }
            p++;
            len--;
        }
    }
    else if (len >= 1 && p[0] == '0')
    {
        // Octal Path
        //
        // Skip the leading zeros.
        //
        p++;
        len--;
        while (*p == '0' && len)
        {
            p++;
            len--;
        }
        if (len > decode_IPv4_table[nType].maxOctLen)
        {
            return false;
        }
        while (len)
        {
            const auto ch = *p;
            ul2 = ul;
            ul  = (ul << 3) & 0xFFFFFFFFUL;
            if (ul < ul2)
            {
                // Overflow
                //
                return false;
            }
            if ('0' <= ch && ch <= '7')
            {
                ul |= ch - '0';
            }
            else
            {
                return false;
            }
            p++;
            len--;
        }
    }
    else
    {
        // Decimal Path
        //
        if (len > decode_IPv4_table[nType].maxDecLen)
        {
            return false;
        }
        while (len)
        {
            const auto ch = *p;
            ul2 = ul;
            ul  = (ul * 10) & 0xFFFFFFFFUL;
            if (ul < ul2)
            {
                // Overflow
                //
                return false;
            }
            ul2 = ul;
            if ('0' <= ch && ch <= '9')
            {
                ul += ch - '0';
            }
            else
            {
                return false;
            }
            if (ul < ul2)
            {
                // Overflow
                //
                return false;
            }
            p++;
            len--;
        }
    }
    if (ul > decode_IPv4_table[nType].maxValue)
    {
        return false;
    }
    *pu32 |= ul;
    return true;
}

// ---------------------------------------------------------------------------
// MakeCanonicalIPv4: inet_addr() does not do reasonable checking for sane
// syntax on all platforms. On certain operating systems, if passed less than
// four octets, it will cause a segmentation violation. Furthermore, there is
// confusion between return values for valid input "255.255.255.255" and
// return values for invalid input (INADDR_NONE as -1). To overcome these
// problems, it appears necessary to re-implement inet_addr() with a different
// interface.
//
// n8.n8.n8.n8  Class A format. 0 <= n8 <= 255.
//
// Supported Berkeley IP formats:
//
//    n8.n8.n16  Class B 128.net.host format. 0 <= n16 <= 65535.
//    n8.n24     Class A net.host format. 0 <= n24 <= 16777215.
//    n32        Single 32-bit number. 0 <= n32 <= 4294967295.
//
// Each element may be expressed in decimal, octal or hexadecimal. '0' is the
// octal prefix. '0x' or '0X' is the hexadecimal prefix. Otherwise the number
// is taken as decimal.
//
//    08  Octal
//    0x8 Hexadecimal
//    0X8 Hexadecimal
//    8   Decimal
//
bool make_canonical_IPv4(const UTF8 *str, in_addr_t *pnIP)
{
    *pnIP = 0;
    if (!str)
    {
        return false;
    }

    // Skip leading spaces.
    //
    auto q = str;
    while (*q == ' ')
    {
        q++;
    }

    const auto* p = reinterpret_cast<UTF8 const *>(strchr(reinterpret_cast<char const *>(q), '.'));
    auto n = 0;
    while (p)
    {
        // Decode
        //
        n++;
        if (n > 3)
        {
            return false;
        }
        if (!DecodeN(0, p-q, q, pnIP))
        {
            return false;
        }
        q = p + 1;
        p = reinterpret_cast<UTF8 const *>(strchr(reinterpret_cast<char const *>(q), '.'));
    }

    // Decode last element.
    //
    const auto len = strlen(reinterpret_cast<char const *>(q));
    return DecodeN(3 - n, len, q, pnIP);
}

// Given a host-ordered mask, this function will determine whether it is a
// valid one. Valid masks consist of a N-bit sequence of '1' bits followed by
// a (32-N)-bit sequence of '0' bits, where N is 0 to 32.
//
bool mux_in_addr::isValidMask(int *pnLeadingBits) const
{
    in_addr_t test = 0xFFFFFFFFUL;
    const in_addr_t mask = m_ia.s_addr;
    for (auto i = 0; i <= 32; i++)
    {
        if (mask == test)
        {
            *pnLeadingBits = i;
            return true;
        }
        test = (test << 1) & 0xFFFFFFFFUL;
    }
    return false;
}

void mux_in_addr::makeMask(const int num_leading_bits)
{
    // << [0,31] works. << 32 is problematic on some systems.
    //
    in_addr_t mask = 0;
    if (num_leading_bits > 0)
    {
        mask = (0xFFFFFFFFUL << (32 - num_leading_bits)) & 0xFFFFFFFFUL;
    }
    m_ia.s_addr = htonl(mask);
}
#endif

#if defined(HAVE_IN6_ADDR)
bool mux_in6_addr::isValidMask(int *pnLeadingBits) const
{
    const unsigned char allones = 0xFF;
    unsigned char mask = 0;
    size_t i;
    for (i = 0; i < sizeof(m_ia6.s6_addr)/sizeof(m_ia6.s6_addr[0]); i++)
    {
        mask = m_ia6.s6_addr[i];
        if (allones != mask)
        {
            break;
        }
    }

    int num_leading_bits = 8*i;

    if (i < sizeof(m_ia6.s6_addr)/sizeof(m_ia6.s6_addr[0]))
    {
        if (0 != mask)
        {
            auto found = false;
            auto test = allones;
            for (auto j = 0; j <= 8 && !found; j++)
            {
                if (mask == test)
                {
                    num_leading_bits += j;
                    found = true;
                    break;
                }
                test = (test << 1) & allones;
            }

            if (!found)
            {
                return false;
            }
            i++;
        }

        for ( ; i < sizeof(m_ia6.s6_addr)/sizeof(m_ia6.s6_addr[0]); i++)
        {
            mask = m_ia6.s6_addr[i];
            if (0 != mask)
            {
                return false;
            }
        }
    }
    *pnLeadingBits = num_leading_bits;
    return true;
}

void mux_in6_addr::makeMask(const int num_leading_bits)
{
    constexpr unsigned char allones = 0xFF;
    memset(&m_ia6, 0, sizeof(m_ia6));
    const size_t num_bytes = num_leading_bits / 8;
    for (size_t i = 0; i < num_bytes; i++)
    {
        m_ia6.s6_addr[i] = allones;
    }

    if (num_bytes < sizeof(m_ia6.s6_addr)/sizeof(m_ia6.s6_addr[0]))
    {
        const size_t num_leftover_bits = num_leading_bits % 8;
        if (num_leftover_bits > 0)
        {
            m_ia6.s6_addr[num_bytes] = (allones << (8 - num_leftover_bits)) & allones;
        }
    }
}
#endif

mux_subnet::~mux_subnet()
{
    delete m_iaBase;
    delete m_iaMask;
    delete m_iaEnd;
}

bool mux_subnet::listinfo(UTF8 *sAddress, int *pnLeadingBits) const
{
    // Base Address
    //
    mux_sockaddr msa;
    msa.set_address(m_iaBase);
    msa.ntop(sAddress, LBUF_SIZE);

    // Leading significant bits
    //
    *pnLeadingBits = m_iLeadingBits;

    return true;
}

mux_subnet::SubnetComparison mux_subnet::compare_to(mux_subnet *t) const
{
    if (*(t->m_iaEnd) < *m_iaBase)
    {
        // this > t
        //
        return SubnetComparison::kGreaterThan;
    }
    else if (*m_iaEnd < *(t->m_iaBase))
    {
        // this < t
        //
        return SubnetComparison::kLessThan;
    }
    else if (  *m_iaBase < *(t->m_iaBase)
            && *(t->m_iaEnd) < *m_iaEnd)
    {
        // this contains t
        //
        return SubnetComparison::kContains;
    }
    else if (  *m_iaBase == *(t->m_iaBase)
            && m_iLeadingBits == t->m_iLeadingBits)
    {
        // this == t
        //
        return SubnetComparison::kEqual;
    }
    else
    {
        // this is contained by t
        //
        return SubnetComparison::kContainedBy;
    }
}

mux_subnet::SubnetComparison mux_subnet::compare_to(MUX_SOCKADDR *msa) const
{
    mux_addr *ma = nullptr;
    switch (msa->Family())
    {
#if defined(HAVE_IN_ADDR)
    case AF_INET:
        {
            struct in_addr ia{};
            msa->get_address(&ia);
            ma = static_cast<mux_addr *>(new mux_in_addr(&ia));
        }
        break;
#endif

#if defined(HAVE_IN6_ADDR)
    case AF_INET6:
        {
            struct in6_addr ia6{};
            msa->get_address(&ia6);
            ma = static_cast<mux_addr *>(new mux_in6_addr(&ia6));
        }
        break;
#endif
    default:
        return SubnetComparison::kGreaterThan;
    }

    mux_subnet::SubnetComparison fComp;
    if (*ma < *m_iaBase)
    {
        // this > t
        //
        fComp = SubnetComparison::kGreaterThan;
    }
    else if (*m_iaEnd < *ma)
    {
        // this < t
        //
        fComp = SubnetComparison::kLessThan;
    }
    else
    {
        // this contains t
        //
        fComp = SubnetComparison::kContains;
    }
    delete ma;
    return fComp;
}

mux_subnet *parse_subnet(UTF8 *str, const dbref player, UTF8 *cmd)
{
    mux_addr *mux_address_mask = nullptr;
    mux_addr *mux_address_base = nullptr;
    mux_addr *mux_address_end  = nullptr;
    auto num_leading_bits = 0;

    MUX_ADDRINFO hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICHOST|AI_NUMERICSERV;

    int n;
    in_addr_t net_address_bits;
    MUX_ADDRINFO *servinfo;

    UTF8 *addr_txt;
    auto mask_txt = reinterpret_cast<UTF8 *>(strchr(reinterpret_cast<char *>(str), '/'));
    if (nullptr == mask_txt)
    {
        // Standard IP range and netmask notation.
        //
        string_token st(str, T(" \t=,"));
        addr_txt = st.parse();
        if (nullptr != addr_txt)
        {
            mask_txt = st.parse();
        }

        if (  nullptr == addr_txt
           || '\0' == *addr_txt
           || nullptr == mask_txt
           || '\0' == *mask_txt)
        {
            cf_log_syntax(player, cmd, T("Missing host address or mask."));
            return nullptr;
        }

        n = 0;
        if (0 == mux_getaddrinfo(mask_txt, nullptr, &hints, &servinfo))
        {
            for (auto ai = servinfo; nullptr != ai; ai = ai->ai_next)
            {
                delete mux_address_mask;
                switch (ai->ai_family)
                {
#if defined(HAVE_SOCKADDR_IN) && defined(HAVE_IN_ADDR)
                case AF_INET:
                    {
                        const auto sai = reinterpret_cast<struct sockaddr_in *>(ai->ai_addr);
                        mux_address_mask = static_cast<mux_addr *>(new mux_in_addr(&sai->sin_addr));
                    }
                    break;
#endif
#if defined(HAVE_SOCKADDR_IN6) && defined(HAVE_IN6_ADDR)
                case AF_INET6:
                    {
                        const auto sai6 = reinterpret_cast<struct sockaddr_in6 *>(ai->ai_addr);
                        mux_address_mask = static_cast<mux_addr *>(new mux_in6_addr(&sai6->sin6_addr));
                    }
                    break;
#endif
                default:
                    return nullptr;
                }
                n++;
            }
            mux_freeaddrinfo(servinfo);
        }
#if defined(HAVE_SOCKADDR_IN) && defined(HAVE_IN_ADDR)
        else if (make_canonical_IPv4(mask_txt, &net_address_bits))
        {
            delete mux_address_mask;
            mux_address_mask = static_cast<mux_addr *>(new mux_in_addr(net_address_bits));
            n++;
        }
#endif

        if (  1 != n
           || !mux_address_mask->isValidMask(&num_leading_bits))
        {
            cf_log_syntax(player, cmd, T("Malformed mask address: %s"), mask_txt);
            delete mux_address_mask;
            return nullptr;
        }
    }
    else
    {
        // RFC 1517, 1518, 1519, 1520: CIDR IP prefix notation
        //
        addr_txt = str;
        *mask_txt++ = '\0';
        if (!is_integer(mask_txt, nullptr))
        {
            cf_log_syntax(player, cmd, T("Mask field (%s) in CIDR IP prefix is not numeric."), mask_txt);
            return nullptr;
        }

        num_leading_bits = mux_atol(mask_txt);
    }

    n = 0;
    if (0 == mux_getaddrinfo(addr_txt, nullptr, &hints, &servinfo))
    {
        for (MUX_ADDRINFO *ai = servinfo; nullptr != ai; ai = ai->ai_next)
        {
            delete mux_address_base;
            switch (ai->ai_family)
            {
#if defined(HAVE_SOCKADDR_IN) && defined(HAVE_IN_ADDR)
            case AF_INET:
                {
                    const auto sai = reinterpret_cast<struct sockaddr_in *>(ai->ai_addr);
                    mux_address_base = static_cast<mux_addr *>(new mux_in_addr(&sai->sin_addr));
                }
                break;
#endif
#if defined(HAVE_SOCKADDR_IN6) &&  defined(HAVE_IN6_ADDR)
            case AF_INET6:
                {
                    const auto sai6 = reinterpret_cast<struct sockaddr_in6 *>(ai->ai_addr);
                    mux_address_base = static_cast<mux_addr *>(new mux_in6_addr(&sai6->sin6_addr));
                }
                break;
#endif
            default:
                delete mux_address_mask;
                return nullptr;
            }
            n++;
        }
        mux_freeaddrinfo(servinfo);
    }
#if defined(HAVE_IN_ADDR)
    else if (make_canonical_IPv4(addr_txt, &net_address_bits))
    {
        delete mux_address_base;
        mux_address_base = static_cast<mux_addr *>(new mux_in_addr(net_address_bits));
        n++;
    }
#endif

    if (1 != n)
    {
        cf_log_syntax(player, cmd, T("Malformed host address: %s"), addr_txt);
        delete mux_address_mask;
        delete mux_address_base;
        return nullptr;
    }

    if (nullptr == mux_address_mask)
    {
        bool fOutOfRange = false;
        switch (mux_address_base->getFamily())
        {
#if defined(HAVE_IN_ADDR)
        case AF_INET:
            mux_address_mask = static_cast<mux_addr *>(new mux_in_addr());
            if (  num_leading_bits < 0
               || 32 < num_leading_bits)
            {
                fOutOfRange = true;
            }
            break;
#endif
#if defined(HAVE_IN6_ADDR)
        case AF_INET6:
            mux_address_mask = static_cast<mux_addr *>(new mux_in6_addr());
            if (  num_leading_bits < 0
               || 128 < num_leading_bits)
            {
                fOutOfRange = true;
            }
            break;
#endif
        default:
            return nullptr;
        }

        if (fOutOfRange)
        {
            cf_log_syntax(player, cmd, T("Mask bits (%d) in CIDR IP prefix out of range."), num_leading_bits);
            return nullptr;
        }
        mux_address_mask->makeMask(num_leading_bits);
    }
    else if (mux_address_base->getFamily() != mux_address_mask->getFamily())
    {
        cf_log_syntax(player, cmd, T("Mask type is not compatible with address type: %s %s"), addr_txt, mask_txt);
        delete mux_address_mask;
        delete mux_address_base;
        return nullptr;
    }

    if (mux_address_base->clearOutsideMask(*mux_address_mask))
    {
        // The given subnet address contains 'one' bits which are outside the given subnet mask. If we don't clear these bits, they
        // will interfere with the subnet tests in site_check. The subnet spec would be defunct and useless.
        //
        cf_log_syntax(player, cmd, T("Non-zero host address bits outside the subnet mask (fixed): %s %s"), addr_txt, mask_txt);
    }

    delete mux_address_end;
    mux_address_end = mux_address_base->calculateEnd(*mux_address_mask);

    const auto msn = new mux_subnet();
    msn->m_iaBase = mux_address_base;
    msn->m_iaMask = mux_address_mask;
    msn->m_iaEnd = mux_address_end;
    msn->m_iLeadingBits = num_leading_bits;
    return msn;
}

#if !defined(HAVE_GETADDRINFO) && defined(HAVE_IN_ADDR)
static struct addrinfo *gai_addrinfo_new(const int socktype, const UTF8 *canonical, const struct in_addr addr, const unsigned short port)
{
    const auto ai = static_cast<struct addrinfo *>(MEMALLOC(sizeof(struct addrinfo)));
    if (nullptr == ai)
    {
        return nullptr;
    }
    ai->ai_addr = static_cast<sockaddr *>(MEMALLOC(sizeof(struct sockaddr_in)));
    if (nullptr == ai->ai_addr)
    {
        free(ai);
        return nullptr;
    }
    ai->ai_next = nullptr;
    if (nullptr == canonical)
    {
        ai->ai_canonname = nullptr;
    }
    else
    {
        ai->ai_canonname = reinterpret_cast<char *>(StringClone(canonical));
        if (nullptr == ai->ai_canonname)
        {
            mux_freeaddrinfo(ai);
            return nullptr;
        }
    }
    memset(ai->ai_addr, 0, sizeof(struct sockaddr_in));
    ai->ai_flags = 0;
    ai->ai_family = AF_INET;
    ai->ai_socktype = socktype;
    ai->ai_protocol = (socktype == SOCK_DGRAM) ? IPPROTO_UDP : IPPROTO_TCP;
    ai->ai_addrlen = sizeof(struct sockaddr_in);
    reinterpret_cast<struct sockaddr_in *>(ai->ai_addr)->sin_family = AF_INET;
    reinterpret_cast<struct sockaddr_in *>(ai->ai_addr)->sin_addr = addr;
    reinterpret_cast<struct sockaddr_in *>(ai->ai_addr)->sin_port = htons(port);
    return ai;
}

static bool convert_service(const UTF8 *string, long *result)
{
    if ('\0' == *string)
    {
        return false;
    }
    *result = mux_atol(string);
    return *result >= 0;
}

static int gai_service(const UTF8 *servname, int flags, int *type, unsigned short *port)
{
    long value;
    if (convert_service(servname, &value))
    {
        if (value > (1L << 16) - 1)
        {
            return EAI_SERVICE;
        }
        *port = static_cast<unsigned short>(value);
    }
    else
    {
        if (flags & AI_NUMERICSERV)
        {
            return EAI_NONAME;
        }
        const UTF8 *protocol;
        if (0 != *type)
            protocol = (SOCK_DGRAM == *type) ? T("udp") : T("tcp");
        else
            protocol = nullptr;

        struct servent *servent = getservbyname(reinterpret_cast<const char*>(servname), reinterpret_cast<const char*>(protocol));
        if (nullptr == servent)
        {
            return EAI_NONAME;
        }
        if (strcmp(servent->s_proto, "udp") == 0)
        {
            *type = SOCK_DGRAM;
        }
        else if (strcmp(servent->s_proto, "tcp") == 0)
        {
            *type = SOCK_STREAM;
        }
        else
        {
            return EAI_SERVICE;
        }
        *port = htons(servent->s_port);
    }
    return 0;
}

static int gai_lookup(const UTF8 *nodename, const int flags, const int socktype, const unsigned short port, struct addrinfo **res)
{
    struct addrinfo *ai;
    struct in_addr addr{};
    const UTF8 *canonical;

    in_addr_t address_bits;
    if (make_canonical_IPv4(nodename, &address_bits))
    {
        addr.s_addr = address_bits;
        canonical = (flags & AI_CANONNAME) ? nodename : nullptr;
        ai = gai_addrinfo_new(socktype, canonical, addr, port);
        if (nullptr == ai)
        {
            return EAI_MEMORY;
        }
        *res = ai;
        return 0;
    }
    else
    {
        if (flags & AI_NUMERICHOST)
        {
            return EAI_NONAME;
        }
        const auto host = gethostbyname(reinterpret_cast<const char *>(nodename));
        if (nullptr == host)
        {
            switch (h_errno)
            {
            case HOST_NOT_FOUND:
                return EAI_NONAME;
            case TRY_AGAIN:
            case NO_DATA:
                return EAI_AGAIN;
            default:
                return EAI_FAIL;
            }
        }
        if (nullptr == host->h_addr_list[0])
        {
            return EAI_FAIL;
        }
        if (flags & AI_CANONNAME)
        {
            if (nullptr != host->h_name)
            {
                canonical = reinterpret_cast<UTF8 *>(host->h_name);
            }
            else
            {
                canonical = nodename;
            }
        }
        else
        {
            canonical = nullptr;
        }
        struct addrinfo *first = nullptr;
        struct addrinfo *prev = nullptr;
        for (auto i = 0; host->h_addr_list[i] != nullptr; i++)
        {
            if (host->h_length != sizeof(addr))
            {
                mux_freeaddrinfo(first);
                return EAI_FAIL;
            }
            memcpy(&addr, host->h_addr_list[i], sizeof(addr));
            ai = gai_addrinfo_new(socktype, canonical, addr, port);
            if (nullptr == ai)
            {
                mux_freeaddrinfo(first);
                return EAI_MEMORY;
            }
            if (first == nullptr)
            {
                first = ai;
                prev = ai;
            }
            else
            {
                prev->ai_next = ai;
                prev = ai;
            }
        }
        *res = first;
        return 0;
    }
}

#endif

int mux_getaddrinfo(const UTF8 *node, const UTF8 *service, const MUX_ADDRINFO *hints, MUX_ADDRINFO **res)
{
#if defined(HAVE_GETADDRINFO)
    return getaddrinfo((const char *)node, (const char *)service, hints, res);
#elif !defined(HAVE_GETADDRINFO) && defined(HAVE_IN_ADDR)
    unsigned short port;

    int flags;
    int socktype;
    if (nullptr != hints)
    {
        flags = hints->ai_flags;
        socktype = hints->ai_socktype;
        if ((flags & (AI_PASSIVE|AI_CANONNAME|AI_NUMERICHOST|AI_NUMERICSERV|AI_ADDRCONFIG|AI_V4MAPPED)) != flags)
        {
            return EAI_BADFLAGS;
        }

        if (  hints->ai_family != AF_UNSPEC
           && hints->ai_family != AF_INET)
        {
            return EAI_FAMILY;
        }

        if (  0 != socktype
           && SOCK_STREAM != socktype
           && SOCK_DGRAM != socktype)
        {
            return EAI_SOCKTYPE;
        }

        if (0 != hints->ai_protocol)
        {
            if (  IPPROTO_TCP != hints->ai_protocol
               && IPPROTO_UDP != hints->ai_protocol)
            {
                return EAI_SOCKTYPE;
            }
        }
    }
    else
    {
        flags = 0;
        socktype = 0;
    }

    if (nullptr == service)
    {
        port = 0;
    }
    else
    {
        const auto status = gai_service(service, flags, &socktype, &port);
        if (0 != status)
        {
            return status;
        }
    }
    if (node != nullptr)
    {
        return gai_lookup(node, flags, socktype, port, res);
    }
    else
    {
        in_addr addr;
        if (nullptr == service)
        {
            return EAI_NONAME;
        }
        if ((flags & AI_PASSIVE) == AI_PASSIVE)
        {
            addr.s_addr = INADDR_ANY;
        }
        else
        {
            addr.s_addr = htonl(0x7f000001UL);
        }
        struct addrinfo *ai = gai_addrinfo_new(socktype, nullptr, addr, port);
        if (nullptr == ai)
        {
            return EAI_MEMORY;
        }
        *res = ai;
        return 0;
    }
#endif
}

void mux_freeaddrinfo(MUX_ADDRINFO *res)
{
#if defined(HAVE_GETADDRINFO)
    freeaddrinfo(res);
#else
    while (nullptr != res)
    {
        const auto next = res->ai_next;
        if (nullptr != res->ai_addr)
        {
            free(res->ai_addr);
        }
        if (nullptr != res->ai_canonname)
        {
            free(res->ai_canonname);
        }
        free(res);
        res = next;
    }
#endif
}

#if !defined(HAVE_GETNAMEINFO)
static bool try_name(const char *name, UTF8 *host, size_t hostlen, int *status)
{
    if (nullptr == strchr(static_cast<const char *>(name), '.'))
    {
        return false;
    }
    UTF8 *bufc = host;
    safe_str(reinterpret_cast<const UTF8 *>(name), host, &bufc);
    *bufc = '\0';
    return true;
}

static int lookup_hostname(const struct in_addr *addr, UTF8 *host, size_t hostlen, int flags)
{
    UTF8 *bufc;
#ifdef HAVE_GETHOSTBYADDR
    if (0 == (flags & NI_NUMERICHOST))
    {
        auto he = gethostbyaddr(reinterpret_cast<const char *>(addr), sizeof(struct in_addr), AF_INET);
        if (nullptr == he)
        {
            if (flags & NI_NAMEREQD)
            {
                return EAI_NONAME;
            }
        }
        else
        {
            int status;
            if (try_name(he->h_name, host, hostlen, &status))
            {
                return status;
            }

            for (char **alias = he->h_aliases; nullptr != *alias; alias++)
            {
                if (try_name(*alias, host, hostlen, &status))
                {
                    return status;
                }
            }
        }
    }
#endif

    bufc = host;
    safe_str(reinterpret_cast<UTF8 *>(inet_ntoa(*addr)), host, &bufc);
    *bufc = '\0';
    return 0;
}

static int lookup_servicename(const unsigned short port, UTF8 *serv, size_t servlen, const int flags)
{
    UTF8 *bufc;
    if (0 == (flags & NI_NUMERICSERV))
    {
        const auto protocol = (flags & NI_DGRAM) ? "udp" : "tcp";
        const auto srv = getservbyport(htons(port), protocol);
        if (nullptr != srv)
        {
            bufc = serv;
            safe_str(reinterpret_cast<UTF8 *>(srv->s_name), serv, &bufc);
            *bufc = '\0';
            return 0;
        }
    }

    bufc = serv;
    safe_ltoa(port, serv, &bufc);
    *bufc = '\0';
    return 0;
}
#endif

int mux_getnameinfo(const MUX_SOCKADDR *msa, UTF8 *host, const size_t hostlen, UTF8 *serv, const size_t servlen, const int flags)
{
#if defined(HAVE_GETNAMEINFO)
    return getnameinfo(msa->saro(), msa->salen(), reinterpret_cast<char *>(host), hostlen, reinterpret_cast<char *>(serv), servlen, flags);
#else
    if (  (  nullptr == host
          || hostlen <= 0)
       && (  nullptr == serv
          || servlen <= 0))
    {
        return EAI_NONAME;
    }

    if (AF_INET != msa->Family())
    {
        return EAI_FAMILY;
    }

    if (  nullptr != host
       && 0 < hostlen)
    {
        const auto status = lookup_hostname(&msa->sairo()->sin_addr, host, hostlen, flags);
        if (0 != status)
        {
            return status;
        }
    }

    if (  nullptr != serv
       && 0 < servlen)
    {
        const auto port = msa->port();
        return lookup_servicename(port, serv, servlen, flags);
    }
    return 0;
#endif
}

unsigned short mux_sockaddr::port() const
{
    switch (u.sa.sa_family)
    {
#if defined(HAVE_SOCKADDR_IN)
    case AF_INET:
        return ntohs(u.sai.sin_port);
#endif

#if defined(HAVE_SOCKADDR_IN6)
    case AF_INET6:
        return ntohs(u.sai6.sin6_port);
#endif

    default:
        return 0;
    }
}

struct sockaddr *mux_sockaddr::sa()
{
    return &u.sa;
}

size_t mux_sockaddr::maxaddrlen() const
{
    return sizeof(u);
}

void mux_sockaddr::ntop(UTF8 *sAddress, size_t len) const
{
    if (0 != mux_getnameinfo(this, sAddress, len, nullptr, 0, NI_NUMERICHOST|NI_NUMERICSERV))
    {
        sAddress[0] = '\0';
    }
}

void mux_sockaddr::set_address(mux_addr *ma)
{
    switch (ma->getFamily())
    {
#if defined(HAVE_IN_ADDR)
    case AF_INET:
        {
            const auto mia = dynamic_cast<mux_in_addr *>(ma);
            u.sai.sin_family = AF_INET;
            u.sai.sin_addr = mia->m_ia;
        }
        break;
#endif
#if defined(HAVE_IN6_ADDR)
    case AF_INET6:
        {
            const auto mia6 = dynamic_cast<mux_in6_addr *>(ma);
            u.sai6.sin6_family = AF_INET6;
            u.sai6.sin6_addr = mia6->m_ia6;
        }
        break;
#endif
    }
}

void mux_sockaddr::Clear()
{
    memset(&u, 0, sizeof(u));
}

#if defined(HAVE_SOCKADDR_IN)
struct sockaddr_in *mux_sockaddr::sai()
{
    return &u.sai;
}

struct sockaddr_in const *mux_sockaddr::sairo() const
{
    return &u.sai;
}
#endif

unsigned short mux_sockaddr::Family() const
{
    return u.sa.sa_family;
}

struct sockaddr const *mux_sockaddr::saro() const
{
    return &u.sa;
}

size_t mux_sockaddr::salen() const
{
    switch (u.sa.sa_family)
    {
#if defined(HAVE_SOCKADDR_IN)
    case AF_INET:
        return sizeof(u.sai);
#endif
#if defined(HAVE_SOCKADDR_IN6)
    case AF_INET6:
        return sizeof(u.sai6);
#endif

    default:
        return 0;
    }
}

mux_sockaddr::mux_sockaddr()
{
    Clear();
}

mux_sockaddr::mux_sockaddr(const sockaddr *sa)
{
    switch (sa->sa_family)
    {
#if defined(HAVE_SOCKADDR_IN)
    case AF_INET:
        memcpy(&u.sai, sa, sizeof(u.sai));
        break;
#endif
#if defined(HAVE_SOCKADDR_IN6)
    case AF_INET6:
        memcpy(&u.sai6, sa, sizeof(u.sai6));
        break;
#endif
    }
}

bool mux_sockaddr::operator==(const mux_sockaddr &it) const
{
    if (it.u.sa.sa_family != u.sa.sa_family)
    {
        return false;
    }

    switch (u.sa.sa_family)
    {
#if defined(HAVE_SOCKADDR_IN)
    case AF_INET:
        if (  memcmp(&it.u.sai.sin_addr, &u.sai.sin_addr, sizeof(u.sai.sin_addr)) == 0
           && it.u.sai.sin_family == u.sai.sin_family
           && it.u.sai.sin_port == u.sai.sin_port)
        {
            return true;
        }
        break;
#endif
#if defined(HAVE_SOCKADDR_IN6)
    case AF_INET6:
        // Intentionally ignoring sin6_flowinfo, sin6_scopeid, and others for now.
        //
        if (  memcmp(&it.u.sai6.sin6_addr, &u.sai6.sin6_addr, sizeof(u.sai6.sin6_family)) == 0
           && it.u.sai6.sin6_family == u.sai6.sin6_family
           && it.u.sai6.sin6_port == u.sai6.sin6_port)
        {
            return true;
        }
        break;
#endif
    }
    return false;
}

mux_addr::~mux_addr() = default;

#if defined(HAVE_IN_ADDR)
mux_in_addr::~mux_in_addr() = default;

mux_in_addr::mux_in_addr(in_addr *ia) : m_ia(*ia)
{
}

mux_in_addr::mux_in_addr(const unsigned int bits)
{
    m_ia.s_addr = htonl(bits);
}

void mux_sockaddr::get_address(in_addr *ia) const
{
    *ia = u.sai.sin_addr;
}

bool mux_in_addr::operator<(const mux_addr &it) const
{
    if (AF_INET == it.getFamily())
    {
        const auto* t = dynamic_cast<const mux_in_addr *>(&it);
        return (ntohl(m_ia.s_addr) < ntohl(t->m_ia.s_addr));
    }
    return true;
}

bool mux_in_addr::operator==(const mux_addr &it) const
{
    if (AF_INET == it.getFamily())
    {
        const auto* t = dynamic_cast<const mux_in_addr *>(&it);
        return (ntohl(m_ia.s_addr) == ntohl(t->m_ia.s_addr));
    }
    return false;
}

bool mux_in_addr::clearOutsideMask(const mux_addr &it)
{
    if (AF_INET == it.getFamily())
    {
        const auto* t = dynamic_cast<const mux_in_addr *>(&it);
        if (m_ia.s_addr & ~t->m_ia.s_addr)
        {
            m_ia.s_addr &= t->m_ia.s_addr;
            return true;
        }
        return false;
    }
    return true;
}

mux_addr *mux_in_addr::calculateEnd(const mux_addr &it) const
{
    if (AF_INET == it.getFamily())
    {
        const auto* t = dynamic_cast<const mux_in_addr *>(&it);
        auto* e = new mux_in_addr();
        e->m_ia.s_addr = m_ia.s_addr | ~t->m_ia.s_addr;
        return static_cast<mux_addr *>(e);
    }
    return nullptr;
}
#endif

#if defined(HAVE_IN6_ADDR)
mux_in6_addr::~mux_in6_addr() = default;

mux_in6_addr::mux_in6_addr(in6_addr *ia6) : m_ia6(*ia6)
{
}

void mux_sockaddr::get_address(in6_addr *ia6) const
{
    *ia6 = u.sai6.sin6_addr;
}

bool mux_in6_addr::operator<(const mux_addr &it) const
{
    if (AF_INET6 == it.getFamily())
    {
        const auto* t = dynamic_cast<const mux_in6_addr *>(&it);
        for (size_t i = 0; i < sizeof(m_ia6.s6_addr)/sizeof(m_ia6.s6_addr[0]); i++)
        {
            if (m_ia6.s6_addr[i] < t->m_ia6.s6_addr[i])
            {
                return true;
            }
        }
    }
    return false;
}

bool mux_in6_addr::operator==(const mux_addr &it) const
{
    if (AF_INET6 == it.getFamily())
    {
        const auto* t = dynamic_cast<const mux_in6_addr *>(&it);
        return (m_ia6.s6_addr == t->m_ia6.s6_addr);
    }
    return false;
}

bool mux_in6_addr::clearOutsideMask(const mux_addr &it)
{
    if (AF_INET6 == it.getFamily())
    {
        bool fOutside = false;
        const auto* t = dynamic_cast<const mux_in6_addr *>(&it);
        for (size_t  i = 0; i < sizeof(m_ia6.s6_addr)/sizeof(m_ia6.s6_addr[0]); i++)
        {
            if (m_ia6.s6_addr[i] & ~t->m_ia6.s6_addr[i])
            {
                fOutside = true;
                m_ia6.s6_addr[i] &= t->m_ia6.s6_addr[i];
            }
        }
        return fOutside;
    }
    return true;
}

mux_addr *mux_in6_addr::calculateEnd(const mux_addr &it) const
{
    if (AF_INET6 == it.getFamily())
    {
        const auto* t = dynamic_cast<const mux_in6_addr *>(&it);
        auto* e = new mux_in6_addr();
        for (size_t  i = 0; i < sizeof(m_ia6.s6_addr)/sizeof(m_ia6.s6_addr[0]); i++)
        {
            e->m_ia6.s6_addr[i] = m_ia6.s6_addr[i] | ~t->m_ia6.s6_addr[i];
        }
        return static_cast<mux_addr *>(e);
    }
    return nullptr;
}
#endif
