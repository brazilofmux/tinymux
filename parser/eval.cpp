/*
 * eval.cpp - MUX expression AST evaluator study tool.
 *
 * Stage 3 of the parser study: walk the AST and evaluate it, producing
 * output equivalent to mux_exec for the subset of expressions that
 * don't require database access.
 *
 * Supported features:
 *   - Literal text concatenation
 *   - Eval brackets [...] (recursive evaluation)
 *   - Brace groups {...} (deferred — strip outer braces, don't evaluate)
 *   - %-substitutions (simulated with a register bank)
 *   - \-escapes (emit the escaped character)
 *   - Space handling (passthrough for now, no compression)
 *   - Pure functions: add, sub, mul, div, mod, abs, inc, dec,
 *     eq, neq, gt, gte, lt, lte, and, or, not, xor,
 *     if/ifelse, switch, case,
 *     cat, strcat, strlen, mid, left, right, first, rest, last,
 *     words, trim, ljust, rjust, center,
 *     iter, list, filter, map, fold, sort, setunion, setdiff, setinter,
 *     setq, setr, r,
 *     lnum, repeat, space, null, @@,
 *     t, comp, match, strmatch
 *
 * Not supported (require database/runtime):
 *   - u(), get(), v(), xget() — attribute access
 *   - name(), loc(), num() — database queries
 *   - pemit(), emit(), remit() — side effects
 *   - Dynamic function calls (DynCall nodes)
 *
 * This evaluator demonstrates that pure-expression MUX softcode CAN
 * be evaluated from an AST without the stream-transformer approach.
 */

#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <algorithm>
#include <sstream>

// ---------------------------------------------------------------
// Token types and tokenizer (same as parse.cpp)
// ---------------------------------------------------------------

enum TokenType {
    TOK_LIT,
    TOK_FUNC,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACK,
    TOK_RBRACK,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COMMA,
    TOK_SEMI,
    TOK_PCT,
    TOK_ESC,
    TOK_SPACE,
    TOK_EOF
};

struct Token {
    TokenType type;
    std::string text;
};

static std::string gather_pct(const char *&p)
{
    std::string sub("%");
    char ch = *p;

    if (!ch) {
        return sub;
    }

    char upper = static_cast<char>(toupper(static_cast<unsigned char>(ch)));

    if (ch >= '0' && ch <= '9') {
        sub += *p++;
    } else if (upper == 'Q') {
        sub += *p++;
        if (*p == '<') {
            sub += *p++;
            while (*p && *p != '>') {
                sub += *p++;
            }
            if (*p == '>') {
                sub += *p++;
            }
        } else if (*p) {
            sub += *p++;
        }
    } else if (upper == 'V') {
        sub += *p++;
        if (*p && isalpha(static_cast<unsigned char>(*p))) {
            sub += *p++;
        }
    } else if (upper == 'C' || upper == 'X') {
        sub += *p++;
        if (*p == '<') {
            sub += *p++;
            while (*p && *p != '>') {
                sub += *p++;
            }
            if (*p == '>') {
                sub += *p++;
            }
        } else if (*p) {
            sub += *p++;
        }
    } else if (ch == '=') {
        sub += *p++;
        if (*p == '<') {
            sub += *p++;
            while (*p && *p != '>') {
                sub += *p++;
            }
            if (*p == '>') {
                sub += *p++;
            }
        }
    } else if (upper == 'I') {
        sub += *p++;
        if (*p && *p >= '0' && *p <= '9') {
            sub += *p++;
        }
    } else {
        sub += *p++;
    }

    return sub;
}

static std::vector<Token> tokenize(const char *input)
{
    std::vector<Token> tokens;
    const char *p = input;

    while (*p) {
        if (*p == '[') {
            tokens.push_back({TOK_LBRACK, "["});
            p++;
        } else if (*p == ']') {
            tokens.push_back({TOK_RBRACK, "]"});
            p++;
        } else if (*p == '{') {
            tokens.push_back({TOK_LBRACE, "{"});
            p++;
        } else if (*p == '}') {
            tokens.push_back({TOK_RBRACE, "}"});
            p++;
        } else if (*p == '(') {
            if (!tokens.empty() && tokens.back().type == TOK_LIT) {
                tokens.back().type = TOK_FUNC;
            }
            tokens.push_back({TOK_LPAREN, "("});
            p++;
        } else if (*p == ')') {
            tokens.push_back({TOK_RPAREN, ")"});
            p++;
        } else if (*p == ',') {
            tokens.push_back({TOK_COMMA, ","});
            p++;
        } else if (*p == ';') {
            tokens.push_back({TOK_SEMI, ";"});
            p++;
        } else if (*p == '%') {
            p++;
            tokens.push_back({TOK_PCT, gather_pct(p)});
        } else if (*p == '\\') {
            std::string esc;
            esc += *p++;
            if (*p) {
                esc += *p++;
            }
            tokens.push_back({TOK_ESC, esc});
        } else if (*p == ' ' || *p == '\t') {
            std::string sp;
            while (*p == ' ' || *p == '\t') {
                sp += *p++;
            }
            tokens.push_back({TOK_SPACE, sp});
        } else {
            std::string lit;
            while (*p && *p != '[' && *p != ']' && *p != '{' && *p != '}'
                   && *p != '(' && *p != ')' && *p != ',' && *p != ';'
                   && *p != '%' && *p != '\\' && *p != ' ' && *p != '\t') {
                lit += *p++;
            }
            tokens.push_back({TOK_LIT, lit});
        }
    }

    tokens.push_back({TOK_EOF, ""});
    return tokens;
}

