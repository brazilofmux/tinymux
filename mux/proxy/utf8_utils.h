#ifndef HYDRA_UTF8_UTILS_H
#define HYDRA_UTF8_UTILS_H

#include "hydra_log.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

enum class Utf8IssueType {
    None,
    InvalidSequence,
    TruncatedSequence,
};

struct Utf8Issue {
    Utf8IssueType type{Utf8IssueType::None};
    size_t offset{0};
    size_t bytes{0};

    bool hasIssue() const { return type != Utf8IssueType::None; }
};

inline Utf8Issue findFirstUtf8Issue(std::string_view s) {
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c <= 0x7F) {
            i++;
            continue;
        }

        auto truncated = [&]() {
            return Utf8Issue{Utf8IssueType::TruncatedSequence, i, s.size() - i};
        };
        auto invalid = [&](size_t badBytes = 1u) {
            return Utf8Issue{Utf8IssueType::InvalidSequence, i, badBytes};
        };

        if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 >= s.size()) return truncated();
            const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            if ((c1 & 0xC0) != 0x80) return invalid();
            i += 2;
            continue;
        }

        if (c == 0xE0) {
            if (i + 2 >= s.size()) return truncated();
            const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
            if (c1 < 0xA0 || c1 > 0xBF || (c2 & 0xC0) != 0x80) return invalid();
            i += 3;
            continue;
        }
        if ((c >= 0xE1 && c <= 0xEC) || (c >= 0xEE && c <= 0xEF)) {
            if (i + 2 >= s.size()) return truncated();
            const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return invalid();
            i += 3;
            continue;
        }
        if (c == 0xED) {
            if (i + 2 >= s.size()) return truncated();
            const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
            if (c1 < 0x80 || c1 > 0x9F || (c2 & 0xC0) != 0x80) return invalid();
            i += 3;
            continue;
        }

        if (c == 0xF0) {
            if (i + 3 >= s.size()) return truncated();
            const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
            const unsigned char c3 = static_cast<unsigned char>(s[i + 3]);
            if (c1 < 0x90 || c1 > 0xBF ||
                (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
                return invalid();
            }
            i += 4;
            continue;
        }
        if (c >= 0xF1 && c <= 0xF3) {
            if (i + 3 >= s.size()) return truncated();
            const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
            const unsigned char c3 = static_cast<unsigned char>(s[i + 3]);
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 ||
                (c3 & 0xC0) != 0x80) {
                return invalid();
            }
            i += 4;
            continue;
        }
        if (c == 0xF4) {
            if (i + 3 >= s.size()) return truncated();
            const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
            const unsigned char c3 = static_cast<unsigned char>(s[i + 3]);
            if (c1 < 0x80 || c1 > 0x8F ||
                (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
                return invalid();
            }
            i += 4;
            continue;
        }

        return invalid();
    }

    return {};
}

inline std::string hexWindow(std::string_view s, size_t offset, size_t radius = 8) {
    size_t start = (offset > radius) ? offset - radius : 0;
    size_t end = std::min(s.size(), offset + radius);
    std::string out;
    for (size_t i = start; i < end; i++) {
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%02X", static_cast<unsigned char>(s[i]));
        if (!out.empty()) out.push_back(' ');
        out += buf;
    }
    return out;
}

inline std::string sanitizeUtf8(std::string_view s) {
    std::string out;
    out.reserve(s.size());

    size_t pos = 0;
    while (pos < s.size()) {
        Utf8Issue issue = findFirstUtf8Issue(s.substr(pos));
        if (!issue.hasIssue()) {
            out.append(s.substr(pos));
            break;
        }

        out.append(s.substr(pos, issue.offset));
        out.append("\xEF\xBF\xBD");

        size_t absolute = pos + issue.offset;
        size_t skip = 1;
        if (issue.type == Utf8IssueType::TruncatedSequence) {
            skip = s.size() - absolute;
        }
        pos = absolute + skip;
    }

    return out;
}

inline std::string issueTypeName(Utf8IssueType type) {
    switch (type) {
    case Utf8IssueType::None:
        return "none";
    case Utf8IssueType::InvalidSequence:
        return "invalid";
    case Utf8IssueType::TruncatedSequence:
        return "truncated";
    }
    return "unknown";
}

inline std::string sanitizeProtoTextForLog(const std::string& text,
                                           const char* path,
                                           const std::string& source,
                                           int linkNumber) {
    Utf8Issue issue = findFirstUtf8Issue(text);
    if (!issue.hasIssue()) return text;

    LOG_WARN("Proto UTF-8 issue on %s source=%s link=%d type=%s offset=%zu bytes=%zu hex=[%s]",
             path, source.c_str(), linkNumber, issueTypeName(issue.type).c_str(),
             issue.offset, issue.bytes, hexWindow(text, issue.offset).c_str());
    return sanitizeUtf8(text);
}

#endif // HYDRA_UTF8_UTILS_H
