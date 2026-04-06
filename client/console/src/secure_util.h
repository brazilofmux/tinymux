#ifndef SECURE_UTIL_H
#define SECURE_UTIL_H

#include <string>

// Overwrite a string's data buffer with zeros in a way that the compiler
// cannot optimise away, then clear the string.
inline void secure_zero(std::string& s) {
    if (!s.empty()) {
#if defined(_WIN32)
        SecureZeroMemory(s.data(), s.size());
#elif defined(__GLIBC__) || defined(__FreeBSD__)
        explicit_bzero(s.data(), s.size());
#else
        volatile char *p = s.data();
        for (size_t i = 0; i < s.size(); ++i) p[i] = 0;
#endif
        s.clear();
    }
}

#endif // SECURE_UTIL_H
