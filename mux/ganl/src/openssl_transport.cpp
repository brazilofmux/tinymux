#include "openssl_transport.h"
#include <iostream>
#include <openssl/err.h>
#include <vector>
#include <mutex>

// Define a macro for debug logging
#ifndef NDEBUG // Only compile debug messages if NDEBUG is not defined
#define GANL_SSL_DEBUG(conn, x) \
    do { std::cerr << "[OpenSSL:" << (conn == 0 ? "Global" : std::to_string(conn)) << "] " << x << std::endl; } while (0)
#else
#define GANL_SSL_DEBUG(conn, x) do {} while (0)
#endif

namespace ganl {

// --- Constructor / Destructor ---

OpenSSLTransport::OpenSSLTransport() {
    GANL_SSL_DEBUG(0, "Handler Created.");
}

OpenSSLTransport::~OpenSSLTransport() {
    GANL_SSL_DEBUG(0, "Handler Destroyed.");
    shutdown(); // Ensure cleanup happens
}

// --- Global Initialization / Shutdown ---

bool OpenSSLTransport::initialize(const TlsConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    GANL_SSL_DEBUG(0, "Initializing OpenSSL library...");
    // Initialize OpenSSL (thread-safe in modern OpenSSL >= 1.1.0)
    // SSL_library_init(); // Deprecated since 1.1.0, OPENSSL_init_ssl is preferred
    if (OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr) == 0) {
         lastGlobalError_ = "OPENSSL_init_ssl failed: " + getOpenSSLErrorString(0);
         std::cerr << "[OpenSSL:Global] FATAL: " << lastGlobalError_ << std::endl;
         return false;
    }
    // OpenSSL_add_all_algorithms(); // Deprecated since 1.1.0

    GANL_SSL_DEBUG(0, "Creating SSL_CTX...");
    // Create SSL context (use TLS_server_method for modern TLS)
    // ctx_ = SSL_CTX_new(SSLv23_server_method()); // Allows negotiation down to TLS 1.0
    ctx_ = SSL_CTX_new(TLS_server_method()); // Prefer modern TLS versions
    if (!ctx_) {
        lastGlobalError_ = "SSL_CTX_new failed: " + getOpenSSLErrorString(0);
        std::cerr << "[OpenSSL:Global] FATAL: " << lastGlobalError_ << std::endl;
        // ERR_free_strings(); // No need if OPENSSL_init_ssl was used
        // EVP_cleanup(); // No need if OPENSSL_init_ssl was used
        return false;
    }

    // Set secure options: disable legacy protocols, enable best practices
    long options = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1; // Require TLS 1.2+
    options |= SSL_OP_ALL; // Enable bug workarounds
    options |= SSL_OP_SINGLE_DH_USE;
    options |= SSL_OP_SINGLE_ECDH_USE;
    SSL_CTX_set_options(ctx_, options);

    // Prefer server cipher order
    SSL_CTX_set_options(ctx_, SSL_OP_CIPHER_SERVER_PREFERENCE);
    // Consider setting cipher list: SSL_CTX_set_cipher_list(ctx_, "HIGH:!aNULL:!MD5:!RC4");

    // Load certificate and key
    if (!config.certificateFile.empty() && !config.keyFile.empty()) {
         GANL_SSL_DEBUG(0, "Loading cert: " << config.certificateFile << ", key: " << config.keyFile);
         if (SSL_CTX_use_certificate_chain_file(ctx_, config.certificateFile.c_str()) != 1) { // Use chain file for intermediates
             lastGlobalError_ = "Failed to load certificate chain file: " + getOpenSSLErrorString(0);
             std::cerr << "[OpenSSL:Global] FATAL: " << lastGlobalError_ << std::endl;
             SSL_CTX_free(ctx_); ctx_ = nullptr;
             return false;
         }

         // TODO: Add password callback support if keys are encrypted
         // SSL_CTX_set_default_passwd_cb(ctx_, password_callback_function);
         // SSL_CTX_set_default_passwd_cb_userdata(ctx_, (void*)"your_password_here");

         if (SSL_CTX_use_PrivateKey_file(ctx_, config.keyFile.c_str(), SSL_FILETYPE_PEM) != 1) {
             lastGlobalError_ = "Failed to load private key file: " + getOpenSSLErrorString(0);
             std::cerr << "[OpenSSL:Global] FATAL: " << lastGlobalError_ << std::endl;
             SSL_CTX_free(ctx_); ctx_ = nullptr;
             return false;
         }

         GANL_SSL_DEBUG(0, "Checking private key match...");
         if (SSL_CTX_check_private_key(ctx_) != 1) {
             lastGlobalError_ = "Private key does not match certificate: " + getOpenSSLErrorString(0);
             std::cerr << "[OpenSSL:Global] FATAL: " << lastGlobalError_ << std::endl;
             SSL_CTX_free(ctx_); ctx_ = nullptr;
             return false;
         }
         GANL_SSL_DEBUG(0, "Certificate and key loaded successfully.");
    } else {
         GANL_SSL_DEBUG(0, "Warning: No certificate or key file provided. TLS will likely fail.");
         // Allow initialization to succeed, but connections will fail handshake later.
    }

