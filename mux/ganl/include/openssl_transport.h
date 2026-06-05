#ifndef GANL_OPENSSL_TRANSPORT_H
#define GANL_OPENSSL_TRANSPORT_H

#include <secure_transport.h>
#include <mutex>
#include <map>
#include <memory>
#include <string>
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

        SSLContext() = default;
        // RAII: the owning handle (a shared_ptr held in sessions_ and by any
        // in-flight operation) frees the SSL object — and, via SSL_set_bio,
        // its attached BIOs — when the last reference goes away. This is what
        // makes it safe for processIncoming/Outgoing/shutdownSession to keep
        // operating on a borrowed handle after dropping mutex_, even if
        // destroySessionContext()/shutdown() removes the map entry meanwhile.
        ~SSLContext() {
            if (ssl) SSL_free(ssl);
        }
        // Sessions are heap-allocated and shared, never copied or moved.
        SSLContext(const SSLContext&) = delete;
        SSLContext& operator=(const SSLContext&) = delete;
        SSLContext(SSLContext&&) = delete;
        SSLContext& operator=(SSLContext&&) = delete;
    };

    std::mutex mutex_;
    SSL_CTX* ctx_{nullptr};
    // Owning handles. A lookup copies the shared_ptr out under mutex_, giving
    // the caller a reference that keeps the session alive across the unlocked
    // OpenSSL call even if it is concurrently destroyed.
    std::map<ConnectionHandle, std::shared_ptr<SSLContext>> sessions_;

    std::string lastGlobalError_;
    std::string keyPassword_;

    std::string getOpenSSLErrorString(ConnectionHandle conn);
    static int passwordCallback(char* buf, int size, int rwflag, void* userdata);
};

} // namespace ganl

#endif // GANL_OPENSSL_TRANSPORT_H
