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
#include <list>
#include <unordered_map>

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
            while (  p < pEnd
                  && *p != '>'
                  && *p != '}'
                  && *p != ')'
                  && *p != ']'
                  && *p != '['
                  && *p != '{'
                  && *p != ',')
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
            while (  p < pEnd
                  && *p != '>'
                  && *p != '}'
                  && *p != ')'
                  && *p != ']'
                  && *p != '['
                  && *p != '{'
                  && *p != ',')
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
            while (  p < pEnd
                  && *p != '>'
                  && *p != '}'
                  && *p != ')'
                  && *p != ']'
                  && *p != '['
                  && *p != '{'
                  && *p != ',')
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
        else if (  *p == '#'
                && p + 1 < pEnd
                && (  p[1] == '#'
                   || p[1] == '@'
                   || p[1] == '$'))
        {
            std::string hash("#");
            hash += static_cast<char>(p[1]);
            p += 2;
            tokens.push_back({ASTTOK_PCT, hash});
        }
        else if (*p == ' ')
        {
            std::string sp;
            while (p < pEnd && *p == ' ')
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
                  && *p != ' ')
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
        : m_tokens(tokens), m_pos(0), m_bracketDepth(0), m_braceDepth(0) {}

    std::unique_ptr<ASTNode> parse()
    {
        return parseSequence(false, false, false, false);
    }

