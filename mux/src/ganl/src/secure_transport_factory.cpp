#include "secure_transport_factory.h"

// Include platform-specific headers conditionally
#if defined(_WIN32) || defined(WIN32)
#include "schannel_transport.h"
#define HAVE_SCHANNEL 1
#else
#define HAVE_SCHANNEL 0
#endif

#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#include "openssl_transport.h"
#define HAVE_OPENSSL 1
#else
#define HAVE_OPENSSL 0
#endif

#include <iostream>
#include <sstream>

namespace ganl {

    std::unique_ptr<SecureTransport> SecureTransportFactory::createTransport(SecureTransportType type) {
        // If Auto type requested, determine the best available
        if (type == SecureTransportType::Auto) {
#if HAVE_SCHANNEL
            type = SecureTransportType::Schannel;
#elif HAVE_OPENSSL
            type = SecureTransportType::OpenSSL;
#endif

#if 0
            std::cout << "Auto-selected secure transport type: ";
            switch (type) {
            case SecureTransportType::Schannel: std::cout << "Schannel"; break;
            case SecureTransportType::OpenSSL: std::cout << "OpenSSL"; break;
            default: std::cout << "none"; break;
            }
            std::cout << std::endl;
#endif
        }

        // Create the requested transport type
        switch (type) {
        case SecureTransportType::OpenSSL:
#if HAVE_OPENSSL
            return std::make_unique<OpenSSLTransport>();
#else
            std::cerr << "Error: OpenSSL transport requested but not available on this platform." << std::endl;
            return nullptr;
#endif

        case SecureTransportType::Schannel:
#if HAVE_SCHANNEL
            return std::make_unique<SchannelTransport>();
#else
            std::cerr << "Error: Schannel transport requested but not available on this platform." << std::endl;
            return nullptr;
#endif

        default:
            std::cerr << "Error: No supported secure transport available." << std::endl;
            return nullptr;
        }
    }

    bool SecureTransportFactory::isTransportTypeAvailable(SecureTransportType type) {
        switch (type) {
        case SecureTransportType::OpenSSL:
#if HAVE_OPENSSL
            return true;
#else
            return false;
#endif

        case SecureTransportType::Schannel:
#if HAVE_SCHANNEL
            return true;
#else
            return false;
#endif

        case SecureTransportType::Auto:
            return HAVE_OPENSSL || HAVE_SCHANNEL;

        default:
            return false;
        }
    }

    std::string SecureTransportFactory::getAvailableTransportTypes() {
        std::ostringstream oss;
        oss << "Available secure transports: ";

        bool first = true;

#if HAVE_OPENSSL
        oss << "OpenSSL";
        first = false;
#endif

#if HAVE_SCHANNEL
        if (!first) oss << ", ";
        oss << "Schannel";
        first = false;
#endif

        if (first) {
            oss << "none";
        }

        return oss.str();
    }

} // namespace ganl
