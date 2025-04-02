#include "schannel_transport.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cassert>
#include <fstream>

// Define a macro for debug logging
#ifndef NDEBUG // Only compile debug messages if NDEBUG is not defined
#define GANL_SCHANNEL_DEBUG(conn, x) \
    do { std::cerr << "[Schannel:" << (conn == 0 ? "Global" : std::to_string(conn)) << "] " << x << std::endl; } while (0)
#else
#define GANL_SCHANNEL_DEBUG(conn, x) do {} while (0)
#endif

// Helper functions to check and set flags in SecBufferDesc
#define SCHANNEL_BUFFER_TOKEN_FLAG     (1)
#define SCHANNEL_BUFFER_EMPTY_FLAG     (2)
#define SCHANNEL_BUFFER_DATA_FLAG      (4)
#define SCHANNEL_BUFFER_MISSING_FLAG   (8)
#define SCHANNEL_BUFFER_EXTRA_FLAG     (16)
#define SCHANNEL_BUFFER_ALERT_FLAG     (32)

namespace ganl {

    // --- SessionContext Implementation ---

    SchannelTransport::SessionContext::SessionContext(bool server)
        : isServer(server),
        established(false),
        needsRenegotiate(false),
        waitingForData(false),
        credHandleInitialized(false),
        contextHandleInitialized(false),
        streamSizesSet(false) {
        // Initialize handshake buffer with reasonable size
        handshakeBuffer.reserve(16384);

        // Initialize buffer for handling incomplete messages
        incompleteBuffer.reserve(16384);

        // Zero-initialize handles
        SecInvalidateHandle(&credHandle);
        SecInvalidateHandle(&contextHandle);
    }

    // --- SchannelTransport Implementation ---

    SchannelTransport::SchannelTransport()
        : certStore_(nullptr),
        serverCertContext_(nullptr),
        certStoreOpen_(false) {
        GANL_SCHANNEL_DEBUG(0, "Transport Created");
    }

    SchannelTransport::~SchannelTransport() {
        GANL_SCHANNEL_DEBUG(0, "Transport Destroyed");
        shutdown();
    }

    bool SchannelTransport::initialize(const TlsConfig& config) {
        GANL_SCHANNEL_DEBUG(0, "Initializing Schannel Transport");
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;

        if (config.certificateFile.empty()) {
            lastGlobalError_ = "No certificate file provided for TLS";
            GANL_SCHANNEL_DEBUG(0, "Error: " << lastGlobalError_);
            return false;
        }

        // Open the .p12 file with STL
        std::ifstream pfxFile(config.certificateFile, std::ios::binary);
        if (!pfxFile) {
            lastGlobalError_ = "Failed to open .p12 file: " + config.certificateFile;
            GANL_SCHANNEL_DEBUG(0, "Error: " << lastGlobalError_);
            return false;
        }

        // Get file size (C++17 way)
        pfxFile.seekg(0, std::ios::end);
        std::streampos size = pfxFile.tellg();
        pfxFile.seekg(0, std::ios::beg);

        // Read into a vector
        std::vector<char> buffer(size);
        pfxFile.read(buffer.data(), size);
        if (!pfxFile) {
            lastGlobalError_ = "Failed to read .p12 file: " + config.certificateFile;
            GANL_SCHANNEL_DEBUG(0, "Error: " << lastGlobalError_);
            return false;
        }
        pfxFile.close();

        // Convert password from std::string to std::wstring for PFXImportCertStore
        std::wstring widePassword;
        if (!config_.password.empty()) {
            int bufferSize = MultiByteToWideChar(CP_UTF8, 0, config_.password.c_str(), -1, nullptr, 0);
            if (bufferSize > 0) {
                widePassword.resize(bufferSize);
                if (MultiByteToWideChar(CP_UTF8, 0, config_.password.c_str(), -1, &widePassword[0], bufferSize) == 0) {
                    lastGlobalError_ = "Failed to convert password to wide string: " + std::to_string(GetLastError());
                    GANL_SCHANNEL_DEBUG(0, "Error: " << lastGlobalError_);
                    return false;
                }
                // Remove null terminator from std::wstring
                widePassword.resize(wcslen(widePassword.c_str()));
            } else {
                lastGlobalError_ = "Failed to determine password conversion size: " + std::to_string(GetLastError());
                GANL_SCHANNEL_DEBUG(0, "Error: " << lastGlobalError_);
                return false;
            }
        }

        CRYPT_DATA_BLOB pfxBlob = { (DWORD)size, (BYTE*)buffer.data() };
        HCERTSTORE tempStore = PFXImportCertStore(&pfxBlob, widePassword.empty() ? L"" : widePassword.c_str(), 0);
        if (!tempStore) {
            lastGlobalError_ = "Failed to import .p12: " + std::to_string(GetLastError());
            GANL_SCHANNEL_DEBUG(0, "Error: " << lastGlobalError_);
            return false;
        }

        // Note: This assumes the first certificate found in the PFX store is the desired server certificate.
        // In a production environment, you might want to search for a specific certificate or validate it further.
        serverCertContext_ = CertEnumCertificatesInStore(tempStore, NULL);
        if (!serverCertContext_) {
            CertCloseStore(tempStore, 0);
            lastGlobalError_ = "No certificate in .p12: " + std::to_string(GetLastError());
            GANL_SCHANNEL_DEBUG(0, "Error: " << lastGlobalError_);
            return false;
        }

        certStore_ = tempStore;
        certStoreOpen_ = true;
        GANL_SCHANNEL_DEBUG(0, "Loaded .p12 from " << config.certificateFile);

        return true;
    }