    GANL_SSL_DEBUG(0, "OpenSSLTransport initialized successfully.");
    return true;
}

void OpenSSLTransport::shutdown() {
    // Using the revised shutdown logic which avoids re-locking
    GANL_SSL_DEBUG(0, "Shutting down (Revised)...");
    std::vector<SSL*> sslObjectsToFree;
    SSL_CTX* ctxToFree = nullptr; // Renamed from ctxToFreeRevised for clarity

    { // --- Lock Scope Start ---
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ctx_) { // Check if already shut down
             GANL_SSL_DEBUG(0, "Already shut down.");
             return;
        }
        sslObjectsToFree.reserve(sessions_.size());
        GANL_SSL_DEBUG(0, "Gathering " << sessions_.size() << " SSL objects to free...");
        // Iterate and move SSL pointers out of the map contexts
        for (auto& pair : sessions_) {
            if (pair.second.ssl) {
                sslObjectsToFree.push_back(pair.second.ssl);
                pair.second.ssl = nullptr; // Null out pointer in map context
                pair.second.readBio = nullptr;
                pair.second.writeBio = nullptr;
            }
        }
        sessions_.clear(); // Clear map *after* extracting pointers

        ctxToFree = ctx_; // Copy global context pointer
        ctx_ = nullptr;  // Null out global context pointer
    } // --- Lock Scope End ---

    // --- Perform freeing outside the lock ---
    GANL_SSL_DEBUG(0, "Freeing " << sslObjectsToFree.size() << " SSL objects (outside lock)...");
    for (SSL* ssl : sslObjectsToFree) {
        SSL_free(ssl); // This also frees associated BIOs
    }

    if (ctxToFree) {
        GANL_SSL_DEBUG(0, "Freeing SSL_CTX (outside lock).");
        SSL_CTX_free(ctxToFree);
    }
    // --- End freeing ---

    GANL_SSL_DEBUG(0, "Shutdown complete.");
}

// --- Per-Connection Context Management ---

bool OpenSSLTransport::createSessionContext(ConnectionHandle conn, bool isServer) {
    GANL_SSL_DEBUG(conn, "Creating session context. isServer=" << isServer);

    SSLContext context; // Create context locally first
    SSL_CTX* localCtx = nullptr;

    // --- Check prerequisites under lock ---
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ctx_) {
            lastGlobalError_ = "SSL context not initialized";
            GANL_SSL_DEBUG(conn, "Error: " << lastGlobalError_);
            return false;
        }
        if (sessions_.count(conn)) {
            GANL_SSL_DEBUG(conn, "Error: Session context already exists.");
            return false;
        }
        localCtx = ctx_; // Copy ctx pointer safely
    } // --- Lock released ---

    // --- Perform OpenSSL operations outside the lock ---
    GANL_SSL_DEBUG(conn, "Creating SSL object...");
    context.ssl = SSL_new(localCtx);
    if (!context.ssl) {
        context.lastError = "SSL_new failed: " + getOpenSSLErrorString(conn);
        GANL_SSL_DEBUG(conn, "Error: " << context.lastError);
        return false;
    }

    GANL_SSL_DEBUG(conn, "Creating memory BIOs...");
    context.readBio = BIO_new(BIO_s_mem());
    context.writeBio = BIO_new(BIO_s_mem());
    if (!context.readBio || !context.writeBio) {
        context.lastError = "BIO_new failed: " + getOpenSSLErrorString(conn);
        GANL_SSL_DEBUG(conn, "Error: " << context.lastError);
        // Cleanup locally created resources
        if (context.readBio) BIO_free(context.readBio);
        if (context.writeBio) BIO_free(context.writeBio);
        SSL_free(context.ssl); // context.ssl is guaranteed non-null here
        return false;
    }

    BIO_set_mem_eof_return(context.readBio, -1);
    BIO_set_mem_eof_return(context.writeBio, -1);

    GANL_SSL_DEBUG(conn, "Setting BIOs for SSL object...");
    SSL_set_bio(context.ssl, context.readBio, context.writeBio); // SSL takes ownership of BIOs here

    if (isServer) {
        GANL_SSL_DEBUG(conn, "Setting SSL accept state.");
        SSL_set_accept_state(context.ssl);
    } else {
        GANL_SSL_DEBUG(conn, "Setting SSL connect state.");
        SSL_set_connect_state(context.ssl);
    }
    // --- End OpenSSL operations ---

    // --- Insert into map under lock ---
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Double-check for concurrent creation before inserting
        if (sessions_.count(conn)) {
             GANL_SSL_DEBUG(conn, "Error: Session context created concurrently. Cleaning up.");
             SSL_free(context.ssl); // Frees BIOs too
             return false;
        }
        // Move the fully prepared local context into the map
        sessions_.emplace(conn, std::move(context));
    } // --- Lock released ---

    GANL_SSL_DEBUG(conn, "Session context created and stored successfully.");
    return true;
}

