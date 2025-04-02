#ifndef GANL_SECURE_TRANSPORT_FACTORY_H
#define GANL_SECURE_TRANSPORT_FACTORY_H

#include "secure_transport.h"
#include <memory>

namespace ganl {

    enum class SecureTransportType {
        Auto,      // Automatically choose the best available
        OpenSSL,   // Use OpenSSL (Unix-based systems)
        Schannel   // Use Schannel (Windows)
    };

    class SecureTransportFactory {
    public:
        /**
         * Create a secure transport of the specified type
         *
         * @param type Transport type to create
         * @return Unique pointer to the created transport, or nullptr if not available
         */
        static std::unique_ptr<SecureTransport> createTransport(SecureTransportType type = SecureTransportType::Auto);

        /**
         * Check if a specific transport type is available on this platform
         *
         * @param type Transport type to check
         * @return true if available, false otherwise
         */
        static bool isTransportTypeAvailable(SecureTransportType type);

        /**
         * Get a list of available transport types on this platform
         *
         * @return String describing available transports
         */
        static std::string getAvailableTransportTypes();
    };

} // namespace ganl

#endif // GANL_SECURE_TRANSPORT_FACTORY_H