// ---------------------------------------------------------------
// AST node types (same as parse.cpp)
// ---------------------------------------------------------------

enum NodeType {
    NODE_SEQUENCE,
    NODE_LITERAL,
    NODE_SPACE,
    NODE_SUBST,
    NODE_ESCAPE,
    NODE_FUNCCALL,
    NODE_DYNCALL,
    NODE_EVALBRACKET,
    NODE_BRACEGROUP,
    NODE_SEMICOLON,
};

struct ASTNode {
    NodeType type;
    std::string text;
    std::vector<std::unique_ptr<ASTNode>> children;

    ASTNode(NodeType t, const std::string &s = "")
        : type(t), text(s) {}

    void addChild(std::unique_ptr<ASTNode> child) {
        children.push_back(std::move(child));
    }
};

// ---------------------------------------------------------------
// Parser (same as parse.cpp)
// ---------------------------------------------------------------

class Parser {
public:
    Parser(const std::vector<Token> &tokens)
        : m_tokens(tokens), m_pos(0) {}

    std::unique_ptr<ASTNode> parse() {
        return parseSequence(false, false, false, false);
    }

private:
    const std::vector<Token> &m_tokens;
    size_t m_pos;

    const Token &peek() const { return m_tokens[m_pos]; }
    Token advance() { return m_tokens[m_pos++]; }
    bool atEnd() const {
        return m_pos >= m_tokens.size() || m_tokens[m_pos].type == TOK_EOF;
    }

    std::unique_ptr<ASTNode> parseSequence(bool stopRP, bool stopRB,
                                            bool stopRC, bool stopCM)
    {
        auto seq = std::make_unique<ASTNode>(NODE_SEQUENCE);
        while (!atEnd()) {
            TokenType t = peek().type;
            if (stopRP && t == TOK_RPAREN) break;
            if (stopRB && t == TOK_RBRACK) break;
            if (stopRC && t == TOK_RBRACE) break;
            if (stopCM && t == TOK_COMMA)  break;

            auto node = parseOne();
            if (node) seq->addChild(std::move(node));
        }
        if (seq->children.size() == 1)
            return std::move(seq->children[0]);
        return seq;
    }

    std::unique_ptr<ASTNode> parseOne() {
        const Token &tok = peek();
        switch (tok.type) {
        case TOK_LIT: {
            auto n = std::make_unique<ASTNode>(NODE_LITERAL, tok.text);
            advance();
            return n;
        }
        case TOK_SPACE: {
            auto n = std::make_unique<ASTNode>(NODE_SPACE, tok.text);
            advance();
            return n;
        }
        case TOK_PCT: {
            auto n = std::make_unique<ASTNode>(NODE_SUBST, tok.text);
            advance();
            if (!atEnd() && peek().type == TOK_LPAREN)
                return parseDynCall(std::move(n));
            return n;
        }
        case TOK_ESC: {
            auto n = std::make_unique<ASTNode>(NODE_ESCAPE, tok.text);
            advance();
            return n;
        }
        case TOK_SEMI: {
            auto n = std::make_unique<ASTNode>(NODE_SEMICOLON, tok.text);
            advance();
            return n;
        }
        case TOK_FUNC:   return parseFuncCall();
        case TOK_LBRACK: return parseEvalBracket();
        case TOK_LBRACE: return parseBraceGroup();
        case TOK_RPAREN: case TOK_RBRACK: case TOK_RBRACE:
        case TOK_COMMA: case TOK_LPAREN: {
            auto n = std::make_unique<ASTNode>(NODE_LITERAL, tok.text);
            advance();
            return n;
        }
        case TOK_EOF: return nullptr;
        }
        return nullptr;
    }

    std::unique_ptr<ASTNode> parseFuncCall() {
        Token funcTok = advance();
        auto call = std::make_unique<ASTNode>(NODE_FUNCCALL, funcTok.text);
        if (atEnd() || peek().type != TOK_LPAREN) {
            call->type = NODE_LITERAL;
            return call;
        }
        advance();
        parseArgList(call.get());
        return call;
    }

    std::unique_ptr<ASTNode> parseDynCall(std::unique_ptr<ASTNode> nameExpr) {
        auto call = std::make_unique<ASTNode>(NODE_DYNCALL);
        call->addChild(std::move(nameExpr));
        advance();
        parseArgList(call.get());
        return call;
    }

