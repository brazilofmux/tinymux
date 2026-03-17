#ifndef TF_REGEX_UTILS_H
#define TF_REGEX_UTILS_H

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <string>
#include <vector>

class RegexPattern {
public:
    RegexPattern() = default;
    ~RegexPattern();

    RegexPattern(const RegexPattern&) = delete;
    RegexPattern& operator=(const RegexPattern&) = delete;
    RegexPattern(RegexPattern&& other) noexcept;
    RegexPattern& operator=(RegexPattern&& other) noexcept;

    bool compile(const std::string& pattern);
    bool search(const std::string& text) const;
    bool search(const std::string& text, std::vector<std::string>& captures) const;
    bool valid() const { return code_ != nullptr; }

private:
    pcre2_code* code_ = nullptr;
};

std::string regex_escape(const std::string& text);
bool regex_search_pattern(const std::string& text, const std::string& pattern);
bool regex_search_pattern(const std::string& text, const std::string& pattern,
                          std::vector<std::string>& captures);

#endif // TF_REGEX_UTILS_H
