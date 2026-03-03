#include "schannel_transport.h"
#include <sstream>
#include <algorithm>
#include <cassert>
#include <fstream>

// Define a macro for debug logging (disabled — stdout/stderr not valid on Windows detached process)
#define GANL_SCHANNEL_DEBUG(conn, x) do {} while (0)

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

    // --- Certificate scoring for smart selection ---

    int SchannelTransport::scoreCertForServerTls(PCCERT_CONTEXT cert) {
        // Check for private key — base requirement.
        DWORD keyInfoSize = 0;
        BOOL hasKey = CertGetCertificateContextProperty(cert,
            CERT_KEY_PROV_INFO_PROP_ID, nullptr, &keyInfoSize);
        if (!hasKey) {
            // Also check CNG key handle property.
            NCRYPT_KEY_HANDLE hKey = 0;
            DWORD cbData = sizeof(hKey);
            hasKey = CertGetCertificateContextProperty(cert,
                CERT_NCRYPT_KEY_HANDLE_PROP_ID, &hKey, &cbData);
        }
        if (!hasKey) {
            return 0;
        }

        int score = 1;

        // +10 if NOT a CA cert (check Basic Constraints extension).
        PCERT_EXTENSION bcExt = CertFindExtension(szOID_BASIC_CONSTRAINTS2,
            cert->pCertInfo->cExtension, cert->pCertInfo->rgExtension);
        if (bcExt) {
            DWORD cbDecoded = 0;
            CERT_BASIC_CONSTRAINTS2_INFO* bcInfo = nullptr;
            if (CryptDecodeObjectEx(X509_ASN_ENCODING, szOID_BASIC_CONSTRAINTS2,
                    bcExt->Value.pbData, bcExt->Value.cbData,
                    CRYPT_DECODE_ALLOC_FLAG, nullptr, &bcInfo, &cbDecoded)) {
                if (!bcInfo->fCA) {
                    score += 10;
                }
                LocalFree(bcInfo);
            }
        } else {
            // No Basic Constraints extension — likely a leaf cert.
            score += 10;
        }

        // Check Enhanced Key Usage extension.
        PCERT_EXTENSION ekuExt = CertFindExtension(szOID_ENHANCED_KEY_USAGE,
            cert->pCertInfo->cExtension, cert->pCertInfo->rgExtension);
        if (ekuExt) {
            DWORD cbDecoded = 0;
            CERT_ENHKEY_USAGE* ekuInfo = nullptr;
            if (CryptDecodeObjectEx(X509_ASN_ENCODING, szOID_ENHANCED_KEY_USAGE,
                    ekuExt->Value.pbData, ekuExt->Value.cbData,
                    CRYPT_DECODE_ALLOC_FLAG, nullptr, &ekuInfo, &cbDecoded)) {
                for (DWORD i = 0; i < ekuInfo->cUsageIdentifier; i++) {
                    if (strcmp(ekuInfo->rgpszUsageIdentifier[i],
                              szOID_PKIX_KP_SERVER_AUTH) == 0) {
                        score += 5;
                        break;
                    }
                }
                LocalFree(ekuInfo);
            }
        } else {
            // No EKU extension — unrestricted cert, acceptable.
            score += 2;
        }

        return score;
    }

    // --- Initialize: dispatch by file format ---

    bool SchannelTransport::initialize(const TlsConfig& config) {
        GANL_SCHANNEL_DEBUG(0, "Initializing Schannel Transport");
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;

        if (config.certificateFile.empty()) {
            lastGlobalError_ = "No certificate file provided for TLS";
            GANL_SCHANNEL_DEBUG(0, "Error: " << lastGlobalError_);
            return false;
        }

        // Detect format and dispatch.
        const std::string& certFile = config.certificateFile;
        size_t dotPos = certFile.rfind('.');
        std::string ext;
        if (dotPos != std::string::npos) {
            ext = certFile.substr(dotPos);
            // Lowercase the extension for comparison.
            for (auto& ch : ext) {
                ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
            }
        }

        if (ext == ".p12" || ext == ".pfx") {
            ganl::logMessage("TLS: Certificate file '%s' detected as PFX format",
                certFile.c_str());
            return initializeFromPfx(config);
        }

        if (!config.keyFile.empty()) {
            ganl::logMessage("TLS: Certificate file '%s' with key file '%s' detected as PEM format",
                certFile.c_str(), config.keyFile.c_str());
            return initializeFromPem(config);
        }

        lastGlobalError_ = "Cannot determine certificate format for '" + certFile +
            "': use .p12/.pfx or provide a separate key file";
        ganl::logMessage("TLS initialize: %s", lastGlobalError_.c_str());
        return false;
    }

    // --- PFX (.p12) loading path ---

    bool SchannelTransport::initializeFromPfx(const TlsConfig& config) {
        // Open the .p12 file with STL
        std::ifstream pfxFile(config.certificateFile, std::ios::binary);
        if (!pfxFile) {
            lastGlobalError_ = "Failed to open .p12 file: " + config.certificateFile;
            GANL_SCHANNEL_DEBUG(0, "Error: " << lastGlobalError_);
            return false;
        }

        // Get file size
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
                widePassword.resize(wcslen(widePassword.c_str()));
            } else {
                lastGlobalError_ = "Failed to determine password conversion size: " + std::to_string(GetLastError());
                GANL_SCHANNEL_DEBUG(0, "Error: " << lastGlobalError_);
                return false;
            }
        }

        CRYPT_DATA_BLOB pfxBlob = { (DWORD)size, (BYTE*)buffer.data() };

        // Try import strategies in order of preference.
        struct { DWORD flags; const char* desc; } importStrategies[] = {
            { CRYPT_USER_KEYSET | CRYPT_EXPORTABLE, "USER_KEYSET|EXPORTABLE" },
            { CRYPT_EXPORTABLE,                     "EXPORTABLE" },
        };

        for (auto& strategy : importStrategies) {
            HCERTSTORE tryStore = PFXImportCertStore(&pfxBlob,
                widePassword.empty() ? L"" : widePassword.c_str(),
                strategy.flags);
            if (!tryStore) {
                ganl::logMessage("TLS: PFXImportCertStore(%s) failed: %u",
                    strategy.desc, (unsigned)GetLastError());
                continue;
            }
            ganl::logMessage("TLS: PFXImportCertStore(%s) succeeded", strategy.desc);

            // Enumerate ALL certificates, score them, pick best candidate.
            struct CertCandidate {
                PCCERT_CONTEXT cert;
                int score;
                int index;
                char name[256];
            };
            std::vector<CertCandidate> candidates;

            PCCERT_CONTEXT tryCert = nullptr;
            int certIndex = 0;
            while ((tryCert = CertEnumCertificatesInStore(tryStore, tryCert)) != nullptr) {
                CertCandidate c;
                c.index = certIndex;
                c.name[0] = '\0';
                CertGetNameStringA(tryCert, CERT_NAME_SIMPLE_DISPLAY_TYPE,
                    0, nullptr, c.name, sizeof(c.name));
                c.score = scoreCertForServerTls(tryCert);
                c.cert = CertDuplicateCertificateContext(tryCert);

                ganl::logMessage("TLS: Cert[%d] subject='%s' score=%d",
                    certIndex, c.name, c.score);

                if (c.score > 0) {
                    candidates.push_back(c);
                } else {
                    CertFreeCertificateContext(c.cert);
                }
                certIndex++;
            }

            // Sort candidates by score descending.
            std::sort(candidates.begin(), candidates.end(),
                [](const CertCandidate& a, const CertCandidate& b) {
                    return a.score > b.score;
                });

            // Try AcquireCredentialsHandle with each candidate in priority order.
            for (auto& c : candidates) {
                SCHANNEL_CRED cred = { 0 };
                cred.dwVersion = SCHANNEL_CRED_VERSION;
                cred.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER;
                cred.dwMinimumCipherStrength = 128;
                cred.cCreds = 1;
                cred.paCred = &c.cert;
                cred.dwFlags = SCH_CRED_NO_SYSTEM_MAPPER | SCH_CRED_NO_DEFAULT_CREDS;

                CredHandle testCredHandle;
                SecInvalidateHandle(&testCredHandle);
                TimeStamp expiry;
                SECURITY_STATUS status = AcquireCredentialsHandle(
                    NULL,
                    const_cast<LPWSTR>(UNISP_NAME_W),
                    SECPKG_CRED_INBOUND,
                    NULL,
                    &cred,
                    NULL, NULL,
                    &testCredHandle,
                    &expiry);

                if (SUCCEEDED(status)) {
                    FreeCredentialsHandle(&testCredHandle);
                    serverCertContext_ = c.cert;   // takes ownership
                    certStore_ = tryStore;
                    certStoreOpen_ = true;
                    ganl::logMessage("TLS: Selected cert[%d] '%s' (score=%d) with %s",
                        c.index, c.name, c.score, strategy.desc);

                    // Free remaining candidates.
                    for (auto& other : candidates) {
                        if (other.cert != c.cert) {
                            CertFreeCertificateContext(other.cert);
                        }
                    }
                    return true;
                }

                ganl::logMessage("TLS: AcquireCredentialsHandle(%s) cert[%d] '%s' failed: %s",
                    strategy.desc, c.index, c.name,
                    getSchannelErrorString(status).c_str());
            }

            // None of the candidates worked — free them all.
            for (auto& c : candidates) {
                CertFreeCertificateContext(c.cert);
            }
            CertCloseStore(tryStore, 0);
        }

        lastGlobalError_ = "All .p12 import strategies failed for " + config.certificateFile;
        ganl::logMessage("TLS initialize: %s", lastGlobalError_.c_str());
        return false;
    }

    // --- PEM (.crt + .key) loading path ---

    bool SchannelTransport::loadPemCertificate(const std::string& certFile, PCCERT_CONTEXT& outCert) {
        // Read the PEM file.
        std::ifstream file(certFile, std::ios::binary);
        if (!file) {
            lastGlobalError_ = "Failed to open certificate file: " + certFile;
            return false;
        }
        std::string pem((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
        file.close();

        // Decode PEM to DER.
        DWORD cbBinary = 0;
        if (!CryptStringToBinaryA(pem.c_str(), static_cast<DWORD>(pem.size()),
                CRYPT_STRING_BASE64HEADER, nullptr, &cbBinary, nullptr, nullptr)) {
            lastGlobalError_ = "CryptStringToBinaryA(size) failed: " + std::to_string(GetLastError());
            return false;
        }

        std::vector<BYTE> derCert(cbBinary);
        if (!CryptStringToBinaryA(pem.c_str(), static_cast<DWORD>(pem.size()),
                CRYPT_STRING_BASE64HEADER, derCert.data(), &cbBinary, nullptr, nullptr)) {
            lastGlobalError_ = "CryptStringToBinaryA(decode) failed: " + std::to_string(GetLastError());
            return false;
        }

        // Create certificate context from DER.
        outCert = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
            derCert.data(), cbBinary);
        if (!outCert) {
            lastGlobalError_ = "CertCreateCertificateContext failed: " + std::to_string(GetLastError());
            return false;
        }

        char subjectName[256] = {0};
        CertGetNameStringA(outCert, CERT_NAME_SIMPLE_DISPLAY_TYPE,
            0, nullptr, subjectName, sizeof(subjectName));
        ganl::logMessage("TLS: PEM certificate loaded successfully: '%s'", subjectName);
        return true;
    }

    bool SchannelTransport::loadPemPrivateKey(const std::string& keyFile, NCRYPT_KEY_HANDLE& outKey) {
        // Read the PEM file.
        std::ifstream file(keyFile, std::ios::binary);
        if (!file) {
            lastGlobalError_ = "Failed to open key file: " + keyFile;
            return false;
        }
        std::string pem((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
        file.close();

        // Check for encrypted keys (not supported).
        if (pem.find("ENCRYPTED PRIVATE KEY") != std::string::npos) {
            lastGlobalError_ = "Encrypted private keys are not supported; use an unencrypted PKCS#8 key";
            return false;
        }

        // Check for traditional RSA format (not supported).
        if (pem.find("BEGIN RSA PRIVATE KEY") != std::string::npos) {
            lastGlobalError_ = "Traditional RSA key format not supported; convert to PKCS#8 (BEGIN PRIVATE KEY)";
            return false;
        }

        // Decode PEM to DER.
        DWORD cbBinary = 0;
        if (!CryptStringToBinaryA(pem.c_str(), static_cast<DWORD>(pem.size()),
                CRYPT_STRING_BASE64HEADER, nullptr, &cbBinary, nullptr, nullptr)) {
            lastGlobalError_ = "CryptStringToBinaryA(key size) failed: " + std::to_string(GetLastError());
            return false;
        }

        std::vector<BYTE> derKey(cbBinary);
        if (!CryptStringToBinaryA(pem.c_str(), static_cast<DWORD>(pem.size()),
                CRYPT_STRING_BASE64HEADER, derKey.data(), &cbBinary, nullptr, nullptr)) {
            lastGlobalError_ = "CryptStringToBinaryA(key decode) failed: " + std::to_string(GetLastError());
            return false;
        }

        // Unwrap PKCS#8 envelope to get the inner RSA key.
        CRYPT_PRIVATE_KEY_INFO* pkcs8Info = nullptr;
        DWORD cbPkcs8 = 0;
        if (!CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                PKCS_PRIVATE_KEY_INFO, derKey.data(), cbBinary,
                CRYPT_DECODE_ALLOC_FLAG, nullptr, &pkcs8Info, &cbPkcs8)) {
            lastGlobalError_ = "CryptDecodeObjectEx(PKCS_PRIVATE_KEY_INFO) failed: " +
                std::to_string(GetLastError());
            return false;
        }

        // Convert the inner key blob to a CNG RSA blob.
        DWORD cbCngBlob = 0;
        BYTE* pCngBlob = nullptr;
        BOOL bResult = CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
            CNG_RSA_PRIVATE_KEY_BLOB,
            pkcs8Info->PrivateKey.pbData, pkcs8Info->PrivateKey.cbData,
            CRYPT_DECODE_ALLOC_FLAG, nullptr, &pCngBlob, &cbCngBlob);
        LocalFree(pkcs8Info);

        if (!bResult) {
            lastGlobalError_ = "CryptDecodeObjectEx(CNG_RSA_PRIVATE_KEY_BLOB) failed: " +
                std::to_string(GetLastError()) + " (only RSA keys are supported)";
            return false;
        }

        // Import into CNG as an ephemeral key (no key name → not persisted).
        NCRYPT_PROV_HANDLE hProv = 0;
        SECURITY_STATUS secStatus = NCryptOpenStorageProvider(&hProv,
            MS_KEY_STORAGE_PROVIDER, 0);
        if (FAILED(secStatus)) {
            LocalFree(pCngBlob);
            lastGlobalError_ = "NCryptOpenStorageProvider failed: " + std::to_string(secStatus);
            return false;
        }

        secStatus = NCryptImportKey(hProv, 0, BCRYPT_RSAPRIVATE_BLOB, nullptr,
            &outKey, pCngBlob, cbCngBlob, 0);
        LocalFree(pCngBlob);
        NCryptFreeObject(hProv);

        if (FAILED(secStatus)) {
            lastGlobalError_ = "NCryptImportKey failed: " + std::to_string(secStatus);
            return false;
        }

        ganl::logMessage("TLS: PEM private key loaded successfully via CNG");
        return true;
    }

    bool SchannelTransport::initializeFromPem(const TlsConfig& config) {
        // Step 1: Load the certificate.
        PCCERT_CONTEXT pemCert = nullptr;
        if (!loadPemCertificate(config.certificateFile, pemCert)) {
            ganl::logMessage("TLS initializeFromPem: %s", lastGlobalError_.c_str());
            return false;
        }

        // Step 2: Load the private key.
        NCRYPT_KEY_HANDLE hKey = 0;
        if (!loadPemPrivateKey(config.keyFile, hKey)) {
            CertFreeCertificateContext(pemCert);
            ganl::logMessage("TLS initializeFromPem: %s", lastGlobalError_.c_str());
            return false;
        }

        // Step 3: Create an in-memory certificate store.
        HCERTSTORE memStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
        if (!memStore) {
            NCryptFreeObject(hKey);
            CertFreeCertificateContext(pemCert);
            lastGlobalError_ = "CertOpenStore(MEMORY) failed: " + std::to_string(GetLastError());
            ganl::logMessage("TLS initializeFromPem: %s", lastGlobalError_.c_str());
            return false;
        }

        // Step 4: Add the certificate to the store and get a store-bound context.
        PCCERT_CONTEXT storeCert = nullptr;
        if (!CertAddCertificateContextToStore(memStore, pemCert,
                CERT_STORE_ADD_ALWAYS, &storeCert)) {
            CertCloseStore(memStore, 0);
            NCryptFreeObject(hKey);
            CertFreeCertificateContext(pemCert);
            lastGlobalError_ = "CertAddCertificateContextToStore failed: " + std::to_string(GetLastError());
            ganl::logMessage("TLS initializeFromPem: %s", lastGlobalError_.c_str());
            return false;
        }
        CertFreeCertificateContext(pemCert);  // store now owns its copy

        // Step 5: Associate the CNG key with the store certificate.
        // Caller retains ownership of the key handle.
        if (!CertSetCertificateContextProperty(storeCert,
                CERT_NCRYPT_KEY_HANDLE_PROP_ID, 0, &hKey)) {
            CertFreeCertificateContext(storeCert);
            CertCloseStore(memStore, 0);
            NCryptFreeObject(hKey);
            lastGlobalError_ = "CertSetCertificateContextProperty(NCRYPT_KEY_HANDLE) failed: " +
                std::to_string(GetLastError());
            ganl::logMessage("TLS initializeFromPem: %s", lastGlobalError_.c_str());
            return false;
        }

        // Step 6: Verify with AcquireCredentialsHandle.
        SCHANNEL_CRED cred = { 0 };
        cred.dwVersion = SCHANNEL_CRED_VERSION;
        cred.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER;
        cred.dwMinimumCipherStrength = 128;
        cred.cCreds = 1;
        cred.paCred = &storeCert;
        cred.dwFlags = SCH_CRED_NO_SYSTEM_MAPPER | SCH_CRED_NO_DEFAULT_CREDS;

        CredHandle testCredHandle;
        SecInvalidateHandle(&testCredHandle);
        TimeStamp expiry;
        SECURITY_STATUS status = AcquireCredentialsHandle(
            NULL,
            const_cast<LPWSTR>(UNISP_NAME_W),
            SECPKG_CRED_INBOUND,
            NULL,
            &cred,
            NULL, NULL,
            &testCredHandle,
            &expiry);

        if (FAILED(status)) {
            CertFreeCertificateContext(storeCert);
            CertCloseStore(memStore, 0);
            NCryptFreeObject(hKey);
            lastGlobalError_ = "AcquireCredentialsHandle(PEM) failed: " +
                getSchannelErrorString(status);
            ganl::logMessage("TLS initializeFromPem: %s", lastGlobalError_.c_str());
            return false;
        }

        FreeCredentialsHandle(&testCredHandle);

        // Step 7: Store results.
        serverCertContext_ = storeCert;
        certStore_ = memStore;
        certStoreOpen_ = true;
        ncryptKey_ = hKey;

        char subjectName[256] = {0};
        CertGetNameStringA(storeCert, CERT_NAME_SIMPLE_DISPLAY_TYPE,
            0, nullptr, subjectName, sizeof(subjectName));
        ganl::logMessage("TLS: PEM initialization complete: '%s'", subjectName);
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

        // Free CNG key handle BEFORE freeing the cert context that references it.
        if (ncryptKey_ != 0) {
            NCryptFreeObject(ncryptKey_);
            ncryptKey_ = 0;
        }

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
            ganl::logMessage("TLS[%u] createSessionContext: credential acquisition failed", (unsigned)conn);
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
                    ganl::logMessage("TLS[%u] handshake complete", (unsigned)conn);
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
                ganl::logMessage("TLS[%u] handshake: %s", (unsigned)conn, context.lastError.c_str());
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
            ganl::logMessage("TLS[%u] decrypt: %s", (unsigned)conn, context.lastError.c_str());
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
            ganl::logMessage("TLS[%u] encrypt: %s", (unsigned)conn, context.lastError.c_str());
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
            ganl::logMessage("TLS acquireCredentials failed: %s", context.lastError.c_str());
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
            char hexBuf[32];
            snprintf(hexBuf, sizeof(hexBuf), "0x%08X", (unsigned int)status);
            return std::string("Unknown Schannel error: ") + hexBuf;
        }

        std::string message(buffer);
        LocalFree(buffer);

        // Trim trailing whitespace and newlines
        size_t endpos = message.find_last_not_of(" \n\r\t");
        if (std::string::npos != endpos) {
            message = message.substr(0, endpos + 1);
        }

        char hexBuf[32];
        snprintf(hexBuf, sizeof(hexBuf), "0x%08X", (unsigned int)status);
        return message + " (" + hexBuf + ")";
    }

} // namespace ganl
