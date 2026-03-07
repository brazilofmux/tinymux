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
 *   PCT     - %-substitution (%0, %q0, %r, %c<rgb>, %=<attr>, etc.)
 *   ESC     - \-escape
 *   SPACE   - whitespace run
 *
 * This mirrors the character classifications in eval.cpp's isSpecial
 * tables L1-L4.  The %-substitution handling follows the L2 dispatch
 * table exactly: multi-character sequences like %q<name>, %=<attr>,
 * %c<rgb>, %vA, and %i0 are gathered into a single PCT token.
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

// Gather a %-substitution sequence.  This follows the L2 dispatch
// table in eval.cpp.  On entry, p points to the character AFTER '%'.
// On return, p points past the last consumed character.
//
static std::string gather_pct(const char *&p)
{
    std::string sub("%");
    char ch = *p;

    if (!ch) {
        // Bare % at end of string.
        return sub;
    }

    char upper = static_cast<char>(toupper(static_cast<unsigned char>(ch)));

    if (ch >= '0' && ch <= '9') {
        // %0-%9: command argument
        sub += *p++;

    } else if (upper == 'Q') {
        // %q0-%qz or %q<name>
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
        // %va-%vz: variable attributes
        sub += *p++;
        if (*p && isalpha(static_cast<unsigned char>(*p))) {
            sub += *p++;
        }

    } else if (upper == 'C' || upper == 'X') {
        // %cn, %ch, %c<r,g,b>: color codes
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
        // %=<attr> or %=<nnn>: attribute/arg shorthand
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
        // %i0, %i1, etc.: iterator text
        sub += *p++;
        if (*p && *p >= '0' && *p <= '9') {
            sub += *p++;
        }

    } else {
        // Single-character substitutions:
        // %# %! %@ %r %b %t %l %n %s %o %p %a %m %k %% %| %: %+
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
            // Look backwards: is the preceding token a LIT that could be
            // a function name?  This mirrors mux_exec's backwards scan of
            // the output buffer.
            //
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
            // Escape: \ followed by the next character.
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
            // These are the L1 specials: NUL SP % ( [ \ {
            // plus the structural characters: ) ] } , ;
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
