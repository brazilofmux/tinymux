#ifndef SCRIPT_H
#define SCRIPT_H

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstdlib>

// ---- Token types ----

enum class Tok : uint8_t {
    // Literals
    INT_LIT, FLOAT_LIT, STRING_LIT,
    IDENT,

    // Arithmetic
    PLUS, MINUS, STAR, SLASH,

    // Comparison (numeric)
    EQ, NE, LT, GT, LE, GE,

    // String comparison
    STREQ, STRNE,   // =~  !~
    MATCH, NMATCH,  // =/  !/

    // Logical
    AND, OR, NOT,   // &  |  !

    // Assignment
    ASSIGN,         // :=
    PLUS_ASSIGN, MINUS_ASSIGN, STAR_ASSIGN, SLASH_ASSIGN,

    // Increment/Decrement
    INC, DEC,       // ++  --

    // Grouping
    LPAREN, RPAREN,
    COMMA,

    // Ternary
    QUESTION, COLON,

    // Substitution markers (for later phases)
    PERCENT_LBRACE, // %{
    RBRACE,         // }

    // Sentinels
    END,
    ERROR,
};

struct Token {
    Tok         type;
    std::string sval;     // string content for STRING_LIT, IDENT
    int64_t     ival = 0; // integer value for INT_LIT
    double      fval = 0; // float value for FLOAT_LIT
};

// ---- Value type (TF dynamic typing) ----

enum class ValType : uint8_t { STR, INT, FLOAT };

struct Value {
    ValType     type = ValType::INT;
    int64_t     ival = 0;
    double      fval = 0.0;
    std::string sval;

    static Value make_str(std::string s) {
        Value v; v.type = ValType::STR; v.sval = std::move(s); return v;
    }
    static Value make_int(int64_t i) {
        Value v; v.type = ValType::INT; v.ival = i; return v;
    }
    static Value make_float(double f) {
        Value v; v.type = ValType::FLOAT; v.fval = f; return v;
    }

    // TF coercion
    int64_t as_int() const {
        switch (type) {
            case ValType::INT:   return ival;
            case ValType::FLOAT: return (int64_t)fval;
            case ValType::STR: {
                char* end;
                long long r = strtoll(sval.c_str(), &end, 0);
                return (end != sval.c_str()) ? (int64_t)r : 0;
            }
        }
        return 0;
    }

    double as_float() const {
        switch (type) {
            case ValType::INT:   return (double)ival;
            case ValType::FLOAT: return fval;
            case ValType::STR: {
                char* end;
                double r = strtod(sval.c_str(), &end);
                return (end != sval.c_str()) ? r : 0.0;
            }
        }
        return 0.0;
    }

    bool as_bool() const {
        switch (type) {
            case ValType::INT:   return ival != 0;
            case ValType::FLOAT: return fval != 0.0;
            case ValType::STR:   return !sval.empty() && sval != "0";
        }
        return false;
    }

    std::string as_str() const {
        switch (type) {
            case ValType::STR:   return sval;
            case ValType::INT: {
                char buf[32];
                snprintf(buf, sizeof(buf), "%lld", (long long)ival);
                return buf;
            }
            case ValType::FLOAT: {
                char buf[64];
                snprintf(buf, sizeof(buf), "%g", fval);
                return buf;
            }
        }
        return {};
    }
};

// ---- Script lexer (Ragel -G2) ----

class ScriptLexer {
public:
    // Tokenize the full expression string.
    void tokenize(const char* data, size_t len);
    void tokenize(const std::string& s) { tokenize(s.data(), s.size()); }

    const std::vector<Token>& tokens() const { return tokens_; }

private:
    void emit(Tok t);
    void emit_int(const char* ts, const char* te);
    void emit_float(const char* ts, const char* te);
    void emit_string(const char* ts, const char* te);
    void emit_ident(const char* ts, const char* te);

    std::vector<Token> tokens_;
};

// ---- Script environment ----

struct App;  // forward declaration — defined in app.h
using VarMap = std::unordered_map<std::string, std::string>;

class ScriptEnv {
public:
    explicit ScriptEnv(VarMap& vars, App* app = nullptr)
        : vars_(vars), app_(app) {}

    Value get(const std::string& name) const {
        auto it = vars_.find(name);
        if (it != vars_.end()) return Value::make_str(it->second);
        return Value::make_int(0);
    }

    void set(const std::string& name, const Value& v) {
        vars_[name] = v.as_str();
    }

    VarMap& vars() { return vars_; }
    App* app() const { return app_; }

private:
    VarMap& vars_;
    App* app_;
};

// ---- Expression parser/evaluator ----

// Evaluate an expression string.  Returns the result.
Value eval_expr(const std::string& expr, ScriptEnv& env);

// Expand %{var} and $[expr] substitutions in a string.
std::string expand_subs(const std::string& input, ScriptEnv& env);

#endif // SCRIPT_H
