/*
 * eval.cpp - MUX expression AST evaluator study tool.
 *
 * Stage 3 of the parser study: walk the AST and evaluate it, producing
 * output equivalent to mux_exec for the subset of expressions that
 * don't require database access.
 *
 * Two-tier function dispatch:
 *   - Normal functions: arguments pre-evaluated, handler gets strings
 *   - FN_NOEVAL functions: arguments NOT pre-evaluated, handler gets
 *     AST subtrees and calls eval() selectively
 *
 * This demonstrates that MUX softcode CAN be evaluated from an AST
 * with proper deferred evaluation for control flow and iteration.
 */

#include "mux_parse.h"

#include <cstdlib>
#include <cmath>
#include <map>
#include <functional>
#include <algorithm>

// ---------------------------------------------------------------
// Evaluation context
// ---------------------------------------------------------------

// Eval flags — mirror the defines in mux/src/externs.h.
// These control what the evaluator does with each node type.
//
enum {
    EV_EVAL         = 0x0004,   // Evaluate %-substitutions
    EV_STRIP_CURLY  = 0x0008,   // Strip outer {} from args
    EV_FCHECK       = 0x0010,   // Check for function calls on (
    EV_FMAND        = 0x0020,   // Require valid function name
    EV_NOFCHECK     = 0x0040,   // Suppress [ evaluation
    EV_NO_COMPRESS  = 0x0080,   // Don't compress spaces
    EV_STRIP_LS     = 0x1000,   // Strip leading spaces
    EV_STRIP_TS     = 0x2000,   // Strip trailing spaces
    EV_TOP          = 0x0800,   // Top-level evaluation
};

// Default eval flags for top-level evaluation.
static constexpr int EV_DEFAULT = EV_EVAL | EV_FCHECK | EV_TOP;

struct EvalContext {
    // Registers %q0-%q9, %qa-%qz, and named %q<name>
    std::map<std::string, std::string> registers;

    // Command arguments %0-%9
    std::string args[10];
    int nargs = 2;             // number of valid args

    // Iterator state for iter/list
    struct IterFrame {
        std::string itext;   // current item (itext(n))
        int inum;            // current index (inum(n), 1-based)
    };
    std::vector<IterFrame> iterStack;

    // Special substitutions
    std::string enactorName;   // %n
    std::string enactorDbref;  // %#
    std::string executorDbref; // %!
    std::string callerDbref;   // %@
    std::string enactorObjid;  // %:  (dbref:creation_time)
    std::string lastCommand;   // %m
    std::string moniker;       // %k
    std::string pipedOutput;   // %|
    std::string switchToken;   // #$

    // Pronoun substitutions — subjective, possessive, objective, abs-possessive
    std::string pronSub;       // %s  (he/she/they)
    std::string pronPos;       // %p  (his/her/their)
    std::string pronObj;       // %o  (him/her/them)
    std::string pronAbs;       // %a  (his/hers/theirs)

    // Variable attributes %va–%vz (26 slots)
    std::string varAttrs[26];

    // Penn-specific
    std::string rawCommand;    // Penn %c — raw command line
    std::string evaledCommand; // Penn %u — evaluated command line
    std::string accentedName;  // Penn %~ — accented name
    std::string attrName;      // Penn %= — current attribute name

    // Current eval flags
    int evalFlags = EV_DEFAULT;

    // Parser/evaluator compatibility profile.
    ParserProfile profile = PROFILE_MUX214_AST;
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
        return evalWithFlags(node, m_ctx.evalFlags);
    }

    // Evaluate with specific flags (used for recursive contexts).
    //
    std::string evalWithFlags(const ASTNode *node, int flags) {
        if (!node) return "";

        // Save and restore flags for this scope.
        int savedFlags = m_ctx.evalFlags;
        m_ctx.evalFlags = flags;

        std::string result;

        switch (node->type) {
        case NODE_SEQUENCE:
            result = evalSequence(node);
            break;
        case NODE_LITERAL:
        case NODE_SPACE:
            result = node->text;
            break;
        case NODE_SUBST:
            // Only resolve substitutions if EV_EVAL is set.
            result = (flags & EV_EVAL) ? evalSubst(node) : node->text;
            break;
        case NODE_ESCAPE:
            result = (flags & EV_EVAL) ? evalEscape(node) : node->text;
            break;
        case NODE_FUNCCALL:
            // Only dispatch functions if EV_FCHECK is set.
            if (flags & EV_FCHECK) {
                result = evalFuncCall(node);
            } else {
                // No function checking — treat as literal text + parens.
                result = node->text + "(";
                for (size_t i = 0; i < node->children.size(); i++) {
                    if (i > 0) result += ",";
                    result += evalWithFlags(node->children[i].get(), flags);
                }
                result += ")";
            }
            break;
        case NODE_DYNCALL:
            result = "#-1 DYNAMIC CALL NOT SUPPORTED";
            break;
        case NODE_EVALBRACKET:
            // Only recurse into [...] if EV_NOFCHECK is NOT set.
            if (!(flags & EV_NOFCHECK)) {
                result = evalEvalBracket(node);
            } else {
                // Pass through as literal brackets.
                result = "[";
                if (!node->children.empty())
                    result += evalWithFlags(node->children[0].get(), flags);
                result += "]";
            }
            break;
        case NODE_BRACEGROUP:
            result = evalBraceGroup(node);
            break;
        case NODE_SEMICOLON:
            result = "";
            break;
        }

        m_ctx.evalFlags = savedFlags;
        return result;
    }

