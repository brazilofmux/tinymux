// script_parse.cpp — Recursive descent expression parser/evaluator.
//
// Directly evaluates expressions (no AST). Each precedence level is a
// function returning Value. Short-circuit semantics for & and |.
//
// Precedence (lowest to highest):
//   1. comma          a, b
//   2. assignment      x := y, x += y, ...
//   3. ternary         a ? b : c
//   4. logical or      a | b
//   5. logical and     a & b
//   6. relational      a == b, a < b, a =~ b, ...
//   7. additive        a + b, a - b
//   8. multiplicative  a * b, a / b
//   9. unary           !a, -a, ++x, --x
//  10. primary         literal, ident, func(args), (expr), %{var}

#include "script.h"
#include "app.h"
#include <cmath>
#include <ctime>
#include <regex>
#include <unistd.h>
#include <algorithm>
#include <functional>

// Built-in function type
using BuiltinFunc = std::function<Value(ScriptEnv&, const std::vector<Value>&)>;

struct FuncDef {
    int min_args;
    int max_args;  // -1 = unlimited
    BuiltinFunc func;
};

// Forward declarations
static const std::unordered_map<std::string, FuncDef>& builtin_funcs();

// ---- Parser state ----

class Parser {
public:
    Parser(const std::vector<Token>& tokens, ScriptEnv& env)
        : tokens_(tokens), env_(env), pos_(0) {}

    Value parse_expr() { return comma_expr(); }

private:
    const Token& peek() const { return tokens_[pos_]; }
    const Token& advance() { return tokens_[pos_++]; }
    bool at(Tok t) const { return peek().type == t; }
    bool match(Tok t) { if (at(t)) { advance(); return true; } return false; }

    void expect(Tok t) {
        if (!match(t)) {
            // Skip — best effort
        }
    }

    // --- Precedence levels ---

    Value comma_expr() {
        Value v = assignment_expr();
        while (match(Tok::COMMA)) {
            v = assignment_expr();
        }
        return v;
    }

    Value assignment_expr() {
        // If IDENT followed by assignment op, handle assignment
        if (peek().type == Tok::IDENT) {
            size_t save = pos_;
            std::string name = advance().sval;
            Tok op = peek().type;

            if (op == Tok::ASSIGN || op == Tok::PLUS_ASSIGN ||
                op == Tok::MINUS_ASSIGN || op == Tok::STAR_ASSIGN ||
                op == Tok::SLASH_ASSIGN) {
                advance();
                Value rhs = assignment_expr();  // right-associative
                Value result;

                if (op == Tok::ASSIGN) {
                    result = rhs;
                } else {
                    Value lhs = env_.get(name);
                    switch (op) {
                        case Tok::PLUS_ASSIGN:
                            if (lhs.type == ValType::FLOAT || rhs.type == ValType::FLOAT)
                                result = Value::make_float(lhs.as_float() + rhs.as_float());
                            else
                                result = Value::make_int(lhs.as_int() + rhs.as_int());
                            break;
                        case Tok::MINUS_ASSIGN:
                            if (lhs.type == ValType::FLOAT || rhs.type == ValType::FLOAT)
                                result = Value::make_float(lhs.as_float() - rhs.as_float());
                            else
                                result = Value::make_int(lhs.as_int() - rhs.as_int());
                            break;
                        case Tok::STAR_ASSIGN:
                            if (lhs.type == ValType::FLOAT || rhs.type == ValType::FLOAT)
                                result = Value::make_float(lhs.as_float() * rhs.as_float());
                            else
                                result = Value::make_int(lhs.as_int() * rhs.as_int());
                            break;
                        case Tok::SLASH_ASSIGN:
                            if (rhs.as_float() == 0.0)
                                result = Value::make_int(0);
                            else if (lhs.type == ValType::FLOAT || rhs.type == ValType::FLOAT)
                                result = Value::make_float(lhs.as_float() / rhs.as_float());
                            else {
                                int64_t d = rhs.as_int();
                                result = d ? Value::make_int(lhs.as_int() / d) : Value::make_int(0);
                            }
                            break;
                        default: result = rhs; break;
                    }
                }

                env_.set(name, result);
                return result;
            }

            // Not an assignment — backtrack
            pos_ = save;
        }
        return ternary_expr();
    }

    Value ternary_expr() {
        Value v = or_expr();
        if (match(Tok::QUESTION)) {
            Value then_val = comma_expr();
            expect(Tok::COLON);
            Value else_val = ternary_expr();
            return v.as_bool() ? then_val : else_val;
        }
        return v;
    }

    Value or_expr() {
        Value v = and_expr();
        while (match(Tok::OR)) {
            if (v.as_bool()) {
                and_expr();  // consume but discard (short-circuit)
                v = Value::make_int(1);
            } else {
                v = Value::make_int(and_expr().as_bool() ? 1 : 0);
            }
        }
        return v;
    }

