// schannel_tls.h -- Client-side Schannel TLS wrapper for the console client.
#ifndef SCHANNEL_TLS_H
#define SCHANNEL_TLS_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#define SECURITY_WIN32
#include <schannel.h>
#include <security.h>
#include <sspi.h>

#include <string>
#include <vector>

// Client-side TLS session using Schannel.
// Much simpler than a server implementation: no certificates, no PFX/PEM,
// just outbound credentials and InitializeSecurityContext.
class SchannelSession {
public:
    SchannelSession();
    ~SchannelSession();

    SchannelSession(const SchannelSession&) = delete;
    SchannelSession& operator=(const SchannelSession&) = delete;

    // Perform the TLS handshake over an already-connected socket.
    // server_name is the hostname for SNI / certificate validation.
    // Returns true on success.
    bool handshake(SOCKET sock, const std::string& server_name);

    // Encrypt plaintext data for sending. Appends to out.
    bool encrypt(const void* data, size_t len, std::vector<char>& out);

    // Decrypt received ciphertext. Appends decrypted data to out.
    // extra_out receives any leftover bytes not yet forming a complete record.
    // Returns: 1 = success (data decrypted), 0 = need more data,
    //         -1 = error/closed, 2 = renegotiate.
    int decrypt(const char* data, size_t len, std::vector<char>& out);

    // Graceful TLS shutdown.
    void shutdown(SOCKET sock);

    bool is_established() const { return established_; }
    const std::string& last_error() const { return last_error_; }

private:
    bool acquire_credentials();
    // Send raw bytes to the socket (blocking).
    bool sock_send(SOCKET sock, const void* data, size_t len);
    // Receive raw bytes from the socket (blocking, with timeout).
    int sock_recv(SOCKET sock, char* buf, size_t len);

    CredHandle cred_handle_;
    bool cred_initialized_ = false;
    CtxtHandle ctx_handle_;
    bool ctx_initialized_ = false;
    SecPkgContext_StreamSizes stream_sizes_;
    bool stream_sizes_valid_ = false;
    bool established_ = false;

    // Buffer for incomplete TLS records across decrypt calls.
    std::vector<char> pending_;

    std::string last_error_;
};

#endif // SCHANNEL_TLS_H
