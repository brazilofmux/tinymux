#ifndef GANL_OPENSSL_TRANSPORT_H
#define GANL_OPENSSL_TRANSPORT_H

#include "secure_transport.h"
#include <mutex>
#include <map>
#include <openssl/ssl.h>

namespace ganl {

class OpenSSLTransport : public SecureTransport {
public:
    OpenSSLTransport();
    ~OpenSSLTransport() override;

    bool initialize(const TlsConfig& config) override;
    void shutdown() override;

    bool createSessionContext(ConnectionHandle conn, bool isServer = true) override;
    void destroySessionContext(ConnectionHandle conn) override;

    TlsResult processIncoming(ConnectionHandle conn, IoBuffer& encrypted_in,
                            IoBuffer& decrypted_out, IoBuffer& encrypted_out,
                            bool consumeInput = true) override;
    TlsResult processOutgoing(ConnectionHandle conn, IoBuffer& plain_in,
                            IoBuffer& encrypted_out, bool consumeInput = true) override;
    TlsResult shutdownSession(ConnectionHandle conn, IoBuffer& encrypted_out) override;

    bool isEstablished(ConnectionHandle conn) override;
    bool needsNetworkRead(ConnectionHandle conn) override;
    bool needsNetworkWrite(ConnectionHandle conn) override;
    std::string getLastTlsErrorString(ConnectionHandle conn) override;

private:
    struct SSLContext {
        SSL* ssl{nullptr};
        BIO* readBio{nullptr}; // Managed by SSL_free
        BIO* writeBio{nullptr}; // Managed by SSL_free
        bool established{false};
        std::string lastError;

        // Add move constructor/assignment for map insertion
        SSLContext() = default; // Needed for map default construction before move
        SSLContext(SSLContext&& other) noexcept
            : ssl(other.ssl), readBio(other.readBio), writeBio(other.writeBio),
              established(other.established), lastError(std::move(other.lastError)) {
            other.ssl = nullptr;
            other.readBio = nullptr;
            other.writeBio = nullptr;
        }
        SSLContext& operator=(SSLContext&& other) noexcept {
            if (this != &other) {
                // Clean up existing resources if any (shouldn't happen with unique_ptr pattern, but safe)
                if (ssl) SSL_free(ssl);

                ssl = other.ssl;
                readBio = other.readBio;
                writeBio = other.writeBio;
                established = other.established;
                lastError = std::move(other.lastError);

                other.ssl = nullptr;
                other.readBio = nullptr;
                other.writeBio = nullptr;
            }
            return *this;
        }
        // Prevent copying
        SSLContext(const SSLContext&) = delete;
        SSLContext& operator=(const SSLContext&) = delete;
    };

    std::mutex mutex_;
    SSL_CTX* ctx_{nullptr};
    std::map<ConnectionHandle, SSLContext> sessions_;

    std::string lastGlobalError_;

    std::string getOpenSSLErrorString(ConnectionHandle conn);
};

} // namespace ganl

#endif // GANL_OPENSSL_TRANSPORT_H
