#include "regex_utils.h"
#include <cstring>

namespace {

constexpr uint32_t kRegexOptions = PCRE2_UTF | PCRE2_UCP;

bool pattern_search(pcre2_code* code, const std::string& text,
                    std::vector<std::string>* captures) {
    if (!code) return false;

    pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(code, nullptr);
    if (!match_data) return false;

    int rc = pcre2_match(code,
        reinterpret_cast<PCRE2_SPTR>(text.data()),
        text.size(),
        0,
        0,
        match_data,
        nullptr);
    if (rc <= 0) {
        pcre2_match_data_free(match_data);
        return false;
    }

    if (captures) {
        PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match_data);
        captures->clear();
        captures->reserve(static_cast<size_t>(rc));
        for (int i = 0; i < rc; ++i) {
            PCRE2_SIZE start = ovector[2 * i];
            PCRE2_SIZE end = ovector[2 * i + 1];
            if (start == PCRE2_UNSET || end == PCRE2_UNSET || end < start) {
                captures->emplace_back();
            } else {
                captures->emplace_back(text.substr(start, end - start));
            }
        }
    }

    pcre2_match_data_free(match_data);
    return true;
}

} // namespace

RegexPattern::~RegexPattern() {
    if (code_) pcre2_code_free(code_);
}

RegexPattern::RegexPattern(RegexPattern&& other) noexcept
    : code_(other.code_) {
    other.code_ = nullptr;
}

RegexPattern& RegexPattern::operator=(RegexPattern&& other) noexcept {
    if (this == &other) return *this;
    if (code_) pcre2_code_free(code_);
    code_ = other.code_;
    other.code_ = nullptr;
    return *this;
}

bool RegexPattern::compile(const std::string& pattern) {
    if (code_) {
        pcre2_code_free(code_);
        code_ = nullptr;
    }

    int errcode = 0;
    PCRE2_SIZE erroffset = 0;
    code_ = pcre2_compile(
        reinterpret_cast<PCRE2_SPTR>(pattern.c_str()),
        PCRE2_ZERO_TERMINATED,
        kRegexOptions,
        &errcode,
        &erroffset,
        nullptr);
    if (!code_) return false;

    pcre2_jit_compile(code_, PCRE2_JIT_COMPLETE);
    return true;
}

bool RegexPattern::search(const std::string& text) const {
    return pattern_search(code_, text, nullptr);
}

bool RegexPattern::search(const std::string& text, std::vector<std::string>& captures) const {
    return pattern_search(code_, text, &captures);
}

std::string regex_escape(const std::string& text) {
    static constexpr char kMeta[] = R"(\.^$|()[]{}*+?)";

    std::string out;
    out.reserve(text.size() * 2);
    for (char ch : text) {
        if (std::strchr(kMeta, ch)) out += '\\';
        out += ch;
    }
    return out;
}

bool regex_search_pattern(const std::string& text, const std::string& pattern) {
    RegexPattern compiled;
    return compiled.compile(pattern) && compiled.search(text);
}

bool regex_search_pattern(const std::string& text, const std::string& pattern,
                          std::vector<std::string>& captures) {
    RegexPattern compiled;
    return compiled.compile(pattern) && compiled.search(text, captures);
}
