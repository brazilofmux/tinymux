#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

enum class Profile {
    Mux213,
    Penn
};

enum class PassMode {
    Eval,
    Noeval
};

enum class UnitType {
    Text,
    Escape,
    Percent
};

struct Unit {
    UnitType type;
    std::string text;
};

static bool parse_profile(const std::string &s, Profile &profile)
{
    if (s == "mux213") {
        profile = Profile::Mux213;
        return true;
    }
    if (s == "penn") {
        profile = Profile::Penn;
        return true;
    }
    return false;
}

static bool parse_pass(const std::string &s, PassMode &mode)
{
    if (s == "eval") {
        mode = PassMode::Eval;
        return true;
    }
    if (s == "noeval") {
        mode = PassMode::Noeval;
        return true;
    }
    return false;
}

static std::vector<PassMode> parse_pipeline(const std::string &s)
{
    std::vector<PassMode> out;
    std::stringstream ss(s);
    std::string item;

    while (std::getline(ss, item, ',')) {
        PassMode mode;
        if (!parse_pass(item, mode)) {
            out.clear();
            return out;
        }
        out.push_back(mode);
    }
    return out;
}

static void gather_angle(const std::string &input, size_t &i, std::string &out)
{
    if (i >= input.size() || input[i] != '<') {
        return;
    }
    out.push_back(input[i++]);
    while (i < input.size() && input[i] != '>') {
        out.push_back(input[i++]);
    }
    if (i < input.size() && input[i] == '>') {
        out.push_back(input[i++]);
    }
}

static std::string gather_percent_unit(const std::string &input, size_t &i, Profile profile)
{
    std::string out("%");
    if (i >= input.size()) {
        return out;
    }

    char ch = input[i];
    char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));

    if (profile == Profile::Penn && ch == ' ') {
        out.push_back(input[i++]);
        return out;
    }

    if (ch >= '0' && ch <= '9') {
        out.push_back(input[i++]);
    } else if (upper == 'Q') {
        out.push_back(input[i++]);
        if (i < input.size() && input[i] == '<') {
            gather_angle(input, i, out);
        } else if (i < input.size()) {
            out.push_back(input[i++]);
        }
    } else if (upper == 'V') {
        out.push_back(input[i++]);
        if (i < input.size() && std::isalpha(static_cast<unsigned char>(input[i]))) {
            out.push_back(input[i++]);
        }
    } else if (profile == Profile::Penn && upper == 'W') {
        out.push_back(input[i++]);
        if (i < input.size() && std::isalpha(static_cast<unsigned char>(input[i]))) {
            out.push_back(input[i++]);
        }
    } else if (upper == 'C' || upper == 'X') {
        out.push_back(input[i++]);
        if (profile == Profile::Penn) {
            if (upper == 'X' && i < input.size()
                && std::isalpha(static_cast<unsigned char>(input[i]))) {
                out.push_back(input[i++]);
            }
        } else if (i < input.size()) {
            if (input[i] == '<') {
                gather_angle(input, i, out);
            } else {
                out.push_back(input[i++]);
            }
        }
    } else if (ch == '=') {
        out.push_back(input[i++]);
        if (i < input.size() && input[i] == '<') {
            gather_angle(input, i, out);
        }
    } else if (upper == 'I') {
        out.push_back(input[i++]);
        if (i < input.size() && std::isdigit(static_cast<unsigned char>(input[i]))) {
            out.push_back(input[i++]);
        } else if (profile == Profile::Penn && i < input.size()
                   && std::toupper(static_cast<unsigned char>(input[i])) == 'L') {
            out.push_back(input[i++]);
        }
    } else if (profile == Profile::Penn && ch == '$') {
        out.push_back(input[i++]);
        if (i < input.size() && (std::isdigit(static_cast<unsigned char>(input[i]))
            || std::toupper(static_cast<unsigned char>(input[i])) == 'L')) {
            out.push_back(input[i++]);
        }
    } else {
        out.push_back(input[i++]);
    }

    return out;
}

static std::vector<Unit> freeze_units(const std::string &input, Profile profile)
{
    std::vector<Unit> units;
    size_t i = 0;

    while (i < input.size()) {
        if (input[i] == '\\') {
            std::string unit;
            unit.push_back(input[i++]);
            if (i < input.size()) {
                unit.push_back(input[i++]);
            }
            units.push_back({UnitType::Escape, unit});
            continue;
        }

        if (input[i] == '%') {
            ++i;
            units.push_back({UnitType::Percent, gather_percent_unit(input, i, profile)});
            continue;
        }

        std::string text;
        while (i < input.size() && input[i] != '\\' && input[i] != '%') {
            text.push_back(input[i++]);
        }
        units.push_back({UnitType::Text, text});
    }

    return units;
}