    void parseArgList(ASTNode *call) {
        auto arg = parseSequence(true, false, false, true);
        call->addChild(std::move(arg));
        while (!atEnd() && peek().type == TOK_COMMA) {
            advance();
            arg = parseSequence(true, false, false, true);
            call->addChild(std::move(arg));
        }
        if (!atEnd() && peek().type == TOK_RPAREN)
            advance();
    }

    std::unique_ptr<ASTNode> parseEvalBracket() {
        advance();
        auto bracket = std::make_unique<ASTNode>(NODE_EVALBRACKET);
        auto contents = parseSequence(false, true, false, false);
        bracket->addChild(std::move(contents));
        if (!atEnd() && peek().type == TOK_RBRACK)
            advance();
        return bracket;
    }

    std::unique_ptr<ASTNode> parseBraceGroup() {
        advance();
        auto group = std::make_unique<ASTNode>(NODE_BRACEGROUP);
        auto contents = parseSequence(false, false, true, false);
        group->addChild(std::move(contents));
        if (!atEnd() && peek().type == TOK_RBRACE)
            advance();
        return group;
    }
};

// ---------------------------------------------------------------
// Evaluation context
// ---------------------------------------------------------------

struct EvalContext {
    // Registers %q0-%q9, %qa-%qz, and named %q<name>
    std::map<std::string, std::string> registers;

    // Command arguments %0-%9
    std::string args[10];

    // Iterator state for iter/list
    struct IterFrame {
        std::string itext;   // ## current item
        int inum;            // #@ current index
    };
    std::vector<IterFrame> iterStack;

    // Special substitutions
    std::string enactorName;   // %n
    std::string enactorDbref;  // %#
    std::string executorDbref; // %!
};

// ---------------------------------------------------------------
// Evaluator
// ---------------------------------------------------------------

class Evaluator {
public:
    Evaluator(EvalContext &ctx) : m_ctx(ctx) {
        registerBuiltins();
    }

    std::string eval(const ASTNode *node) {
        if (!node) return "";

        switch (node->type) {
        case NODE_SEQUENCE:
            return evalSequence(node);
        case NODE_LITERAL:
        case NODE_SPACE:
            return node->text;
        case NODE_SUBST:
            return evalSubst(node);
        case NODE_ESCAPE:
            return evalEscape(node);
        case NODE_FUNCCALL:
            return evalFuncCall(node);
        case NODE_DYNCALL:
            return "#-1 DYNAMIC CALL NOT SUPPORTED";
        case NODE_EVALBRACKET:
            return evalEvalBracket(node);
        case NODE_BRACEGROUP:
            return evalBraceGroup(node);
        case NODE_SEMICOLON:
            return "";
        }
        return "";
    }

private:
    EvalContext &m_ctx;

    // Two dispatch tables:
    // - m_funcs: normal functions, receive pre-evaluated string args
    // - m_noeval_funcs: FN_NOEVAL functions, receive unevaluated AST children
    //   and call eval() selectively (deferred evaluation)
    //
    using FuncHandler = std::function<std::string(const std::vector<std::string>&)>;
    std::map<std::string, FuncHandler> m_funcs;

    using NoevalHandler = std::function<std::string(const std::vector<std::unique_ptr<ASTNode>>&)>;
    std::map<std::string, NoevalHandler> m_noeval_funcs;

    // Helper: uppercase a string
    static std::string toUpper(const std::string &s) {
        std::string r = s;
        for (auto &c : r) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        return r;
    }

    // Helper: convert to integer
    static long toLong(const std::string &s) {
        if (s.empty()) return 0;
        char *end;
        long v = strtol(s.c_str(), &end, 10);
        return v;
    }

    // Helper: convert to double
    static double toDouble(const std::string &s) {
        if (s.empty()) return 0.0;
        char *end;
        double v = strtod(s.c_str(), &end);
        return v;
    }

    // Helper: boolean test (MUX truth: non-zero number or non-empty string)
    static bool toBool(const std::string &s) {
        if (s.empty()) return false;
        // Try as number first
        char *end;
        double v = strtod(s.c_str(), &end);
        if (end != s.c_str()) return v != 0.0;
        // Non-empty string is true
        return true;
    }

