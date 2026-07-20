#ifndef GANL_SCHANNEL_TRANSPORT_H
#define GANL_SCHANNEL_TRANSPORT_H

#include <secure_transport.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <memory>

// Windows Headers
#define SECURITY_WIN32
// Expose the modern SCH_CREDENTIALS / TLS_PARAMETERS structures (and TLS 1.3
// support) from <schannel.h>. Must be defined before the header is included.
// The legacy SCHANNEL_CRED remains available; grbitDisabledProtocols in
// TLS_PARAMETERS is a *disable* mask (inverted from SCHANNEL_CRED's enable
// mask). See #952.
#define SCHANNEL_USE_BLACKLISTS
#include <windows.h>
// SCHANNEL_USE_BLACKLISTS exposes CRYPTO_SETTINGS / TLS_PARAMETERS in
// <schannel.h>, whose members are typed UNICODE_STRING / PUNICODE_STRING.
// windows.h alone does not declare those in user mode, so pull in <winternl.h>
// (which defines UNICODE_STRING) before <schannel.h> (#952).
#include <winternl.h>
#include <wincrypt.h>
#include <ncrypt.h>
#include <schannel.h>
#include <security.h>

namespace ganl {

    class SchannelTransport : public SecureTransport {
    public:
        SchannelTransport();
        ~SchannelTransport() override;

        // SecureTransport Interface Implementation
        bool initialize(const TlsConfig& config) override;
        void shutdown() override;

        bool createSessionContext(ConnectionHandle conn, bool isServer = true) override;
        void destroySessionContext(ConnectionHandle conn) override;

        TlsResult processIncoming(ConnectionHandle conn, IoBuffer& encrypted_in,
            IoBuffer& decrypted_out, IoBuffer& encrypted_out, bool consumeInput = true) override;
        TlsResult processOutgoing(ConnectionHandle conn, IoBuffer& plain_in,
            IoBuffer& encrypted_out, bool consumeInput = true) override;
        TlsResult shutdownSession(ConnectionHandle conn, IoBuffer& encrypted_out) override;

        bool isEstablished(ConnectionHandle conn) override;
        bool needsNetworkRead(ConnectionHandle conn) override;
        bool needsNetworkWrite(ConnectionHandle conn) override;
        std::string getLastTlsErrorString(ConnectionHandle conn) override;

    private:
        // Schannel-specific session context
        struct SessionContext {
            bool isServer;                      // Whether this is a server-side connection
            bool established;                   // Whether the handshake is complete
            bool needsRenegotiate;              // Whether renegotiation is needed
            bool waitingForData;                // Whether we're waiting for more data

            CredHandle credHandle;              // Credential handle
            bool credHandleInitialized;         // Whether the credential handle is initialized

            CtxtHandle contextHandle;           // Security context handle
            bool contextHandleInitialized;      // Whether the context handle is initialized

            SecPkgContext_StreamSizes streamSizes;  // Stream size information
            bool streamSizesSet;                // Whether stream sizes have been retrieved

            // Handshake state tracking
            std::vector<BYTE> handshakeBuffer;  // Buffer to store partial handshake messages

            // I/O buffers to handle SEC_E_INCOMPLETE_MESSAGE
            std::vector<BYTE> incompleteBuffer; // Buffer for handling SEC_E_INCOMPLETE_MESSAGE

            // Error tracking
            std::string lastError;              // Last error message

            // Constructor
            SessionContext(bool server = true);
        };

        // Helper methods for Schannel operations
        TlsResult performHandshake(ConnectionHandle conn, IoBuffer& encrypted_in, IoBuffer& encrypted_out);
        TlsResult decryptMessage(ConnectionHandle conn, IoBuffer& encrypted_in, IoBuffer& decrypted_out);
        TlsResult encryptMessage(ConnectionHandle conn, IoBuffer& plain_in, IoBuffer& encrypted_out);

        // Helper methods for credential and context management
        bool acquireCredentials(SessionContext& context);
        bool initializeSecurityContext(SessionContext& context, IoBuffer& encrypted_in, IoBuffer& encrypted_out);
        bool queryStreamSizes(SessionContext& context);

        // Helper for mapping Schannel error to TlsResult
        TlsResult mapSchannelError(SECURITY_STATUS status, SessionContext& context);
        std::string getSchannelErrorString(SECURITY_STATUS status);

        // Certificate loading helpers
        bool initializeFromPfx(const TlsConfig& config);
        bool initializeFromPem(const TlsConfig& config);
        bool loadPemCertificate(const std::string& certFile, PCCERT_CONTEXT& outCert);
        bool loadPemPrivateKey(const std::string& keyFile, NCRYPT_KEY_HANDLE& outKey);

        // Delete the persisted CNG key container backing a cert we imported via
        // PFXImportCertStore, so it does not leak on disk (#975).
        void deleteCertKeyContainer(PCCERT_CONTEXT cert);

        // Certificate selection helper
        static int scoreCertForServerTls(PCCERT_CONTEXT cert);

        // Member variables
        std::mutex mutex_;                       // Mutex for thread safety
        std::map<ConnectionHandle, std::unique_ptr<SessionContext>> sessions_; // Session contexts

        // Certificate handling
        HCERTSTORE certStore_;                   // Certificate store handle
        PCCERT_CONTEXT serverCertContext_;       // Server certificate context
        bool certStoreOpen_;                     // Whether the certificate store is open
        NCRYPT_KEY_HANDLE ncryptKey_{0};         // CNG key handle for PEM-loaded keys

        TlsConfig config_;                       // TLS configuration
        std::string lastGlobalError_;            // Last global error message
    };

} // namespace ganl

#endif // GANL_SCHANNEL_TRANSPORT_H