static std::string evaluate_percent(const std::string &unit, Profile profile, bool eval)
{
    if (!eval) {
        return unit;
    }

    if (unit == "%%") {
        return "%";
    }
    if (unit == "%b" || unit == "%B") {
        return " ";
    }
    if (unit == "%t" || unit == "%T") {
        return "\t";
    }
    if (unit == "%r" || unit == "%R") {
        return (profile == Profile::Penn) ? "\n" : "\r\n";
    }
    if (profile == Profile::Penn && unit == "% ") {
        return "% ";
    }

    if (unit.size() >= 2) {
        return std::string(1, unit[1]);
    }
    return "%";
}

static std::string run_stream_pass(const std::string &input, Profile profile, bool eval)
{
    std::string out;
    size_t i = 0;

    while (i < input.size()) {
        if (input[i] == '\\') {
            ++i;
            if (i < input.size()) {
                out.push_back(input[i++]);
            }
            continue;
        }

        if (input[i] == '%') {
            ++i;
            std::string unit = gather_percent_unit(input, i, profile);
            out += evaluate_percent(unit, profile, eval);
            continue;
        }

        out.push_back(input[i++]);
    }

    return out;
}

static std::string run_frozen_pass(const std::vector<Unit> &units, Profile profile, bool eval)
{
    std::string out;

    for (const Unit &unit : units) {
        switch (unit.type) {
        case UnitType::Text:
            out += unit.text;
            break;
        case UnitType::Escape:
            if (unit.text.size() >= 2) {
                out.push_back(unit.text[1]);
            }
            break;
        case UnitType::Percent:
            out += evaluate_percent(unit.text, profile, eval);
            break;
        }
    }

    return out;
}

static std::string quote_string(const std::string &s)
{
    std::string out("\"");
    for (char ch : s) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    out.push_back('"');
    return out;
}

static const char *unit_name(UnitType type)
{
    switch (type) {
    case UnitType::Text:
        return "TEXT";
    case UnitType::Escape:
        return "ESC";
    case UnitType::Percent:
        return "PCT";
    }
    return "?";
}

static void print_usage()
{
    std::cerr
        << "usage: ./stream_passes --profile mux213|penn"
        << " --model stream|frozen|refrozen|boundary"
        << " --passes noeval,eval [text]\n";
}