    // Helper: format number (strip trailing zeros)
    static std::string fmtNum(double v) {
        // If it's an integer, print without decimal
        if (v == floor(v) && fabs(v) < 1e15) {
            return std::to_string(static_cast<long long>(v));
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", v);
        return buf;
    }

    // Helper: split string by separator
    static std::vector<std::string> splitList(const std::string &s,
                                               const std::string &sep = " ")
    {
        std::vector<std::string> result;
        if (s.empty()) return result;

        if (sep == " ") {
            // Space-separated: skip leading/trailing spaces, compress
            size_t i = 0;
            while (i < s.size() && s[i] == ' ') i++;
            while (i < s.size()) {
                size_t j = i;
                while (j < s.size() && s[j] != ' ') j++;
                result.push_back(s.substr(i, j - i));
                while (j < s.size() && s[j] == ' ') j++;
                i = j;
            }
        } else {
            size_t start = 0;
            size_t pos;
            while ((pos = s.find(sep, start)) != std::string::npos) {
                result.push_back(s.substr(start, pos - start));
                start = pos + sep.size();
            }
            result.push_back(s.substr(start));
        }
        return result;
    }

    std::string evalSequence(const ASTNode *node) {
        std::string result;
        for (const auto &child : node->children) {
            result += eval(child.get());
        }
        return result;
    }

    std::string evalSubst(const ASTNode *node) {
        const std::string &sub = node->text;
        if (sub.size() < 2) return "%";

        char ch = sub[1];

        // %0-%9: command arguments
        if (ch >= '0' && ch <= '9') {
            return m_ctx.args[ch - '0'];
        }

        char upper = static_cast<char>(toupper(static_cast<unsigned char>(ch)));

        // %q register
        if (upper == 'Q') {
            std::string regname;
            if (sub.size() >= 4 && sub[2] == '<') {
                // %q<name>
                regname = sub.substr(3, sub.size() - 4);
            } else if (sub.size() >= 3) {
                // %q0-%qz
                regname = std::string(1, sub[2]);
            }
            auto it = m_ctx.registers.find(regname);
            if (it != m_ctx.registers.end()) return it->second;
            return "";
        }

        // %r → newline, %b → space, %t → tab
        if (upper == 'R') return "\r\n";
        if (upper == 'B') return " ";
        if (upper == 'T') return "\t";

        // %% → literal %
        if (ch == '%') return "%";

        // %# → enactor dbref
        if (ch == '#') return m_ctx.enactorDbref;

        // %! → executor dbref
        if (ch == '!') return m_ctx.executorDbref;

        // %n/%N → enactor name
        if (upper == 'N') {
            std::string name = m_ctx.enactorName;
            if (ch == 'N' && !name.empty()) {
                name[0] = static_cast<char>(toupper(static_cast<unsigned char>(name[0])));
            }
            return name;
        }

        // %i0-%i9: iterator text
        if (upper == 'I' && sub.size() >= 3) {
            int depth = sub[2] - '0';
            int idx = static_cast<int>(m_ctx.iterStack.size()) - 1 - depth;
            if (idx >= 0 && idx < static_cast<int>(m_ctx.iterStack.size())) {
                return m_ctx.iterStack[idx].itext;
            }
            return "";
        }

        // Unsupported substitutions — return the raw text
        return sub;
    }

    std::string evalEscape(const ASTNode *node) {
        // \x → just x
        if (node->text.size() >= 2) {
            return node->text.substr(1);
        }
        return "\\";
    }

    std::string evalFuncCall(const ASTNode *node) {
        std::string fname = toUpper(node->text);

        // Check FN_NOEVAL functions first — they receive unevaluated
        // AST children and call eval() selectively.
        //
        auto nit = m_noeval_funcs.find(fname);
        if (nit != m_noeval_funcs.end()) {
            return nit->second(node->children);
        }

        // Normal functions — evaluate all arguments first.
        //
        auto it = m_funcs.find(fname);
        if (it == m_funcs.end()) {
            return "#-1 FUNCTION (" + fname + ") NOT FOUND";
        }

        std::vector<std::string> args;
        for (const auto &child : node->children) {
            args.push_back(eval(child.get()));
        }

        return it->second(args);
    }

    std::string evalEvalBracket(const ASTNode *node) {
        if (node->children.empty()) return "";
        return eval(node->children[0].get());
    }

    std::string evalBraceGroup(const ASTNode *node) {
        // In MUX, {braced text} with EV_STRIP_CURLY strips the braces
        // and returns the interior unevaluated. We approximate this by
        // returning the children's text without evaluating substitutions.
        //
        // For this study tool, we just evaluate the contents — a real
        // interpreter would need the eval flags to decide.
        if (node->children.empty()) return "";
        return eval(node->children[0].get());
    }

    // ---------------------------------------------------------------
    // Builtin function registration
    // ---------------------------------------------------------------

    void registerBuiltins() {
        // Arithmetic
        m_funcs["ADD"] = [](const std::vector<std::string> &args) -> std::string {
            double sum = 0;
            for (const auto &a : args) sum += toDouble(a);
            return fmtNum(sum);
        };
        m_funcs["SUB"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "0";
            return fmtNum(toDouble(args[0]) - toDouble(args[1]));
        };
        m_funcs["MUL"] = [](const std::vector<std::string> &args) -> std::string {
            double prod = 1;
            for (const auto &a : args) prod *= toDouble(a);
            return fmtNum(prod);
        };
        m_funcs["DIV"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "0";
            long b = toLong(args[1]);
            if (b == 0) return "#-1 DIVIDE BY ZERO";
            return std::to_string(toLong(args[0]) / b);
        };
        m_funcs["MOD"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "0";
            long b = toLong(args[1]);
            if (b == 0) return "#-1 DIVIDE BY ZERO";
            return std::to_string(toLong(args[0]) % b);
        };
        m_funcs["ABS"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "0";
            return fmtNum(fabs(toDouble(args[0])));
        };
        m_funcs["INC"] = [](const std::vector<std::string> &args) -> std::string {
            long v = args.empty() ? 0 : toLong(args[0]);
            return std::to_string(v + 1);
        };
        m_funcs["DEC"] = [](const std::vector<std::string> &args) -> std::string {
            long v = args.empty() ? 0 : toLong(args[0]);
            return std::to_string(v - 1);
        };
        m_funcs["FLOOR"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "0";
            return std::to_string(static_cast<long long>(floor(toDouble(args[0]))));
        };
        m_funcs["CEIL"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "0";
            return std::to_string(static_cast<long long>(ceil(toDouble(args[0]))));
        };
        m_funcs["ROUND"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "0";
            double v = toDouble(args[0]);
            int places = args.size() > 1 ? static_cast<int>(toLong(args[1])) : 0;
            double factor = pow(10.0, places);
            v = round(v * factor) / factor;
            if (places <= 0) return std::to_string(static_cast<long long>(v));
            char buf[64];
            snprintf(buf, sizeof(buf), "%.*f", places, v);
            return buf;
        };
        m_funcs["MAX"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "0";
            double m = toDouble(args[0]);
            for (size_t i = 1; i < args.size(); i++) {
                double v = toDouble(args[i]);
                if (v > m) m = v;
            }
            return fmtNum(m);
        };
        m_funcs["MIN"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "0";
            double m = toDouble(args[0]);
            for (size_t i = 1; i < args.size(); i++) {
                double v = toDouble(args[i]);
                if (v < m) m = v;
            }
            return fmtNum(m);
        };
        m_funcs["POWER"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "0";
            return fmtNum(pow(toDouble(args[0]), toDouble(args[1])));
        };
        m_funcs["SQRT"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "0";
            double v = toDouble(args[0]);
            if (v < 0) return "#-1 SQUARE ROOT OF NEGATIVE";
            return fmtNum(sqrt(v));
        };

        // Comparison
        m_funcs["EQ"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "0";
            return toLong(args[0]) == toLong(args[1]) ? "1" : "0";
        };
        m_funcs["NEQ"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "0";
            return toLong(args[0]) != toLong(args[1]) ? "1" : "0";
        };
        m_funcs["GT"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "0";
            return toLong(args[0]) > toLong(args[1]) ? "1" : "0";
        };
        m_funcs["GTE"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "0";
            return toLong(args[0]) >= toLong(args[1]) ? "1" : "0";
        };
        m_funcs["LT"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "0";
            return toLong(args[0]) < toLong(args[1]) ? "1" : "0";
        };
        m_funcs["LTE"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "0";
            return toLong(args[0]) <= toLong(args[1]) ? "1" : "0";
        };
        m_funcs["COMP"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "0";
            int r = args[0].compare(args[1]);
            return std::to_string(r < 0 ? -1 : (r > 0 ? 1 : 0));
        };

        // Boolean
        m_funcs["AND"] = [](const std::vector<std::string> &args) -> std::string {
            for (const auto &a : args) {
                if (!toBool(a)) return "0";
            }
            return "1";
        };
        m_funcs["OR"] = [](const std::vector<std::string> &args) -> std::string {
            for (const auto &a : args) {
                if (toBool(a)) return "1";
            }
            return "0";
        };
        m_funcs["NOT"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "1";
            return toBool(args[0]) ? "0" : "1";
        };
        m_funcs["XOR"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "0";
            return (toBool(args[0]) != toBool(args[1])) ? "1" : "0";
        };
        m_funcs["T"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "0";
            return toBool(args[0]) ? "1" : "0";
        };

        // String functions
        m_funcs["STRLEN"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "0";
            return std::to_string(args[0].size());
        };
        m_funcs["MID"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 3) return "";
            std::string s = args[0];
            long pos = toLong(args[1]);
            long len = toLong(args[2]);
            if (pos < 0 || len < 0 || pos >= static_cast<long>(s.size())) return "";
            return s.substr(pos, len);
        };
        m_funcs["LEFT"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "";
            long len = toLong(args[1]);
            if (len <= 0) return "";
            return args[0].substr(0, len);
        };
        m_funcs["RIGHT"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "";
            long len = toLong(args[1]);
            if (len <= 0) return "";
            std::string s = args[0];
            if (len >= static_cast<long>(s.size())) return s;
            return s.substr(s.size() - len);
        };
        m_funcs["CAPSTR"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty() || args[0].empty()) return "";
            std::string s = args[0];
            s[0] = static_cast<char>(toupper(static_cast<unsigned char>(s[0])));
            return s;
        };
        m_funcs["LCSTR"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "";
            std::string s = args[0];
            for (auto &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            return s;
        };
        m_funcs["UCSTR"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "";
            std::string s = args[0];
            for (auto &c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            return s;
        };
        m_funcs["CAT"] = [](const std::vector<std::string> &args) -> std::string {
            std::string result;
            for (size_t i = 0; i < args.size(); i++) {
                if (i > 0) result += " ";
                result += args[i];
            }
            return result;
        };
        m_funcs["STRCAT"] = [](const std::vector<std::string> &args) -> std::string {
            std::string result;
            for (const auto &a : args) result += a;
            return result;
        };
        m_funcs["REPEAT"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "";
            long n = toLong(args[1]);
            if (n <= 0) return "";
            std::string result;
            for (long i = 0; i < n; i++) result += args[0];
            return result;
        };
        m_funcs["SPACE"] = [](const std::vector<std::string> &args) -> std::string {
            long n = args.empty() ? 1 : toLong(args[0]);
            if (n <= 0) return "";
            return std::string(n, ' ');
        };
        m_funcs["TRIM"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "";
            std::string s = args[0];
            size_t start = s.find_first_not_of(' ');
            if (start == std::string::npos) return "";
            size_t end = s.find_last_not_of(' ');
            return s.substr(start, end - start + 1);
        };

        // List functions
        m_funcs["WORDS"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "0";
            auto words = splitList(args[0]);
            return std::to_string(words.size());
        };
        m_funcs["FIRST"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "";
            auto words = splitList(args[0]);
            return words.empty() ? "" : words[0];
        };
        m_funcs["REST"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "";
            auto words = splitList(args[0]);
            if (words.size() <= 1) return "";
            std::string result;
            for (size_t i = 1; i < words.size(); i++) {
                if (i > 1) result += " ";
                result += words[i];
            }
            return result;
        };
        m_funcs["LAST"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "";
            auto words = splitList(args[0]);
            return words.empty() ? "" : words.back();
        };
        m_funcs["EXTRACT"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 3) return "";
            auto words = splitList(args[0]);
            long first = toLong(args[1]) - 1; // 1-based
            long count = toLong(args[2]);
            if (first < 0) first = 0;
            std::string result;
            for (long i = first; i < first + count && i < static_cast<long>(words.size()); i++) {
                if (i > first) result += " ";
                result += words[i];
            }
            return result;
        };
        m_funcs["LNUM"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "";
            long n = toLong(args[0]);
            std::string sep = args.size() > 1 ? args[1] : " ";
            std::string result;
            for (long i = 0; i < n; i++) {
                if (i > 0) result += sep;
                result += std::to_string(i);
            }
            return result;
        };
        m_funcs["SORT"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "";
            auto words = splitList(args[0]);
            // Try numeric sort first
            bool allNumeric = true;
            for (const auto &w : words) {
                char *end;
                strtod(w.c_str(), &end);
                if (end == w.c_str() || *end != '\0') {
                    allNumeric = false;
                    break;
                }
            }
            if (allNumeric) {
                std::sort(words.begin(), words.end(),
                    [](const std::string &a, const std::string &b) {
                        return strtod(a.c_str(), nullptr) < strtod(b.c_str(), nullptr);
                    });
            } else {
                std::sort(words.begin(), words.end());
            }
            std::string result;
            for (size_t i = 0; i < words.size(); i++) {
                if (i > 0) result += " ";
                result += words[i];
            }
            return result;
        };
        m_funcs["MEMBER"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "0";
            auto words = splitList(args[0]);
            for (size_t i = 0; i < words.size(); i++) {
                if (words[i] == args[1]) return std::to_string(i + 1);
            }
            return "0";
        };
        m_funcs["INDEX"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 4) return "";
            std::string sep = args[1];
            long first = toLong(args[2]) - 1;
            long count = toLong(args[3]);
            auto items = splitList(args[0], sep);
            if (first < 0) first = 0;
            std::string result;
            for (long i = first; i < first + count && i < static_cast<long>(items.size()); i++) {
                if (i > first) result += sep;
                result += items[i];
            }
            return result;
        };

