// credential_store.cpp -- Platform secret store for Hydra account passwords.
#include "credential_store.h"

#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincred.h>
#pragma comment(lib, "advapi32.lib")
#endif

namespace {

std::mutex g_mu;
std::string g_file_path;  // Unix file backend path

#if defined(_WIN32)

static std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), ws.data(), len);
    return ws;
}

static std::string to_utf8(const wchar_t* ws, int len = -1) {
    if (!ws || (len == 0)) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, ws, len, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, len, s.data(), n, nullptr, nullptr);
    if (len == -1 && !s.empty() && s.back() == '\0') {
        s.pop_back();
    }
    return s;
}

// Distinct from win32gui's "Titan:" target prefix.
static std::wstring make_target(const std::string& world_name) {
    return L"HydraConsole:" + to_wide(world_name);
}

#else

// Escape for tab-separated file: backslash, tab, newline.
static std::string escape_field(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

static std::string unescape_field(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            if (n == 't') out += '\t';
            else if (n == 'n') out += '\n';
            else if (n == 'r') out += '\r';
            else if (n == '\\') out += '\\';
            else out += n;
        } else {
            out += s[i];
        }
    }
    return out;
}

struct Entry {
    std::string user;
    std::string pass;
};

static bool load_file(const std::string& path,
                      std::unordered_map<std::string, Entry>& map,
                      std::string* err) {
    map.clear();
    if (path.empty()) {
        if (err) *err = "no credential file path set";
        return false;
    }
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return true;  // missing is OK
    }
    if ((st.st_mode & 077) != 0) {
        if (err) {
            *err = "credential file has insecure permissions (need 0600): " + path;
        }
        return false;
    }
    std::ifstream f(path);
    if (!f) return true;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        // name \t user \t pass
        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        size_t t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        std::string name = unescape_field(line.substr(0, t1));
        Entry e;
        e.user = unescape_field(line.substr(t1 + 1, t2 - t1 - 1));
        e.pass = unescape_field(line.substr(t2 + 1));
        if (!name.empty()) map[name] = std::move(e);
    }
    return true;
}

static bool save_file(const std::string& path,
                      const std::unordered_map<std::string, Entry>& map) {
    if (path.empty()) return false;
    // Create/truncate with 0600 before writing any secret material.
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    std::string body;
    body += "# HydraConsole credentials — do not share; mode 0600\n";
    for (const auto& kv : map) {
        body += escape_field(kv.first);
        body += '\t';
        body += escape_field(kv.second.user);
        body += '\t';
        body += escape_field(kv.second.pass);
        body += '\n';
    }
    const char* p = body.data();
    size_t left = body.size();
    while (left > 0) {
        ssize_t n = ::write(fd, p, left);
        if (n < 0) {
            ::close(fd);
            return false;
        }
        p += n;
        left -= static_cast<size_t>(n);
    }
    ::close(fd);
    ::chmod(path.c_str(), 0600);
    return true;
}

#endif

} // namespace

void CredStore::SetFilePath(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_file_path = path;
}

bool CredStore::Save(const std::string& world_name, const std::string& username,
                     const std::string& password) {
#if defined(_WIN32)
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
#else
    std::lock_guard<std::mutex> lock(g_mu);
    std::unordered_map<std::string, Entry> map;
    std::string err;
    if (!load_file(g_file_path, map, &err)) {
        return false;
    }
    map[world_name] = Entry{username, password};
    return save_file(g_file_path, map);
#endif
}

std::string CredStore::LoadPassword(const std::string& world_name) {
#if defined(_WIN32)
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
#else
    std::lock_guard<std::mutex> lock(g_mu);
    std::unordered_map<std::string, Entry> map;
    std::string err;
    if (!load_file(g_file_path, map, &err)) {
        return {};
    }
    auto it = map.find(world_name);
    return it == map.end() ? std::string() : it->second.pass;
#endif
}

std::string CredStore::LoadUsername(const std::string& world_name) {
#if defined(_WIN32)
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
#else
    std::lock_guard<std::mutex> lock(g_mu);
    std::unordered_map<std::string, Entry> map;
    std::string err;
    if (!load_file(g_file_path, map, &err)) {
        return {};
    }
    auto it = map.find(world_name);
    return it == map.end() ? std::string() : it->second.user;
#endif
}

void CredStore::Remove(const std::string& world_name) {
#if defined(_WIN32)
    std::wstring target = make_target(world_name);
    CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
#else
    std::lock_guard<std::mutex> lock(g_mu);
    std::unordered_map<std::string, Entry> map;
    std::string err;
    if (!load_file(g_file_path, map, &err)) {
        return;
    }
    map.erase(world_name);
    save_file(g_file_path, map);
#endif
}
