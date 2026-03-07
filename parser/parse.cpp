/*
 * parse.cpp - MUX expression recursive-descent parser study tool.
 *
 * Reads MUX expressions from stdin (one per line), tokenizes them,
 * then builds an AST (Abstract Syntax Tree) and prints it.
 *
 * This is Stage 2 of the parser study: can we build a tree that
 * faithfully represents MUX softcode structure?
 *
 * AST node types:
 *   Sequence     - ordered list of child nodes
 *   Literal      - plain text
 *   Space        - whitespace (kept separate for space compression)
 *   Substitution - %-substitution (%0, %q0, %q<name>, etc.)
 *   Escape       - \-escape sequence
 *   FuncCall     - static function call: name + arg list
 *   DynCall      - dynamic function call: preceding node + arg list
 *   EvalBracket  - [...] evaluation bracket (contents are a Sequence)
 *   BraceGroup   - {...} deferred evaluation (contents are a Sequence)
 *
 * Key design decisions:
 *
 * 1. FuncCall vs DynCall: When the tokenizer identifies a FUNC token
 *    (literal text immediately before '('), we emit a FuncCall with
 *    the static name. When '(' follows a non-literal (e.g., a PCT
 *    like %q0), we emit a DynCall — the function name will be
 *    determined at runtime from the preceding expression's value.
 *
 * 2. BraceGroup contents: In MUX, {braced text} is deferred — not
 *    evaluated until a command handler explicitly evaluates it. We
 *    still parse the interior structure so the AST captures nesting,
 *    but a future interpreter would skip evaluation of BraceGroup
 *    children unless instructed otherwise.
 *
 * 3. EvalBracket: Contents of [...] are fully evaluated in MUX with
 *    EV_FCHECK | EV_FMAND. Our parser recursively parses the bracket
 *    interior the same way it parses top-level expressions.
 *
 * 4. Argument lists: Function arguments are comma-separated sequences.
 *    Each argument is itself a Sequence that can contain any node type.
 *    Nesting of () [] {} is respected when scanning for ',' and ')'.
 */

#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <memory>

// ---------------------------------------------------------------
// Token types and tokenizer (same as tokenize.cpp)
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
// AST node types
// ---------------------------------------------------------------

enum NodeType {
    NODE_SEQUENCE,      // Ordered list of children
    NODE_LITERAL,       // Plain text
    NODE_SPACE,         // Whitespace run
    NODE_SUBST,         // %-substitution
    NODE_ESCAPE,        // \-escape
    NODE_FUNCCALL,      // Static function call: name(args...)
    NODE_DYNCALL,       // Dynamic function call: <expr>(args...)
    NODE_EVALBRACKET,   // [expression]
    NODE_BRACEGROUP,    // {deferred expression}
    NODE_SEMICOLON,     // ; command separator
};

struct ASTNode {
    NodeType type;
    std::string text;                           // For leaves: the token text
    std::vector<std::unique_ptr<ASTNode>> children;  // For compound nodes

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

// ---------------------------------------------------------------
// Parser
// ---------------------------------------------------------------

class Parser {
public:
    Parser(const std::vector<Token> &tokens)
        : m_tokens(tokens), m_pos(0) {}

    // Parse the entire token stream into a Sequence node.
    //
    std::unique_ptr<ASTNode> parse() {
        return parseSequence(/*stopAtRParen=*/false,
                             /*stopAtRBrack=*/false,
                             /*stopAtRBrace=*/false,
                             /*stopAtComma=*/false);
    }

private:
    const std::vector<Token> &m_tokens;
    size_t m_pos;

    const Token &peek() const {
        return m_tokens[m_pos];
    }

    Token advance() {
        return m_tokens[m_pos++];
    }

    bool atEnd() const {
        return m_pos >= m_tokens.size() || m_tokens[m_pos].type == TOK_EOF;
    }

