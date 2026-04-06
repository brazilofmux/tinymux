// credential_store.cpp -- Windows Credential Manager wrapper for Hydra passwords.
#include "credential_store.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincred.h>
#include <vector>

#pragma comment(lib, "advapi32.lib")

// Convert a UTF-8 std::string to a wide string.
static std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), ws.data(), len);
    return ws;
}

// Convert a wide string to a UTF-8 std::string.
static std::string to_utf8(const wchar_t* ws, int len = -1) {
    if (!ws || (len == 0)) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, ws, len, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, len, s.data(), n, nullptr, nullptr);
    // WideCharToMultiByte with len=-1 includes a null terminator in the count.
    if (len == -1 && !s.empty() && s.back() == '\0') {
        s.pop_back();
    }
    return s;
}

// Build the credential target name: "Titan:<world_name>"
static std::wstring make_target(const std::string& world_name) {
    return L"Titan:" + to_wide(world_name);
}

bool CredStore::Save(const std::string& world_name, const std::string& username,
                     const std::string& password) {
    std::wstring target = make_target(world_name);
    std::wstring wuser = to_wide(username);

    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target.c_str());
    cred.UserName = const_cast<LPWSTR>(wuser.c_str());
    cred.CredentialBlobSize = (DWORD)password.size();
    cred.CredentialBlob = (LPBYTE)password.data();
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

    return CredWriteW(&cred, 0) != FALSE;
}

std::string CredStore::LoadPassword(const std::string& world_name) {
    std::wstring target = make_target(world_name);
    PCREDENTIALW pcred = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &pcred)) {
        return {};
    }
    std::string password;
    if (pcred->CredentialBlob && pcred->CredentialBlobSize > 0) {
        password.assign(reinterpret_cast<const char*>(pcred->CredentialBlob),
                        pcred->CredentialBlobSize);
    }
    CredFree(pcred);
    return password;
}

std::string CredStore::LoadUsername(const std::string& world_name) {
    std::wstring target = make_target(world_name);
    PCREDENTIALW pcred = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &pcred)) {
        return {};
    }
    std::string username;
    if (pcred->UserName) {
        username = to_utf8(pcred->UserName);
    }
    CredFree(pcred);
    return username;
}

void CredStore::Remove(const std::string& world_name) {
    std::wstring target = make_target(world_name);
    CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
}
