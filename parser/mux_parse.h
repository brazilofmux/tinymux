/*
 * mux_parse.h - Shared tokenizer, parser, and AST definitions.
 *
 * This header is included by the study tools (tokenize.cpp, parse.cpp,
 * eval.cpp) and will eventually be the basis for the production parser
 * in mux/src/ast.h.
 */

#ifndef MUX_PARSE_H
#define MUX_PARSE_H

#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <memory>

enum ParserProfile {
    PROFILE_MUX214_AST,
    PROFILE_MUX213_COMPAT,
    PROFILE_PENN
};

static const char *profile_name(ParserProfile profile)
{
    switch (profile) {
    case PROFILE_MUX214_AST:   return "mux214";
    case PROFILE_MUX213_COMPAT:return "mux213";
    case PROFILE_PENN:         return "penn";
    }
    return "unknown";
}

static bool parse_profile_string(const char *text, ParserProfile &profile)
{
    if (0 == strcmp(text, "mux214") || 0 == strcmp(text, "2.14") || 0 == strcmp(text, "ast")) {
        profile = PROFILE_MUX214_AST;
        return true;
    }
    if (0 == strcmp(text, "mux213") || 0 == strcmp(text, "2.13") || 0 == strcmp(text, "legacy")) {
        profile = PROFILE_MUX213_COMPAT;
        return true;
    }
    if (0 == strcmp(text, "penn") || 0 == strcmp(text, "pennmush")) {
        profile = PROFILE_PENN;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------
// Token types
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

inline const char *token_name(TokenType t)
{
    switch (t) {
    case TOK_LIT:    return "LIT";
    case TOK_FUNC:   return "FUNC";
    case TOK_LPAREN: return "LPAREN";
    case TOK_RPAREN: return "RPAREN";
    case TOK_LBRACK: return "LBRACK";
    case TOK_RBRACK: return "RBRACK";
    case TOK_LBRACE: return "LBRACE";
    case TOK_RBRACE: return "RBRACE";
    case TOK_COMMA:  return "COMMA";
    case TOK_SEMI:   return "SEMI";
    case TOK_PCT:    return "PCT";
    case TOK_ESC:    return "ESC";
    case TOK_SPACE:  return "SPACE";
    case TOK_EOF:    return "EOF";
    }
    return "???";
}

// ---------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------

// Gather a %-substitution sequence following the L2 dispatch table
// in eval.cpp. On entry, p points to the character AFTER '%'.
// On return, p points past the last consumed character.
//
// Gather an angle-bracket delimited sequence: <...>
// On entry, p points at '<'.  On return, p points past '>'.
//
static void gather_angle(const char *&p, std::string &out)
{
    out += *p++;  // '<'
    while (*p && *p != '>') {
        out += *p++;
    }
    if (*p == '>') {
        out += *p++;
    }
}

static std::string gather_pct(const char *&p, ParserProfile profile)
{
    std::string sub("%");
    char ch = *p;

    if (!ch) {
        return sub;
    }

    // Penn: %<space> is an atomic substitution → "% "
    if (profile == PROFILE_PENN && ch == ' ') {
        sub += *p++;
        return sub;
    }

    char upper = static_cast<char>(toupper(static_cast<unsigned char>(ch)));

    if (ch >= '0' && ch <= '9') {
        // %0–%9: command arguments
        sub += *p++;
    } else if (upper == 'Q') {
        // %q0–%qz, %q<name>
        sub += *p++;
        if (*p == '<') {
            gather_angle(p, sub);
        } else if (*p) {
            sub += *p++;
        }
    } else if (upper == 'V') {
        // %va–%vz: variable attributes
        sub += *p++;
        if (*p && isalpha(static_cast<unsigned char>(*p))) {
            sub += *p++;
        }
    } else if (upper == 'W' && profile == PROFILE_PENN) {
        // Penn: %wa–%wz (W-attributes).
        sub += *p++;
        if (*p && isalpha(static_cast<unsigned char>(*p))) {
            sub += *p++;
        }
    } else if (upper == 'C' || upper == 'X') {
        if (profile == PROFILE_PENN) {
            // Penn: %c is a single-character substitution (raw command).
            // Penn: %x<letter> is an X-attribute lookup.
            sub += *p++;
            if (upper == 'X' && *p && isalpha(static_cast<unsigned char>(*p))) {
                sub += *p++;
            }
        } else {
            // MUX: %cx/%xx color codes, %c<rgb>/%x<rgb> extended.
            sub += *p++;
            if (*p == '<') {
                gather_angle(p, sub);
            } else if (*p) {
                sub += *p++;
            }
        }
    } else if (ch == '=') {
        // %=, %=<attr>, %=<N>
        sub += *p++;
        if (*p == '<') {
            gather_angle(p, sub);
        }
    } else if (upper == 'I') {
        // %i0–%i9: loop itext at depth
        // Penn also has %iL for current level
        sub += *p++;
        if (*p && (*p >= '0' && *p <= '9')) {
            // Consume digit or 'L'
            sub += *p++;
        } else if (profile == PROFILE_PENN
                && *p
                && toupper(static_cast<unsigned char>(*p)) == 'L') {
            sub += *p++;
        }
    } else if (ch == '$' && profile == PROFILE_PENN) {
        // Penn: %$0–%$9, %$L — stack variables
        sub += *p++;
        if (*p && ((*p >= '0' && *p <= '9')
                   || toupper(static_cast<unsigned char>(*p)) == 'L')) {
            sub += *p++;
        }
    } else {
        // Single-character form: %%, %#, %!, %@, %r, %b, %t, %n,
        // %s, %p, %o, %a, %k, %l, %m, %|, %+, %:, %~, %?, %u, etc.
        sub += *p++;
    }

    return sub;
}

static std::vector<Token> tokenize(const char *input,
                                   ParserProfile profile = PROFILE_MUX214_AST)
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
        } else if (*p == '#' && (p[1] == '#' || p[1] == '@' || p[1] == '$')) {
            // Hash forms: ##, #@, #$ (MUX-only loop/switch substitutions)
            std::string hash;
            hash += *p++;
            hash += *p++;
            tokens.push_back({TOK_PCT, hash});
        } else if (*p == '%') {
            p++;
            tokens.push_back({TOK_PCT, gather_pct(p, profile)});
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
// AST node types
// ---------------------------------------------------------------

enum NodeType {
    NODE_SEQUENCE,      // Ordered list of children
    NODE_LITERAL,       // Plain text
    NODE_SPACE,         // Whitespace run
    NODE_SUBST,         // %-substitution
    NODE_ESCAPE,        // \-escape
    NODE_FUNCCALL,      // function(args...) — name is static string
    NODE_DYNCALL,       // Dynamic function call (unsupported in new path)
    NODE_EVALBRACKET,   // [expression]
    NODE_BRACEGROUP,    // {deferred expression}
    NODE_SEMICOLON,     // ; command separator
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

static const char *node_name(NodeType t)
{
    switch (t) {
    case NODE_SEQUENCE:    return "Seq";
    case NODE_LITERAL:     return "Lit";
    case NODE_SPACE:       return "Sp";
    case NODE_SUBST:       return "Sub";
    case NODE_ESCAPE:      return "Esc";
    case NODE_FUNCCALL:    return "Call";
    case NODE_DYNCALL:     return "DynCall";
    case NODE_EVALBRACKET: return "Eval";
    case NODE_BRACEGROUP:  return "Brace";
    case NODE_SEMICOLON:   return "Semi";
    }
    return "???";
}

// Reconstruct the raw source text from an AST subtree.
//
static std::string ast_raw_text(const ASTNode *n)
{
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
            r += ast_raw_text(n->children[i].get());
        }
        return r + ")";
    }
    case NODE_DYNCALL:
        return "#-1 DYNAMIC";
    case NODE_EVALBRACKET: {
        std::string r = "[";
        for (const auto &c : n->children) r += ast_raw_text(c.get());
        return r + "]";
    }
    case NODE_BRACEGROUP: {
        std::string r = "{";
        for (const auto &c : n->children) r += ast_raw_text(c.get());
        return r + "}";
    }
    case NODE_SEQUENCE: {
        std::string r;
        for (const auto &c : n->children) r += ast_raw_text(c.get());
        return r;
    }
    }
    return "";
}

// Print an AST tree for debugging.
//
static void ast_print(const ASTNode *node, int indent)
{
    if (!node) {
        printf("%*s(null)\n", indent, "");
        return;
    }

    printf("%*s%s", indent, "", node_name(node->type));

    if (!node->text.empty() && node->children.empty()) {
        printf(" \"%s\"", node->text.c_str());
    } else if ((node->type == NODE_FUNCCALL) && !node->text.empty()) {
        printf(" \"%s\"", node->text.c_str());
        if (!node->children.empty()) {
            printf("  [%zu args]", node->children.size());
        }
    }

    printf("\n");

    for (const auto &child : node->children) {
        ast_print(child.get(), indent + 2);
    }
}

// ---------------------------------------------------------------
// Recursive-descent parser
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

#endif // MUX_PARSE_H