    // Parse a sequence of nodes until we hit a stop condition or EOF.
    // Stop tokens are consumed by the caller, not by this function.
    //
    std::unique_ptr<ASTNode> parseSequence(bool stopAtRParen,
                                            bool stopAtRBrack,
                                            bool stopAtRBrace,
                                            bool stopAtComma)
    {
        auto seq = std::make_unique<ASTNode>(NODE_SEQUENCE);

        while (!atEnd()) {
            TokenType t = peek().type;

            // Check stop conditions — don't consume the stop token.
            //
            if (stopAtRParen && t == TOK_RPAREN) break;
            if (stopAtRBrack && t == TOK_RBRACK) break;
            if (stopAtRBrace && t == TOK_RBRACE) break;
            if (stopAtComma  && t == TOK_COMMA)  break;

            auto node = parseOne();
            if (node) {
                seq->addChild(std::move(node));
            }
        }

        // Flatten: if the sequence has exactly one child, return that child.
        //
        if (seq->children.size() == 1) {
            return std::move(seq->children[0]);
        }

        return seq;
    }

    // Parse a single node (may consume multiple tokens for compound nodes).
    //
    std::unique_ptr<ASTNode> parseOne() {
        const Token &tok = peek();

        switch (tok.type) {
        case TOK_LIT: {
            auto node = std::make_unique<ASTNode>(NODE_LITERAL, tok.text);
            advance();
            return node;
        }

        case TOK_SPACE: {
            auto node = std::make_unique<ASTNode>(NODE_SPACE, tok.text);
            advance();
            return node;
        }

        case TOK_PCT: {
            auto node = std::make_unique<ASTNode>(NODE_SUBST, tok.text);
            advance();

            // Check if this substitution is followed by '(' — that's a
            // dynamic function call. The function name will be determined
            // at runtime from the substitution's value.
            //
            if (!atEnd() && peek().type == TOK_LPAREN) {
                return parseDynCall(std::move(node));
            }

            return node;
        }

        case TOK_ESC: {
            auto node = std::make_unique<ASTNode>(NODE_ESCAPE, tok.text);
            advance();
            return node;
        }

        case TOK_SEMI: {
            auto node = std::make_unique<ASTNode>(NODE_SEMICOLON, tok.text);
            advance();
            return node;
        }

        case TOK_FUNC:
            return parseFuncCall();

        case TOK_LBRACK:
            return parseEvalBracket();

        case TOK_LBRACE:
            return parseBraceGroup();

        // These shouldn't appear at top level if the input is well-formed,
        // but handle gracefully.
        //
        case TOK_RPAREN:
        case TOK_RBRACK:
        case TOK_RBRACE:
        case TOK_COMMA: {
            auto node = std::make_unique<ASTNode>(NODE_LITERAL, tok.text);
            advance();
            return node;
        }

        case TOK_LPAREN: {
            // Bare '(' not preceded by a function name.
            auto node = std::make_unique<ASTNode>(NODE_LITERAL, tok.text);
            advance();
            return node;
        }

        case TOK_EOF:
            return nullptr;
        }

        return nullptr;
    }

    // Parse a static function call: FUNC LPAREN args RPAREN
    //
    // The FUNC token has already been identified by the tokenizer.
    // Children of the FuncCall node are the arguments, each a Sequence.
    //
    std::unique_ptr<ASTNode> parseFuncCall() {
        Token funcTok = advance();  // consume FUNC
        auto call = std::make_unique<ASTNode>(NODE_FUNCCALL, funcTok.text);

        if (atEnd() || peek().type != TOK_LPAREN) {
            // No '(' — degenerate case, treat as literal.
            call->type = NODE_LITERAL;
            return call;
        }
        advance();  // consume LPAREN

        // Parse comma-separated arguments until RPAREN or EOF.
        //
        parseArgList(call.get());

        return call;
    }