void OpenSSLTransport::destroySessionContext(ConnectionHandle conn) {
    GANL_SSL_DEBUG(conn, "Destroying session context...");
    SSL* sslToFree = nullptr;

    // --- Find and remove from map under lock ---
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(conn);
        if (it != sessions_.end()) {
            // Extract SSL pointer before erasing
            sslToFree = it->second.ssl;
            it->second.ssl = nullptr; // Prevent potential double-free in SSLContext destructor if it existed
            sessions_.erase(it);
            GANL_SSL_DEBUG(conn, "Session context removed from map.");
        } else {
            GANL_SSL_DEBUG(conn, "Warning: Context not found for destruction.");
            return; // Nothing found, nothing to free
        }
    } // --- Lock released ---

    // --- Free SSL object outside lock ---
    if (sslToFree) {
        GANL_SSL_DEBUG(conn, "Freeing SSL object (outside lock).");
        SSL_free(sslToFree); // Frees associated BIOs as well
    }
    // --- End free ---
    GANL_SSL_DEBUG(conn, "Session context destroyed.");
}

// --- Data Processing ---
// These methods primarily operate on a single connection's context.
// We need to retrieve the context pointer safely, but the SSL/BIO operations
// on that specific context can generally happen outside the main lock,
// as long as no other thread is simultaneously operating on the *same* context.
// The design assumes one thread calls processEvents, which then calls these handlers.

// Helper function to get context (maybe use unique_lock for potential re-locking?)
// Using a raw pointer requires care that the context isn't destroyed concurrently.
// Returning a copy isn't feasible. Let's lock, get ptr, unlock, operate, re-lock if needed.
// Or, pass the lock down? No, keep lock scope minimal.