        // Set operations
        m_funcs["SETUNION"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "";
            auto a = splitList(args[0]);
            auto b = splitList(args[1]);
            std::vector<std::string> result;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            std::set_union(a.begin(), a.end(), b.begin(), b.end(),
                           std::back_inserter(result));
            std::string out;
            for (size_t i = 0; i < result.size(); i++) {
                if (i > 0) out += " ";
                out += result[i];
            }
            return out;
        };
        m_funcs["SETDIFF"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "";
            auto a = splitList(args[0]);
            auto b = splitList(args[1]);
            std::vector<std::string> result;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            std::set_difference(a.begin(), a.end(), b.begin(), b.end(),
                                std::back_inserter(result));
            std::string out;
            for (size_t i = 0; i < result.size(); i++) {
                if (i > 0) out += " ";
                out += result[i];
            }
            return out;
        };
        m_funcs["SETINTER"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "";
            auto a = splitList(args[0]);
            auto b = splitList(args[1]);
            std::vector<std::string> result;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
                                  std::back_inserter(result));
            std::string out;
            for (size_t i = 0; i < result.size(); i++) {
                if (i > 0) out += " ";
                out += result[i];
            }
            return out;
        };

        // Register functions
        // Note: setq/setr need access to m_ctx, so we capture 'this'.
        m_funcs["SETQ"] = [this](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "";
            m_ctx.registers[args[0]] = args[1];
            return "";
        };
        m_funcs["SETR"] = [this](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "";
            m_ctx.registers[args[0]] = args[1];
            return args[1];
        };
        m_funcs["R"] = [this](const std::vector<std::string> &args) -> std::string {
            if (args.empty()) return "";
            auto it = m_ctx.registers.find(args[0]);
            if (it != m_ctx.registers.end()) return it->second;
            return "";
        };

        // ---------------------------------------------------------
        // FN_NOEVAL functions: receive unevaluated AST children,
        // call eval() selectively (deferred evaluation).
        //
        // This is the key architectural difference from the old
        // evaluator. These handlers get the AST subtrees and
        // choose which ones to evaluate and when.
        // ---------------------------------------------------------

        // if(condition, true_branch [, false_branch])
        //
        m_noeval_funcs["IF"] = [this](const std::vector<std::unique_ptr<ASTNode>> &children) -> std::string {
            if (children.size() < 2) return "";
            std::string cond = eval(children[0].get());
            if (toBool(cond)) {
                return eval(children[1].get());
            }
            return children.size() > 2 ? eval(children[2].get()) : "";
        };
        m_noeval_funcs["IFELSE"] = [this](const std::vector<std::unique_ptr<ASTNode>> &children) -> std::string {
            if (children.size() < 3) return "";
            std::string cond = eval(children[0].get());
            if (toBool(cond)) {
                return eval(children[1].get());
            }
            return eval(children[2].get());
        };

        // switch(val, pat1, result1, pat2, result2, ..., default)
        // Evaluates val and each pattern; only evaluates the matching result.
        //
        m_noeval_funcs["SWITCH"] = [this](const std::vector<std::unique_ptr<ASTNode>> &children) -> std::string {
            if (children.size() < 2) return "";
            std::string val = eval(children[0].get());
            for (size_t i = 1; i + 1 < children.size(); i += 2) {
                std::string pat = eval(children[i].get());
                if (pat == val || pat == "*") {
                    return eval(children[i + 1].get());
                }
            }
            // Default: odd remaining arg
            if (children.size() % 2 == 0) {
                return eval(children.back().get());
            }
            return "";
        };

        // case(val, pat1, result1, ..., default)
        // Like switch but exact match (no wildcard).
        //
        m_noeval_funcs["CASE"] = [this](const std::vector<std::unique_ptr<ASTNode>> &children) -> std::string {
            if (children.size() < 2) return "";
            std::string val = eval(children[0].get());
            for (size_t i = 1; i + 1 < children.size(); i += 2) {
                std::string pat = eval(children[i].get());
                if (pat == val) {
                    return eval(children[i + 1].get());
                }
            }
            if (children.size() % 2 == 0) {
                return eval(children.back().get());
            }
            return "";
        };

        // cand(expr1, expr2, ...) — short-circuit AND
        //
        m_noeval_funcs["CAND"] = [this](const std::vector<std::unique_ptr<ASTNode>> &children) -> std::string {
            for (const auto &child : children) {
                if (!toBool(eval(child.get()))) return "0";
            }
            return "1";
        };

        // cor(expr1, expr2, ...) — short-circuit OR
        //
        m_noeval_funcs["COR"] = [this](const std::vector<std::unique_ptr<ASTNode>> &children) -> std::string {
            for (const auto &child : children) {
                if (toBool(eval(child.get()))) return "1";
            }
            return "0";
        };

        // @@(comment) — discard without evaluating
        //
        m_noeval_funcs["@@"] = [](const std::vector<std::unique_ptr<ASTNode>> &) -> std::string {
            return "";
        };

        // lit(text) — return unevaluated text
        //
        m_noeval_funcs["LIT"] = [](const std::vector<std::unique_ptr<ASTNode>> &children) -> std::string {
            // Reconstruct the raw source text without evaluating.
            if (children.empty()) return "";
            std::function<std::string(const ASTNode*)> rawText =
                [&rawText](const ASTNode *n) -> std::string {
                if (!n) return "";
                switch (n->type) {
                case NODE_LITERAL:
                case NODE_SPACE:
                case NODE_SUBST:
                case NODE_ESCAPE:
                    return n->text;
                case NODE_SEMICOLON:
                    return ";";
                case NODE_FUNCCALL: {
                    std::string r = n->text + "(";
                    for (size_t i = 0; i < n->children.size(); i++) {
                        if (i > 0) r += ",";
                        r += rawText(n->children[i].get());
                    }
                    return r + ")";
                }
                case NODE_EVALBRACKET: {
                    std::string r = "[";
                    for (const auto &c : n->children) r += rawText(c.get());
                    return r + "]";
                }
                case NODE_BRACEGROUP: {
                    std::string r = "{";
                    for (const auto &c : n->children) r += rawText(c.get());
                    return r + "}";
                }
                case NODE_SEQUENCE: {
                    std::string r;
                    for (const auto &c : n->children) r += rawText(c.get());
                    return r;
                }
                case NODE_DYNCALL:
                    return "#-1 DYNAMIC";
                }
                return "";
            };
            return rawText(children[0].get());
        };

        // Null (not FN_NOEVAL — args are already evaluated, just discard)
        m_funcs["NULL"] = [](const std::vector<std::string> &) -> std::string {
            return "";
        };

        // Match
        m_funcs["MATCH"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "0";
            auto words = splitList(args[0]);
            for (size_t i = 0; i < words.size(); i++) {
                if (words[i] == args[1]) return std::to_string(i + 1);
            }
            return "0";
        };
        m_funcs["STRMATCH"] = [](const std::vector<std::string> &args) -> std::string {
            if (args.size() < 2) return "0";
            // Simple wildcard match: * matches anything
            if (args[1] == "*") return "1";
            return (args[0] == args[1]) ? "1" : "0";
        };

        // Misc
        m_funcs["ITEXT"] = [this](const std::vector<std::string> &args) -> std::string {
            int depth = args.empty() ? 0 : static_cast<int>(toLong(args[0]));
            int idx = static_cast<int>(m_ctx.iterStack.size()) - 1 - depth;
            if (idx >= 0 && idx < static_cast<int>(m_ctx.iterStack.size())) {
                return m_ctx.iterStack[idx].itext;
            }
            return "";
        };
        m_funcs["INUM"] = [this](const std::vector<std::string> &args) -> std::string {
            int depth = args.empty() ? 0 : static_cast<int>(toLong(args[0]));
            int idx = static_cast<int>(m_ctx.iterStack.size()) - 1 - depth;
            if (idx >= 0 && idx < static_cast<int>(m_ctx.iterStack.size())) {
                return std::to_string(m_ctx.iterStack[idx].inum);
            }
            return "";
        };

        // iter(list, body, osep, isep) — FN_NOEVAL
        // Evaluate list, then for each item, push iterator state
        // and evaluate body. Body uses %i0 for current item.
        //
        m_noeval_funcs["ITER"] = [this](const std::vector<std::unique_ptr<ASTNode>> &children) -> std::string {
            if (children.size() < 2) return "";

            // Evaluate the list argument
            std::string listVal = eval(children[0].get());

            // Evaluate separator arguments if present
            std::string sep = " ";
            std::string osep = " ";
            if (children.size() > 3) sep = eval(children[3].get());
            if (children.size() > 2) osep = eval(children[2].get());

            auto items = splitList(listVal, sep);
            std::string result;

            for (size_t i = 0; i < items.size(); i++) {
                if (i > 0) result += osep;

                // Push iterator frame — body can read via %i0 or itext(0)
                m_ctx.iterStack.push_back({items[i], static_cast<int>(i + 1)});

                // Evaluate the body subtree with iterator state active
                result += eval(children[1].get());

                m_ctx.iterStack.pop_back();
            }
            return result;
        };
    }
};

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------