    void SchannelTransport::shutdown() {
        GANL_SCHANNEL_DEBUG(0, "Shutting down Schannel Transport");
        std::lock_guard<std::mutex> lock(mutex_);

        // Destroy all session contexts
        for (auto& pair : sessions_) {
            GANL_SCHANNEL_DEBUG(pair.first, "Destroying session context during shutdown");

            SessionContext& context = *pair.second;

            // Free security context
            if (context.contextHandleInitialized) {
                DeleteSecurityContext(&context.contextHandle);
                context.contextHandleInitialized = false;
            }

            // Free credentials
            if (context.credHandleInitialized) {
                FreeCredentialsHandle(&context.credHandle);
                context.credHandleInitialized = false;
            }
        }

        // Clear all session contexts
        sessions_.clear();

        // Free certificate context
        if (serverCertContext_ != nullptr) {
            CertFreeCertificateContext(serverCertContext_);
            serverCertContext_ = nullptr;
        }

        // Close certificate store
        if (certStoreOpen_ && certStore_ != nullptr) {
            CertCloseStore(certStore_, 0);
            certStore_ = nullptr;
            certStoreOpen_ = false;
        }

        GANL_SCHANNEL_DEBUG(0, "Schannel Transport shutdown complete");
    }

    bool SchannelTransport::createSessionContext(ConnectionHandle conn, bool isServer) {
        GANL_SCHANNEL_DEBUG(conn, "Creating session context. isServer=" << isServer);
        std::lock_guard<std::mutex> lock(mutex_);

        if (sessions_.find(conn) != sessions_.end()) {
            GANL_SCHANNEL_DEBUG(conn, "Error: Session context already exists");
            return false;
        }

        // Create new session context
        std::unique_ptr<SessionContext> context = std::make_unique<SessionContext>(isServer);

        // Acquire credentials
        if (!acquireCredentials(*context)) {
            GANL_SCHANNEL_DEBUG(conn, "Error: Failed to acquire credentials: " << context->lastError);
            return false;
        }

        // Store session context
        sessions_[conn] = std::move(context);

        GANL_SCHANNEL_DEBUG(conn, "Session context created successfully");
        return true;
    }

    void SchannelTransport::destroySessionContext(ConnectionHandle conn) {
        GANL_SCHANNEL_DEBUG(conn, "Destroying session context");
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = sessions_.find(conn);
        if (it == sessions_.end()) {
            GANL_SCHANNEL_DEBUG(conn, "Warning: Session context not found");
            return;
        }

        SessionContext& context = *it->second;

        // Free security context
        if (context.contextHandleInitialized) {
            DeleteSecurityContext(&context.contextHandle);
            context.contextHandleInitialized = false;
        }

        // Free credentials
        if (context.credHandleInitialized) {
            FreeCredentialsHandle(&context.credHandle);
            context.credHandleInitialized = false;
        }

        // Remove from map
        sessions_.erase(it);

        GANL_SCHANNEL_DEBUG(conn, "Session context destroyed");
    }

    TlsResult SchannelTransport::processIncoming(ConnectionHandle conn, IoBuffer& encrypted_in,
        IoBuffer& decrypted_out, IoBuffer& encrypted_out, bool consumeInput) {
        // Note: The consumeInput parameter is present for interface compatibility,
        // but Schannel's processing dictates buffer consumption internally.
        // The handshake and decryption logic must consume the exact amount of data
        // processed by SChannel, regardless of this flag.
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = sessions_.find(conn);
        if (it == sessions_.end()) {
            lastGlobalError_ = "No session context for connection";
            GANL_SCHANNEL_DEBUG(conn, "Error: " << lastGlobalError_);
            return TlsResult::Error;
        }

        SessionContext& context = *it->second;

        GANL_SCHANNEL_DEBUG(conn, "Processing Incoming. EncryptedIn=" << encrypted_in.readableBytes()
            << ", Established=" << context.established);

        // If connection is not established, perform handshake
        if (!context.established) {
            return performHandshake(conn, encrypted_in, encrypted_out);
        }

        // If connection is established, decrypt incoming data
        if (encrypted_in.readableBytes() > 0 || !context.incompleteBuffer.empty()) {
            return decryptMessage(conn, encrypted_in, decrypted_out);
        }

        // No data to process
        return TlsResult::Success;
    }