TlsResult OpenSSLTransport::processIncoming(ConnectionHandle conn, IoBuffer& encrypted_in,
                                           IoBuffer& decrypted_out, IoBuffer& encrypted_out,
                                           bool consumeInput) {
    SSLContext* contextPtr = nullptr;

    // --- Get context pointer under lock ---
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(conn);
        if (it == sessions_.end()) {
            // Should we set lastGlobalError_? It requires lock. Yes.
            lastGlobalError_ = "No SSL context for connection";
            GANL_SSL_DEBUG(conn, "Error: " << lastGlobalError_);
            return TlsResult::Error;
        }
        // Check if SSL object is valid (might have been nulled during concurrent shutdown/destroy)
        if (!it->second.ssl) {
             GANL_SSL_DEBUG(conn, "Error: SSL object is null (likely destroyed).");
             return TlsResult::Error; // Or Closed? Error seems safer.
        }
        contextPtr = &it->second;
    } // --- Lock released ---

    // --- Operate on contextPtr outside lock ---
    SSLContext& context = *contextPtr; // Use reference
    TlsResult finalResult = TlsResult::Success; // Default assumption

    GANL_SSL_DEBUG(conn, "Processing Incoming. EncryptedIn=" << encrypted_in.readableBytes()
             << ", State: " << SSL_state_string_long(context.ssl)
             << ", Established: " << context.established);

    // 1. Feed data to BIO (outside lock)
    if (encrypted_in.readableBytes() > 0) {
        int written = BIO_write(context.readBio, encrypted_in.readPtr(), encrypted_in.readableBytes());
        GANL_SSL_DEBUG(conn, "BIO_write(" << encrypted_in.readableBytes() << ") returned " << written);
        if (written > 0) {
            if (consumeInput) {
                encrypted_in.consumeRead(written);
                GANL_SSL_DEBUG(conn, "Consumed " << written << " bytes from encrypted_in. Remaining=" << encrypted_in.readableBytes());
            } else {
                GANL_SSL_DEBUG(conn, "Did not consume " << written << " bytes from encrypted_in (consumeInput=false)");
            }
        } else { // BIO_write returns <= 0 is an error for memory BIO unless retry? Treat as error.
             context.lastError = "BIO_write failed: " + getOpenSSLErrorString(conn);
             GANL_SSL_DEBUG(conn, "Error: " << context.lastError);
             return TlsResult::Error;
        }
    }

    // 2. Loop processing (SSL_do_handshake / SSL_read) (outside lock)
    bool processingNeeded = true;
    while (processingNeeded) {
        processingNeeded = false;
        int ssl_ret = 0;
        int ssl_err = SSL_ERROR_NONE;
        char readBuffer[16384]; // Local buffer on stack

        // Clear OpenSSL error queue before calling SSL functions
        ERR_clear_error();

        if (!context.established) {
            GANL_SSL_DEBUG(conn, "Attempting SSL_do_handshake()...");
            ssl_ret = SSL_do_handshake(context.ssl);
            ssl_err = SSL_get_error(context.ssl, ssl_ret);
            GANL_SSL_DEBUG(conn, "SSL_do_handshake() returned " << ssl_ret << ", SSL_get_error()=" << ssl_err);

            if (ssl_ret > 0) { // Handshake SUCCESS
                 GANL_SSL_DEBUG(conn, "Handshake SUCCESS!");
                 context.established = true;
                 processingNeeded = true; // Re-loop to try SSL_read immediately
            } else if (ssl_err == SSL_ERROR_WANT_READ) {
                 GANL_SSL_DEBUG(conn, "Handshake wants more data (SSL_ERROR_WANT_READ).");
                 finalResult = TlsResult::WantRead;
            } else if (ssl_err == SSL_ERROR_WANT_WRITE) {
                 GANL_SSL_DEBUG(conn, "Handshake wants to write data (SSL_ERROR_WANT_WRITE).");
                 finalResult = TlsResult::WantWrite;
            } else { // Handshake Error
                 context.lastError = "SSL_do_handshake failed: " + getOpenSSLErrorString(conn);
                 GANL_SSL_DEBUG(conn, "Error: " << context.lastError);
                 finalResult = TlsResult::Error;
            }
        } else { // context.established == true
            GANL_SSL_DEBUG(conn, "Attempting SSL_read()...");
            ssl_ret = SSL_read(context.ssl, readBuffer, sizeof(readBuffer));
            ssl_err = SSL_get_error(context.ssl, ssl_ret);
            GANL_SSL_DEBUG(conn, "SSL_read() returned " << ssl_ret << ", SSL_get_error()=" << ssl_err);

            if (ssl_ret > 0) { // Read SUCCESS
                 GANL_SSL_DEBUG(conn, "Read " << ssl_ret << " bytes of decrypted application data.");
                 decrypted_out.append(readBuffer, ssl_ret);
                 processingNeeded = true; // Re-loop to try SSL_read again
            } else if (ssl_ret == 0) { // Clean shutdown
                 GANL_SSL_DEBUG(conn, "Peer notified shutdown via SSL_read() returning 0.");
                 finalResult = TlsResult::Closed;
            } else { // ssl_ret < 0
                 if (ssl_err == SSL_ERROR_WANT_READ) {
                     GANL_SSL_DEBUG(conn, "SSL_read wants more data (SSL_ERROR_WANT_READ).");
                     finalResult = TlsResult::WantRead;
                 } else if (ssl_err == SSL_ERROR_WANT_WRITE) {
                     GANL_SSL_DEBUG(conn, "SSL_read wants to write data (SSL_ERROR_WANT_WRITE - renegotiation?).");
                     finalResult = TlsResult::WantWrite;
                 } else if (ssl_err == SSL_ERROR_ZERO_RETURN) { // Double check for clean shutdown
                      GANL_SSL_DEBUG(conn, "Peer notified shutdown via SSL_ERROR_ZERO_RETURN.");
                      finalResult = TlsResult::Closed;
                 } else { // Read Error
                      context.lastError = "SSL_read failed: " + getOpenSSLErrorString(conn);
                      GANL_SSL_DEBUG(conn, "Error: " << context.lastError);
                      finalResult = TlsResult::Error;
                 }
            }
        } // end else (context.established)

        // If an error or final state occurred, stop the processing loop
        if (finalResult != TlsResult::Success) {
             processingNeeded = false;
        }
    } // end while(processingNeeded)

    // 3. Drain the write BIO (outside lock)
    int pending = BIO_pending(context.writeBio);
    if (pending > 0) {
        GANL_SSL_DEBUG(conn, "Draining " << pending << " bytes from write BIO -> encrypted_out.");
        encrypted_out.ensureWritable(pending);
        int bytes_read = BIO_read(context.writeBio, encrypted_out.writePtr(), pending);
        if (bytes_read > 0) {
            encrypted_out.commitWrite(bytes_read);
            GANL_SSL_DEBUG(conn, "Drained " << bytes_read << " bytes. encrypted_out size=" << encrypted_out.readableBytes());
            // If the operation result wasn't an error/close, and we drained data,
            // it implies the caller needs to send this data. Signal WantWrite.
            if (finalResult != TlsResult::Error && finalResult != TlsResult::Closed) {
                 finalResult = TlsResult::WantWrite;
            }
        } else { // BIO_read error
             context.lastError = "BIO_read from write BIO failed: " + getOpenSSLErrorString(conn);
             GANL_SSL_DEBUG(conn, "Error: " << context.lastError);
             // Prioritize error over previous state
             finalResult = TlsResult::Error;
        }
    } else {
         GANL_SSL_DEBUG(conn, "Write BIO is empty after processing.");
    }

    GANL_SSL_DEBUG(conn, "processIncoming finished. Final Result: " << static_cast<int>(finalResult));
    return finalResult;
}