private:
    EvalContext &m_ctx;

    // Two dispatch tables:
    // - m_funcs: normal functions, receive pre-evaluated string args
    // - m_noeval_funcs: FN_NOEVAL functions, receive unevaluated AST
    //   children and call eval() selectively (deferred evaluation)
    //
    using FuncHandler = std::function<std::string(const std::vector<std::string>&)>;
    std::map<std::string, FuncHandler> m_funcs;

    using NoevalHandler = std::function<std::string(const std::vector<std::unique_ptr<ASTNode>>&)>;
    std::map<std::string, NoevalHandler> m_noeval_funcs;

    static std::string toUpper(const std::string &s) {
        std::string r = s;
        for (auto &c : r) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        return r;
    }

    static long toLong(const std::string &s) {
        if (s.empty()) return 0;
        return strtol(s.c_str(), nullptr, 10);
    }

    static double toDouble(const std::string &s) {
        if (s.empty()) return 0.0;
        return strtod(s.c_str(), nullptr);
    }

    static bool toBool(const std::string &s) {
        if (s.empty()) return false;
        char *end;
        double v = strtod(s.c_str(), &end);
        if (end != s.c_str()) return v != 0.0;
        return true;
    }

    static std::string fmtNum(double v) {
        if (v == floor(v) && fabs(v) < 1e15) {
            return std::to_string(static_cast<long long>(v));
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", v);
        return buf;
    }

    static std::vector<std::string> splitList(const std::string &s,
                                               const std::string &sep = " ")
    {
        std::vector<std::string> result;
        if (s.empty()) return result;

        if (sep == " ") {
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
        int flags = m_ctx.evalFlags;
        bool bFCheckPending = (flags & EV_FCHECK) != 0 && (flags & EV_FMAND) == 0;

        for (size_t i = 0; i < node->children.size(); i++) {
            const auto &child = node->children[i];
            if (child->type == NODE_ESCAPE
                && child->text == "\\\\"
                && i + 1 < node->children.size()
                && node->children[i + 1]->type == NODE_SUBST) {
                result += node->children[i + 1]->text;
                i++;
                if (bFCheckPending) {
                    bFCheckPending = false;
                }
                continue;
            }
            int childFlags = flags;
            if (bFCheckPending) {
                if (child->type != NODE_FUNCCALL) {
                    childFlags &= ~EV_FCHECK;
                }
                if (child->type != NODE_SPACE) {
                    bFCheckPending = false;
                }
            } else if ((flags & EV_FCHECK) != 0 && (flags & EV_FMAND) == 0) {
                childFlags &= ~EV_FCHECK;
            }

            result += evalWithFlags(child.get(), childFlags);
        }
        return result;
    }

    // Apply first-character uppercasing when the substitution letter
    // was uppercase (%N, %S, %P, %O, %A, %K, %M, %Q, %V).
    //
    static std::string maybeCapitalize(const std::string &s, char origLetter) {
        if (s.empty()) return s;
        if (isupper(static_cast<unsigned char>(origLetter))) {
            std::string r = s;
            r[0] = static_cast<char>(toupper(static_cast<unsigned char>(r[0])));
            return r;
        }
        return s;
    }

    std::string evalSubst(const ASTNode *node) {
        const std::string &sub = node->text;

        // Hash forms: ##, #@, #$
        if (sub.size() == 2 && sub[0] == '#') {
            switch (sub[1]) {
            case '#': {
                // ## — same as %i0 (current loop item)
                int idx = static_cast<int>(m_ctx.iterStack.size()) - 1;
                return (idx >= 0) ? m_ctx.iterStack[idx].itext : "";
            }
            case '@': {
                // #@ — same as inum(0) (current loop position)
                int idx = static_cast<int>(m_ctx.iterStack.size()) - 1;
                return (idx >= 0) ? std::to_string(m_ctx.iterStack[idx].inum) : "";
            }
            case '$':
                // #$ — switch matched value
                return m_ctx.switchToken;
            }
        }

        if (sub.size() < 2) return "%";

        char ch = sub[1];
        char upper = static_cast<char>(toupper(static_cast<unsigned char>(ch)));

        // %0–%9: command arguments
        if (ch >= '0' && ch <= '9') {
            int idx = ch - '0';
            return (idx < m_ctx.nargs) ? m_ctx.args[idx] : "";
        }

        // %q/%Q: registers
        if (upper == 'Q') {
            std::string regname;
            if (sub.size() >= 4 && sub[2] == '<') {
                regname = sub.substr(3, sub.size() - 4);
            } else if (sub.size() >= 3) {
                regname = std::string(1, sub[2]);
            }
            auto it = m_ctx.registers.find(regname);
            std::string val = (it != m_ctx.registers.end()) ? it->second : "";
            return maybeCapitalize(val, ch);
        }

        // %v/%V: variable attributes A–Z
        if (upper == 'V' && sub.size() >= 3) {
            char attrCh = static_cast<char>(toupper(static_cast<unsigned char>(sub[2])));
            if (attrCh >= 'A' && attrCh <= 'Z') {
                std::string val = m_ctx.varAttrs[attrCh - 'A'];
                return maybeCapitalize(val, ch);
            }
            return "";
        }

        // %w/%W: Penn W-attributes (profile-dependent)
        if (upper == 'W' && sub.size() >= 3) {
            if (m_ctx.profile == PROFILE_PENN) {
                return "";
            }
            // MUX: unknown → literal. In MUX profiles the tokenizer
            // should not have consumed the trailing attribute letter.
            return std::string(1, ch);
        }

        // %c/%C, %x/%X: color codes (MUX) or command/X-attr (Penn)
        if (upper == 'C' || upper == 'X') {
            if (m_ctx.profile == PROFILE_PENN) {
                if (upper == 'C') {
                    // Penn: %c = raw command line
                    return m_ctx.rawCommand;
                }
                // Penn: %x = X-attribute (if sub.size() >= 3)
                if (sub.size() >= 3) {
                    return "thing";
                }
                return "";
            }
            // MUX: color codes — simplified for study tool
            if (sub.size() >= 3) {
                return "[color:" + sub.substr(1) + "]";
            }
            return "";
        }

        // %i/%I: loop itext at depth
        if (upper == 'I' && sub.size() == 2 && m_ctx.profile == PROFILE_MUX214_AST) {
            return "";
        }
        if (upper == 'I' && sub.size() >= 3) {
            char depthCh = sub[2];
            if (depthCh >= '0' && depthCh <= '9') {
                int depth = depthCh - '0';
                int idx = static_cast<int>(m_ctx.iterStack.size()) - 1 - depth;
                if (idx >= 0 && idx < static_cast<int>(m_ctx.iterStack.size())) {
                    return m_ctx.iterStack[idx].itext;
                }
                return "";
            }
            if (toupper(static_cast<unsigned char>(depthCh)) == 'L'
                && m_ctx.profile == PROFILE_PENN) {
                if (m_ctx.iterStack.empty()) {
                    return "#-1 ARGUMENT OUT OF RANGE";
                }
                return m_ctx.iterStack.back().itext;
            }
            return "";
        }

        // %=: attribute name (Penn) or extended arg/attr lookup (MUX)
        if (ch == '=') {
            if (sub.size() == 2) {
                // Bare %=
                if (m_ctx.profile == PROFILE_PENN) {
                    return m_ctx.attrName;
                }
                return "=";
            }
            // %=<...> — extended form (MUX only)
            if (sub.size() >= 4 && sub[2] == '<') {
                std::string inner = sub.substr(3, sub.size() - 4);
                // Check if numeric → extended arg
                bool isNum = !inner.empty();
                for (char c : inner) {
                    if (!isdigit(static_cast<unsigned char>(c))) {
                        isNum = false;
                        break;
                    }
                }
                if (isNum) {
                    int idx = atoi(inner.c_str());
                    return (idx < m_ctx.nargs) ? m_ctx.args[idx] : "";
                }
                return "[attr:" + inner + "]";
            }
            return "=";
        }

        // %$: Penn stack variables
        if (ch == '$' && m_ctx.profile == PROFILE_PENN) {
            return "[stack]";
        }

        // Simple single-character forms
        switch (upper) {
        case 'R':
            // MUX: \r\n.  Penn: \n only.
            return (m_ctx.profile == PROFILE_PENN) ? "\n" : "\r\n";
        case 'B': return " ";
        case 'T': return "\t";
        case 'N': return maybeCapitalize(m_ctx.enactorName, ch);
        case 'S': return maybeCapitalize(m_ctx.pronSub, ch);
        case 'P': return maybeCapitalize(m_ctx.pronPos, ch);
        case 'O': return maybeCapitalize(m_ctx.pronObj, ch);
        case 'A': return maybeCapitalize(m_ctx.pronAbs, ch);
        case 'K': return maybeCapitalize(m_ctx.moniker, ch);
        case 'M': return maybeCapitalize(m_ctx.lastCommand, ch);
        case 'L': return "[location]";
        }

        switch (ch) {
        case '%': return "%";
        case '#': return m_ctx.enactorDbref;
        case '!': return m_ctx.executorDbref;
        case '@': return m_ctx.callerDbref;
        case ':': return m_ctx.enactorObjid;
        case '+': return std::to_string(m_ctx.nargs);
        case '|': return m_ctx.pipedOutput;
        }

        // Penn-specific single-char forms
        if (m_ctx.profile == PROFILE_PENN) {
            if (ch == ' ') return "% ";
            if (ch == '~') return m_ctx.accentedName;
            if (ch == '?') return "[limits]";
            if (upper == 'U') return m_ctx.evaledCommand;
        }

        // Unknown substitution — output the character literally
        // (matches iCode == 0 fallback in all engines).
        return std::string(1, ch);
    }

    std::string evalEscape(const ASTNode *node) {
        if (node->text.size() >= 2) {
            if (node->text == "\\%" && m_ctx.profile != PROFILE_PENN) {
                return "";
            }
            return node->text.substr(1);
        }
        return "\\";
    }

    // Replicate 2.13's noeval pass on an AST subtree.
    //
    // In 2.13, mux_exec with EV_EVAL off still processes backslash
    // escapes (the handler at line 2439 has no EV_EVAL guard).
    // Percent substitutions are copied literally (guarded by EV_EVAL).
    //
    // This produces a string with one layer of backslash stripping,
    // which can then be re-tokenized and evaluated.
    //
    std::string noevalPass(const ASTNode *node) {
        if (!node) return "";

        switch (node->type) {
        case NODE_LITERAL:
        case NODE_SPACE:
        case NODE_SEMICOLON:
            return node->text;

        case NODE_SUBST:
            // Percent handler IS guarded by EV_EVAL in 2.13.
            // With EV_EVAL off, it copies % and following char literally.
            return node->text;

        case NODE_ESCAPE:
            // Backslash handler is NOT guarded by EV_EVAL in 2.13.
            // It unconditionally consumes the \ and emits the next char.
            if (node->text.size() >= 2) {
                return node->text.substr(1);
            }
            return "\\";

        case NODE_FUNCCALL:
            // In noeval, function calls aren't dispatched.
            // Copy the name and parens literally.
            {
                std::string r = node->text + "(";
                for (size_t i = 0; i < node->children.size(); i++) {
                    if (i > 0) r += ",";
                    r += noevalPass(node->children[i].get());
                }
                return r + ")";
            }

        case NODE_EVALBRACKET:
            // In noeval with EV_NOFCHECK, [...] is not evaluated.
            // Copy brackets and contents literally.
            {
                std::string r = "[";
                for (const auto &c : node->children)
                    r += noevalPass(c.get());
                return r + "]";
            }

        case NODE_BRACEGROUP:
            // Nested brace groups: copy braces and recurse.
            {
                std::string r = "{";
                for (const auto &c : node->children)
                    r += noevalPass(c.get());
                return r + "}";
            }

        case NODE_SEQUENCE:
            {
                std::string r;
                for (size_t i = 0; i < node->children.size(); i++) {
                    const auto &c = node->children[i];
                    if (c->type == NODE_ESCAPE
                        && c->text == "\\\\"
                        && i + 1 < node->children.size()
                        && node->children[i + 1]->type == NODE_SUBST) {
                        r += node->children[i + 1]->text;
                        i++;
                        continue;
                    }
                    r += noevalPass(c.get());
                }
                return r;
            }

        case NODE_DYNCALL:
            return "#-1 DYNAMIC";
        }
        return "";
    }

    // Evaluate a selected argument from a FN_NOEVAL function using the
    // 2.13-style noeval pass followed by reparse/re-eval.
    //
    // This replicates the two-pass behavior observed in 2.13:
    //   Pass 1: noeval — strip one layer of backslash (noevalPass)
    //   Pass 2: eval — re-tokenize the result and evaluate it
    //
    std::string evalNoevalLegacyArg(const ASTNode *node) {
        if (!node) return "";

        const ASTNode *inner = node;
        if (node->type == NODE_BRACEGROUP && !node->children.empty()) {
            inner = node->children[0].get();
        }

        // Pass 1: produce text with one backslash layer stripped.
        std::string text = noevalPass(inner);

        // Pass 2: re-tokenize and evaluate.
        auto tokens = tokenize(text.c_str(), m_ctx.profile);
        Parser parser(tokens);
        auto ast = parser.parse();
        int flags = (m_ctx.evalFlags & ~(EV_TOP | EV_FMAND))
                  | EV_EVAL | EV_FCHECK;
        return evalWithFlags(ast.get(), flags);
    }

    // Evaluate an argument to a FN_NOEVAL function.
    //
    std::string evalNoevalArg(const ASTNode *node) {
        if (!node) return "";
        if (m_ctx.profile == PROFILE_MUX213_COMPAT) {
            return evalNoevalLegacyArg(node);
        }
        if (node->type == NODE_BRACEGROUP) {
            // Baseline AST/Penn study behavior: brace groups selected by
            // FN_NOEVAL functions are evaluated with brace stripping.
            int flags = (m_ctx.evalFlags & ~(EV_TOP | EV_FMAND))
                      | EV_EVAL | EV_FCHECK | EV_STRIP_CURLY;
            return evalWithFlags(node, flags);
        }
        return eval(node);
    }

    std::string evalFuncCall(const ASTNode *node) {
        std::string fname = toUpper(node->text);

        // Check FN_NOEVAL first — deferred evaluation.
        auto nit = m_noeval_funcs.find(fname);
        if (nit != m_noeval_funcs.end()) {
            return nit->second(node->children);
        }

        // Normal — evaluate all arguments first.
        auto it = m_funcs.find(fname);
        if (it == m_funcs.end()) {
            return "#-1 FUNCTION (" + fname + ") NOT FOUND";
        }

        std::vector<std::string> args;
        int flags = (m_ctx.evalFlags & ~(EV_TOP | EV_FMAND)) | EV_EVAL | EV_FCHECK;
        for (const auto &child : node->children) {
            args.push_back(evalWithFlags(child.get(), flags));
        }
        return it->second(args);
    }

    std::string evalEvalBracket(const ASTNode *node) {
        if (node->children.empty()) return "";
        // Inside [...], functions are checked and mandatory.
        // This matches mux_exec: eval | EV_FCHECK | EV_FMAND | EV_EVAL
        int flags = (m_ctx.evalFlags & ~EV_TOP) | EV_EVAL | EV_FCHECK | EV_FMAND;
        return evalWithFlags(node->children[0].get(), flags);
    }

    std::string evalBraceGroup(const ASTNode *node) {
        if (node->children.empty()) return "";
        int flags = m_ctx.evalFlags;

        if (flags & EV_STRIP_CURLY) {
            // Strip braces, evaluate contents without function checking.
            // This matches mux_exec behavior: inside {} with EV_EVAL,
            // the flags become eval & ~(EV_STRIP_CURLY|EV_FCHECK|EV_FMAND).
            int innerFlags = (flags & ~(EV_STRIP_CURLY | EV_FCHECK | EV_FMAND));
            return evalWithFlags(node->children[0].get(), innerFlags);
        } else {
            // No strip — return the raw text including braces.
            return "{" + ast_raw_text(node->children[0].get()) + "}";
        }
    }

    // ---------------------------------------------------------------
    // Builtin function registration
    // ---------------------------------------------------------------

    void registerBuiltins() {
        // -- Arithmetic --
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
            return std::to_string((args.empty() ? 0 : toLong(args[0])) + 1);
        };
        m_funcs["DEC"] = [](const std::vector<std::string> &args) -> std::string {
            return std::to_string((args.empty() ? 0 : toLong(args[0])) - 1);
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

        // -- Comparison --
        m_funcs["EQ"] = [](const std::vector<std::string> &a) { return a.size() < 2 ? "0" : (toLong(a[0]) == toLong(a[1]) ? "1" : "0"); };
        m_funcs["NEQ"] = [](const std::vector<std::string> &a) { return a.size() < 2 ? "0" : (toLong(a[0]) != toLong(a[1]) ? "1" : "0"); };
        m_funcs["GT"] = [](const std::vector<std::string> &a) { return a.size() < 2 ? "0" : (toLong(a[0]) > toLong(a[1]) ? "1" : "0"); };
        m_funcs["GTE"] = [](const std::vector<std::string> &a) { return a.size() < 2 ? "0" : (toLong(a[0]) >= toLong(a[1]) ? "1" : "0"); };
        m_funcs["LT"] = [](const std::vector<std::string> &a) { return a.size() < 2 ? "0" : (toLong(a[0]) < toLong(a[1]) ? "1" : "0"); };
        m_funcs["LTE"] = [](const std::vector<std::string> &a) { return a.size() < 2 ? "0" : (toLong(a[0]) <= toLong(a[1]) ? "1" : "0"); };
        m_funcs["COMP"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.size() < 2) return "0";
            int r = a[0].compare(a[1]);
            return std::to_string(r < 0 ? -1 : (r > 0 ? 1 : 0));
        };

        // -- Boolean --
        m_funcs["AND"] = [](const std::vector<std::string> &a) -> std::string {
            for (const auto &x : a) if (!toBool(x)) return "0";
            return "1";
        };
        m_funcs["OR"] = [](const std::vector<std::string> &a) -> std::string {
            for (const auto &x : a) if (toBool(x)) return "1";
            return "0";
        };
        m_funcs["NOT"] = [](const std::vector<std::string> &a) { return (a.empty() || !toBool(a[0])) ? "1" : "0"; };
        m_funcs["XOR"] = [](const std::vector<std::string> &a) { return a.size() < 2 ? "0" : ((toBool(a[0]) != toBool(a[1])) ? "1" : "0"); };
        m_funcs["T"] = [](const std::vector<std::string> &a) { return (a.empty() || !toBool(a[0])) ? "0" : "1"; };

        // -- String --
        m_funcs["STRLEN"] = [](const std::vector<std::string> &a) { return a.empty() ? "0" : std::to_string(a[0].size()); };
        m_funcs["MID"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.size() < 3) return "";
            long pos = toLong(a[1]), len = toLong(a[2]);
            if (pos < 0 || len < 0 || pos >= static_cast<long>(a[0].size())) return "";
            return a[0].substr(pos, len);
        };
        m_funcs["LEFT"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.size() < 2) return "";
            long len = toLong(a[1]);
            return len <= 0 ? "" : a[0].substr(0, len);
        };
        m_funcs["RIGHT"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.size() < 2) return "";
            long len = toLong(a[1]);
            if (len <= 0) return "";
            if (len >= static_cast<long>(a[0].size())) return a[0];
            return a[0].substr(a[0].size() - len);
        };
        m_funcs["CAPSTR"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.empty() || a[0].empty()) return "";
            std::string s = a[0];
            s[0] = static_cast<char>(toupper(static_cast<unsigned char>(s[0])));
            return s;
        };
        m_funcs["LCSTR"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.empty()) return "";
            std::string s = a[0];
            for (auto &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            return s;
        };
        m_funcs["UCSTR"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.empty()) return "";
            std::string s = a[0];
            for (auto &c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            return s;
        };
        m_funcs["CAT"] = [](const std::vector<std::string> &a) -> std::string {
            std::string r;
            for (size_t i = 0; i < a.size(); i++) { if (i > 0) r += " "; r += a[i]; }
            return r;
        };
        m_funcs["STRCAT"] = [](const std::vector<std::string> &a) -> std::string {
            std::string r;
            for (const auto &x : a) r += x;
            return r;
        };
        m_funcs["REPEAT"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.size() < 2) return "";
            long n = toLong(a[1]);
            if (n <= 0) return "";
            std::string r;
            for (long i = 0; i < n; i++) r += a[0];
            return r;
        };
        m_funcs["SPACE"] = [](const std::vector<std::string> &a) -> std::string {
            long n = a.empty() ? 1 : toLong(a[0]);
            return n <= 0 ? "" : std::string(n, ' ');
        };
        m_funcs["TRIM"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.empty()) return "";
            size_t start = a[0].find_first_not_of(' ');
            if (start == std::string::npos) return "";
            size_t end = a[0].find_last_not_of(' ');
            return a[0].substr(start, end - start + 1);
        };

        // -- List --
        m_funcs["WORDS"] = [](const std::vector<std::string> &a) {
            return a.empty() ? "0" : std::to_string(splitList(a[0]).size());
        };
        m_funcs["FIRST"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.empty()) return "";
            auto w = splitList(a[0]);
            return w.empty() ? "" : w[0];
        };
        m_funcs["REST"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.empty()) return "";
            auto w = splitList(a[0]);
            if (w.size() <= 1) return "";
            std::string r;
            for (size_t i = 1; i < w.size(); i++) { if (i > 1) r += " "; r += w[i]; }
            return r;
        };
        m_funcs["LAST"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.empty()) return "";
            auto w = splitList(a[0]);
            return w.empty() ? "" : w.back();
        };
        m_funcs["EXTRACT"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.size() < 3) return "";
            auto w = splitList(a[0]);
            long first = toLong(a[1]) - 1, count = toLong(a[2]);
            if (first < 0) first = 0;
            std::string r;
            for (long i = first; i < first + count && i < static_cast<long>(w.size()); i++) {
                if (i > first) r += " ";
                r += w[i];
            }
            return r;
        };
        m_funcs["LNUM"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.empty()) return "";
            long n = toLong(a[0]);
            std::string sep = a.size() > 1 ? a[1] : " ";
            std::string r;
            for (long i = 0; i < n; i++) { if (i > 0) r += sep; r += std::to_string(i); }
            return r;
        };
        m_funcs["SORT"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.empty()) return "";
            auto w = splitList(a[0]);
            bool allNum = true;
            for (const auto &x : w) {
                char *end;
                strtod(x.c_str(), &end);
                if (end == x.c_str() || *end != '\0') { allNum = false; break; }
            }
            if (allNum) {
                std::sort(w.begin(), w.end(), [](const std::string &x, const std::string &y) {
                    return strtod(x.c_str(), nullptr) < strtod(y.c_str(), nullptr);
                });
            } else {
                std::sort(w.begin(), w.end());
            }
            std::string r;
            for (size_t i = 0; i < w.size(); i++) { if (i > 0) r += " "; r += w[i]; }
            return r;
        };
        m_funcs["MEMBER"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.size() < 2) return "0";
            auto w = splitList(a[0]);
            for (size_t i = 0; i < w.size(); i++) if (w[i] == a[1]) return std::to_string(i + 1);
            return "0";
        };
        m_funcs["INDEX"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.size() < 4) return "";
            auto items = splitList(a[0], a[1]);
            long first = toLong(a[2]) - 1, count = toLong(a[3]);
            if (first < 0) first = 0;
            std::string r;
            for (long i = first; i < first + count && i < static_cast<long>(items.size()); i++) {
                if (i > first) r += a[1];
                r += items[i];
            }
            return r;
        };

        // -- Set operations --
        m_funcs["SETUNION"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.size() < 2) return "";
            auto x = splitList(a[0]), y = splitList(a[1]);
            std::vector<std::string> result;
            std::sort(x.begin(), x.end()); std::sort(y.begin(), y.end());
            std::set_union(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(result));
            std::string r;
            for (size_t i = 0; i < result.size(); i++) { if (i > 0) r += " "; r += result[i]; }
            return r;
        };
        m_funcs["SETDIFF"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.size() < 2) return "";
            auto x = splitList(a[0]), y = splitList(a[1]);
            std::vector<std::string> result;
            std::sort(x.begin(), x.end()); std::sort(y.begin(), y.end());
            std::set_difference(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(result));
            std::string r;
            for (size_t i = 0; i < result.size(); i++) { if (i > 0) r += " "; r += result[i]; }
            return r;
        };
        m_funcs["SETINTER"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.size() < 2) return "";
            auto x = splitList(a[0]), y = splitList(a[1]);
            std::vector<std::string> result;
            std::sort(x.begin(), x.end()); std::sort(y.begin(), y.end());
            std::set_intersection(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(result));
            std::string r;
            for (size_t i = 0; i < result.size(); i++) { if (i > 0) r += " "; r += result[i]; }
            return r;
        };

        // -- Registers --
        m_funcs["SETQ"] = [this](const std::vector<std::string> &a) -> std::string {
            if (a.size() < 2) return "";
            m_ctx.registers[a[0]] = a[1];
            return "";
        };
        m_funcs["SETR"] = [this](const std::vector<std::string> &a) -> std::string {
            if (a.size() < 2) return "";
            m_ctx.registers[a[0]] = a[1];
            return a[1];
        };
        m_funcs["R"] = [this](const std::vector<std::string> &a) -> std::string {
            if (a.empty()) return "";
            auto it = m_ctx.registers.find(a[0]);
            return (it != m_ctx.registers.end()) ? it->second : "";
        };

        // -- Misc (normal eval) --
        m_funcs["NULL"] = [](const std::vector<std::string> &) -> std::string { return ""; };
        m_funcs["MATCH"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.size() < 2) return "0";
            auto w = splitList(a[0]);
            for (size_t i = 0; i < w.size(); i++) if (w[i] == a[1]) return std::to_string(i + 1);
            return "0";
        };
        m_funcs["STRMATCH"] = [](const std::vector<std::string> &a) -> std::string {
            if (a.size() < 2) return "0";
            if (a[1] == "*") return "1";
            return (a[0] == a[1]) ? "1" : "0";
        };
        m_funcs["ITEXT"] = [this](const std::vector<std::string> &a) -> std::string {
            int depth = a.empty() ? 0 : static_cast<int>(toLong(a[0]));
            int idx = static_cast<int>(m_ctx.iterStack.size()) - 1 - depth;
            if (idx >= 0 && idx < static_cast<int>(m_ctx.iterStack.size()))
                return m_ctx.iterStack[idx].itext;
            return "";
        };
        m_funcs["INUM"] = [this](const std::vector<std::string> &a) -> std::string {
            int depth = a.empty() ? 0 : static_cast<int>(toLong(a[0]));
            int idx = static_cast<int>(m_ctx.iterStack.size()) - 1 - depth;
            if (idx >= 0 && idx < static_cast<int>(m_ctx.iterStack.size()))
                return std::to_string(m_ctx.iterStack[idx].inum);
            return "";
        };

        // ---------------------------------------------------------------
        // FN_NOEVAL functions — deferred evaluation
        // ---------------------------------------------------------------

        m_noeval_funcs["IFELSE"] = [this](const std::vector<std::unique_ptr<ASTNode>> &c) -> std::string {
            if (c.size() < 3) return "";
            return toBool(eval(c[0].get())) ? evalNoevalArg(c[1].get()) : evalNoevalArg(c[2].get());
        };

        // switch(val, pat1, result1, ..., default)
        m_noeval_funcs["SWITCH"] = [this](const std::vector<std::unique_ptr<ASTNode>> &c) -> std::string {
            if (c.size() < 2) return "";
            std::string val = eval(c[0].get());
            for (size_t i = 1; i + 1 < c.size(); i += 2) {
                std::string pat = eval(c[i].get());
                if (pat == val || pat == "*") return evalNoevalArg(c[i + 1].get());
            }
            if (c.size() % 2 == 0) return evalNoevalArg(c.back().get());
            return "";
        };
        m_noeval_funcs["CASE"] = [this](const std::vector<std::unique_ptr<ASTNode>> &c) -> std::string {
            if (c.size() < 2) return "";
            std::string val = eval(c[0].get());
            for (size_t i = 1; i + 1 < c.size(); i += 2) {
                if (eval(c[i].get()) == val) return evalNoevalArg(c[i + 1].get());
            }
            if (c.size() % 2 == 0) return evalNoevalArg(c.back().get());
            return "";
        };

        // Short-circuit boolean
        m_noeval_funcs["CAND"] = [this](const std::vector<std::unique_ptr<ASTNode>> &c) -> std::string {
            for (const auto &x : c) if (!toBool(eval(x.get()))) return "0";
            return "1";
        };
        m_noeval_funcs["COR"] = [this](const std::vector<std::unique_ptr<ASTNode>> &c) -> std::string {
            for (const auto &x : c) if (toBool(eval(x.get()))) return "1";
            return "0";
        };

        // @@(comment) — discard without evaluating
        m_noeval_funcs["@@"] = [](const std::vector<std::unique_ptr<ASTNode>> &) -> std::string {
            return "";
        };

        // lit(text) — return unevaluated source text
        m_noeval_funcs["LIT"] = [](const std::vector<std::unique_ptr<ASTNode>> &c) -> std::string {
            return c.empty() ? "" : ast_raw_text(c[0].get());
        };

        // iter(list, body, osep, isep) — evaluate body per item
        m_noeval_funcs["ITER"] = [this](const std::vector<std::unique_ptr<ASTNode>> &c) -> std::string {
            if (c.size() < 2) return "";
            std::string listVal = eval(c[0].get());
            std::string sep = c.size() > 3 ? eval(c[3].get()) : " ";
            std::string osep = c.size() > 2 ? eval(c[2].get()) : " ";
            auto items = splitList(listVal, sep);
            std::string result;
            for (size_t i = 0; i < items.size(); i++) {
                if (i > 0) result += osep;
                m_ctx.iterStack.push_back({items[i], static_cast<int>(i + 1)});
                result += evalNoevalArg(c[1].get());
                m_ctx.iterStack.pop_back();
            }
            return result;
        };

        m_noeval_funcs["IF"] = [this](const std::vector<std::unique_ptr<ASTNode>> &c) -> std::string {
            if (c.empty()) return "";
            std::string cond_str = evalWithFlags(c[0].get(), (m_ctx.evalFlags & ~EV_TOP) | EV_EVAL | EV_FCHECK);
            bool cond = !cond_str.empty() && cond_str != "0";
            if (cond) {
                return (c.size() > 1) ? evalNoevalArg(c[1].get()) : "";
            } else {
                return (c.size() > 2) ? evalNoevalArg(c[2].get()) : "";
            }
        };

        m_noeval_funcs["EVAL"] = [this](const std::vector<std::unique_ptr<ASTNode>> &c) -> std::string {
            if (c.empty()) return "";
            std::string text = evalWithFlags(c[0].get(), (m_ctx.evalFlags & ~EV_TOP) | EV_EVAL | EV_FCHECK);
            auto tokens = tokenize(text.c_str(), m_ctx.profile);
            Parser parser(tokens);
            auto ast = parser.parse();
            return evalWithFlags(ast.get(), (m_ctx.evalFlags & ~EV_TOP) | EV_EVAL | EV_FCHECK);
        };
    }
};

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------

