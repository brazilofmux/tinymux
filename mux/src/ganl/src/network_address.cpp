#include "network_types.h"
#include <cstring>
#include <arpa/inet.h>

namespace ganl {

NetworkAddress::NetworkAddress(const struct sockaddr* addr, socklen_t addrLen) {
    if (addr && addrLen > 0 && addrLen <= sizeof(sockaddr_storage)) {
        std::memcpy(&storage_, addr, addrLen);
        addrLen_ = addrLen;
        valid_ = true;
    }
}

NetworkAddress::Family NetworkAddress::getFamily() const {
    if (!valid_) {
        return Family::IPv4; // Default to IPv4 for invalid addresses
    }

    switch (storage_.ss_family) {
        case AF_INET6:
            return Family::IPv6;
        case AF_INET:
        default:
            return Family::IPv4;
    }
}

std::string NetworkAddress::toString() const {
    if (!valid_) {
        return "invalid";
    }

    char ipStr[INET6_ADDRSTRLEN]; // Large enough for both IPv4 and IPv6
    uint16_t port = 0;

    if (storage_.ss_family == AF_INET) {
        // IPv4 address
        const sockaddr_in* addr4 = reinterpret_cast<const sockaddr_in*>(&storage_);
        inet_ntop(AF_INET, &addr4->sin_addr, ipStr, sizeof(ipStr));
        port = ntohs(addr4->sin_port);
        return std::string(ipStr) + ":" + std::to_string(port);
    } else if (storage_.ss_family == AF_INET6) {
        // IPv6 address - use [address]:port format
        const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(&storage_);
        inet_ntop(AF_INET6, &addr6->sin6_addr, ipStr, sizeof(ipStr));
        port = ntohs(addr6->sin6_port);
        return "[" + std::string(ipStr) + "]:" + std::to_string(port);
    }

    return "unknown";
}

const struct sockaddr* NetworkAddress::getSockAddr() const {
    if (!valid_) {
        return nullptr;
    }
    return reinterpret_cast<const struct sockaddr*>(&storage_);
}

socklen_t NetworkAddress::getSockAddrLen() const {
    return valid_ ? addrLen_ : 0;
}

uint16_t NetworkAddress::getPort() const {
    if (!valid_) {
        return 0;
    }

    if (storage_.ss_family == AF_INET) {
        const sockaddr_in* addr4 = reinterpret_cast<const sockaddr_in*>(&storage_);
        return ntohs(addr4->sin_port);
    } else if (storage_.ss_family == AF_INET6) {
        const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(&storage_);
        return ntohs(addr6->sin6_port);
    }

    return 0;
}

bool NetworkAddress::isValid() const {
    return valid_;
}

} // namespace ganl