    Value and_expr() {
        Value v = relational_expr();
        while (match(Tok::AND)) {
            if (!v.as_bool()) {
                relational_expr();  // consume but discard (short-circuit)
                v = Value::make_int(0);
            } else {
                v = Value::make_int(relational_expr().as_bool() ? 1 : 0);
            }
        }
        return v;
    }

    Value relational_expr() {
        Value v = additive_expr();
        while (true) {
            Tok op = peek().type;
            if (op == Tok::EQ || op == Tok::NE || op == Tok::LT ||
                op == Tok::GT || op == Tok::LE || op == Tok::GE ||
                op == Tok::STREQ || op == Tok::STRNE ||
                op == Tok::MATCH || op == Tok::NMATCH) {
                advance();
                Value rhs = additive_expr();
                int result = 0;

                switch (op) {
                    case Tok::EQ:  result = (v.as_float() == rhs.as_float()); break;
                    case Tok::NE:  result = (v.as_float() != rhs.as_float()); break;
                    case Tok::LT:  result = (v.as_float() <  rhs.as_float()); break;
                    case Tok::GT:  result = (v.as_float() >  rhs.as_float()); break;
                    case Tok::LE:  result = (v.as_float() <= rhs.as_float()); break;
                    case Tok::GE:  result = (v.as_float() >= rhs.as_float()); break;
                    case Tok::STREQ:  result = (v.as_str() == rhs.as_str()); break;
                    case Tok::STRNE:  result = (v.as_str() != rhs.as_str()); break;
                    case Tok::MATCH: {
                        try {
                            std::regex re(rhs.as_str());
                            result = std::regex_search(v.as_str(), re) ? 1 : 0;
                        } catch (...) { result = 0; }
                        break;
                    }
                    case Tok::NMATCH: {
                        try {
                            std::regex re(rhs.as_str());
                            result = std::regex_search(v.as_str(), re) ? 0 : 1;
                        } catch (...) { result = 1; }
                        break;
                    }
                    default: break;
                }
                v = Value::make_int(result);
            } else {
                break;
            }
        }
        return v;
    }

    Value additive_expr() {
        Value v = multiplicative_expr();
        while (true) {
            Tok op = peek().type;
            if (op == Tok::PLUS || op == Tok::MINUS) {
                advance();
                Value rhs = multiplicative_expr();
                if (op == Tok::PLUS) {
                    // String concatenation if both are strings and non-numeric
                    if (v.type == ValType::STR && rhs.type == ValType::STR) {
                        // Try numeric first (TF behavior)
                        char* e1; char* e2;
                        strtod(v.sval.c_str(), &e1);
                        strtod(rhs.sval.c_str(), &e2);
                        if (*e1 != '\0' || *e2 != '\0') {
                            // Not both numeric — concatenate
                            v = Value::make_str(v.sval + rhs.sval);
                            continue;
                        }
                    }
                    if (v.type == ValType::FLOAT || rhs.type == ValType::FLOAT)
                        v = Value::make_float(v.as_float() + rhs.as_float());
                    else
                        v = Value::make_int(v.as_int() + rhs.as_int());
                } else {
                    if (v.type == ValType::FLOAT || rhs.type == ValType::FLOAT)
                        v = Value::make_float(v.as_float() - rhs.as_float());
                    else
                        v = Value::make_int(v.as_int() - rhs.as_int());
                }
            } else {
                break;
            }
        }
        return v;
    }

    Value multiplicative_expr() {
        Value v = unary_expr();
        while (true) {
            Tok op = peek().type;
            if (op == Tok::STAR || op == Tok::SLASH) {
                advance();
                Value rhs = unary_expr();
                if (op == Tok::STAR) {
                    if (v.type == ValType::FLOAT || rhs.type == ValType::FLOAT)
                        v = Value::make_float(v.as_float() * rhs.as_float());
                    else
                        v = Value::make_int(v.as_int() * rhs.as_int());
                } else {
                    if (rhs.as_float() == 0.0) {
                        v = Value::make_int(0);
                    } else if (v.type == ValType::FLOAT || rhs.type == ValType::FLOAT) {
                        v = Value::make_float(v.as_float() / rhs.as_float());
                    } else {
                        int64_t d = rhs.as_int();
                        v = d ? Value::make_int(v.as_int() / d) : Value::make_int(0);
                    }
                }
            } else {
                break;
            }
        }
        return v;
    }