TlsResult OpenSSLTransport::processOutgoing(ConnectionHandle conn, IoBuffer& plain_in,
                                           IoBuffer& encrypted_out, bool consumeInput) {
    SSLContext* contextPtr = nullptr;
    // --- Get context pointer under lock ---
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(conn);
        if (it == sessions_.end()) {
            lastGlobalError_ = "No SSL context for connection";
            GANL_SSL_DEBUG(conn, "Error: " << lastGlobalError_);
            return TlsResult::Error;
        }
         if (!it->second.ssl) { // Check for null SSL*
             GANL_SSL_DEBUG(conn, "Error: SSL object is null (likely destroyed).");
             return TlsResult::Error;
        }
        contextPtr = &it->second;
    } // --- Lock released ---

    SSLContext& context = *contextPtr;
    TlsResult finalResult = TlsResult::Success;

    GANL_SSL_DEBUG(conn, "Processing Outgoing. PlainIn=" << plain_in.readableBytes()
             << ", State: " << SSL_state_string_long(context.ssl)
             << ", Established: " << context.established);

    if (!context.established) {
        context.lastError = "Cannot send data: SSL handshake not complete";
        GANL_SSL_DEBUG(conn, "Error: " << context.lastError);
        return TlsResult::Error;
    }

    // 1. Write plain data using SSL_write (outside lock)
    if (plain_in.readableBytes() > 0) {
        GANL_SSL_DEBUG(conn, "Attempting SSL_write(" << plain_in.readableBytes() << " bytes)...");
        ERR_clear_error(); // Clear error queue before call
        int bytesWritten = SSL_write(context.ssl, plain_in.readPtr(), plain_in.readableBytes());
        int ssl_err = SSL_get_error(context.ssl, bytesWritten);
        GANL_SSL_DEBUG(conn, "SSL_write() returned " << bytesWritten << ", SSL_get_error()=" << ssl_err);

        if (bytesWritten > 0) {
            if (consumeInput) {
                plain_in.consumeRead(bytesWritten);
                GANL_SSL_DEBUG(conn, "Consumed " << bytesWritten << " bytes from plain_in. Remaining=" << plain_in.readableBytes());
            } else {
                GANL_SSL_DEBUG(conn, "Did not consume " << bytesWritten << " bytes from plain_in (consumeInput=false)");
            }
            // Data is now in writeBio, need to drain it. Success so far.
            finalResult = TlsResult::Success;
        } else { // bytesWritten <= 0
            if (ssl_err == SSL_ERROR_WANT_READ) {
                GANL_SSL_DEBUG(conn, "SSL_write wants read (SSL_ERROR_WANT_READ - renegotiation?).");
                finalResult = TlsResult::WantRead;
            } else if (ssl_err == SSL_ERROR_WANT_WRITE) {
                GANL_SSL_DEBUG(conn, "SSL_write wants write (SSL_ERROR_WANT_WRITE - BIO full?).");
                 finalResult = TlsResult::WantWrite;
            } else {
                context.lastError = "SSL_write failed: " + getOpenSSLErrorString(conn);
                GANL_SSL_DEBUG(conn, "Error: " << context.lastError);
                finalResult = TlsResult::Error;
                return finalResult; // Return early on fundamental write error
            }
        }
    } else {
        GANL_SSL_DEBUG(conn, "No plain input data to write.");
    }

    // 2. Drain the write BIO (outside lock)
    int pending = BIO_pending(context.writeBio);
    if (pending > 0) {
        GANL_SSL_DEBUG(conn, "Draining " << pending << " bytes from write BIO -> encrypted_out.");
        encrypted_out.ensureWritable(pending);
        int bytes_read = BIO_read(context.writeBio, encrypted_out.writePtr(), pending);
        if (bytes_read > 0) {
            encrypted_out.commitWrite(bytes_read);
            GANL_SSL_DEBUG(conn, "Drained " << bytes_read << " bytes. encrypted_out size=" << encrypted_out.readableBytes());
            // If the result wasn't error/close, signal WantWrite because data was drained.
             if (finalResult != TlsResult::Error && finalResult != TlsResult::Closed) {
                 finalResult = TlsResult::WantWrite;
             }
        } else { // BIO_read error
             context.lastError = "BIO_read from write BIO failed: " + getOpenSSLErrorString(conn);
             GANL_SSL_DEBUG(conn, "Error: " << context.lastError);
             finalResult = TlsResult::Error; // Prioritize error
        }
    } else {
         GANL_SSL_DEBUG(conn, "Write BIO is empty after processing.");
    }

    GANL_SSL_DEBUG(conn, "processOutgoing finished. Final Result: " << static_cast<int>(finalResult));
    return finalResult;
}