    TlsResult SchannelTransport::processOutgoing(ConnectionHandle conn, IoBuffer& plain_in,
        IoBuffer& encrypted_out, bool consumeInput) {
        // Note: The consumeInput parameter is present for interface compatibility,
        // but the implementation fully consumes the input buffer as Schannel requires
        // complete messages for encryption. Partial consumption is not supported in
        // the current implementation.
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = sessions_.find(conn);
        if (it == sessions_.end()) {
            lastGlobalError_ = "No session context for connection";
            GANL_SCHANNEL_DEBUG(conn, "Error: " << lastGlobalError_);
            return TlsResult::Error;
        }

        SessionContext& context = *it->second;

        GANL_SCHANNEL_DEBUG(conn, "Processing Outgoing. PlainIn=" << plain_in.readableBytes()
            << ", Established=" << context.established);

        // If connection is not established, return error
        if (!context.established) {
            context.lastError = "Cannot send data: TLS handshake not complete";
            GANL_SCHANNEL_DEBUG(conn, "Error: " << context.lastError);
            return TlsResult::Error;
        }

        // If no data to send, return success
        if (plain_in.readableBytes() == 0) {
            return TlsResult::Success;
        }

        // Encrypt outgoing data
        return encryptMessage(conn, plain_in, encrypted_out);
    }

    TlsResult SchannelTransport::shutdownSession(ConnectionHandle conn, IoBuffer& encrypted_out) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = sessions_.find(conn);
        if (it == sessions_.end()) {
            lastGlobalError_ = "No session context for connection";
            GANL_SCHANNEL_DEBUG(conn, "Error: " << lastGlobalError_);
            return TlsResult::Error;
        }

        SessionContext& context = *it->second;

        GANL_SCHANNEL_DEBUG(conn, "Initiating TLS shutdown");

        if (!context.established) {
            GANL_SCHANNEL_DEBUG(conn, "Connection not established, nothing to shutdown");
            return TlsResult::Closed;
        }

        // Create shutdown token
        DWORD dwType = SCHANNEL_SHUTDOWN;
        SecBuffer outBuffers[1];
        outBuffers[0].pvBuffer = &dwType;
        outBuffers[0].cbBuffer = sizeof(dwType);
        outBuffers[0].BufferType = SECBUFFER_TOKEN;

        SecBufferDesc outBuffer;
        outBuffer.ulVersion = SECBUFFER_VERSION;
        outBuffer.cBuffers = 1;
        outBuffer.pBuffers = outBuffers;

        // Apply shutdown token
        SECURITY_STATUS status = ApplyControlToken(&context.contextHandle, &outBuffer);
        if (FAILED(status)) {
            context.lastError = "Failed to apply control token for shutdown: " + getSchannelErrorString(status);
            GANL_SCHANNEL_DEBUG(conn, "Error: " << context.lastError);
            return TlsResult::Error;
        }

        // Prepare shutdown message
        SecBuffer shutdownBuffers[1];
        shutdownBuffers[0].pvBuffer = NULL;
        shutdownBuffers[0].BufferType = SECBUFFER_TOKEN;
        shutdownBuffers[0].cbBuffer = 0;

        SecBufferDesc shutdownDesc;
        shutdownDesc.ulVersion = SECBUFFER_VERSION;
        shutdownDesc.cBuffers = 1;
        shutdownDesc.pBuffers = shutdownBuffers;

        DWORD dwSSPIFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
            ISC_REQ_CONFIDENTIALITY | ISC_RET_EXTENDED_ERROR |
            ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;

        DWORD dwSSPIOutFlags = 0;
        TimeStamp expiry;

        // Generate shutdown message
        status = context.isServer ?
            AcceptSecurityContext(
                &context.credHandle,
                &context.contextHandle,
                NULL,
                dwSSPIFlags,
                SECURITY_NATIVE_DREP,
                NULL,
                &shutdownDesc,
                &dwSSPIOutFlags,
                &expiry) :
            InitializeSecurityContext(
                &context.credHandle,
                &context.contextHandle,
                NULL,
                dwSSPIFlags,
                0,
                SECURITY_NATIVE_DREP,
                NULL,
                0,
                &context.contextHandle,
                &shutdownDesc,
                &dwSSPIOutFlags,
                &expiry);

        if (FAILED(status)) {
            context.lastError = "Failed to generate shutdown message: " + getSchannelErrorString(status);
            GANL_SCHANNEL_DEBUG(conn, "Error: " << context.lastError);
            return TlsResult::Error;
        }

        // Get shutdown token
        if (shutdownBuffers[0].pvBuffer != NULL && shutdownBuffers[0].cbBuffer > 0) {
            // Copy the shutdown token to the output buffer
            encrypted_out.ensureWritable(shutdownBuffers[0].cbBuffer);
            memcpy(encrypted_out.writePtr(), shutdownBuffers[0].pvBuffer, shutdownBuffers[0].cbBuffer);
            encrypted_out.commitWrite(shutdownBuffers[0].cbBuffer);

            // Free the buffer allocated by Schannel
            FreeContextBuffer(shutdownBuffers[0].pvBuffer);

            GANL_SCHANNEL_DEBUG(conn, "Shutdown message generated (" << shutdownBuffers[0].cbBuffer << " bytes)");
        }
        else {
            GANL_SCHANNEL_DEBUG(conn, "No shutdown message data generated");
        }

        // Mark as not established
        context.established = false;