    Value unary_expr() {
        if (match(Tok::NOT)) {
            return Value::make_int(unary_expr().as_bool() ? 0 : 1);
        }
        if (match(Tok::MINUS)) {
            Value v = unary_expr();
            if (v.type == ValType::FLOAT) return Value::make_float(-v.fval);
            return Value::make_int(-v.as_int());
        }
        if (match(Tok::PLUS)) {
            return unary_expr();
        }
        if (match(Tok::INC)) {
            // Prefix ++: must be followed by IDENT
            if (at(Tok::IDENT)) {
                std::string name = advance().sval;
                Value v = env_.get(name);
                Value result = Value::make_int(v.as_int() + 1);
                env_.set(name, result);
                return result;
            }
            return Value::make_int(0);
        }
        if (match(Tok::DEC)) {
            if (at(Tok::IDENT)) {
                std::string name = advance().sval;
                Value v = env_.get(name);
                Value result = Value::make_int(v.as_int() - 1);
                env_.set(name, result);
                return result;
            }
            return Value::make_int(0);
        }
        return primary_expr();
    }

    Value primary_expr() {
        // Integer literal
        if (at(Tok::INT_LIT)) {
            auto& t = advance();
            return Value::make_int(t.ival);
        }
        // Float literal
        if (at(Tok::FLOAT_LIT)) {
            auto& t = advance();
            return Value::make_float(t.fval);
        }
        // String literal
        if (at(Tok::STRING_LIT)) {
            auto& t = advance();
            return Value::make_str(t.sval);
        }
        // Parenthesized expression
        if (match(Tok::LPAREN)) {
            Value v = comma_expr();
            expect(Tok::RPAREN);
            return v;
        }
        // %{variable}
        if (match(Tok::PERCENT_LBRACE)) {
            std::string name;
            if (at(Tok::IDENT)) {
                name = advance().sval;
            } else if (at(Tok::INT_LIT)) {
                // %{1}, %{2} — positional params (future)
                name = std::to_string(advance().ival);
            }
            expect(Tok::RBRACE);
            return env_.get(name);
        }
        // Identifier — variable or function call
        if (at(Tok::IDENT)) {
            std::string name = advance().sval;
            // Function call?
            if (match(Tok::LPAREN)) {
                std::vector<Value> args;
                if (!at(Tok::RPAREN)) {
                    args.push_back(assignment_expr());
                    while (match(Tok::COMMA)) {
                        args.push_back(assignment_expr());
                    }
                }
                expect(Tok::RPAREN);

                auto& funcs = builtin_funcs();
                auto it = funcs.find(name);
                if (it != funcs.end()) {
                    return it->second.func(env_, args);
                }
                return Value::make_int(0);  // unknown function
            }
            // Variable lookup
            return env_.get(name);
        }

        // Unknown token — return 0
        if (!at(Tok::END)) advance();
        return Value::make_int(0);
    }

    const std::vector<Token>& tokens_;
    ScriptEnv& env_;
    size_t pos_;
};

// ---- Built-in functions ----

