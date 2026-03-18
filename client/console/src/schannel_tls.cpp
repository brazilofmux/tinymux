// schannel_tls.cpp -- Client-side Schannel TLS implementation.
#include "schannel_tls.h"
#include <cstring>
#include <algorithm>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")

SchannelSession::SchannelSession() {
    SecInvalidateHandle(&cred_handle_);
    SecInvalidateHandle(&ctx_handle_);
    memset(&stream_sizes_, 0, sizeof(stream_sizes_));
}

SchannelSession::~SchannelSession() {
    if (ctx_initialized_) {
        DeleteSecurityContext(&ctx_handle_);
    }
    if (cred_initialized_) {
        FreeCredentialsHandle(&cred_handle_);
    }
}

bool SchannelSession::acquire_credentials() {
    SCHANNEL_CRED cred = {};
    cred.dwVersion = SCHANNEL_CRED_VERSION;
    cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION;
    cred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;

    TimeStamp expiry;
    SECURITY_STATUS status = AcquireCredentialsHandleW(
        nullptr,
        const_cast<LPWSTR>(UNISP_NAME_W),
        SECPKG_CRED_OUTBOUND,
        nullptr,
        &cred,
        nullptr,
        nullptr,
        &cred_handle_,
        &expiry
    );

    if (FAILED(status)) {
        last_error_ = "AcquireCredentialsHandle failed";
        return false;
    }

    cred_initialized_ = true;
    return true;
}

bool SchannelSession::sock_send(SOCKET sock, const void* data, size_t len) {
    const char* p = (const char*)data;
    while (len > 0) {
        int sent = ::send(sock, p, (int)len, 0);
        if (sent <= 0) return false;
        p += sent;
        len -= sent;
    }
    return true;
}

int SchannelSession::sock_recv(SOCKET sock, char* buf, size_t len) {
    return ::recv(sock, buf, (int)len, 0);
}