int main(int argc, char *argv[])
{
    bool showAST = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--ast") == 0) {
            showAST = true;
        }
    }

    EvalContext ctx;
    // Set up some test values for substitutions
    ctx.enactorName = "testplayer";
    ctx.enactorDbref = "#1234";
    ctx.executorDbref = "#1234";
    ctx.args[0] = "hello";
    ctx.args[1] = "world";

    Evaluator evaluator(ctx);

    char line[8192];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        auto tokens = tokenize(line);
        Parser parser(tokens);
        auto ast = parser.parse();

        if (showAST) {
            printf("INPUT: %s\n", line);
            printf("AST:\n");
            // Quick inline printer
            std::function<void(const ASTNode*, int)> printNode =
                [&](const ASTNode *n, int indent) {
                if (!n) return;
                static const char *names[] = {
                    "Seq", "Lit", "Sp", "Sub", "Esc",
                    "Call", "DynCall", "Eval", "Brace", "Semi"
                };
                printf("%*s%s", indent, "", names[n->type]);
                if (!n->text.empty()) printf(" \"%s\"", n->text.c_str());
                printf("\n");
                for (const auto &c : n->children) printNode(c.get(), indent+2);
            };
            printNode(ast.get(), 2);
        }

        std::string result = evaluator.eval(ast.get());
        printf("%s\n", result.c_str());
    }
    return 0;
}
