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
// mux_exec2 — drop-in replacement
// ---------------------------------------------------------------

// This is the integration point. For now, it simply parses and
// evaluates using the existing mux_exec. The actual AST evaluator
// will be wired in here once it's complete.
//
// Phase 1: Parse, but evaluate via mux_exec (validate parse is correct)
// Phase 2: Parse + AST evaluate, compare with mux_exec
// Phase 3: Parse + AST evaluate only
//
void mux_exec2(const UTF8 *pStr, size_t nStr,
               UTF8 *buff, UTF8 **bufc,
               dbref executor, dbref caller, dbref enactor,
               int eval, const UTF8 *cargs[], int ncargs)
{
    // Phase 1: Just forward to mux_exec.
    // This lets us add ast.cpp to the build without changing behavior.
    //
    mux_exec(pStr, nStr, buff, bufc, executor, caller, enactor,
             eval, cargs, ncargs);
}