TlsResult OpenSSLTransport::shutdownSession(ConnectionHandle conn, IoBuffer& encrypted_out) {
    SSLContext* contextPtr = nullptr;
    // --- Get context pointer under lock ---
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(conn);
        if (it == sessions_.end()) {
            lastGlobalError_ = "No SSL context for connection";
            GANL_SSL_DEBUG(conn, "Error: " << lastGlobalError_);
            return TlsResult::Error;
        }
        if (!it->second.ssl) { // Check for null SSL*
             GANL_SSL_DEBUG(conn, "Error: SSL object is null (likely destroyed).");
             return TlsResult::Error;
        }
        contextPtr = &it->second;
    } // --- Lock released ---

    SSLContext& context = *contextPtr;
    TlsResult finalResult = TlsResult::Closed; // Assume complete unless I/O needed

    GANL_SSL_DEBUG(conn, "Initiating SSL shutdown. Current SSL shutdown state: " << SSL_get_shutdown(context.ssl));

    // Call SSL_shutdown() (outside lock)
    ERR_clear_error(); // Clear error queue
    int ret = SSL_shutdown(context.ssl);
    int ssl_err = SSL_get_error(context.ssl, ret);
    GANL_SSL_DEBUG(conn, "SSL_shutdown() returned " << ret << ", SSL_get_error()=" << ssl_err);

    if (ret == 1) { // Shutdown complete
        GANL_SSL_DEBUG(conn, "SSL shutdown complete (ret=1).");
        finalResult = TlsResult::Closed;
    } else if (ret == 0) { // close_notify sent, wait for peer
        GANL_SSL_DEBUG(conn, "SSL shutdown initiated (ret=0). close_notify sent.");
        finalResult = TlsResult::WantWrite; // Need to send drained data
    } else { // ret < 0
        if (ssl_err == SSL_ERROR_WANT_READ) {
            GANL_SSL_DEBUG(conn, "SSL_shutdown wants read.");
            finalResult = TlsResult::WantRead;
        } else if (ssl_err == SSL_ERROR_WANT_WRITE) {
            GANL_SSL_DEBUG(conn, "SSL_shutdown wants write.");
            finalResult = TlsResult::WantWrite;
        } else {
            context.lastError = "SSL_shutdown failed: " + getOpenSSLErrorString(conn);
            GANL_SSL_DEBUG(conn, "Error: " << context.lastError);
            finalResult = TlsResult::Error;
        }
    }

    // Drain write BIO (outside lock)
    int pending = BIO_pending(context.writeBio);
    if (pending > 0) {
        GANL_SSL_DEBUG(conn, "Draining " << pending << " bytes (likely close_notify) from write BIO.");
        encrypted_out.ensureWritable(pending);
        int bytes_read = BIO_read(context.writeBio, encrypted_out.writePtr(), pending);
        if (bytes_read > 0) {
            encrypted_out.commitWrite(bytes_read);
            GANL_SSL_DEBUG(conn, "Drained " << bytes_read << " bytes.");
            // If result wasn't error/close, ensure WantWrite is set
            if (finalResult != TlsResult::Error && finalResult != TlsResult::Closed) {
                 finalResult = TlsResult::WantWrite;
             }
        } else { // BIO_read error
             context.lastError = "BIO_read from write BIO failed during shutdown: " + getOpenSSLErrorString(conn);
             GANL_SSL_DEBUG(conn, "Error: " << context.lastError);
             finalResult = TlsResult::Error;
        }
    } else {
        GANL_SSL_DEBUG(conn, "Write BIO empty after SSL_shutdown call.");
    }

    GANL_SSL_DEBUG(conn, "shutdownSession finished. Final Result: " << static_cast<int>(finalResult));
    return finalResult;
}