static const std::unordered_map<std::string, FuncDef>& builtin_funcs() {
    static const std::unordered_map<std::string, FuncDef> funcs = {

        // ---- String functions ----

        {"strlen", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            return Value::make_int((int64_t)a[0].as_str().size());
        }}},
        {"strcat", {1, -1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            std::string r;
            for (auto& v : a) r += v.as_str();
            return Value::make_str(r);
        }}},
        {"substr", {2, 3, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            std::string s = a[0].as_str();
            int64_t start = a[1].as_int();
            if (start < 0) start = 0;
            if ((size_t)start >= s.size()) return Value::make_str("");
            if (a.size() >= 3) {
                int64_t len = a[2].as_int();
                return Value::make_str(s.substr(start, len));
            }
            return Value::make_str(s.substr(start));
        }}},
        {"strcmp", {2, 2, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            int c = a[0].as_str().compare(a[1].as_str());
            return Value::make_int(c < 0 ? -1 : c > 0 ? 1 : 0);
        }}},
        {"strncmp", {3, 3, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            int c = a[0].as_str().compare(0, a[2].as_int(), a[1].as_str(), 0, a[2].as_int());
            return Value::make_int(c < 0 ? -1 : c > 0 ? 1 : 0);
        }}},
        {"strchr", {2, 2, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            auto pos = a[0].as_str().find(a[1].as_str());
            return Value::make_int(pos != std::string::npos ? (int64_t)pos : -1);
        }}},
        {"strrchr", {2, 2, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            auto pos = a[0].as_str().rfind(a[1].as_str());
            return Value::make_int(pos != std::string::npos ? (int64_t)pos : -1);
        }}},
        {"strstr", {2, 2, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            auto pos = a[0].as_str().find(a[1].as_str());
            return Value::make_int(pos != std::string::npos ? (int64_t)pos : -1);
        }}},
        {"tolower", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            std::string s = a[0].as_str();
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return Value::make_str(s);
        }}},
        {"toupper", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            std::string s = a[0].as_str();
            std::transform(s.begin(), s.end(), s.begin(), ::toupper);
            return Value::make_str(s);
        }}},
        {"replace", {3, 3, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            std::string s = a[0].as_str();
            std::string from = a[1].as_str();
            std::string to = a[2].as_str();
            if (from.empty()) return Value::make_str(s);
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                s.replace(pos, from.size(), to);
                pos += to.size();
            }
            return Value::make_str(s);
        }}},
        {"ascii", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            std::string s = a[0].as_str();
            return Value::make_int(s.empty() ? 0 : (unsigned char)s[0]);
        }}},
        {"char", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            char c = (char)a[0].as_int();
            return Value::make_str(std::string(1, c));
        }}},
        {"pad", {2, 3, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            // pad(string, width [, pad_char])
            std::string s = a[0].as_str();
            int64_t width = a[1].as_int();
            char fill = (a.size() >= 3 && !a[2].as_str().empty()) ? a[2].as_str()[0] : ' ';
            if ((int64_t)s.size() >= width) return Value::make_str(s);
            s.append(width - s.size(), fill);
            return Value::make_str(s);
        }}},
        {"strrep", {2, 2, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            std::string s = a[0].as_str();
            int64_t n = a[1].as_int();
            std::string r;
            r.reserve(s.size() * n);
            for (int64_t i = 0; i < n; i++) r += s;
            return Value::make_str(r);
        }}},
        {"regmatch", {2, 2, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // regmatch(string, pattern) — returns 1 if match, sets P0..Pn
            try {
                std::regex re(a[1].as_str());
                std::smatch m;
                std::string s = a[0].as_str();
                if (std::regex_search(s, m, re)) {
                    for (size_t i = 0; i < m.size() && i < 10; i++) {
                        env.set("P" + std::to_string(i), Value::make_str(m[i].str()));
                    }
                    return Value::make_int(1);
                }
            } catch (...) {}
            return Value::make_int(0);
        }}},
        {"strip_attr", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            // Strip ANSI escape sequences from a string
            std::string s = a[0].as_str();
            std::string r;
            for (size_t i = 0; i < s.size(); ) {
                if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
                    i += 2;
                    while (i < s.size() && s[i] != 'm' && !(s[i] >= 0x40 && s[i] <= 0x7E)) i++;
                    if (i < s.size()) i++;
                } else {
                    r += s[i++];
                }
            }
            return Value::make_str(r);
        }}},

        // ---- Math functions ----

        {"abs", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            if (a[0].type == ValType::FLOAT) return Value::make_float(fabs(a[0].fval));
            int64_t i = a[0].as_int();
            return Value::make_int(i < 0 ? -i : i);
        }}},
        {"mod", {2, 2, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            int64_t d = a[1].as_int();
            return d ? Value::make_int(a[0].as_int() % d) : Value::make_int(0);
        }}},
        {"trunc", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            return Value::make_int((int64_t)a[0].as_float());
        }}},
        {"sqrt", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            return Value::make_float(sqrt(a[0].as_float()));
        }}},
        {"pow", {2, 2, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            return Value::make_float(pow(a[0].as_float(), a[1].as_float()));
        }}},
        {"exp", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            return Value::make_float(exp(a[0].as_float()));
        }}},
        {"ln", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            return Value::make_float(log(a[0].as_float()));
        }}},
        {"log10", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            return Value::make_float(log10(a[0].as_float()));
        }}},
        {"sin", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            return Value::make_float(sin(a[0].as_float()));
        }}},
        {"cos", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            return Value::make_float(cos(a[0].as_float()));
        }}},
        {"tan", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            return Value::make_float(tan(a[0].as_float()));
        }}},
        {"asin", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            return Value::make_float(asin(a[0].as_float()));
        }}},
        {"acos", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            return Value::make_float(acos(a[0].as_float()));
        }}},
        {"atan", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            return Value::make_float(atan(a[0].as_float()));
        }}},
        {"rand", {0, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            if (a.empty()) return Value::make_int(::rand());
            int64_t max = a[0].as_int();
            return max > 0 ? Value::make_int(::rand() % max) : Value::make_int(0);
        }}},

        // ---- Time functions ----

        {"time", {0, 0, [](ScriptEnv&, const std::vector<Value>&) -> Value {
            return Value::make_int((int64_t)::time(nullptr));
        }}},
        {"ftime", {1, 2, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            // ftime(format [, time])
            std::string fmt = a[0].as_str();
            time_t t = (a.size() >= 2) ? (time_t)a[1].as_int() : ::time(nullptr);
            struct tm tm;
            localtime_r(&t, &tm);
            char buf[256];
            size_t len = strftime(buf, sizeof(buf), fmt.c_str(), &tm);
            return Value::make_str(std::string(buf, len));
        }}},
        {"mktime", {6, 6, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            struct tm tm{};
            tm.tm_year = (int)a[0].as_int() - 1900;
            tm.tm_mon  = (int)a[1].as_int() - 1;
            tm.tm_mday = (int)a[2].as_int();
            tm.tm_hour = (int)a[3].as_int();
            tm.tm_min  = (int)a[4].as_int();
            tm.tm_sec  = (int)a[5].as_int();
            tm.tm_isdst = -1;
            return Value::make_int((int64_t)::mktime(&tm));
        }}},

        // ---- I/O functions (require App) ----

        {"echo", {1, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            if (App* app = env.app()) {
                app->terminal.print_line(a[0].as_str());
            }
            return Value::make_int(1);
        }}},
        {"send", {1, 2, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // send(text [, world_name])
            App* app = env.app();
            if (!app) return Value::make_int(0);
            Connection* target = nullptr;
            if (a.size() >= 2 && !a[1].as_str().empty()) {
                auto it = app->connections.find(a[1].as_str());
                if (it != app->connections.end()) target = it->second.get();
            } else {
                target = app->fg;
            }
            if (target && target->is_connected()) {
                target->send_line(a[0].as_str());
                return Value::make_int(1);
            }
            return Value::make_int(0);
        }}},

        // ---- Query functions (require App) ----

        {"columns", {0, 0, [](ScriptEnv& env, const std::vector<Value>&) -> Value {
            if (App* app = env.app()) return Value::make_int(app->terminal.get_cols());
            return Value::make_int(80);
        }}},
        {"lines", {0, 0, [](ScriptEnv& env, const std::vector<Value>&) -> Value {
            if (App* app = env.app()) return Value::make_int(app->terminal.get_rows());
            return Value::make_int(24);
        }}},
        {"winlines", {0, 0, [](ScriptEnv& env, const std::vector<Value>&) -> Value {
            // Output pane height (total rows - 2 for status + input)
            if (App* app = env.app()) return Value::make_int(app->terminal.max_output_lines());
            return Value::make_int(22);
        }}},
        {"fg_world", {0, 0, [](ScriptEnv& env, const std::vector<Value>&) -> Value {
            if (App* app = env.app()) {
                if (app->fg) return Value::make_str(app->fg->world_name());
            }
            return Value::make_str("");
        }}},
        {"is_connected", {0, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            App* app = env.app();
            if (!app) return Value::make_int(0);
            if (a.empty() || a[0].as_str().empty()) {
                return Value::make_int(app->fg && app->fg->is_connected() ? 1 : 0);
            }
            auto it = app->connections.find(a[0].as_str());
            return Value::make_int(
                (it != app->connections.end() && it->second->is_connected()) ? 1 : 0);
        }}},
        {"is_open", {0, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // Same as is_connected for us
            App* app = env.app();
            if (!app) return Value::make_int(0);
            if (a.empty() || a[0].as_str().empty()) {
                return Value::make_int(app->fg && app->fg->is_connected() ? 1 : 0);
            }
            auto it = app->connections.find(a[0].as_str());
            return Value::make_int(
                (it != app->connections.end() && it->second->is_connected()) ? 1 : 0);
        }}},
        {"nactive", {0, 0, [](ScriptEnv& env, const std::vector<Value>&) -> Value {
            if (App* app = env.app()) {
                int n = 0;
                for (auto& [name, conn] : app->connections)
                    if (conn->is_connected()) n++;
                return Value::make_int(n);
            }
            return Value::make_int(0);
        }}},
        {"world_info", {1, 2, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // world_info(field [, world_name])
            App* app = env.app();
            if (!app) return Value::make_str("");
            std::string field = a[0].as_str();
            std::string wname;
            if (a.size() >= 2) {
                wname = a[1].as_str();
            } else if (app->fg) {
                wname = app->fg->world_name();
            }
            const World* w = app->worlddb.find(wname);
            if (!w) return Value::make_str("");

            if (field == "name")      return Value::make_str(w->name);
            if (field == "type")      return Value::make_str(w->type);
            if (field == "host")      return Value::make_str(w->host);
            if (field == "port")      return Value::make_str(w->port);
            if (field == "character") return Value::make_str(w->character);
            if (field == "mfile")     return Value::make_str(w->mfile);
            if (field == "ssl")       return Value::make_int(w->ssl() ? 1 : 0);
            return Value::make_str("");
        }}},

        // ---- System functions ----

        {"getpid", {0, 0, [](ScriptEnv&, const std::vector<Value>&) -> Value {
            return Value::make_int((int64_t)getpid());
        }}},
        {"systype", {0, 0, [](ScriptEnv&, const std::vector<Value>&) -> Value {
            return Value::make_str("unix");
        }}},
        {"gethostname", {0, 0, [](ScriptEnv&, const std::vector<Value>&) -> Value {
            char buf[256];
            if (::gethostname(buf, sizeof(buf)) == 0) return Value::make_str(buf);
            return Value::make_str("");
        }}},

        // ---- World management functions ----

        {"addworld", {3, 8, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // addworld(name, type, host, port [, char, pass, mfile, flags])
            // TF signature: addworld(name, type, host, port, char, pass, mfile, flags)
            App* app = env.app();
            if (!app) return Value::make_int(0);

            World w;
            w.name = a[0].as_str();
            w.type = (a.size() > 1) ? a[1].as_str() : "tiny";
            w.host = (a.size() > 2) ? a[2].as_str() : "";
            w.port = (a.size() > 3) ? a[3].as_str() : "";
            w.character = (a.size() > 4) ? a[4].as_str() : "";
            w.password  = (a.size() > 5) ? a[5].as_str() : "";
            w.mfile     = (a.size() > 6) ? a[6].as_str() : "";
            w.flags     = (a.size() > 7) ? a[7].as_str() : "";

            if (w.name.empty() || w.host.empty()) return Value::make_int(0);

            bool added = app->worlddb.add(std::move(w));
            return Value::make_int(added ? 1 : 2);  // 1=new, 2=replaced (TF convention)
        }}},
        {"unworld", {1, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            App* app = env.app();
            if (!app) return Value::make_int(0);
            return Value::make_int(app->worlddb.remove(a[0].as_str()) ? 1 : 0);
        }}},

        // ---- File I/O functions ----

        {"tfopen", {2, 2, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // tfopen(filename, mode) — returns handle string, or "" on error
            App* app = env.app();
            if (!app) return Value::make_str("");
            std::string filename = a[0].as_str();
            std::string mode = a[1].as_str();
            // Map TF modes: "r", "w", "a"
            if (mode != "r" && mode != "w" && mode != "a") return Value::make_str("");
            FILE* fp = fopen(filename.c_str(), mode.c_str());
            if (!fp) return Value::make_str("");
            std::string handle = "f" + std::to_string(app->next_file_id++);
            app->open_files[handle] = fp;
            return Value::make_str(handle);
        }}},
        {"tfclose", {1, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            App* app = env.app();
            if (!app) return Value::make_int(0);
            std::string handle = a[0].as_str();
            auto it = app->open_files.find(handle);
            if (it == app->open_files.end()) return Value::make_int(0);
            fclose(it->second);
            app->open_files.erase(it);
            return Value::make_int(1);
        }}},
        {"tfread", {2, 2, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // tfread(handle, varname) — read one line into variable, returns 1 or 0 at EOF
            App* app = env.app();
            if (!app) return Value::make_int(0);
            std::string handle = a[0].as_str();
            std::string varname = a[1].as_str();
            auto it = app->open_files.find(handle);
            if (it == app->open_files.end()) return Value::make_int(0);
            char buf[8192];
            if (!fgets(buf, sizeof(buf), it->second)) return Value::make_int(0);
            std::string line(buf);
            if (!line.empty() && line.back() == '\n') line.pop_back();
            if (!line.empty() && line.back() == '\r') line.pop_back();
            env.set(varname, Value::make_str(line));
            return Value::make_int(1);
        }}},
        {"tfwrite", {2, 2, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // tfwrite(handle, text) — write text + newline
            App* app = env.app();
            if (!app) return Value::make_int(0);
            std::string handle = a[0].as_str();
            std::string text = a[1].as_str();
            auto it = app->open_files.find(handle);
            if (it == app->open_files.end()) return Value::make_int(0);
            fprintf(it->second, "%s\n", text.c_str());
            return Value::make_int(1);
        }}},
        {"tfflush", {1, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            App* app = env.app();
            if (!app) return Value::make_int(0);
            std::string handle = a[0].as_str();
            auto it = app->open_files.find(handle);
            if (it == app->open_files.end()) return Value::make_int(0);
            fflush(it->second);
            return Value::make_int(1);
        }}},
        {"tfreadable", {1, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // tfreadable(handle) — 1 if more data available
            App* app = env.app();
            if (!app) return Value::make_int(0);
            std::string handle = a[0].as_str();
            auto it = app->open_files.find(handle);
            if (it == app->open_files.end()) return Value::make_int(0);
            return Value::make_int(feof(it->second) ? 0 : 1);
        }}},
        {"fwrite", {2, 2, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // fwrite(filename, text) — append text to file (no handle needed)
            std::string filename = a[0].as_str();
            std::string text = a[1].as_str();
            FILE* fp = fopen(filename.c_str(), "a");
            if (!fp) return Value::make_int(0);
            fprintf(fp, "%s\n", text.c_str());
            fclose(fp);
            return Value::make_int(1);
        }}},
        {"read", {0, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // read() — read a line from stdin (not useful in ncurses mode, stub)
            return Value::make_str("");
        }}},
        {"filename", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            // filename(path) — expand ~ and return absolute path
            std::string path = a[0].as_str();
            if (!path.empty() && path[0] == '~') {
                const char* home = getenv("HOME");
                if (home) path = std::string(home) + path.substr(1);
            }
            return Value::make_str(path);
        }}},

        // ---- Eval function ----

        {"eval", {1, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // eval(string) — evaluate string as TF expression
            return eval_expr(a[0].as_str(), env);
        }}},

        // ---- Idle time functions ----

        {"idle", {0, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // idle([world]) — seconds since last received data
            App* app = env.app();
            if (!app) return Value::make_int(0);
            Connection* target = nullptr;
            if (!a.empty() && !a[0].as_str().empty()) {
                auto it = app->connections.find(a[0].as_str());
                if (it != app->connections.end()) target = it->second.get();
            } else {
                target = app->fg;
            }
            return Value::make_int(target ? target->idle_secs() : 0);
        }}},
        {"sidle", {0, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // sidle([world]) — seconds since last send
            App* app = env.app();
            if (!app) return Value::make_int(0);
            Connection* target = nullptr;
            if (!a.empty() && !a[0].as_str().empty()) {
                auto it = app->connections.find(a[0].as_str());
                if (it != app->connections.end()) target = it->second.get();
            } else {
                target = app->fg;
            }
            return Value::make_int(target ? target->sidle_secs() : 0);
        }}},

        // ---- Misc functions ----

        {"whatis", {1, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // whatis(name) — returns type: "macro", "variable", "builtin", ""
            App* app = env.app();
            std::string name = a[0].as_str();
            if (app && app->macros.find(name)) return Value::make_str("macro");
            auto it = env.vars().find(name);
            if (it != env.vars().end()) return Value::make_str("variable");
            auto& funcs = builtin_funcs();
            if (funcs.count(name)) return Value::make_str("builtin");
            return Value::make_str("");
        }}},
        {"cputime", {0, 0, [](ScriptEnv&, const std::vector<Value>&) -> Value {
            return Value::make_float((double)clock() / CLOCKS_PER_SEC);
        }}},
        {"isatty", {0, 0, [](ScriptEnv&, const std::vector<Value>&) -> Value {
            return Value::make_int(::isatty(STDIN_FILENO) ? 1 : 0);
        }}},

        // ---- ANSI encode/decode functions ----

        {"encode_ansi", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            // encode_ansi(text) — convert ANSI escape sequences to TF attribute codes
            // For now, pass through unchanged (we use ANSI natively)
            return Value::make_str(a[0].as_str());
        }}},
        {"decode_ansi", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            // decode_ansi(text) — convert TF attribute codes to ANSI escapes
            return Value::make_str(a[0].as_str());
        }}},
        {"encode_attr", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            return Value::make_str(a[0].as_str());
        }}},
        {"decode_attr", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            return Value::make_str(a[0].as_str());
        }}},
        {"strcmpattr", {2, 2, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            // Compare strings ignoring attributes — same as strcmp for us
            int c = a[0].as_str().compare(a[1].as_str());
            return Value::make_int(c < 0 ? -1 : c > 0 ? 1 : 0);
        }}},

        // ---- Keyboard buffer functions ----
        // These expose the input line editing buffer to scripts.

        {"kblen", {0, 0, [](ScriptEnv& env, const std::vector<Value>&) -> Value {
            // Length of current input buffer
            // Stub — needs Terminal API exposure
            return Value::make_int(0);
        }}},
        {"kbpoint", {0, 0, [](ScriptEnv& env, const std::vector<Value>&) -> Value {
            // Current cursor position in input buffer
            return Value::make_int(0);
        }}},
        {"kbhead", {0, 0, [](ScriptEnv& env, const std::vector<Value>&) -> Value {
            // Text before cursor
            return Value::make_str("");
        }}},
        {"kbtail", {0, 0, [](ScriptEnv& env, const std::vector<Value>&) -> Value {
            // Text after cursor
            return Value::make_str("");
        }}},
        {"kbgoto", {1, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // Move cursor to position
            return Value::make_int(0);
        }}},
        {"kbdel", {1, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // Delete n characters at cursor
            return Value::make_int(0);
        }}},
        {"kbmatch", {0, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // Find matching bracket/paren
            return Value::make_int(-1);
        }}},
        {"kbwordleft", {0, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // Position of start of word to the left
            return Value::make_int(0);
        }}},
        {"kbwordright", {0, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // Position of end of word to the right
            return Value::make_int(0);
        }}},
        {"keycode", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            // keycode(keyname) — return key code string for binding
            return Value::make_str(a[0].as_str());
        }}},

        // ---- More/pager functions ----

        {"morepaused", {0, 1, [](ScriptEnv&, const std::vector<Value>&) -> Value {
            // Whether the pager is paused — we don't have a pager
            return Value::make_int(0);
        }}},
        {"morescroll", {1, 1, [](ScriptEnv&, const std::vector<Value>& a) -> Value {
            // Scroll the pager — stub
            return Value::make_int(0);
        }}},
        {"moresize", {0, 2, [](ScriptEnv& env, const std::vector<Value>&) -> Value {
            // Number of lines in the more buffer
            return Value::make_int(0);
        }}},

        // ---- Status bar functions ----

        {"status_fields", {0, 1, [](ScriptEnv&, const std::vector<Value>&) -> Value {
            return Value::make_str("");
        }}},
        {"status_width", {1, 1, [](ScriptEnv& env, const std::vector<Value>&) -> Value {
            if (App* app = env.app()) return Value::make_int(app->terminal.get_cols());
            return Value::make_int(80);
        }}},

        // ---- Misc functions ----

        {"test", {1, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // test(expr) — evaluate expression, return result
            return eval_expr(a[0].as_str(), env);
        }}},
        {"substitute", {1, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // substitute(text) — replace current line in output (stub)
            // Would need output buffer manipulation
            return Value::make_int(1);
        }}},
        {"prompt", {1, 1, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // prompt(text) — set the prompt display
            if (App* app = env.app()) {
                app->terminal.print_line(a[0].as_str());
            }
            return Value::make_int(1);
        }}},
        {"fake_recv", {1, 3, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // fake_recv(text [, world, attrs]) — inject text as if received from MUD
            if (App* app = env.app()) {
                std::string text = a[0].as_str();
                app->terminal.print_line(text);
                if (app->fg) app->fg->add_to_scrollback(text);
                // Run triggers against it
                check_triggers(*app, text);
            }
            return Value::make_int(1);
        }}},
        {"getopts", {1, 2, [](ScriptEnv& env, const std::vector<Value>& a) -> Value {
            // getopts(optstring [, args]) — parse option flags from args
            // Simplified: return the args unchanged (full getopt is complex)
            if (a.size() >= 2) return a[1];
            return Value::make_str("");
        }}},
        {"nlog", {0, 0, [](ScriptEnv&, const std::vector<Value>&) -> Value {
            // Number of log lines — stub
            return Value::make_int(0);
        }}},
        {"nmail", {0, 0, [](ScriptEnv&, const std::vector<Value>&) -> Value {
            return Value::make_int(0);
        }}},
        {"nread", {0, 0, [](ScriptEnv&, const std::vector<Value>&) -> Value {
            return Value::make_int(0);
        }}},
    };
    return funcs;
}