private:
    const std::vector<ASTToken> &m_tokens;
    size_t m_pos;
    int m_bracketDepth;
    int m_braceDepth;

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
        // Handle zero-argument function calls: foo()
        //
        if (!atEnd() && peek().type == ASTTOK_RPAREN)
        {
            advance();
            call->has_close_paren = true;
            return;
        }

        call->has_close_paren = false;

        // When inside an eval bracket, ] terminates the argument
        // list (matching mux_exec behavior where ] closes the bracket
        // even with unclosed parentheses). Outside brackets, ] is
        // literal text in function arguments.
        //
        bool inBracket = (m_bracketDepth > 0);
        bool inBrace = (m_braceDepth > 0);
        auto arg = parseSequence(true, inBracket, inBrace, true);
        call->addChild(std::move(arg));

        while (!atEnd() && peek().type == ASTTOK_COMMA)
        {
            advance();
            arg = parseSequence(true, inBracket, inBrace, true);
            call->addChild(std::move(arg));
        }

        if (!atEnd() && peek().type == ASTTOK_RPAREN)
        {
            advance();
            call->has_close_paren = true;
        }
    }

    std::unique_ptr<ASTNode> parseEvalBracket()
    {
        advance(); // consume LBRACK
        m_bracketDepth++;

        auto bracket = std::make_unique<ASTNode>(AST_EVALBRACKET);
        auto contents = parseSequence(false, true, false, false);
        bracket->addChild(std::move(contents));

        if (!atEnd() && peek().type == ASTTOK_RBRACK)
        {
            advance();
        }
        m_bracketDepth--;
        return bracket;
    }

    std::unique_ptr<ASTNode> parseBraceGroup()
    {
        advance(); // consume LBRACE
        m_braceDepth++;

        auto group = std::make_unique<ASTNode>(AST_BRACEGROUP);
        auto contents = parseSequence(false, false, true, false);
        group->addChild(std::move(contents));

        if (!atEnd() && peek().type == ASTTOK_RBRACE)
        {
            advance();
        }
        m_braceDepth--;
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

// ---------------------------------------------------------------
// Native %-substitution handler
// ---------------------------------------------------------------
//
// Handles all L2 dispatch table substitutions natively instead of
// delegating to mux_exec. The node->text is the full %-sequence
// as gathered by gather_pct (e.g. "%0", "%qa", "%q<name>", "%xn",
// "%c<rgb>", "%va", "%i0", "%=<attr>", "%%", "%r", "%b", etc.)
//
static void ast_eval_subst(const ASTNode *node, UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int eval, const UTF8 *cargs[], int ncargs)
{
    const std::string &txt = node->text;
    if (txt.size() < 2)
    {
        // Bare '%' at end of string — output literally.
        //
        safe_chr('%', buff, bufc);
        return;
    }

    // ## / #@ / #$ — iterator/switch substitutions.
    //
    if (txt[0] == '#')
    {
        switch (txt[1])
        {
        case '#':
            // ## — bound variable (same as %i0).
            //
            {
                int i = mudstate.in_loop - 1;
                if (0 <= i && i < MAX_ITEXT && mudstate.itext[i])
                {
                    safe_str(mudstate.itext[i], buff, bufc);
                }
            }
            break;

        case '@':
            // #@ — list place number (same as inum()).
            //
            {
                int i = mudstate.in_loop - 1;
                if (0 <= i && i < MAX_ITEXT)
                {
                    safe_ltoa(mudstate.inum[i], buff, bufc);
                }
            }
            break;

        case '$':
            // #$ — switch value (same as switch() matched value).
            // Resolved from mudstate.switch_token if available.
            //
            if (mudstate.switch_token)
            {
                safe_str(mudstate.switch_token, buff, bufc);
            }
            break;
        }
        return;
    }

    unsigned char ch = static_cast<unsigned char>(txt[1]);
    unsigned char upper = static_cast<unsigned char>(mux_toupper_ascii(ch));

    // The L2 table sets flag 0x80 on uppercase A, M, N, O, P, Q, S, V
    // to trigger mux_toupper_first on the substituted value.
    //
    bool bUpperCase = false;
    if ('A' <= ch && ch <= 'Z')
    {
        switch (ch)
        {
        case 'A': case 'M': case 'N': case 'O':
        case 'P': case 'Q': case 'S': case 'V':
            bUpperCase = true;
            break;
        }
    }

    UTF8 scratch[LBUF_SIZE];
    UTF8 *TempPtr = *bufc;

    // %0-%9 — command argument substitution.
    //
    if (ch >= '0' && ch <= '9')
    {
        int i = ch - '0';
        if (i < ncargs && cargs[i])
        {
            safe_str(cargs[i], buff, bufc);
        }
        return;
    }

    switch (upper)
    {
    case 'Q':
        // %q0-%q9, %qa-%qz, %q<name> — register substitution.
        //
        if (txt.size() >= 3)
        {
            if (txt[2] == '<')
            {
                // Named register: %q<name>
                //
                size_t close = txt.find('>', 3);
                if (close != std::string::npos)
                {
                    size_t nName = close - 3;
                    const UTF8 *pName = reinterpret_cast<const UTF8 *>(txt.c_str() + 3);
                    int regnum = -1;
                    if (  1 == nName
                       && (regnum = mux_RegisterSet[pName[0]]) >= 0
                       && regnum < MAX_GLOBAL_REGS)
                    {
                        if (  mudstate.global_regs[regnum]
                           && mudstate.global_regs[regnum]->reg_len > 0)
                        {
                            safe_copy_buf(mudstate.global_regs[regnum]->reg_ptr,
                                mudstate.global_regs[regnum]->reg_len, buff, bufc);
                        }
                    }
                    else if (IsValidNamedReg(pName, nName))
                    {
                        reg_ref *rr = NamedRegRead(mudstate.named_regs, pName, nName);
                        if (rr && rr->reg_len > 0)
                        {
                            safe_copy_buf(rr->reg_ptr, rr->reg_len, buff, bufc);
                        }
                    }
                }
            }
            else
            {
                // Traditional single-char: %q0-%q9, %qa-%qz
                //
                int i = mux_RegisterSet[static_cast<unsigned char>(txt[2])];
                if (  0 <= i
                   && i < MAX_GLOBAL_REGS)
                {
                    if (  mudstate.global_regs[i]
                       && mudstate.global_regs[i]->reg_len > 0)
                    {
                        safe_copy_buf(mudstate.global_regs[i]->reg_ptr,
                            mudstate.global_regs[i]->reg_len, buff, bufc);
                    }
                }
            }
        }
        break;

    case '#':
        // %# — enactor dbref.
        //
        {
            scratch[0] = '#';
            size_t n = mux_ltoa(enactor, scratch + 1);
            safe_copy_buf(scratch, n + 1, buff, bufc);
        }
        break;

    case '!':
        // %! — executor dbref.
        //
        {
            scratch[0] = '#';
            size_t n = mux_ltoa(executor, scratch + 1);
            safe_copy_buf(scratch, n + 1, buff, bufc);
        }
        break;

    case '@':
        // %@ — caller dbref.
        //
        {
            scratch[0] = '#';
            size_t n = mux_ltoa(caller, scratch + 1);
            safe_copy_buf(scratch, n + 1, buff, bufc);
        }
        break;

    case '%':
        // %% — literal percent.
        //
        safe_chr('%', buff, bufc);
        break;

    case 'R':
        // %r — carriage return.
        //
        safe_copy_buf(T("\r\n"), 2, buff, bufc);
        break;

    case 'B':
        // %b — blank (space).
        //
        safe_chr(' ', buff, bufc);
        break;

    case 'T':
        // %t — tab.
        //
        safe_chr('\t', buff, bufc);
        break;

    case 'N':
        // %n/%N — enactor name.
        //
        safe_str(Name(enactor), buff, bufc);
        break;

    case 'L':
        // %l — enactor location dbref.
        //
        if (!(eval & EV_NO_LOCATION))
        {
            scratch[0] = '#';
            size_t n = mux_ltoa(where_is(enactor), scratch + 1);
            safe_copy_buf(scratch, n + 1, buff, bufc);
        }
        break;

    case 'S':
        // %s/%S — subjective pronoun.
        //
        {
            const PRONOUN_SET *ps = get_pronoun_set(enactor);
            safe_str(ps->subjective, buff, bufc);
        }
        break;

    case 'P':
        // %p/%P — possessive pronoun.
        //
        {
            const PRONOUN_SET *ps = get_pronoun_set(enactor);
            safe_str(ps->possessive, buff, bufc);
        }
        break;

    case 'O':
        // %o/%O — objective pronoun.
        //
        {
            const PRONOUN_SET *ps = get_pronoun_set(enactor);
            safe_str(ps->objective, buff, bufc);
        }
        break;

    case 'A':
        // %a/%A — absolute possessive pronoun.
        //
        {
            const PRONOUN_SET *ps = get_pronoun_set(enactor);
            safe_str(ps->absolute, buff, bufc);
        }
        break;

    case 'M':
        // %m — last command.
        //
        safe_str(mudstate.curr_cmd, buff, bufc);
        break;

    case 'K':
        // %k — moniker.
        //
        safe_str(Moniker(enactor), buff, bufc);
        break;

    case '|':
        // %| — piped command output.
        //
        safe_str(mudstate.pout, buff, bufc);
        break;

    case '+':
        // %+ — number of command args.
        //
        safe_i64toa(ncargs, buff, bufc);
        break;

    case ':':
        // %: — enactor objid (#dbref:creation_seconds).
        //
        {
            scratch[0] = '#';
            size_t n = mux_ltoa(enactor, scratch + 1);
            int64_t csecs = creation_seconds(enactor);
            if (0 != csecs)
            {
                scratch[n + 1] = ':';
                mux_i64toa(csecs, scratch + n + 2);
            }
            safe_str(scratch, buff, bufc);
        }
        break;

    case 'V':
        // %va-%vz — variable attribute.
        //
        if (txt.size() >= 3 && mux_isazAZ(txt[2]))
        {
            int i = A_VA + mux_toupper_ascii(txt[2]) - 'A';
            dbref aowner;
            int aflags;
            size_t nAttrGotten;
            atr_pget_str_LEN(scratch, executor, i, &aowner, &aflags, &nAttrGotten);
            if (0 < nAttrGotten)
            {
                safe_copy_buf(scratch, nAttrGotten, buff, bufc);
            }
        }
        break;

    case 'I':
        // %i0-%i9 — itext() substitution.
        //
        if (txt.size() >= 3 && mux_isdigit(txt[2]))
        {
            int depth = txt[2] - '0';
            int i = mudstate.in_loop - depth - 1;
            if (0 <= i && i < MAX_ITEXT)
            {
                safe_str(mudstate.itext[i], buff, bufc);
            }
        }
        else if (txt.size() >= 3)
        {
            // %i followed by non-digit — output the char after %i literally.
            //
            safe_chr(txt[2], buff, bufc);
        }
        break;

    case '=':
        // %= — plain equals sign.
        // %=<name> — attribute or numbered arg substitution.
        //
        if (txt.size() >= 4 && txt[2] == '<')
        {
            size_t close = txt.find('>', 3);
            if (close != std::string::npos)
            {
                size_t nName = close - 3;
                memcpy(scratch, txt.c_str() + 3, nName);
                scratch[nName] = '\0';

                if (mux_isdigit(scratch[0]))
                {
                    // Numeric arg reference: %=<0> through %=<999>
                    //
                    int i;
                    if (!mux_isdigit(scratch[1]))
                    {
                        i = scratch[0] - '0';
                    }
                    else if (!mux_isdigit(scratch[2]))
                    {
                        i = TableATOI(scratch[0] - '0', scratch[1] - '0');
                    }
                    else if (!mux_isdigit(scratch[3]))
                    {
                        i = 10 * TableATOI(scratch[0] - '0', scratch[1] - '0')
                          + scratch[2] - '0';
                    }
                    else
                    {
                        i = MAX_ARG;
                    }
                    if (i < ncargs && nullptr != cargs[i])
                    {
                        safe_str(cargs[i], buff, bufc);
                    }
                }
                else if (mux_isattrnameinitial(scratch))
                {
                    ATTR *ap = atr_str(scratch);
                    if (ap && See_attr(executor, executor, ap))
                    {
                        dbref aowner;
                        int aflags;
                        size_t nLen;
                        atr_pget_str_LEN(scratch, executor, ap->number,
                            &aowner, &aflags, &nLen);
                        safe_copy_buf(scratch, nLen, buff, bufc);
                    }
                }
            }
        }
        break;

    case 'C':
    case 'X':
        // %c/%x — color codes.
        // Uppercase C/X → background (0x40 flag in L2 table).
        // Lowercase c/x → foreground.
        //
        if (txt.size() >= 3)
        {
            bool bBackground = ('A' <= ch && ch <= 'Z');

            if (txt[2] == '<')
            {
                // Extended color: %c<rgb>, %x<name>, etc.
                //
                size_t close = txt.find('>', 3);
                if (close != std::string::npos)
                {
                    size_t nColor = close - 3;
                    const UTF8 *pColor = reinterpret_cast<const UTF8 *>(txt.c_str() + 3);

                    RGB rgb;
                    if (parse_rgb(nColor, pColor, rgb))
                    {
                        unsigned int iColor = FindNearestPaletteEntry(rgb, true);
                        if (bBackground)
                        {
                            safe_str(aColors[iColor + COLOR_INDEX_BG].pUTF, buff, bufc);
                            if (palette[iColor].rgb.r != rgb.r)
                            {
                                safe_str(ConvertToUTF8(static_cast<UTF32>(rgb.r + 0xF0300)), buff, bufc);
                            }
                            if (palette[iColor].rgb.g != rgb.g)
                            {
                                safe_str(ConvertToUTF8(static_cast<UTF32>(rgb.g + 0xF0400)), buff, bufc);
                            }
                            if (palette[iColor].rgb.b != rgb.b)
                            {
                                safe_str(ConvertToUTF8(static_cast<UTF32>(rgb.b + 0xF0500)), buff, bufc);
                            }
                        }
                        else
                        {
                            safe_str(aColors[iColor + COLOR_INDEX_FG].pUTF, buff, bufc);
                            if (palette[iColor].rgb.r != rgb.r)
                            {
                                safe_str(ConvertToUTF8(static_cast<UTF32>(rgb.r + 0xF0000)), buff, bufc);
                            }
                            if (palette[iColor].rgb.g != rgb.g)
                            {
                                safe_str(ConvertToUTF8(static_cast<UTF32>(rgb.g + 0xF0100)), buff, bufc);
                            }
                            if (palette[iColor].rgb.b != rgb.b)
                            {
                                safe_str(ConvertToUTF8(static_cast<UTF32>(rgb.b + 0xF0200)), buff, bufc);
                            }
                        }
                    }
                }
            }
            else
            {
                // Simple color code: %xn, %ch, etc.
                //
                unsigned int iColor = ColorTable[static_cast<unsigned char>(txt[2])];
                if (iColor)
                {
                    safe_str(aColors[iColor].pUTF, buff, bufc);
                }
                else
                {
                    // Unknown color letter — output it literally.
                    //
                    safe_chr(txt[2], buff, bufc);
                }
            }
        }
        break;

    default:
        // Unknown substitution — output the character literally
        // (matches iCode == 0 in mux_exec).
        //
        safe_chr(ch, buff, bufc);
        break;
    }

    // For uppercase escape letters (%S, %N, %P, %O, %A, %K),
    // uppercase the first character of the substituted value.
    //
    if (bUpperCase)
    {
        mux_toupper_first(TempPtr, bufc, LBUF_SIZE);
    }
}

// ---------------------------------------------------------------
// Native NOEVAL handlers
// ---------------------------------------------------------------
//
// These functions handle specific NOEVAL built-in functions by
// evaluating AST subtrees directly instead of serializing back
// to text and re-parsing through mux_exec.
//

// Helper: evaluate an AST subtree for a NOEVAL branch (if/switch/iter).
//
// ##/#@/#$ are resolved natively as AST_SUBST nodes at eval time
// (from mudstate.itext/inum/switch_token), so no serialization or
// replace_tokens pass is needed.
//
static void ast_eval_branch(const ASTNode *child, UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int eval, const UTF8 *cargs[], int ncargs)
{
    ast_eval_node(child, buff, bufc,
        executor, caller, enactor,
        eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
}

// Native cand/candbool: short-circuit AND.
//
static void ast_noeval_cand(const ASTNode *node, UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int eval, const UTF8 *cargs[], int ncargs,
    bool bBool)
{
    int nfargs = static_cast<int>(node->children.size());
    bool val = true;
    UTF8 *temp = alloc_lbuf("ast_noeval_cand");
    for (int i = 0; i < nfargs && val && !alarm_clock.alarmed; i++)
    {
        UTF8 *bp = temp;
        ast_eval_node(node->children[i].get(), temp, &bp,
            executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *bp = '\0';
        val = bBool ? xlate(temp) : isTRUE(mux_atol(temp));
    }
    free_lbuf(temp);
    safe_bool(val, buff, bufc);
}

// Native cor/corbool: short-circuit OR.
//
static void ast_noeval_cor(const ASTNode *node, UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int eval, const UTF8 *cargs[], int ncargs,
    bool bBool)
{
    int nfargs = static_cast<int>(node->children.size());
    bool val = false;
    UTF8 *temp = alloc_lbuf("ast_noeval_cor");
    for (int i = 0; i < nfargs && !val && !alarm_clock.alarmed; i++)
    {
        UTF8 *bp = temp;
        ast_eval_node(node->children[i].get(), temp, &bp,
            executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *bp = '\0';
        val = bBool ? xlate(temp) : isTRUE(mux_atol(temp));
    }
    free_lbuf(temp);
    safe_bool(val, buff, bufc);
}

// Native if/ifelse: conditional branch selection.
//
static void ast_noeval_ifelse(const ASTNode *node, UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int eval, const UTF8 *cargs[], int ncargs)
{
    int nfargs = static_cast<int>(node->children.size());

    // Evaluate the condition.
    //
    UTF8 *lbuff = alloc_lbuf("ast_noeval_if");
    UTF8 *bp = lbuff;
    ast_eval_node(node->children[0].get(), lbuff, &bp,
        executor, caller, enactor,
        eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
    *bp = '\0';

    const UTF8 *saved_switch = mudstate.switch_token;
    mudstate.switch_token = lbuff;

    if (xlate(lbuff))
    {
        ast_eval_branch(node->children[1].get(), buff, bufc,
            executor, caller, enactor, eval, cargs, ncargs);
    }
    else if (nfargs >= 3)
    {
        ast_eval_branch(node->children[2].get(), buff, bufc,
            executor, caller, enactor, eval, cargs, ncargs);
    }

    mudstate.switch_token = saved_switch;
    free_lbuf(lbuff);
}

// Native switch/case: first-match pattern dispatch.
//
static void ast_noeval_switch(const ASTNode *node, UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int eval, const UTF8 *cargs[], int ncargs,
    bool bWild)
{
    int nfargs = static_cast<int>(node->children.size());

    // Evaluate the target in child[0].
    //
    UTF8 *mbuff = alloc_lbuf("ast_noeval_switch");
    UTF8 *bp = mbuff;
    ast_eval_node(node->children[0].get(), mbuff, &bp,
        executor, caller, enactor,
        eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
    *bp = '\0';

    UTF8 *tbuff = alloc_lbuf("ast_noeval_switch.2");

    const UTF8 *saved_switch = mudstate.switch_token;
    mudstate.switch_token = mbuff;

    // Loop through patterns looking for a match.
    //
    int i;
    for (i = 1; i < nfargs - 1 && !alarm_clock.alarmed; i += 2)
    {
        bp = tbuff;
        ast_eval_node(node->children[i].get(), tbuff, &bp,
            executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *bp = '\0';

        if (  bWild
            ? wild_match(tbuff, mbuff)
            : strcmp(reinterpret_cast<char *>(tbuff),
                     reinterpret_cast<char *>(mbuff)) == 0)
        {
            free_lbuf(tbuff);
            ast_eval_branch(node->children[i + 1].get(), buff, bufc,
                executor, caller, enactor, eval, cargs, ncargs);
            mudstate.switch_token = saved_switch;
            free_lbuf(mbuff);
            return;
        }
    }
    free_lbuf(tbuff);

    // No match — evaluate default if present.
    //
    if (i < nfargs)
    {
        ast_eval_branch(node->children[i].get(), buff, bufc,
            executor, caller, enactor, eval, cargs, ncargs);
    }

    mudstate.switch_token = saved_switch;
    free_lbuf(mbuff);
}

// Native switchall/caseall: all-match pattern dispatch.
//
static void ast_noeval_switchall(const ASTNode *node, UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int eval, const UTF8 *cargs[], int ncargs,
    bool bWild)
{
    int nfargs = static_cast<int>(node->children.size());

    // Evaluate the target in child[0].
    //
    UTF8 *mbuff = alloc_lbuf("ast_noeval_switchall");
    UTF8 *bp = mbuff;
    ast_eval_node(node->children[0].get(), mbuff, &bp,
        executor, caller, enactor,
        eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
    *bp = '\0';

    UTF8 *tbuff = alloc_lbuf("ast_noeval_switchall.2");

    const UTF8 *saved_switch = mudstate.switch_token;
    mudstate.switch_token = mbuff;

    // Loop through all patterns, evaluating every match.
    //
    bool bMatched = false;
    int i;
    for (i = 1; i < nfargs - 1 && !alarm_clock.alarmed; i += 2)
    {
        bp = tbuff;
        ast_eval_node(node->children[i].get(), tbuff, &bp,
            executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *bp = '\0';

        if (  bWild
            ? wild_match(tbuff, mbuff)
            : strcmp(reinterpret_cast<char *>(tbuff),
                     reinterpret_cast<char *>(mbuff)) == 0)
        {
            bMatched = true;
            ast_eval_branch(node->children[i + 1].get(), buff, bufc,
                executor, caller, enactor, eval, cargs, ncargs);
        }
    }
    free_lbuf(tbuff);

    // If nothing matched, evaluate the default.
    //
    if (!bMatched && i < nfargs)
    {
        ast_eval_branch(node->children[i].get(), buff, bufc,
            executor, caller, enactor, eval, cargs, ncargs);
    }

    mudstate.switch_token = saved_switch;
    free_lbuf(mbuff);
}

// Native iter: list iteration.
//
static void ast_noeval_iter(const ASTNode *node, UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int eval, const UTF8 *cargs[], int ncargs)
{
    int nfargs = static_cast<int>(node->children.size());

    // Handle optional delimiters (args 3 and 4) by serializing
    // and evaluating them, then parsing into SEP structures.
    // For the common case (no delimiters), use space defaults.
    //
    SEP sep;
    sep.n = 1;
    memcpy(sep.str, " ", 2);

    SEP osep;
    osep.n = 1;
    memcpy(osep.str, " ", 2);

    if (nfargs >= 3)
    {
        // Evaluate input delimiter.
        //
        UTF8 *dbuf = alloc_lbuf("ast_noeval_iter.sep");
        UTF8 *dp = dbuf;
        ast_eval_node(node->children[2].get(), dbuf, &dp,
            executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *dp = '\0';
        size_t dlen = dp - dbuf;
        if (dlen == 1)
        {
            sep.n = 1;
            memcpy(sep.str, dbuf, 2);
        }
        else if (dlen > 1 && dlen <= MAX_SEP_LEN)
        {
            sep.n = dlen;
            memcpy(sep.str, dbuf, dlen);
            sep.str[dlen] = '\0';
        }
        free_lbuf(dbuf);
    }

    if (nfargs >= 4)
    {
        // Evaluate output delimiter.
        //
        UTF8 *dbuf = alloc_lbuf("ast_noeval_iter.osep");
        UTF8 *dp = dbuf;
        ast_eval_node(node->children[3].get(), dbuf, &dp,
            executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *dp = '\0';
        size_t dlen = dp - dbuf;
        if (dlen == 0)
        {
            osep.n = 1;
            memcpy(osep.str, " ", 2);
        }
        else if (dlen == 2 && memcmp(dbuf, "@@", 2) == 0)
        {
            osep.n = 0;
            osep.str[0] = '\0';
        }
        else if (dlen == 2 && memcmp(dbuf, "\r\n", 2) == 0)
        {
            osep.n = 2;
            memcpy(osep.str, "\r\n", 3);
        }
        else if (dlen == 1)
        {
            osep.n = 1;
            memcpy(osep.str, dbuf, 2);
        }
        else if (dlen <= MAX_SEP_LEN)
        {
            osep.n = dlen;
            memcpy(osep.str, dbuf, dlen);
            osep.str[dlen] = '\0';
        }
        free_lbuf(dbuf);
    }

    // Evaluate the list (child[0]).
    //
    UTF8 *curr = alloc_lbuf("ast_noeval_iter");
    UTF8 *dp = curr;
    ast_eval_node(node->children[0].get(), curr, &dp,
        executor, caller, enactor,
        eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
    *dp = '\0';

    size_t ncp;
    UTF8 *cp = trim_space_sep_LEN(curr, dp - curr, sep, &ncp);
    if (!*cp)
    {
        free_lbuf(curr);
        return;
    }

    bool first = true;
    int number = 0;
    bool bLoopInBounds = (  0 <= mudstate.in_loop
                         && mudstate.in_loop < MAX_ITEXT);
    if (bLoopInBounds)
    {
        mudstate.itext[mudstate.in_loop] = nullptr;
        mudstate.inum[mudstate.in_loop] = number;
    }
    mudstate.in_loop++;

    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim
          && !alarm_clock.alarmed)
    {
        if (!first)
        {
            print_sep(osep, buff, bufc);
        }
        first = false;
        number++;
        UTF8 *objstring = split_token(&cp, sep);
        if (bLoopInBounds)
        {
            mudstate.itext[mudstate.in_loop - 1] = objstring;
            mudstate.inum[mudstate.in_loop - 1] = number;
        }

        // Evaluate the body subtree directly. ## and #@ resolve
        // natively from mudstate.itext/inum, making this safe
        // without replace_tokens.
        //
        ast_eval_branch(node->children[1].get(), buff, bufc,
            executor, caller, enactor, eval, cargs, ncargs);
    }

    mudstate.in_loop--;
    if (bLoopInBounds)
    {
        mudstate.itext[mudstate.in_loop] = nullptr;
        mudstate.inum[mudstate.in_loop] = 0;
    }
    free_lbuf(curr);
}

// Dispatch table for native NOEVAL handling. Returns true if the
// function was handled natively (caller should skip generic dispatch).
//
static bool ast_try_native_noeval(const ASTNode *node,
    const UTF8 *funcName, size_t nameLen,
    UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int eval, const UTF8 *cargs[], int ncargs)
{
    if (nameLen == 2 && memcmp(funcName, "IF", 2) == 0)
    {
        ast_noeval_ifelse(node, buff, bufc, executor, caller, enactor,
            eval, cargs, ncargs);
        return true;
    }
    if (nameLen == 3)
    {
        if (memcmp(funcName, "COR", 3) == 0)
        {
            ast_noeval_cor(node, buff, bufc, executor, caller, enactor,
                eval, cargs, ncargs, false);
            return true;
        }
        return false;
    }
    if (nameLen == 4)
    {
        if (memcmp(funcName, "CAND", 4) == 0)
        {
            ast_noeval_cand(node, buff, bufc, executor, caller, enactor,
                eval, cargs, ncargs, false);
            return true;
        }
        if (memcmp(funcName, "CASE", 4) == 0)
        {
            ast_noeval_switch(node, buff, bufc, executor, caller, enactor,
                eval, cargs, ncargs, false);
            return true;
        }
        if (memcmp(funcName, "ITER", 4) == 0)
        {
            ast_noeval_iter(node, buff, bufc, executor, caller, enactor,
                eval, cargs, ncargs);
            return true;
        }
        return false;
    }
    if (nameLen == 6)
    {
        if (memcmp(funcName, "IFELSE", 6) == 0)
        {
            ast_noeval_ifelse(node, buff, bufc, executor, caller, enactor,
                eval, cargs, ncargs);
            return true;
        }
        if (memcmp(funcName, "SWITCH", 6) == 0)
        {
            ast_noeval_switch(node, buff, bufc, executor, caller, enactor,
                eval, cargs, ncargs, true);
            return true;
        }
        return false;
    }
    if (nameLen == 7)
    {
        if (memcmp(funcName, "CASEALL", 7) == 0)
        {
            ast_noeval_switchall(node, buff, bufc, executor, caller, enactor,
                eval, cargs, ncargs, false);
            return true;
        }
        if (memcmp(funcName, "CORBOOL", 7) == 0)
        {
            ast_noeval_cor(node, buff, bufc, executor, caller, enactor,
                eval, cargs, ncargs, true);
            return true;
        }
        return false;
    }
    if (nameLen == 8)
    {
        if (memcmp(funcName, "CANDBOOL", 8) == 0)
        {
            ast_noeval_cand(node, buff, bufc, executor, caller, enactor,
                eval, cargs, ncargs, true);
            return true;
        }
        if (memcmp(funcName, "CASEALL", 7) == 0)
        {
            // CASEALL is 7 chars, handled above.
        }
        return false;
    }
    if (nameLen == 9 && memcmp(funcName, "SWITCHALL", 9) == 0)
    {
        ast_noeval_switchall(node, buff, bufc, executor, caller, enactor,
            eval, cargs, ncargs, true);
        return true;
    }
    return false;
}

// Output a function call node as literal text: name(arg,arg,...).
//
static void ast_emit_literal_funccall(const ASTNode *node, UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int eval, const UTF8 *cargs[], int ncargs)
{
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

// Evaluate a function call node (AST_FUNCCALL).
//
static void ast_eval_funccall(const ASTNode *node, UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int eval, const UTF8 *cargs[], int ncargs)
{
    // EV_FCHECK without EV_FMAND means "check if the first ( is a
    // function call."  If EV_FCHECK has been stripped (by the SEQUENCE
    // handler after the first child), this function call is not at
    // the start of the expression — output as literal text.
    //
    if (!(eval & EV_FCHECK) && !(eval & EV_FMAND))
    {
        ast_emit_literal_funccall(node, buff, bufc,
            executor, caller, enactor, eval, cargs, ncargs);
        return;
    }

    // Missing closing ')' — mux_exec's parse_arglist_lite returns
    // nullptr in this case and the function is NOT dispatched.  Output
    // the function name and '(' literally, then evaluate the argument
    // children (%-substitutions must be resolved, matching the classic
    // parser which evaluates as it scans).
    //
    if (!node->has_close_paren)
    {
        safe_str(reinterpret_cast<const UTF8 *>(node->text.c_str()), buff, bufc);
        safe_chr('(', buff, bufc);
        for (size_t i = 0; i < node->children.size(); i++)
        {
            if (i > 0) safe_chr(',', buff, bufc);
            ast_eval_node(node->children[i].get(), buff, bufc,
                executor, caller, enactor, eval, cargs, ncargs);
        }
        return;
    }

    // Uppercase the function name for lookup.
    //
    size_t nName = node->text.size();
    if (nName == 0 || nName > MAX_UFUN_NAME_LEN)
    {
        ast_emit_literal_funccall(node, buff, bufc,
            executor, caller, enactor, eval, cargs, ncargs);
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
            ast_emit_literal_funccall(node, buff, bufc,
                executor, caller, enactor, eval, cargs, ncargs);
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
            mux_exec2(tbuf, LBUF_SIZE-1, buff, bufc, obj, executor, enactor,
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
        // The AST parser always splits on commas, but mux_exec uses
        // maxArgsParsed to limit splitting — excess args are catenated
        // (with commas) into the last slot.  Apply the same logic here.
        //
        int nParsed = static_cast<int>(node->children.size());
        int nfargs = nParsed;
        if (nfargs > fp->maxArgsParsed && fp->maxArgsParsed > 0)
        {
            nfargs = fp->maxArgsParsed;
        }
        if (nfargs > MAX_ARG)
        {
            nfargs = MAX_ARG;
        }

        if (  fp->minArgs <= nfargs
           && nfargs <= fp->maxArgs
           && !alarm_clock.alarmed)
        {
            // Try native NOEVAL handlers first.
            //
            if (  (fp->flags & FN_NOEVAL)
               && ast_try_native_noeval(node, TempFun, nUpper,
                      buff, bufc, executor, caller, enactor,
                      eval, cargs, ncargs))
            {
                mudstate.func_nest_lev--;
                return;
            }

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
                    fargs[i] = alloc_lbuf("ast_eval.noeval");

                    if (i < nfargs - 1 || nParsed <= nfargs)
                    {
                        std::string raw = ast_raw_text(node->children[i].get());
                        size_t len = raw.size();
                        if (len >= LBUF_SIZE) len = LBUF_SIZE - 1;
                        memcpy(fargs[i], raw.c_str(), len);
                        fargs[i][len] = '\0';
                    }
                    else
                    {
                        // Catenate remaining children with commas.
                        //
                        UTF8 *bp = fargs[i];
                        for (int j = i; j < nParsed; j++)
                        {
                            if (j > i) safe_chr(',', fargs[i], &bp);
                            std::string raw = ast_raw_text(node->children[j].get());
                            safe_str(reinterpret_cast<const UTF8 *>(raw.c_str()),
                                fargs[i], &bp);
                        }
                        *bp = '\0';
                    }
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

                    if (i < nfargs - 1 || nParsed <= nfargs)
                    {
                        ast_eval_node(node->children[i].get(), fargs[i], &bp,
                            executor, caller, enactor,
                            feval | EV_FCHECK | EV_EVAL, cargs, ncargs);
                    }
                    else
                    {
                        // Catenate remaining children with commas.
                        //
                        for (int j = i; j < nParsed; j++)
                        {
                            if (j > i) safe_chr(',', fargs[i], &bp);
                            ast_eval_node(node->children[j].get(), fargs[i], &bp,
                                executor, caller, enactor,
                                feval | EV_FCHECK | EV_EVAL, cargs, ncargs);
                        }
                    }
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
        if (  mudconf.space_compress
           && !(eval & EV_NO_COMPRESS))
        {
            // Space compression: emit a single space regardless of
            // how many whitespace characters are in the source.
            //
            safe_chr(' ', buff, bufc);
        }
        else
        {
            safe_str(reinterpret_cast<const UTF8 *>(node->text.c_str()), buff, bufc);
        }
        break;

    case AST_SEMICOLON:
        safe_chr(';', buff, bufc);
        break;

    case AST_SUBST:
        if (eval & EV_EVAL)
        {
            ast_eval_subst(node, buff, bufc,
                executor, caller, enactor, eval, cargs, ncargs);
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
    {
        size_t first = 0;
        size_t count = node->children.size();
        size_t last = count;

        if (  mudconf.space_compress
           && !(eval & EV_NO_COMPRESS))
        {
            // Skip leading and trailing AST_SPACE children.
            // This matches mux_exec's at_space=1 (suppress leading)
            // and trailing-space strip behavior, without touching
            // spaces generated by function output.
            //
            while (first < count && node->children[first]->type == AST_SPACE)
            {
                first++;
            }
            while (last > first && node->children[last - 1]->type == AST_SPACE)
            {
                last--;
            }
        }

        // EV_FCHECK without EV_FMAND: mux_exec only checks the first
        // '(' as a potential function call, then clears EV_FCHECK.
        // Replicate by only letting the first effective child keep
        // EV_FCHECK; subsequent children get it stripped so their
        // FUNCALL nodes output as literal text.
        //
        bool bStripFCheck = (eval & EV_FCHECK) != 0
                         && (eval & EV_FMAND) == 0;

        for (size_t i = first; i < last; i++)
        {
            int childEval = eval;
            if (bStripFCheck)
            {
                if (i > first)
                {
                    childEval = eval & ~EV_FCHECK;
                }
                else if (node->children[i]->type != AST_FUNCCALL)
                {
                    // First child is not a function call — strip
                    // EV_FCHECK immediately (matches mux_exec where
                    // the text before '(' includes non-function text).
                    //
                    childEval = eval & ~EV_FCHECK;
                }
            }
            ast_eval_node(node->children[i].get(), buff, bufc,
                executor, caller, enactor, childEval, cargs, ncargs);
        }
        break;
    }
    }
}

// ---------------------------------------------------------------
// AST parse cache
// ---------------------------------------------------------------
//
// LRU cache of parsed AST trees, keyed by expression text.
// Parsing is pure (no side effects), so cached ASTs are safe
// to share across evaluations with different contexts.
//

struct ASTCacheEntry
{
    std::shared_ptr<ASTNode> ast;
    std::list<std::string>::iterator lru_it;
};

static std::unordered_map<std::string, ASTCacheEntry> s_astCache;
static std::list<std::string> s_astLru;
static const size_t AST_CACHE_MAX = 1024;
static const size_t AST_CACHE_MIN_LEN = 16;

// ---------------------------------------------------------------
// mux_exec2 — drop-in replacement for mux_exec
// ---------------------------------------------------------------
//
// Parse into AST (with caching), then evaluate via ast_eval_node.
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

    // nStr is a buffer-size limit, not the string length.
    // Use the actual string length for caching and parsing.
    //
    size_t nLen = strlen(reinterpret_cast<const char *>(pStr));
    if (nLen > nStr)
    {
        nLen = nStr;
    }

    // Look up in the parse cache.
    //
    const ASTNode *ast_ptr;
    std::shared_ptr<ASTNode> cache_holder;
    std::unique_ptr<ASTNode> parse_holder;

    if (nLen >= AST_CACHE_MIN_LEN)
    {
        std::string key(reinterpret_cast<const char *>(pStr), nLen);
        auto it = s_astCache.find(key);
        if (it != s_astCache.end())
        {
            // Cache hit — move to front of LRU.
            //
            cache_holder = it->second.ast;
            s_astLru.splice(s_astLru.begin(), s_astLru, it->second.lru_it);
            ast_ptr = cache_holder.get();
        }
        else
        {
            // Cache miss — parse and insert.
            //
            cache_holder = std::shared_ptr<ASTNode>(
                ast_parse_string(pStr, nLen).release());
            ast_ptr = cache_holder.get();

            // Evict LRU entries if cache is full.
            //
            while (s_astCache.size() >= AST_CACHE_MAX)
            {
                s_astCache.erase(s_astLru.back());
                s_astLru.pop_back();
            }

            s_astLru.push_front(key);
            s_astCache[key] = {cache_holder, s_astLru.begin()};
        }
    }
    else
    {
        // Short expressions — parse without caching.
        //
        parse_holder = ast_parse_string(pStr, nLen);
        ast_ptr = parse_holder.get();
    }

    // Evaluate the AST.
    //
    ast_eval_node(ast_ptr, buff, bufc,
        executor, caller, enactor, eval, cargs, ncargs);
}