static bool starts_with(const std::string &s, const std::string &prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

static bool ends_with(const std::string &s, const std::string &suffix)
{
    return s.size() >= suffix.size()
        && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static bool extract_switch_body(const std::string &input, std::string &body)
{
    const std::string prefix = "[switch(1,1,";
    const std::string suffix = ")]";
    if (!starts_with(input, prefix) || !ends_with(input, suffix)) {
        return false;
    }
    body = input.substr(prefix.size(), input.size() - prefix.size() - suffix.size());
    return true;
}

static bool extract_if_body(const std::string &input, std::string &body)
{
    const std::string prefix = "[if(1,";
    const std::string suffix = ")]";
    if (!starts_with(input, prefix) || !ends_with(input, suffix)) {
        return false;
    }
    body = input.substr(prefix.size(), input.size() - prefix.size() - suffix.size());
    return true;
}

static bool extract_case_body(const std::string &input, std::string &body)
{
    const std::string prefix = "[case(1,1,";
    const std::string suffix = ")]";
    if (!starts_with(input, prefix) || !ends_with(input, suffix)) {
        return false;
    }
    body = input.substr(prefix.size(), input.size() - prefix.size() - suffix.size());
    return true;
}

static bool extract_iter_body(const std::string &input, std::string &body, int &count)
{
    const std::string prefix = "[iter(";
    const std::string suffix = ")]";
    if (!starts_with(input, prefix) || !ends_with(input, suffix)) {
        return false;
    }

    std::string inner = input.substr(prefix.size(), input.size() - prefix.size() - suffix.size());
    const std::string list_prefix = "a b,";
    if (!starts_with(inner, list_prefix)) {
        return false;
    }

    body = inner.substr(list_prefix.size());
    count = 2;
    return true;
}

static std::string strip_one_brace_layer(const std::string &s)
{
    if (s.size() >= 2 && s.front() == '{' && s.back() == '}') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

int main(int argc, char **argv)
{
    Profile profile = Profile::Mux213;
    std::string model = "stream";
    std::vector<PassMode> passes;
    std::string input;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--profile" && i + 1 < argc) {
            if (!parse_profile(argv[++i], profile)) {
                print_usage();
                return 1;
            }
        } else if (arg == "--model" && i + 1 < argc) {
            model = argv[++i];
        } else if (arg == "--passes" && i + 1 < argc) {
            passes = parse_pipeline(argv[++i]);
            if (passes.empty()) {
                print_usage();
                return 1;
            }
        } else if (!input.empty()) {
            input += " ";
            input += arg;
        } else {
            input = arg;
        }
    }

    if (passes.empty()) {
        passes.push_back(PassMode::Eval);
    }

    if (input.empty()) {
        if (!std::getline(std::cin, input)) {
            return 1;
        }
    }

    std::cout << "input  : " << quote_string(input) << "\n";

    if (model == "stream") {
        std::string current = input;
        for (size_t i = 0; i < passes.size(); ++i) {
            bool eval = passes[i] == PassMode::Eval;
            current = run_stream_pass(current, profile, eval);
            std::cout << "pass " << (i + 1) << " : "
                      << (eval ? "eval   " : "noeval ")
                      << "-> " << quote_string(current) << "\n";
        }
        return 0;
    }

    if (model == "frozen") {
        std::vector<Unit> units = freeze_units(input, profile);
        std::cout << "units  :";
        for (const Unit &unit : units) {
            std::cout << " " << unit_name(unit.type) << "(" << quote_string(unit.text) << ")";
        }
        std::cout << "\n";

        for (size_t i = 0; i < passes.size(); ++i) {
            bool eval = passes[i] == PassMode::Eval;
            std::string out = run_frozen_pass(units, profile, eval);
            std::cout << "pass " << (i + 1) << " : "
                      << (eval ? "eval   " : "noeval ")
                      << "-> " << quote_string(out) << "\n";
        }
        return 0;
    }

    if (model == "refrozen") {
        std::string current = input;
        for (size_t i = 0; i < passes.size(); ++i) {
            bool eval = passes[i] == PassMode::Eval;

            // Freeze the current string into tokens.
            //
            std::vector<Unit> units = freeze_units(current, profile);
            std::cout << "units  :";
            for (const Unit &unit : units) {
                std::cout << " " << unit_name(unit.type)
                          << "(" << quote_string(unit.text) << ")";
            }
            std::cout << "\n";

            // Evaluate the frozen tokens.
            //
            current = run_frozen_pass(units, profile, eval);
            std::cout << "pass " << (i + 1) << " : "
                      << (eval ? "eval   " : "noeval ")
                      << "-> " << quote_string(current) << "\n";
        }
        return 0;
    }

    if (model == "boundary") {
        std::string body;
        int iter_count = 0;

        if (extract_switch_body(input, body)
            || extract_if_body(input, body)
            || extract_case_body(input, body)) {
            std::cout << "boundary: deferred arg " << quote_string(body) << "\n";
            std::string current = run_stream_pass(body, profile, false);
            std::cout << "pass 1 : noeval -> " << quote_string(current) << "\n";
            current = strip_one_brace_layer(current);
            std::cout << "strip  : braces -> " << quote_string(current) << "\n";
            std::vector<Unit> units = freeze_units(current, profile);
            std::cout << "units  :";
            for (const Unit &unit : units) {
                std::cout << " " << unit_name(unit.type)
                          << "(" << quote_string(unit.text) << ")";
            }
            std::cout << "\n";
            current = run_frozen_pass(units, profile, true);
            std::cout << "pass 2 : eval   -> " << quote_string(current) << "\n";
            return 0;
        }

        if (extract_iter_body(input, body, iter_count)) {
            std::cout << "boundary: iter body " << quote_string(body) << "\n";
            std::string collected = run_stream_pass(body, profile, false);
            std::cout << "pass 1 : noeval -> " << quote_string(collected) << "\n";

            std::string inner = strip_one_brace_layer(collected);
            std::vector<Unit> units = freeze_units(inner, profile);
            std::cout << "units  :";
            for (const Unit &unit : units) {
                std::cout << " " << unit_name(unit.type)
                          << "(" << quote_string(unit.text) << ")";
            }
            std::cout << "\n";

            std::string one = run_frozen_pass(units, profile, true);
            std::string out;
            for (int i = 0; i < iter_count; ++i) {
                if (i) {
                    out.push_back(' ');
                }
                out += one;
            }
            std::cout << "pass 2 : eval   -> " << quote_string(out) << "\n";
            return 0;
        }

        std::cout << "boundary: plain expression, no deferred boundary detected\n";
        std::string current = run_stream_pass(input, profile, true);
        std::cout << "pass 1 : eval   -> " << quote_string(current) << "\n";
        return 0;
    }

    print_usage();
    return 1;
}