// --- State Accessors ---

bool OpenSSLTransport::isEstablished(ConnectionHandle conn) {
    std::lock_guard<std::mutex> lock(mutex_); // Lock for map access
    auto it = sessions_.find(conn);
    // Use SSL_is_init_finished for more accurate state check
    return (it != sessions_.end() && it->second.established && it->second.ssl && SSL_is_init_finished(it->second.ssl));
}

bool OpenSSLTransport::needsNetworkRead(ConnectionHandle conn) {
    std::lock_guard<std::mutex> lock(mutex_); // Lock for map access
    auto it = sessions_.find(conn);
    if (it == sessions_.end() || !it->second.ssl) {
        return false;
    }
    // Check if SSL wants read AND the read BIO is empty
    bool ssl_wants_read = (SSL_want_read(it->second.ssl) != 0);
    bool bio_has_data = (BIO_ctrl_pending(it->second.readBio) > 0);
    return ssl_wants_read && !bio_has_data;
}

bool OpenSSLTransport::needsNetworkWrite(ConnectionHandle conn) {
    std::lock_guard<std::mutex> lock(mutex_); // Lock for map access
    auto it = sessions_.find(conn);
    if (it == sessions_.end() || !it->second.writeBio) {
        return false;
    }
    // Check if write BIO has pending data
    return (BIO_ctrl_pending(it->second.writeBio) > 0);
}

std::string OpenSSLTransport::getLastTlsErrorString(ConnectionHandle conn) {
    std::string sessionError;
    std::string globalError;
    bool foundSession = false;

    // Get errors under lock
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(conn);
        if (it != sessions_.end()) {
             sessionError = it->second.lastError;
             foundSession = true;
        }
        globalError = lastGlobalError_;
    } // Lock released

    // Prioritize session error
    if (foundSession && !sessionError.empty()) {
        return sessionError;
    }
    // Then global error
    if (!globalError.empty()) {
        return globalError;
    }
    // Fallback to OpenSSL error queue (no lock needed)
    return getOpenSSLErrorString(conn);
}

// --- Private Helpers ---

std::string OpenSSLTransport::getOpenSSLErrorString(ConnectionHandle conn) {
    // Reads thread-local error queue, no mutex needed for this part.
    std::string errorMessages;
    unsigned long errCode;
    char errBuf[256];
    bool first = true;

    while ((errCode = ERR_get_error()) != 0) {
        ERR_error_string_n(errCode, errBuf, sizeof(errBuf));
        if (!first) {
            errorMessages += "; ";
        }
        errorMessages += errBuf;
        first = false;
    }

    return errorMessages.empty() ? "No OpenSSL error reported" : errorMessages;
}

} // namespace ganl