// ---- Public API ----

Value eval_expr(const std::string& expr, ScriptEnv& env) {
    ScriptLexer lexer;
    lexer.tokenize(expr);
    Parser parser(lexer.tokens(), env);
    return parser.parse_expr();
}

// ---- Substitution expansion ----

std::string expand_subs(const std::string& input, ScriptEnv& env) {
    std::string result;
    result.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        // %{varname}
        if (input[i] == '%' && i + 1 < input.size() && input[i + 1] == '{') {
            i += 2;
            size_t start = i;
            int depth = 1;
            while (i < input.size() && depth > 0) {
                if (input[i] == '{') depth++;
                else if (input[i] == '}') depth--;
                if (depth > 0) i++;
            }
            std::string varname = input.substr(start, i - start);
            if (i < input.size()) i++; // skip '}'
            Value v = env.get(varname);
            result += v.as_str();
            continue;
        }

        // $[expression]
        if (input[i] == '$' && i + 1 < input.size() && input[i + 1] == '[') {
            i += 2;
            size_t start = i;
            int depth = 1;
            while (i < input.size() && depth > 0) {
                if (input[i] == '[') depth++;
                else if (input[i] == ']') depth--;
                if (depth > 0) i++;
            }
            std::string expr = input.substr(start, i - start);
            if (i < input.size()) i++; // skip ']'
            Value v = eval_expr(expr, env);
            result += v.as_str();
            continue;
        }

        // Backslash escapes
        if (input[i] == '\\' && i + 1 < input.size()) {
            i++;
            switch (input[i]) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case '\\': result += '\\'; break;
                case '%': result += '%'; break;
                case '$': result += '$'; break;
                default: result += '\\'; result += input[i]; break;
            }
            i++;
            continue;
        }

        result += input[i++];
    }

    return result;
}
