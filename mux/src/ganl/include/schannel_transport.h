#ifndef GANL_SCHANNEL_TRANSPORT_H
#define GANL_SCHANNEL_TRANSPORT_H

#include "secure_transport.h"
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <memory>

// Windows Headers
#define SECURITY_WIN32
#include <windows.h>
#include <wincrypt.h>
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

        // Member variables
        std::mutex mutex_;                       // Mutex for thread safety
        std::map<ConnectionHandle, std::unique_ptr<SessionContext>> sessions_; // Session contexts

        // Certificate handling
        HCERTSTORE certStore_;                   // Certificate store handle
        PCCERT_CONTEXT serverCertContext_;       // Server certificate context
        bool certStoreOpen_;                     // Whether the certificate store is open

        TlsConfig config_;                       // TLS configuration
        std::string lastGlobalError_;            // Last global error message
    };

} // namespace ganl

#endif // GANL_SCHANNEL_TRANSPORT_H