        return TlsResult::Closed;
    }

    bool SchannelTransport::isEstablished(ConnectionHandle conn) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = sessions_.find(conn);
        if (it == sessions_.end()) {
            return false;
        }

        return it->second->established;
    }

    bool SchannelTransport::needsNetworkRead(ConnectionHandle conn) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = sessions_.find(conn);
        if (it == sessions_.end()) {
            return false;
        }

        return it->second->waitingForData;
    }

    bool SchannelTransport::needsNetworkWrite(ConnectionHandle conn) {
        // LIMITATION: This method returns false even during handshake, which may not accurately
        // reflect when the connection has handshake data that needs to be sent.
        //
        // A more robust implementation would track if performHandshake generated output
        // that hasn't been fully sent yet. Instead, the connection should rely on
        // TlsResult::WantWrite returned from processIncoming/performHandshake to determine
        // if network writes are needed during handshake.
        //
        // For established connections, this implementation is correct as SChannel
        // doesn't buffer outgoing data between calls.
        return false;
    }

    std::string SchannelTransport::getLastTlsErrorString(ConnectionHandle conn) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = sessions_.find(conn);
        if (it != sessions_.end() && !it->second->lastError.empty()) {
            return it->second->lastError;
        }

        return lastGlobalError_;
    }

    // --- Private Helper Methods ---

    TlsResult SchannelTransport::performHandshake(ConnectionHandle conn, IoBuffer& encrypted_in, IoBuffer& encrypted_out) {
        auto it = sessions_.find(conn);
        if (it == sessions_.end()) { /* handle error */ return TlsResult::Error; }
        SessionContext& context = *it->second;

        GANL_SCHANNEL_DEBUG(conn, "Performing TLS handshake. Buffer size BEFORE append: " << context.handshakeBuffer.size()
            << ", Available network data: " << encrypted_in.readableBytes() << " bytes");

        // Append new incoming data to the PERSISTENT handshake buffer
        if (encrypted_in.readableBytes() > 0) {
            size_t originalSize = context.handshakeBuffer.size();
            context.handshakeBuffer.insert(context.handshakeBuffer.end(),
                encrypted_in.readPtr(),
                encrypted_in.readPtr() + encrypted_in.readableBytes());
            encrypted_in.consumeRead(encrypted_in.readableBytes());
            GANL_SCHANNEL_DEBUG(conn, "Appended " << context.handshakeBuffer.size() - originalSize
                << " bytes to handshake buffer (total: " << context.handshakeBuffer.size() << ")");
        }

        // --- Loop to process potentially multiple handshake messages within the buffer ---
        bool moreProcessingNeeded = true; // Assume we need to call ASC at least once if buffer not empty
        while (moreProcessingNeeded && !context.handshakeBuffer.empty()) {

            // Don't process if already established or waiting for data we don't have
            if (context.established) {
                moreProcessingNeeded = false; // Should not happen, but safety check
                break;
            }
            if (context.waitingForData && context.handshakeBuffer.empty()) {
                moreProcessingNeeded = false; // Need more network data
                break;
            }

            // We have data, reset waiting flag before calling ASC
            context.waitingForData = false;

            // Prepare input buffer using the current handshakeBuffer content
            SecBuffer inBuffers[2];
            inBuffers[0].pvBuffer = context.handshakeBuffer.data();
            inBuffers[0].cbBuffer = static_cast<DWORD>(context.handshakeBuffer.size());
            inBuffers[0].BufferType = SECBUFFER_TOKEN;

            inBuffers[1].pvBuffer = NULL; // For potential SECBUFFER_EXTRA
            inBuffers[1].cbBuffer = 0;
            inBuffers[1].BufferType = SECBUFFER_EMPTY;

            SecBufferDesc inBufferDesc = { SECBUFFER_VERSION, 2, inBuffers };

            // Prepare output buffer
            SecBuffer outBuffers[1] = { 0 }; // Important to zero-initialize
            outBuffers[0].BufferType = SECBUFFER_TOKEN;
            SecBufferDesc outBufferDesc = { SECBUFFER_VERSION, 1, outBuffers };

            DWORD dwSSPIFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                ISC_REQ_CONFIDENTIALITY | ISC_RET_EXTENDED_ERROR |
                ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
            DWORD dwSSPIOutFlags = 0;
            TimeStamp expiry;

            GANL_SCHANNEL_DEBUG(conn, "Calling AcceptSecurityContext with " << inBuffers[0].cbBuffer << " bytes.");
            // --- Dump buffer content before call ---
            // You can wrap the vector temporarily if needed for dumpIoBufferHex
            // or modify dumpIoBufferHex to take raw pointers/size
            // Example temporary wrap:
            IoBuffer tempInputView;
            if (inBuffers[0].cbBuffer > 0) {
                tempInputView.append(inBuffers[0].pvBuffer, inBuffers[0].cbBuffer);
            }
            GANL_SCHANNEL_DEBUG(conn, "Buffer content BEFORE AcceptSecurityContext call:");
            ganl::utils::dumpIoBufferHex(std::cerr, tempInputView, 1024); // Dump max 1k

            // --- Call AcceptSecurityContext ---
            SECURITY_STATUS status = AcceptSecurityContext(
                &context.credHandle,
                context.contextHandleInitialized ? &context.contextHandle : NULL,
                &inBufferDesc, dwSSPIFlags, SECURITY_NATIVE_DREP,
                context.contextHandleInitialized ? NULL : &context.contextHandle,
                &outBufferDesc, &dwSSPIOutFlags, &expiry
            );

            if (!context.contextHandleInitialized) {
                context.contextHandleInitialized = true;
            }

            // Handle output token
            if (outBuffers[0].pvBuffer != NULL && outBuffers[0].cbBuffer > 0) {
                GANL_SCHANNEL_DEBUG(conn, "AcceptSecurityContext generated output token (" << outBuffers[0].cbBuffer << " bytes).");
                encrypted_out.append(outBuffers[0].pvBuffer, outBuffers[0].cbBuffer);
                FreeContextBuffer(outBuffers[0].pvBuffer);
            }
            else {
                GANL_SCHANNEL_DEBUG(conn, "AcceptSecurityContext did not generate output token this call.");
            }

            // --- Process Status and Consume Input from context.handshakeBuffer ---
            size_t bytesConsumed = 0;
            moreProcessingNeeded = false; // Assume loop stops unless CONTINUE_NEEDED happens

            if (status == SEC_E_OK) {
                GANL_SCHANNEL_DEBUG(conn, "AcceptSecurityContext returned SEC_E_OK (Handshake Complete).");
                bytesConsumed = inBuffers[0].cbBuffer; // Assume consumed all input for this final step

                // Check for SECBUFFER_EXTRA (leftover application data)
                if (inBuffers[1].BufferType == SECBUFFER_EXTRA && inBuffers[1].cbBuffer > 0) {
                    GANL_SCHANNEL_DEBUG(conn, "Found SECBUFFER_EXTRA with " << inBuffers[1].cbBuffer << " bytes.");
                    if (inBuffers[1].cbBuffer <= bytesConsumed) {
                        bytesConsumed -= inBuffers[1].cbBuffer; // Don't consume the extra data yet
                        // Move extra data to context.incompleteBuffer
                        context.incompleteBuffer.assign(
                            context.handshakeBuffer.begin() + bytesConsumed,
                            context.handshakeBuffer.end() // end() points one past the last element
                        );
                        GANL_SCHANNEL_DEBUG(conn, "Copied " << context.incompleteBuffer.size() << " extra bytes to incompleteBuffer.");

                    }
                    else {
                        GANL_SCHANNEL_DEBUG(conn, "Error: SECBUFFER_EXTRA size > consumed size!");
                        // Handle error appropriately
                        status = SEC_E_INTERNAL_ERROR; // Treat as error
                        bytesConsumed = 0; // Don't consume anything on error
                    }
                }
                else {
                    // No extra data, clear incomplete buffer
                    context.incompleteBuffer.clear();
                }

                // Erase consumed data from the *start* of the handshakeBuffer
                if (bytesConsumed > 0 && bytesConsumed <= context.handshakeBuffer.size()) {
                    GANL_SCHANNEL_DEBUG(conn, "Erasing " << bytesConsumed << " consumed bytes from handshakeBuffer.");
                    context.handshakeBuffer.erase(context.handshakeBuffer.begin(), context.handshakeBuffer.begin() + bytesConsumed);
                }
                else if (bytesConsumed > context.handshakeBuffer.size()) {
                    GANL_SCHANNEL_DEBUG(conn, "Error: bytesConsumed > handshakeBuffer size!");
                    context.handshakeBuffer.clear(); // Clear buffer on error
                    status = SEC_E_INTERNAL_ERROR; // Treat as error
                }


                if (status == SEC_E_OK) {
                    context.established = true;
                    if (!queryStreamSizes(context)) { return TlsResult::Error; }
                    break; // Handshake done, exit loop
                }
                // else fall through to error handling if status became error

            }
            else if (status == SEC_I_CONTINUE_NEEDED) {
                GANL_SCHANNEL_DEBUG(conn, "AcceptSecurityContext returned SEC_I_CONTINUE_NEEDED.");
                // Consume all input provided *for this specific call*
                bytesConsumed = inBuffers[0].cbBuffer;
                if (bytesConsumed > 0 && bytesConsumed <= context.handshakeBuffer.size()) {
                    GANL_SCHANNEL_DEBUG(conn, "Erasing " << bytesConsumed << " consumed bytes from handshakeBuffer.");
                    context.handshakeBuffer.erase(context.handshakeBuffer.begin(), context.handshakeBuffer.begin() + bytesConsumed);
                    // If buffer is now empty, we need more data from network
                    if (context.handshakeBuffer.empty()) {
                        context.waitingForData = true;
                        break; // Exit loop, wait for network read
                    }
                    else {
                        // Data remains, loop again to call ASC with the rest
                        moreProcessingNeeded = true;
                    }
                }
                else {
                    GANL_SCHANNEL_DEBUG(conn, "Error: CONTINUE_NEEDED but invalid bytesConsumed!");
                    context.handshakeBuffer.clear(); // Clear buffer on error
                    status = SEC_E_INTERNAL_ERROR; // Treat as error
                }


            }
            else if (status == SEC_E_INCOMPLETE_MESSAGE) {
                GANL_SCHANNEL_DEBUG(conn, "AcceptSecurityContext returned SEC_E_INCOMPLETE_MESSAGE.");
                // Did NOT consume input. Need more network data.
                context.waitingForData = true;
                break; // Exit loop

            }
            // Handle other errors (including if status changed to error above)
            if (FAILED(status)) {
                context.lastError = "AcceptSecurityContext failed: " + getSchannelErrorString(status);
                GANL_SCHANNEL_DEBUG(conn, "Error: " << context.lastError);
                context.handshakeBuffer.clear(); // Clear buffer on error
                return TlsResult::Error; // Return error immediately
            }

        } // End while(moreProcessingNeeded && !context.handshakeBuffer.empty())

        // --- Determine overall result after loop ---
        if (context.established) {
            return TlsResult::Success;
        }
        else if (!encrypted_out.empty()) {
            // We generated output to send, client needs to respond
            return TlsResult::WantWrite; // Or maybe WantRead if we also need input? Prefer WantWrite if output generated.
        }
        else {
            // Not established, generated no output, must need more input
            context.waitingForData = true;
            return TlsResult::WantRead;
        }
    }

    TlsResult SchannelTransport::decryptMessage(ConnectionHandle conn, IoBuffer& encrypted_in, IoBuffer& decrypted_out) {
        auto it = sessions_.find(conn);
        if (it == sessions_.end()) {
            lastGlobalError_ = "No session context for connection";
            GANL_SCHANNEL_DEBUG(conn, "Error: " << lastGlobalError_);
            return TlsResult::Error;
        }

        SessionContext& context = *it->second;

        // Ensure we have a security context
        if (!context.contextHandleInitialized) {
            context.lastError = "Security context not initialized";
            GANL_SCHANNEL_DEBUG(conn, "Error: " << context.lastError);
            return TlsResult::Error;
        }

        // Add incoming data to incomplete buffer if we have any
        if (encrypted_in.readableBytes() > 0) {
            size_t originalSize = context.incompleteBuffer.size();
            context.incompleteBuffer.resize(originalSize + encrypted_in.readableBytes());
            memcpy(context.incompleteBuffer.data() + originalSize, encrypted_in.readPtr(), encrypted_in.readableBytes());
            encrypted_in.consumeRead(encrypted_in.readableBytes());

            GANL_SCHANNEL_DEBUG(conn, "Added " << context.incompleteBuffer.size() - originalSize
                << " bytes to incomplete buffer (total: " << context.incompleteBuffer.size() << ")");
        }

        // If there's no data to decrypt, return success
        if (context.incompleteBuffer.empty()) {
            return TlsResult::Success;
        }

        // Prepare decryption buffers
        SecBuffer buffers[4];
        buffers[0].pvBuffer = context.incompleteBuffer.data();
        buffers[0].cbBuffer = static_cast<DWORD>(context.incompleteBuffer.size());
        buffers[0].BufferType = SECBUFFER_DATA;

        buffers[1].pvBuffer = NULL;
        buffers[1].cbBuffer = 0;
        buffers[1].BufferType = SECBUFFER_EMPTY;

        buffers[2].pvBuffer = NULL;
        buffers[2].cbBuffer = 0;
        buffers[2].BufferType = SECBUFFER_EMPTY;

        buffers[3].pvBuffer = NULL;
        buffers[3].cbBuffer = 0;
        buffers[3].BufferType = SECBUFFER_EMPTY;

        SecBufferDesc bufferDesc;
        bufferDesc.ulVersion = SECBUFFER_VERSION;
        bufferDesc.cBuffers = 4;
        bufferDesc.pBuffers = buffers;

        // Decrypt the message
        SECURITY_STATUS status = DecryptMessage(&context.contextHandle, &bufferDesc, 0, NULL);

        // Handle different status codes
        if (status == SEC_E_OK) {
            GANL_SCHANNEL_DEBUG(conn, "DecryptMessage successful");

            // Find the data buffer (should be in buffers[1])
            SecBuffer* pDataBuffer = NULL;
            SecBuffer* pExtraBuffer = NULL;

            for (int i = 0; i < 4; i++) {
                if (buffers[i].BufferType == SECBUFFER_DATA) {
                    pDataBuffer = &buffers[i];
                }
                else if (buffers[i].BufferType == SECBUFFER_EXTRA) {
                    pExtraBuffer = &buffers[i];
                }
            }

            // Copy decrypted data to output buffer
            if (pDataBuffer != NULL && pDataBuffer->cbBuffer > 0) {
                decrypted_out.append(pDataBuffer->pvBuffer, pDataBuffer->cbBuffer);
                GANL_SCHANNEL_DEBUG(conn, "Decrypted " << pDataBuffer->cbBuffer << " bytes");
            }

            // Save any extra data for next operation
            if (pExtraBuffer != NULL && pExtraBuffer->cbBuffer > 0) {
                memmove(context.incompleteBuffer.data(), pExtraBuffer->pvBuffer, pExtraBuffer->cbBuffer);
                context.incompleteBuffer.resize(pExtraBuffer->cbBuffer);
                GANL_SCHANNEL_DEBUG(conn, "Saved " << pExtraBuffer->cbBuffer << " bytes of extra data");
            }
            else {
                context.incompleteBuffer.clear();
            }

            return TlsResult::Success;
        }
        else if (status == SEC_E_INCOMPLETE_MESSAGE) {
            // Need more data
            GANL_SCHANNEL_DEBUG(conn, "DecryptMessage needs more data");
            context.waitingForData = true;
            return TlsResult::WantRead;
        }
        else if (status == SEC_I_RENEGOTIATE) {
            // Renegotiation needed
            GANL_SCHANNEL_DEBUG(conn, "Renegotiation requested by peer");
            context.needsRenegotiate = true;
            context.established = false;

            // Save any extra data for next operation
            SecBuffer* pExtraBuffer = NULL;
            for (int i = 0; i < 4; i++) {
                if (buffers[i].BufferType == SECBUFFER_EXTRA) {
                    pExtraBuffer = &buffers[i];
                    break;
                }
            }

            if (pExtraBuffer != NULL && pExtraBuffer->cbBuffer > 0) {
                // Copy to handshake buffer for renegotiation
                context.handshakeBuffer.resize(pExtraBuffer->cbBuffer);
                memcpy(context.handshakeBuffer.data(), pExtraBuffer->pvBuffer, pExtraBuffer->cbBuffer);
                GANL_SCHANNEL_DEBUG(conn, "Saved " << pExtraBuffer->cbBuffer << " bytes for renegotiation");
            }

            context.incompleteBuffer.clear();
            return TlsResult::WantRead;
        }
        else if (status == SEC_I_CONTEXT_EXPIRED) {
            // Connection is being closed
            GANL_SCHANNEL_DEBUG(conn, "Context expired (connection closing)");
            context.established = false;
            context.incompleteBuffer.clear();
            return TlsResult::Closed;
        }
        else {
            // Other error
            context.lastError = "DecryptMessage failed: " + getSchannelErrorString(status);
            GANL_SCHANNEL_DEBUG(conn, "Error: " << context.lastError);
            context.incompleteBuffer.clear();
            return TlsResult::Error;
        }
    }

    TlsResult SchannelTransport::encryptMessage(ConnectionHandle conn, IoBuffer& plain_in, IoBuffer& encrypted_out) {
        auto it = sessions_.find(conn);
        if (it == sessions_.end()) {
            lastGlobalError_ = "No session context for connection";
            GANL_SCHANNEL_DEBUG(conn, "Error: " << lastGlobalError_);
            return TlsResult::Error;
        }

        SessionContext& context = *it->second;

        // Ensure we have a security context and stream sizes
        if (!context.contextHandleInitialized || !context.streamSizesSet) {
            context.lastError = "Security context not fully initialized";
            GANL_SCHANNEL_DEBUG(conn, "Error: " << context.lastError);
            return TlsResult::Error;
        }

        // Get stream sizes
        const SecPkgContext_StreamSizes& sizes = context.streamSizes;

        // Calculate the message size and ensure it's not too large
        size_t dataSize = plain_in.readableBytes();
        if (dataSize > sizes.cbMaximumMessage) {
            context.lastError = "Message too large for Schannel";
            GANL_SCHANNEL_DEBUG(conn, "Error: " << context.lastError
                << " (" << dataSize << " > " << sizes.cbMaximumMessage << ")");
            return TlsResult::Error;
        }

        // Calculate the total size needed for the message
        size_t totalSize = sizes.cbHeader + dataSize + sizes.cbTrailer;

        // Allocate a buffer for the encrypted message
        std::vector<BYTE> messageBuffer(totalSize);

        // Copy the data to the message buffer (after the header)
        memcpy(messageBuffer.data() + sizes.cbHeader, plain_in.readPtr(), dataSize);

        // Prepare encryption buffers
        SecBuffer buffers[4];
        buffers[0].pvBuffer = messageBuffer.data();
        buffers[0].cbBuffer = sizes.cbHeader;
        buffers[0].BufferType = SECBUFFER_STREAM_HEADER;

        buffers[1].pvBuffer = messageBuffer.data() + sizes.cbHeader;
        buffers[1].cbBuffer = static_cast<DWORD>(dataSize);
        buffers[1].BufferType = SECBUFFER_DATA;

        buffers[2].pvBuffer = messageBuffer.data() + sizes.cbHeader + dataSize;
        buffers[2].cbBuffer = sizes.cbTrailer;
        buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;

        buffers[3].pvBuffer = NULL;
        buffers[3].cbBuffer = 0;
        buffers[3].BufferType = SECBUFFER_EMPTY;

        SecBufferDesc bufferDesc;
        bufferDesc.ulVersion = SECBUFFER_VERSION;
        bufferDesc.cBuffers = 4;
        bufferDesc.pBuffers = buffers;

        // Encrypt the message
        SECURITY_STATUS status = EncryptMessage(&context.contextHandle, 0, &bufferDesc, 0);

        if (status == SEC_E_OK) {
            GANL_SCHANNEL_DEBUG(conn, "EncryptMessage successful");

            // Calculate the total size of the encrypted message
            size_t encryptedSize = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;

            // Copy the encrypted message to the output buffer
            encrypted_out.ensureWritable(encryptedSize);
            char* dest = encrypted_out.writePtr();

            memcpy(dest, buffers[0].pvBuffer, buffers[0].cbBuffer);
            dest += buffers[0].cbBuffer;

            memcpy(dest, buffers[1].pvBuffer, buffers[1].cbBuffer);
            dest += buffers[1].cbBuffer;

            memcpy(dest, buffers[2].pvBuffer, buffers[2].cbBuffer);

            encrypted_out.commitWrite(encryptedSize);
            GANL_SCHANNEL_DEBUG(conn, "Encrypted " << dataSize << " bytes to " << encryptedSize << " bytes");

            // Consume the input data
            plain_in.consumeRead(dataSize);

            return TlsResult::Success;
        }
        else {
            // Error encrypting
            context.lastError = "EncryptMessage failed: " + getSchannelErrorString(status);
            GANL_SCHANNEL_DEBUG(conn, "Error: " << context.lastError);
            return TlsResult::Error;
        }
    }

    bool SchannelTransport::acquireCredentials(SessionContext& context) {
        // Define credential flags
        DWORD dwFlags = 0;

        if (context.isServer) {
            // Server-specific flags
            dwFlags = SCH_CRED_NO_SYSTEM_MAPPER | SCH_CRED_NO_DEFAULT_CREDS;

            // If we have a server certificate, add it to the credentials
            if (serverCertContext_ != nullptr) {
                dwFlags |= SCH_CRED_MANUAL_CRED_VALIDATION;
            }
        }
        else {
            // Client-specific flags
            dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION;
        }

        // Set up authentication data
        SCHANNEL_CRED schannelCred = { 0 };
        schannelCred.dwVersion = SCHANNEL_CRED_VERSION;

        // Set supported protocols (TLS 1.2 by default)
        schannelCred.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER | SP_PROT_TLS1_2_CLIENT;

        // Set cipher strength
        schannelCred.dwMinimumCipherStrength = 128;

        // Set up server certificate if available and server mode
        if (context.isServer && serverCertContext_ != nullptr) {
            schannelCred.cCreds = 1;
            schannelCred.paCred = &serverCertContext_;
        }

        // Set the credential flags
        schannelCred.dwFlags = dwFlags;

        // Acquire credentials handle
        TimeStamp expiry;
        auto status = AcquireCredentialsHandle(
            NULL,                       // Principal (NULL for current credentials)
            const_cast<LPWSTR>(UNISP_NAME_W), // Package name (Schannel)
            context.isServer ? SECPKG_CRED_INBOUND : SECPKG_CRED_OUTBOUND, // Direction
            NULL,                       // Credentials data
            &schannelCred,              // Schannel credential structure
            NULL,                       // GetKey callback
            NULL,                       // GetKey callback context
            &context.credHandle,        // Credential handle
            &expiry                     // Credential expiry
        );

        if (FAILED(status)) {
            context.lastError = "AcquireCredentialsHandle failed: " + getSchannelErrorString(status);
            return false;
        }

        context.credHandleInitialized = true;
        return true;
    }

    // Function acceptSecurityContext removed as its functionality is correctly implemented within the performHandshake method
    bool SchannelTransport::initializeSecurityContext(SessionContext& context, IoBuffer& encrypted_in, IoBuffer& encrypted_out) {
        // This would implement client-side TLS handshake, not needed for now
        context.lastError = "Client-side handshake not implemented";
        return false;
    }

    bool SchannelTransport::queryStreamSizes(SessionContext& context) {
        SECURITY_STATUS status = QueryContextAttributes(
            &context.contextHandle,
            SECPKG_ATTR_STREAM_SIZES,
            &context.streamSizes
        );

        if (FAILED(status)) {
            context.lastError = "QueryContextAttributes failed: " + getSchannelErrorString(status);
            return false;
        }

        context.streamSizesSet = true;
        GANL_SCHANNEL_DEBUG(0, "Stream sizes: Header=" << context.streamSizes.cbHeader
            << ", Trailer=" << context.streamSizes.cbTrailer
            << ", MaxMessage=" << context.streamSizes.cbMaximumMessage);
        return true;
    }

    TlsResult SchannelTransport::mapSchannelError(SECURITY_STATUS status, SessionContext& context) {
        switch (status) {
        case SEC_E_OK:
            return TlsResult::Success;

        case SEC_E_INCOMPLETE_MESSAGE:
            context.waitingForData = true;
            return TlsResult::WantRead;

        case SEC_I_CONTINUE_NEEDED:
            return TlsResult::WantRead;

        case SEC_I_CONTEXT_EXPIRED:
            return TlsResult::Closed;

        default:
            return TlsResult::Error;
        }
    }

    std::string SchannelTransport::getSchannelErrorString(SECURITY_STATUS status) {
        // Format the Windows error code
        char* buffer = nullptr;
        DWORD result = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            status,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&buffer,
            0,
            NULL
        );

        if (result == 0 || buffer == nullptr) {
            return "Unknown Schannel error: " + std::to_string(status);
        }

        std::string message(buffer);
        LocalFree(buffer);

        // Trim trailing whitespace and newlines
        size_t endpos = message.find_last_not_of(" \n\r\t");
        if (std::string::npos != endpos) {
            message = message.substr(0, endpos + 1);
        }

        return message + " (0x" + std::to_string(status) + ")";
    }

} // namespace ganl
