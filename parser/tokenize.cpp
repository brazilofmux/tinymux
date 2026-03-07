/*
 * tokenize.cpp - MUX expression tokenizer study tool.
 *
 * Reads MUX expressions from stdin (one per line) and emits a token
 * stream to stdout.  This is Stage 1 of the parser study: understand
 * what tokens the existing mux_exec would encounter.
 *
 * Token types:
 *   LIT     - literal text (no special characters)
 *   FUNC    - function name (followed by LPAREN)
 *   LPAREN  - (
 *   RPAREN  - )
 *   LBRACK  - [
 *   RBRACK  - ]
 *   LBRACE  - {
 *   RBRACE  - }
 *   COMMA   - , (argument separator)
 *   SEMI    - ; (command separator)
 *   PCT     - %-substitution (%0, %q0, %r, etc.)
 *   ESC     - \-escape
 *   SPACE   - whitespace run
 */

#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>

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

static const char *token_name(TokenType t)
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

// Known MUX built-in function names (subset for study purposes).
// A real implementation would load these from the server's function table.
//
static bool is_func_char(char c)
{
    return isalnum(static_cast<unsigned char>(c)) || c == '_';
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
            // Look backwards: is the preceding token a LIT that could be
            // a function name?
            //
            if (!tokens.empty() && tokens.back().type == TOK_LIT) {
                // Reclassify the preceding literal as a function name.
                //
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
            // %-substitution.  Grab the % and what follows.
            //
            std::string sub;
            sub += *p++;
            if (*p == 'q' || *p == 'Q') {
                sub += *p++;
                if (*p == '<') {
                    // %q<name>
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
            } else if (*p) {
                sub += *p++;
            }
            tokens.push_back({TOK_PCT, sub});
        } else if (*p == '\\') {
            // Escape sequence.
            //
            std::string esc;
            esc += *p++;
            if (*p) {
                esc += *p++;
            }
            tokens.push_back({TOK_ESC, esc});
        } else if (*p == ' ' || *p == '\t') {
            // Whitespace run.
            //
            std::string sp;
            while (*p == ' ' || *p == '\t') {
                sp += *p++;
            }
            tokens.push_back({TOK_SPACE, sp});
        } else {
            // Literal text — accumulate until we hit a special character.
            //
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

int main()
{
    char line[8192];
    while (fgets(line, sizeof(line), stdin)) {
        // Strip trailing newline.
        //
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        printf("INPUT: %s\n", line);
        auto tokens = tokenize(line);
        for (const auto &tok : tokens) {
            if (tok.type == TOK_EOF) {
                printf("  EOF\n");
            } else {
                printf("  %-7s \"%s\"\n", token_name(tok.type), tok.text.c_str());
            }
        }
        printf("\n");
    }
    return 0;
}
