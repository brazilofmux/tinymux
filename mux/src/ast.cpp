/*! \file ast.cpp
 * \brief AST-based expression parser and evaluator.
 *
 * See ast.h for design constraints and public API.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "ast.h"
#include "functions.h"

#include <cctype>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------

// Gather a %-substitution sequence following the L2 dispatch table
// in eval.cpp. On entry, p points to the character AFTER '%'.
//
static std::string gather_pct(const UTF8 *&p, const UTF8 *pEnd)
{
    std::string sub("%");

    if (p >= pEnd)
    {
        return sub;
    }

    UTF8 ch = *p;
    UTF8 upper = static_cast<UTF8>(toupper(ch));

    if (ch >= '0' && ch <= '9')
    {
        sub += static_cast<char>(*p++);
    }
    else if (upper == 'Q')
    {
        sub += static_cast<char>(*p++);
        if (p < pEnd && *p == '<')
        {
            sub += static_cast<char>(*p++);
            while (p < pEnd && *p != '>')
            {
                sub += static_cast<char>(*p++);
            }
            if (p < pEnd && *p == '>')
            {
                sub += static_cast<char>(*p++);
            }
        }
        else if (p < pEnd)
        {
            sub += static_cast<char>(*p++);
        }
    }
    else if (upper == 'V')
    {
        sub += static_cast<char>(*p++);
        if (p < pEnd && isalpha(*p))
        {
            sub += static_cast<char>(*p++);
        }
    }
    else if (upper == 'C' || upper == 'X')
    {
        sub += static_cast<char>(*p++);
        if (p < pEnd && *p == '<')
        {
            sub += static_cast<char>(*p++);
            while (p < pEnd && *p != '>')
            {
                sub += static_cast<char>(*p++);
            }
            if (p < pEnd && *p == '>')
            {
                sub += static_cast<char>(*p++);
            }
        }
        else if (p < pEnd)
        {
            sub += static_cast<char>(*p++);
        }
    }
    else if (ch == '=')
    {
        sub += static_cast<char>(*p++);
        if (p < pEnd && *p == '<')
        {
            sub += static_cast<char>(*p++);
            while (p < pEnd && *p != '>')
            {
                sub += static_cast<char>(*p++);
            }
            if (p < pEnd && *p == '>')
            {
                sub += static_cast<char>(*p++);
            }
        }
    }
    else if (upper == 'I')
    {
        sub += static_cast<char>(*p++);
        if (p < pEnd && *p >= '0' && *p <= '9')
        {
            sub += static_cast<char>(*p++);
        }
    }
    else
    {
        sub += static_cast<char>(*p++);
    }

    return sub;
}

std::vector<ASTToken> ast_tokenize(const UTF8 *input, size_t nLen)
{
    std::vector<ASTToken> tokens;
    const UTF8 *p = input;
    const UTF8 *pEnd = input + nLen;

    while (p < pEnd && *p)
    {
        if (*p == '[')
        {
            tokens.push_back({ASTTOK_LBRACK, "["});
            p++;
        }
        else if (*p == ']')
        {
            tokens.push_back({ASTTOK_RBRACK, "]"});
            p++;
        }
        else if (*p == '{')
        {
            tokens.push_back({ASTTOK_LBRACE, "{"});
            p++;
        }
        else if (*p == '}')
        {
            tokens.push_back({ASTTOK_RBRACE, "}"});
            p++;
        }
        else if (*p == '(')
        {
            if (  !tokens.empty()
               && tokens.back().type == ASTTOK_LIT)
            {
                tokens.back().type = ASTTOK_FUNC;
            }
            tokens.push_back({ASTTOK_LPAREN, "("});
            p++;
        }
        else if (*p == ')')
        {
            tokens.push_back({ASTTOK_RPAREN, ")"});
            p++;
        }
        else if (*p == ',')
        {
            tokens.push_back({ASTTOK_COMMA, ","});
            p++;
        }
        else if (*p == ';')
        {
            tokens.push_back({ASTTOK_SEMI, ";"});
            p++;
        }
        else if (*p == '%')
        {
            p++;
            tokens.push_back({ASTTOK_PCT, gather_pct(p, pEnd)});
        }
        else if (*p == '\\')
        {
            std::string esc;
            esc += static_cast<char>(*p++);
            if (p < pEnd && *p)
            {
                esc += static_cast<char>(*p++);
            }
            tokens.push_back({ASTTOK_ESC, esc});
        }
        else if (*p == ' ' || *p == '\t')
        {
            std::string sp;
            while (p < pEnd && (*p == ' ' || *p == '\t'))
            {
                sp += static_cast<char>(*p++);
            }
            tokens.push_back({ASTTOK_SPACE, sp});
        }
        else
        {
            std::string lit;
            while (  p < pEnd
                  && *p
                  && *p != '['
                  && *p != ']'
                  && *p != '{'
                  && *p != '}'
                  && *p != '('
                  && *p != ')'
                  && *p != ','
                  && *p != ';'
                  && *p != '%'
                  && *p != '\\'
                  && *p != ' '
                  && *p != '\t')
            {
                lit += static_cast<char>(*p++);
            }
            tokens.push_back({ASTTOK_LIT, lit});
        }
    }

    tokens.push_back({ASTTOK_EOF, ""});
    return tokens;
}

// ---------------------------------------------------------------
// Parser
// ---------------------------------------------------------------

class ASTParser {
public:
    ASTParser(const std::vector<ASTToken> &tokens)
        : m_tokens(tokens), m_pos(0) {}

    std::unique_ptr<ASTNode> parse()
    {
        return parseSequence(false, false, false, false);
    }

private:
    const std::vector<ASTToken> &m_tokens;
    size_t m_pos;

    const ASTToken &peek() const { return m_tokens[m_pos]; }
    ASTToken advance() { return m_tokens[m_pos++]; }

    bool atEnd() const
    {
        return m_pos >= m_tokens.size()
            || m_tokens[m_pos].type == ASTTOK_EOF;
    }

    std::unique_ptr<ASTNode> parseSequence(
        bool stopRP, bool stopRB, bool stopRC, bool stopCM)
    {
        auto seq = std::make_unique<ASTNode>(AST_SEQUENCE);
        while (!atEnd())
        {
            ASTTokenType t = peek().type;
            if (stopRP && t == ASTTOK_RPAREN) break;
            if (stopRB && t == ASTTOK_RBRACK) break;
            if (stopRC && t == ASTTOK_RBRACE) break;
            if (stopCM && t == ASTTOK_COMMA)  break;

            auto node = parseOne();
            if (node)
            {
                seq->addChild(std::move(node));
            }
        }
        if (seq->children.size() == 1)
        {
            return std::move(seq->children[0]);
        }
        return seq;
    }

    std::unique_ptr<ASTNode> parseOne()
    {
        const ASTToken &tok = peek();
        switch (tok.type)
        {
        case ASTTOK_LIT:
            {
                auto n = std::make_unique<ASTNode>(AST_LITERAL, tok.text);
                advance();
                return n;
            }

        case ASTTOK_SPACE:
            {
                auto n = std::make_unique<ASTNode>(AST_SPACE, tok.text);
                advance();
                return n;
            }

        case ASTTOK_PCT:
            {
                auto n = std::make_unique<ASTNode>(AST_SUBST, tok.text);
                advance();
                // No DynCall support — if ( follows a substitution,
                // the ( is just a literal parenthesis.
                return n;
            }

        case ASTTOK_ESC:
            {
                auto n = std::make_unique<ASTNode>(AST_ESCAPE, tok.text);
                advance();
                return n;
            }

        case ASTTOK_SEMI:
            {
                auto n = std::make_unique<ASTNode>(AST_SEMICOLON, tok.text);
                advance();
                return n;
            }

        case ASTTOK_FUNC:
            return parseFuncCall();

        case ASTTOK_LBRACK:
            return parseEvalBracket();

        case ASTTOK_LBRACE:
            return parseBraceGroup();

        case ASTTOK_RPAREN:
        case ASTTOK_RBRACK:
        case ASTTOK_RBRACE:
        case ASTTOK_COMMA:
        case ASTTOK_LPAREN:
            {
                auto n = std::make_unique<ASTNode>(AST_LITERAL, tok.text);
                advance();
                return n;
            }

        case ASTTOK_EOF:
            return nullptr;
        }
        return nullptr;
    }

    std::unique_ptr<ASTNode> parseFuncCall()
    {
        ASTToken funcTok = advance();
        auto call = std::make_unique<ASTNode>(AST_FUNCCALL, funcTok.text);

        if (atEnd() || peek().type != ASTTOK_LPAREN)
        {
            call->type = AST_LITERAL;
            return call;
        }
        advance(); // consume LPAREN

        parseArgList(call.get());
        return call;
    }

    void parseArgList(ASTNode *call)
    {
        auto arg = parseSequence(true, false, false, true);
        call->addChild(std::move(arg));

        while (!atEnd() && peek().type == ASTTOK_COMMA)
        {
            advance();
            arg = parseSequence(true, false, false, true);
            call->addChild(std::move(arg));
        }

        if (!atEnd() && peek().type == ASTTOK_RPAREN)
        {
            advance();
        }
    }

    std::unique_ptr<ASTNode> parseEvalBracket()
    {
        advance(); // consume LBRACK

        auto bracket = std::make_unique<ASTNode>(AST_EVALBRACKET);
        auto contents = parseSequence(false, true, false, false);
        bracket->addChild(std::move(contents));

        if (!atEnd() && peek().type == ASTTOK_RBRACK)
        {
            advance();
        }
        return bracket;
    }

    std::unique_ptr<ASTNode> parseBraceGroup()
    {
        advance(); // consume LBRACE

        auto group = std::make_unique<ASTNode>(AST_BRACEGROUP);
        auto contents = parseSequence(false, false, true, false);
        group->addChild(std::move(contents));

        if (!atEnd() && peek().type == ASTTOK_RBRACE)
        {
            advance();
        }
        return group;
    }
};

// ---------------------------------------------------------------
// Public parse API
// ---------------------------------------------------------------

std::unique_ptr<ASTNode> ast_parse(const std::vector<ASTToken> &tokens)
{
    ASTParser parser(tokens);
    return parser.parse();
}

std::unique_ptr<ASTNode> ast_parse_string(const UTF8 *input, size_t nLen)
{
    auto tokens = ast_tokenize(input, nLen);
    return ast_parse(tokens);
}

// ---------------------------------------------------------------
// Utility functions
// ---------------------------------------------------------------

std::string ast_raw_text(const ASTNode *n)
{
    if (!n)
    {
        return "";
    }

    switch (n->type)
    {
    case AST_LITERAL:
    case AST_SPACE:
    case AST_SUBST:
    case AST_ESCAPE:
        return n->text;

    case AST_SEMICOLON:
        return ";";

    case AST_FUNCCALL:
        {
            std::string r = n->text + "(";
            for (size_t i = 0; i < n->children.size(); i++)
            {
                if (i > 0) r += ",";
                r += ast_raw_text(n->children[i].get());
            }
            return r + ")";
        }

    case AST_EVALBRACKET:
        {
            std::string r = "[";
            for (const auto &c : n->children)
            {
                r += ast_raw_text(c.get());
            }
            return r + "]";
        }

    case AST_BRACEGROUP:
        {
            std::string r = "{";
            for (const auto &c : n->children)
            {
                r += ast_raw_text(c.get());
            }
            return r + "}";
        }

    case AST_SEQUENCE:
        {
            std::string r;
            for (const auto &c : n->children)
            {
                r += ast_raw_text(c.get());
            }
            return r;
        }
    }
    return "";
}

static const char *ast_node_name(ASTNodeType t)
{
    switch (t)
    {
    case AST_SEQUENCE:    return "Seq";
    case AST_LITERAL:     return "Lit";
    case AST_SPACE:       return "Sp";
    case AST_SUBST:       return "Sub";
    case AST_ESCAPE:      return "Esc";
    case AST_FUNCCALL:    return "Call";
    case AST_EVALBRACKET: return "Eval";
    case AST_BRACEGROUP:  return "Brace";
    case AST_SEMICOLON:   return "Semi";
    }
    return "???";
}

void ast_dump(const ASTNode *node, int indent)
{
    if (!node)
    {
        return;
    }

    STARTLOG(LOG_DEBUG, "AST", "DUMP");
    Log.tinyprintf(T("%*s%s"), indent, "", ast_node_name(node->type));
    if (!node->text.empty())
    {
        Log.tinyprintf(T(" \"%s\""), node->text.c_str());
    }
    ENDLOG;

    for (const auto &child : node->children)
    {
        ast_dump(child.get(), indent + 2);
    }
}

// ---------------------------------------------------------------
// AST Evaluator (Phase 2)
// ---------------------------------------------------------------
//
// Walks the AST tree and produces evaluated output into buff/bufc.
// Uses the same runtime context as mux_exec (mudstate, mudconf,
// builtin function table, registers, iterator stack, etc.)
//
// For %-substitutions, delegates to mux_exec on the short
// substitution text (2-6 bytes). This gives exact compatibility
// with all L2 dispatch table entries without reimplementing them.
//
// For function calls, looks up in mudstate.builtin_functions and
// dispatches through the existing FUN handler. NOEVAL functions
// receive raw text via ast_raw_text() — the handler calls mux_exec
// internally as before.
//

// Forward declaration.
//
static void ast_eval_node(const ASTNode *node, UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int eval, const UTF8 *cargs[], int ncargs);

// Evaluate a function call node (AST_FUNCCALL).
//
static void ast_eval_funccall(const ASTNode *node, UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int eval, const UTF8 *cargs[], int ncargs)
{
    // Uppercase the function name for lookup.
    //
    size_t nName = node->text.size();
    if (nName == 0 || nName > MAX_UFUN_NAME_LEN)
    {
        // Too long or empty — not a valid function name.
        //
        safe_str(reinterpret_cast<const UTF8 *>(node->text.c_str()), buff, bufc);
        safe_chr('(', buff, bufc);
        for (size_t i = 0; i < node->children.size(); i++)
        {
            if (i > 0) safe_chr(',', buff, bufc);
            ast_eval_node(node->children[i].get(), buff, bufc,
                executor, caller, enactor, eval, cargs, ncargs);
        }
        safe_chr(')', buff, bufc);
        return;
    }

    UTF8 TempFun[LBUF_SIZE];
    memcpy(TempFun, node->text.c_str(), nName);
    TempFun[nName] = '\0';
    size_t nUpper;
    UTF8 *pUpper = mux_strupr(TempFun, nUpper);
    if (nUpper >= LBUF_SIZE)
    {
        nUpper = LBUF_SIZE - 1;
    }
    memcpy(TempFun, pUpper, nUpper);
    TempFun[nUpper] = '\0';

    std::vector<UTF8> name_key(TempFun, TempFun + nUpper);
    FUN *fp = nullptr;
    UFUN *ufp = nullptr;

    const auto it = mudstate.builtin_functions.find(name_key);
    if (it != mudstate.builtin_functions.end())
    {
        fp = it->second;
    }

    if (!fp)
    {
        auto it_ufunc = mudstate.ufunc_htab.find(name_key);
        ufp = (it_ufunc != mudstate.ufunc_htab.end())
            ? static_cast<UFUN*>(it_ufunc->second) : nullptr;
    }

    if (!fp && !ufp)
    {
        if (eval & EV_FMAND)
        {
            safe_str(T("#-1 FUNCTION ("), buff, bufc);
            safe_str(TempFun, buff, bufc);
            safe_str(T(") NOT FOUND"), buff, bufc);
        }
        else
        {
            // Not a mandatory function context — output as literal text.
            //
            safe_str(reinterpret_cast<const UTF8 *>(node->text.c_str()), buff, bufc);
            safe_chr('(', buff, bufc);
            for (size_t i = 0; i < node->children.size(); i++)
            {
                if (i > 0) safe_chr(',', buff, bufc);
                ast_eval_node(node->children[i].get(), buff, bufc,
                    executor, caller, enactor, eval, cargs, ncargs);
            }
            safe_chr(')', buff, bufc);
        }
        return;
    }

    // Check invocation limits.
    //
    mudstate.func_nest_lev++;
    mudstate.func_invk_ctr++;

    UTF8 *oldp = *bufc;

    if (mudconf.func_nest_lim <= mudstate.func_nest_lev)
    {
        safe_str(T("#-1 FUNCTION RECURSION LIMIT EXCEEDED"), buff, bufc);
    }
    else if (mudconf.func_invk_lim <= mudstate.func_invk_ctr)
    {
        safe_str(T("#-1 FUNCTION INVOCATION LIMIT EXCEEDED"), buff, bufc);
    }
    else if (Going(executor))
    {
        safe_str(T("#-1 BAD EXECUTOR"), buff, bufc);
    }
    else if (!check_access(executor, ufp ? ufp->perms : fp->perms))
    {
        safe_noperm(buff, bufc);
    }
    else if (ufp && (ufp->flags & FN_RESTRICT) && !Wizard(executor))
    {
        safe_noperm(buff, bufc);
    }
    else if (alarm_clock.alarmed)
    {
        safe_str(T("#-1 CPU LIMITED"), buff, bufc);
    }
    else if (ufp)
    {
        // User-defined function — fetch attribute and evaluate.
        //
        dbref aowner;
        int aflags;
        UTF8 *tbuf = atr_get("ast_eval.ufun", ufp->obj, ufp->atr,
            &aowner, &aflags);

        dbref obj = (ufp->flags & FN_PRIV) ? ufp->obj : executor;

        int nfargs = static_cast<int>(node->children.size());
        if (nfargs > MAX_ARG)
        {
            nfargs = MAX_ARG;
        }

        // Evaluate arguments for UFUN.
        //
        UTF8 *fargs[MAX_ARG];
        memset(fargs, 0, sizeof(fargs));
        for (int i = 0; i < nfargs; i++)
        {
            fargs[i] = alloc_lbuf("ast_eval.ufun.arg");
            UTF8 *bp = fargs[i];
            ast_eval_node(node->children[i].get(), fargs[i], &bp,
                executor, caller, enactor,
                eval | EV_FCHECK | EV_EVAL, cargs, ncargs);
            *bp = '\0';
        }

        if ((aflags & AF_NOEVAL) || NoEval(ufp->obj))
        {
            size_t nLen = strlen(reinterpret_cast<const char *>(tbuf));
            safe_copy_buf(tbuf, nLen, buff, bufc);
        }
        else
        {
            reg_ref **preserve = nullptr;
            if (ufp->flags & FN_PRES)
            {
                preserve = PushRegisters(MAX_GLOBAL_REGS);
                save_global_regs(preserve);
            }

            int feval = eval & ~(EV_TOP | EV_FMAND);
            mux_exec(tbuf, LBUF_SIZE-1, buff, bufc, obj, executor, enactor,
                AttrTrace(aflags, feval),
                const_cast<const UTF8 **>(fargs), nfargs);

            if (ufp->flags & FN_PRES)
            {
                restore_global_regs(preserve);
                PopRegisters(preserve, MAX_GLOBAL_REGS);
            }
        }

        for (int i = 0; i < nfargs; i++)
        {
            free_lbuf(fargs[i]);
        }
        free_lbuf(tbuf);
    }
    else
    {
        // Built-in function.
        //
        int nfargs = static_cast<int>(node->children.size());
        if (nfargs > MAX_ARG)
        {
            nfargs = MAX_ARG;
        }

        if (  fp->minArgs <= nfargs
           && nfargs <= fp->maxArgs
           && !alarm_clock.alarmed)
        {
            UTF8 *fargs[MAX_ARG];
            memset(fargs, 0, sizeof(fargs));
            int feval;

            if (fp->flags & FN_NOEVAL)
            {
                // NOEVAL functions receive raw text. The handler
                // calls mux_exec internally on args it wants to
                // evaluate.
                //
                feval = eval & ~(EV_EVAL | EV_TOP | EV_FMAND | EV_STRIP_CURLY);
                for (int i = 0; i < nfargs; i++)
                {
                    std::string raw = ast_raw_text(node->children[i].get());
                    fargs[i] = alloc_lbuf("ast_eval.noeval");
                    size_t len = raw.size();
                    if (len >= LBUF_SIZE)
                    {
                        len = LBUF_SIZE - 1;
                    }
                    memcpy(fargs[i], raw.c_str(), len);
                    fargs[i][len] = '\0';
                }
            }
            else
            {
                // Normal functions: evaluate each argument.
                //
                feval = eval & ~(EV_TOP | EV_FMAND);
                for (int i = 0; i < nfargs; i++)
                {
                    fargs[i] = alloc_lbuf("ast_eval.arg");
                    UTF8 *bp = fargs[i];
                    ast_eval_node(node->children[i].get(), fargs[i], &bp,
                        executor, caller, enactor,
                        eval | EV_FCHECK | EV_EVAL, cargs, ncargs);
                    *bp = '\0';
                }
            }

            fp->fun(fp, buff, &oldp, executor, caller, enactor,
                feval & EV_TRACE, fargs, nfargs, cargs, ncargs);
            *bufc = oldp;

            for (int i = 0; i < nfargs; i++)
            {
                free_lbuf(fargs[i]);
            }
        }
        else
        {
            // Wrong argument count.
            //
            if (fp->minArgs == fp->maxArgs)
            {
                safe_tprintf_str(buff, bufc,
                    T("#-1 FUNCTION (%s) EXPECTS %d ARGUMENTS"),
                    fp->name, fp->minArgs);
            }
            else if (fp->minArgs + 1 == fp->maxArgs)
            {
                safe_tprintf_str(buff, bufc,
                    T("#-1 FUNCTION (%s) EXPECTS %d OR %d ARGUMENTS"),
                    fp->name, fp->minArgs, fp->maxArgs);
            }
            else
            {
                safe_tprintf_str(buff, bufc,
                    T("#-1 FUNCTION (%s) EXPECTS BETWEEN %d AND %d ARGUMENTS"),
                    fp->name, fp->minArgs, fp->maxArgs);
            }
        }
    }

    mudstate.func_nest_lev--;
}

// Evaluate a single AST node into buff/bufc.
//
static void ast_eval_node(const ASTNode *node, UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int eval, const UTF8 *cargs[], int ncargs)
{
    if (!node || alarm_clock.alarmed)
    {
        return;
    }

    switch (node->type)
    {
    case AST_LITERAL:
        safe_str(reinterpret_cast<const UTF8 *>(node->text.c_str()), buff, bufc);
        break;

    case AST_SPACE:
        safe_str(reinterpret_cast<const UTF8 *>(node->text.c_str()), buff, bufc);
        break;

    case AST_SEMICOLON:
        safe_chr(';', buff, bufc);
        break;

    case AST_SUBST:
        if (eval & EV_EVAL)
        {
            // Delegate to mux_exec for the substitution text.
            // The text is short (2-6 bytes) and contains only the
            // %-sequence, so mux_exec returns quickly.
            //
            mux_exec(reinterpret_cast<const UTF8 *>(node->text.c_str()),
                node->text.size(), buff, bufc,
                executor, caller, enactor,
                eval, cargs, ncargs);
        }
        else
        {
            // Without EV_EVAL, pass through literally.
            //
            safe_str(reinterpret_cast<const UTF8 *>(node->text.c_str()), buff, bufc);
        }
        break;

    case AST_ESCAPE:
        // Output the escaped character (skip the backslash).
        //
        if (node->text.size() > 1)
        {
            safe_chr(node->text[1], buff, bufc);
        }
        break;

    case AST_FUNCCALL:
        if (eval & EV_FCHECK)
        {
            ast_eval_funccall(node, buff, bufc,
                executor, caller, enactor, eval, cargs, ncargs);
        }
        else
        {
            // Without EV_FCHECK, output as literal text.
            //
            safe_str(reinterpret_cast<const UTF8 *>(node->text.c_str()), buff, bufc);
            safe_chr('(', buff, bufc);
            for (size_t i = 0; i < node->children.size(); i++)
            {
                if (i > 0) safe_chr(',', buff, bufc);
                ast_eval_node(node->children[i].get(), buff, bufc,
                    executor, caller, enactor, eval, cargs, ncargs);
            }
            safe_chr(')', buff, bufc);
        }
        break;

    case AST_EVALBRACKET:
        if (eval & EV_NOFCHECK)
        {
            // Brackets suppressed — pass through as literal text.
            //
            safe_chr('[', buff, bufc);
            if (!node->children.empty())
            {
                std::string raw = ast_raw_text(node->children[0].get());
                safe_str(reinterpret_cast<const UTF8 *>(raw.c_str()), buff, bufc);
            }
            safe_chr(']', buff, bufc);
        }
        else
        {
            // Evaluate contents with function checking enabled.
            //
            mudstate.nStackNest++;
            if (!node->children.empty())
            {
                ast_eval_node(node->children[0].get(), buff, bufc,
                    executor, caller, enactor,
                    eval | EV_FCHECK | EV_FMAND, cargs, ncargs);
            }
            mudstate.nStackNest--;
        }
        break;

    case AST_BRACEGROUP:
        mudstate.nStackNest++;
        if (eval & EV_STRIP_CURLY)
        {
            // Strip braces and evaluate contents without
            // function checking.
            //
            int innerEval = eval & ~(EV_STRIP_CURLY | EV_FCHECK | EV_FMAND);
            if (!node->children.empty())
            {
                ast_eval_node(node->children[0].get(), buff, bufc,
                    executor, caller, enactor, innerEval, cargs, ncargs);
            }
        }
        else
        {
            // Pass through as literal braces.
            //
            safe_chr('{', buff, bufc);
            if (!node->children.empty())
            {
                int innerEval = eval & ~(EV_TOP | EV_FMAND);
                if (eval & EV_EVAL)
                {
                    innerEval = innerEval & ~(EV_STRIP_CURLY | EV_FCHECK | EV_FMAND);
                    ast_eval_node(node->children[0].get(), buff, bufc,
                        executor, caller, enactor, innerEval, cargs, ncargs);
                }
                else
                {
                    innerEval = (innerEval & ~EV_FCHECK) | EV_NOFCHECK;
                    ast_eval_node(node->children[0].get(), buff, bufc,
                        executor, caller, enactor, innerEval, cargs, ncargs);
                }
            }
            safe_chr('}', buff, bufc);
        }
        mudstate.nStackNest--;
        break;

    case AST_SEQUENCE:
        for (const auto &child : node->children)
        {
            ast_eval_node(child.get(), buff, bufc,
                executor, caller, enactor, eval, cargs, ncargs);
        }
        break;
    }
}

// ---------------------------------------------------------------
// mux_exec2 — drop-in replacement for mux_exec
// ---------------------------------------------------------------
//
// Phase 2: Parse into AST, then evaluate via ast_eval_node.
// The AST evaluator delegates %-substitutions to mux_exec for
// exact compatibility, and dispatches function calls through
// the existing FUN handler table.
//
void mux_exec2(const UTF8 *pStr, size_t nStr,
               UTF8 *buff, UTF8 **bufc,
               dbref executor, dbref caller, dbref enactor,
               int eval, const UTF8 *cargs[], int ncargs)
{
    if (  nullptr == pStr
       || '\0' == pStr[0]
       || alarm_clock.alarmed)
    {
        return;
    }

    // Stack limit checking.
    //
    if (mudconf.nStackLimit < mudstate.nStackNest)
    {
        mudstate.bStackLimitReached = true;
        return;
    }

    // Parse the input into an AST.
    //
    auto ast = ast_parse_string(pStr, nStr);

    // Evaluate the AST.
    //
    ast_eval_node(ast.get(), buff, bufc,
        executor, caller, enactor, eval, cargs, ncargs);
}