int main(int argc, char *argv[])
{
    bool showAST = false;
    ParserProfile profile = PROFILE_MUX214_AST;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--ast") == 0) {
            showAST = true;
        } else if (0 == strcmp(argv[i], "--profile")) {
            if (i + 1 >= argc || !parse_profile_string(argv[i + 1], profile)) {
                fprintf(stderr, "usage: %s [--ast] [--profile mux214|mux213|penn]\n", argv[0]);
                return 2;
            }
            i++;
        } else if (0 == strncmp(argv[i], "--profile=", 10)) {
            if (!parse_profile_string(argv[i] + 10, profile)) {
                fprintf(stderr, "usage: %s [--ast] [--profile mux214|mux213|penn]\n", argv[0]);
                return 2;
            }
        } else {
            fprintf(stderr, "usage: %s [--ast] [--profile mux214|mux213|penn]\n", argv[0]);
            return 2;
        }
    }

    EvalContext ctx;
    ctx.profile = profile;
    ctx.enactorName = "testplayer";
    ctx.enactorDbref = "#1234";
    ctx.executorDbref = "#1234";
    ctx.callerDbref = "#1234";
    ctx.enactorObjid = "#1234:1700000000";
    ctx.lastCommand = "test";
    ctx.moniker = "TestPlayer";
    ctx.pronSub = "they";
    ctx.pronPos = "their";
    ctx.pronObj = "them";
    ctx.pronAbs = "theirs";
    ctx.rawCommand = "@pemit me=%cg";
    ctx.evaledCommand = "@pemit me=%cg";
    ctx.accentedName = "testplayer";
    ctx.attrName = "";
    ctx.varAttrs['G' - 'A'] = "thing";
    ctx.args[0] = "hello";
    ctx.args[1] = "world";
    ctx.nargs = 2;

    Evaluator evaluator(ctx);

    char line[8192];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        auto tokens = tokenize(line, profile);
        Parser parser(tokens);
        auto ast = parser.parse();

        if (showAST) {
            printf("INPUT: %s\n", line);
            printf("PROFILE: %s\n", profile_name(profile));
            printf("AST:\n");
            ast_print(ast.get(), 2);
        }

        std::string result = evaluator.eval(ast.get());
        printf("%s\n", result.c_str());
    }
    return 0;
}