bool SchannelSession::handshake(SOCKET sock, const std::string& server_name) {
    if (!acquire_credentials()) return false;

    // Convert server name to wide string for SNI
    int wlen = MultiByteToWideChar(CP_UTF8, 0, server_name.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wname(wlen);
    MultiByteToWideChar(CP_UTF8, 0, server_name.c_str(), -1, wname.data(), wlen);

    DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                  ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM |
                  ISC_REQ_ALLOCATE_MEMORY;

    // First call to InitializeSecurityContext — generates ClientHello
    SecBuffer out_buf = {};
    out_buf.BufferType = SECBUFFER_TOKEN;
    SecBufferDesc out_desc = { SECBUFFER_VERSION, 1, &out_buf };

    DWORD out_flags = 0;
    TimeStamp expiry;

    SECURITY_STATUS status = InitializeSecurityContextW(
        &cred_handle_,
        nullptr,           // No existing context (first call)
        wname.data(),      // Server name (SNI)
        flags,
        0,
        SECURITY_NATIVE_DREP,
        nullptr,           // No input (first call)
        0,
        &ctx_handle_,
        &out_desc,
        &out_flags,
        &expiry
    );

    if (status != SEC_I_CONTINUE_NEEDED) {
        last_error_ = "Initial InitializeSecurityContext failed";
        return false;
    }
    ctx_initialized_ = true;

    // Send the ClientHello
    if (out_buf.cbBuffer > 0 && out_buf.pvBuffer) {
        if (!sock_send(sock, out_buf.pvBuffer, out_buf.cbBuffer)) {
            FreeContextBuffer(out_buf.pvBuffer);
            last_error_ = "Failed to send ClientHello";
            return false;
        }
        FreeContextBuffer(out_buf.pvBuffer);
    }

    // Handshake loop
    std::vector<char> recv_buf;
    recv_buf.reserve(16384);
    char tmp[8192];

    while (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_INCOMPLETE_MESSAGE) {
        // Read more data from server
        int n = sock_recv(sock, tmp, sizeof(tmp));
        if (n <= 0) {
            last_error_ = "Connection closed during handshake";
            return false;
        }
        recv_buf.insert(recv_buf.end(), tmp, tmp + n);

        // Set up input buffers
        SecBuffer in_bufs[2];
        in_bufs[0].pvBuffer = recv_buf.data();
        in_bufs[0].cbBuffer = (DWORD)recv_buf.size();
        in_bufs[0].BufferType = SECBUFFER_TOKEN;
        in_bufs[1].pvBuffer = nullptr;
        in_bufs[1].cbBuffer = 0;
        in_bufs[1].BufferType = SECBUFFER_EMPTY;
        SecBufferDesc in_desc = { SECBUFFER_VERSION, 2, in_bufs };

        // Set up output buffer
        SecBuffer out_buf2 = {};
        out_buf2.BufferType = SECBUFFER_TOKEN;
        SecBufferDesc out_desc2 = { SECBUFFER_VERSION, 1, &out_buf2 };

        status = InitializeSecurityContextW(
            &cred_handle_,
            &ctx_handle_,
            wname.data(),
            flags,
            0,
            SECURITY_NATIVE_DREP,
            &in_desc,
            0,
            nullptr,       // Context handle already initialized
            &out_desc2,
            &out_flags,
            &expiry
        );

        // Send any output token
        if (out_buf2.cbBuffer > 0 && out_buf2.pvBuffer) {
            if (!sock_send(sock, out_buf2.pvBuffer, out_buf2.cbBuffer)) {
                FreeContextBuffer(out_buf2.pvBuffer);
                last_error_ = "Failed to send handshake data";
                return false;
            }
            FreeContextBuffer(out_buf2.pvBuffer);
        }

        // Handle extra data (not consumed by this handshake step)
        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            // Need more data — keep recv_buf as-is and loop
            continue;
        }

        // Check for EXTRA buffer — leftover data after handshake
        SecBuffer* extra = nullptr;
        for (int i = 0; i < 2; i++) {
            if (in_bufs[i].BufferType == SECBUFFER_EXTRA) {
                extra = &in_bufs[i];
                break;
            }
        }

        if (extra && extra->cbBuffer > 0) {
            // Keep the extra bytes
            size_t consumed = recv_buf.size() - extra->cbBuffer;
            recv_buf.erase(recv_buf.begin(), recv_buf.begin() + consumed);
        } else {
            recv_buf.clear();
        }

        if (status == SEC_E_OK) {
            // Handshake complete
            break;
        }

        if (FAILED(status)) {
            last_error_ = "Handshake failed";
            return false;
        }
    }

    // Query stream sizes for encrypt/decrypt
    SECURITY_STATUS qs = QueryContextAttributes(&ctx_handle_, SECPKG_ATTR_STREAM_SIZES,
                                                 &stream_sizes_);
    if (FAILED(qs)) {
        last_error_ = "QueryContextAttributes failed";
        return false;
    }
    stream_sizes_valid_ = true;
    established_ = true;

    // Save any post-handshake leftover data
    if (!recv_buf.empty()) {
        pending_.insert(pending_.end(), recv_buf.begin(), recv_buf.end());
    }

    return true;
}

bool SchannelSession::encrypt(const void* data, size_t len, std::vector<char>& out) {
    if (!established_ || !stream_sizes_valid_) return false;

    const char* p = (const char*)data;
    while (len > 0) {
        DWORD chunk = (DWORD)std::min(len, (size_t)stream_sizes_.cbMaximumMessage);

        // Allocate: header + data + trailer
        size_t total = stream_sizes_.cbHeader + chunk + stream_sizes_.cbTrailer;
        std::vector<char> buf(total);

        memcpy(buf.data() + stream_sizes_.cbHeader, p, chunk);

        SecBuffer bufs[4];
        bufs[0].pvBuffer = buf.data();
        bufs[0].cbBuffer = stream_sizes_.cbHeader;
        bufs[0].BufferType = SECBUFFER_STREAM_HEADER;

        bufs[1].pvBuffer = buf.data() + stream_sizes_.cbHeader;
        bufs[1].cbBuffer = chunk;
        bufs[1].BufferType = SECBUFFER_DATA;

        bufs[2].pvBuffer = buf.data() + stream_sizes_.cbHeader + chunk;
        bufs[2].cbBuffer = stream_sizes_.cbTrailer;
        bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;

        bufs[3].pvBuffer = nullptr;
        bufs[3].cbBuffer = 0;
        bufs[3].BufferType = SECBUFFER_EMPTY;

        SecBufferDesc desc = { SECBUFFER_VERSION, 4, bufs };

        SECURITY_STATUS status = EncryptMessage(&ctx_handle_, 0, &desc, 0);
        if (FAILED(status)) {
            last_error_ = "EncryptMessage failed";
            return false;
        }

        // Output = header + data + trailer (actual sizes may differ)
        DWORD out_len = bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer;
        out.insert(out.end(), buf.data(), buf.data() + out_len);

        p += chunk;
        len -= chunk;
    }
    return true;
}