    // Parse a dynamic function call: <preceding_node> LPAREN args RPAREN
    //
    // The preceding node (e.g., a PCT substitution) has already been parsed.
    // We wrap it as the first child of a DynCall node, then parse the arglist.
    //
    std::unique_ptr<ASTNode> parseDynCall(std::unique_ptr<ASTNode> nameExpr) {
        auto call = std::make_unique<ASTNode>(NODE_DYNCALL);
        call->addChild(std::move(nameExpr));  // child[0] = name expression

        advance();  // consume LPAREN

        parseArgList(call.get());

        return call;
    }

    // Parse arguments: comma-separated sequences until ')' or EOF.
    //
    void parseArgList(ASTNode *call) {
        // First argument (may be empty).
        //
        auto arg = parseSequence(/*stopAtRParen=*/true,
                                  /*stopAtRBrack=*/false,
                                  /*stopAtRBrace=*/false,
                                  /*stopAtComma=*/true);
        call->addChild(std::move(arg));

        // Subsequent arguments separated by commas.
        //
        while (!atEnd() && peek().type == TOK_COMMA) {
            advance();  // consume COMMA
            arg = parseSequence(/*stopAtRParen=*/true,
                                 /*stopAtRBrack=*/false,
                                 /*stopAtRBrace=*/false,
                                 /*stopAtComma=*/true);
            call->addChild(std::move(arg));
        }

        // Consume the closing ')' if present.
        //
        if (!atEnd() && peek().type == TOK_RPAREN) {
            advance();
        }
    }

    // Parse an evaluation bracket: [ contents ]
    //
    std::unique_ptr<ASTNode> parseEvalBracket() {
        advance();  // consume LBRACK

        auto bracket = std::make_unique<ASTNode>(NODE_EVALBRACKET);
        auto contents = parseSequence(/*stopAtRParen=*/false,
                                       /*stopAtRBrack=*/true,
                                       /*stopAtRBrace=*/false,
                                       /*stopAtComma=*/false);
        bracket->addChild(std::move(contents));

        if (!atEnd() && peek().type == TOK_RBRACK) {
            advance();  // consume RBRACK
        }

        return bracket;
    }

    // Parse a brace group: { contents }
    //
    // In MUX, braces delimit deferred evaluation. We still parse the
    // interior to capture structure, but a future interpreter would
    // not evaluate BraceGroup children unless explicitly requested.
    //
    std::unique_ptr<ASTNode> parseBraceGroup() {
        advance();  // consume LBRACE

        auto group = std::make_unique<ASTNode>(NODE_BRACEGROUP);
        auto contents = parseSequence(/*stopAtRParen=*/false,
                                       /*stopAtRBrack=*/false,
                                       /*stopAtRBrace=*/true,
                                       /*stopAtComma=*/false);
        group->addChild(std::move(contents));

        if (!atEnd() && peek().type == TOK_RBRACE) {
            advance();  // consume RBRACE
        }

        return group;
    }
};

// ---------------------------------------------------------------
// AST printer
// ---------------------------------------------------------------

static void printAST(const ASTNode *node, int indent)
{
    if (!node) {
        printf("%*s(null)\n", indent, "");
        return;
    }

    printf("%*s%s", indent, "", node_name(node->type));

    // For leaf nodes with text, print the text.
    //
    if (!node->text.empty() && node->children.empty()) {
        printf(" \"%s\"", node->text.c_str());
    }
    // For function calls, print the name.
    //
    else if ((node->type == NODE_FUNCCALL) && !node->text.empty()) {
        printf(" \"%s\"", node->text.c_str());
        if (!node->children.empty()) {
            printf("  [%zu args]", node->children.size());
        }
    }

    printf("\n");

    for (const auto &child : node->children) {
        printAST(child.get(), indent + 2);
    }
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------

int main()
{
    char line[8192];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        printf("INPUT: %s\n", line);
        auto tokens = tokenize(line);
        Parser parser(tokens);
        auto ast = parser.parse();
        printAST(ast.get(), 2);
        printf("\n");
    }
    return 0;
}