int SchannelSession::decrypt(const char* data, size_t len, std::vector<char>& out) {
    if (!established_) return -1;

    // Append new data to pending buffer
    if (len > 0) {
        pending_.insert(pending_.end(), data, data + len);
    }

    if (pending_.empty()) return 0;

    // Set up decryption buffers
    SecBuffer bufs[4];
    bufs[0].pvBuffer = pending_.data();
    bufs[0].cbBuffer = (DWORD)pending_.size();
    bufs[0].BufferType = SECBUFFER_DATA;
    bufs[1] = { 0, 0, SECBUFFER_EMPTY };
    bufs[2] = { 0, 0, SECBUFFER_EMPTY };
    bufs[3] = { 0, 0, SECBUFFER_EMPTY };

    SecBufferDesc desc = { SECBUFFER_VERSION, 4, bufs };

    SECURITY_STATUS status = DecryptMessage(&ctx_handle_, &desc, 0, nullptr);

    if (status == SEC_E_OK) {
        // Find data and extra buffers
        for (int i = 0; i < 4; i++) {
            if (bufs[i].BufferType == SECBUFFER_DATA && bufs[i].cbBuffer > 0) {
                out.insert(out.end(), (char*)bufs[i].pvBuffer,
                          (char*)bufs[i].pvBuffer + bufs[i].cbBuffer);
            }
        }

        // Save extra data
        SecBuffer* extra = nullptr;
        for (int i = 0; i < 4; i++) {
            if (bufs[i].BufferType == SECBUFFER_EXTRA) {
                extra = &bufs[i];
                break;
            }
        }
        if (extra && extra->cbBuffer > 0) {
            std::vector<char> remain((char*)extra->pvBuffer,
                                     (char*)extra->pvBuffer + extra->cbBuffer);
            pending_ = std::move(remain);
        } else {
            pending_.clear();
        }
        return 1;
    }

    if (status == SEC_E_INCOMPLETE_MESSAGE) {
        return 0;  // Need more data
    }

    if (status == SEC_I_RENEGOTIATE) {
        return 2;
    }

    if (status == SEC_I_CONTEXT_EXPIRED) {
        established_ = false;
        return -1;
    }

    last_error_ = "DecryptMessage failed";
    established_ = false;
    return -1;
}

void SchannelSession::shutdown(SOCKET sock) {
    if (!ctx_initialized_) return;

    DWORD dwType = SCHANNEL_SHUTDOWN;
    SecBuffer buf = {};
    buf.pvBuffer = &dwType;
    buf.cbBuffer = sizeof(dwType);
    buf.BufferType = SECBUFFER_TOKEN;
    SecBufferDesc desc = { SECBUFFER_VERSION, 1, &buf };

    ApplyControlToken(&ctx_handle_, &desc);

    // Generate shutdown message
    SecBuffer out_buf = {};
    out_buf.BufferType = SECBUFFER_TOKEN;
    SecBufferDesc out_desc = { SECBUFFER_VERSION, 1, &out_buf };

    DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                  ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM |
                  ISC_REQ_ALLOCATE_MEMORY;
    DWORD out_flags = 0;
    TimeStamp expiry;

    SECURITY_STATUS status = InitializeSecurityContextW(
        &cred_handle_,
        &ctx_handle_,
        nullptr,
        flags,
        0,
        SECURITY_NATIVE_DREP,
        nullptr,
        0,
        nullptr,
        &out_desc,
        &out_flags,
        &expiry
    );

    if (out_buf.cbBuffer > 0 && out_buf.pvBuffer) {
        sock_send(sock, out_buf.pvBuffer, out_buf.cbBuffer);
        FreeContextBuffer(out_buf.pvBuffer);
    }

    established_ = false;
}
