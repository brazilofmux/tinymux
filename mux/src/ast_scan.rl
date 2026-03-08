/*! \file ast_scan.rl
 * \brief Ragel-generated scanner for MUX expression tokenizer.
 *
 * This file is processed by Ragel to generate ast_scan.cpp.
 * Do not edit ast_scan.cpp directly.
 *
 * Build: ragel -G2 -o ast_scan.cpp ast_scan.rl
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "ast.h"

#include <string_view>
#include <vector>

// Helper: construct token text view from Ragel ts/te pointers.
// Views point directly into the input buffer — no allocation.
//
#define TOK_TEXT() std::string_view(reinterpret_cast<const char *>(ts), \
                                    static_cast<size_t>(te - ts))

%%{
    machine ast_scanner;

    alphtype unsigned char;

    # ---------------------------------------------------------------
    # Character classes
    # ---------------------------------------------------------------

    # Plain literal characters: everything except structural tokens,
    # %-sub starters, escape, space, NUL, and '#' (handled separately).
    #
    lit_plain = [^[\]{}(),;%\\ \0#];

    # After '#', these characters trigger hash tokens (##, #@, #$).
    # Anything else after '#' is part of a literal.
    #
    lit_after_hash = lit_plain - [@$];

    # A literal unit: either a plain char, or '#' followed by a char
    # that does NOT trigger a hash token.
    #
    lit_char = lit_plain | ( '#' lit_after_hash );

    # Angle-bracket body: everything up to a structural delimiter.
    # Stops at: > } ) ] [ { ,
    #
    angle_body = [^>\}\)\]\[\{,\0]*;

    # ---------------------------------------------------------------
    # Scanner
    # ---------------------------------------------------------------

    main := |*

        # %-substitutions with angle brackets (longest match wins).
        #
        '%' [qQ] '<' angle_body '>'        => {
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        };
        '%' [qQ] '<' angle_body            => {
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        };
        '%' [cCxX] '<' angle_body '>'      => {
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        };
        '%' [cCxX] '<' angle_body          => {
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        };
        '%' '=' '<' angle_body '>'         => {
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        };
        '%' '=' '<' angle_body             => {
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        };

        # %-substitutions (fixed length, 2-3 bytes total).
        #
        '%' [0-9]                           => {
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        };
        '%' [qQ] any                        => {
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        };
        '%' [vV] alpha                      => {
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        };
        '%' [cCxX] any                      => {
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        };
        '%' [iI] [0-9]                      => {
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        };
        '%' '='                             => {
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        };
        '%' any                             => {
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        };
        '%'                                 => {
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        };

        # Hash tokens: ##, #@, #$
        #
        '##' | '#@' | '#$'                  => {
            tokens.push_back({ASTTOK_PCT, TOK_TEXT()});
        };

        # Escape sequences: \ followed by one character.
        #
        '\\' any                            => {
            tokens.push_back({ASTTOK_ESC, TOK_TEXT()});
        };
        '\\'                                => {
            tokens.push_back({ASTTOK_ESC, TOK_TEXT()});
        };

        # Structural tokens.
        #
        '['                                 => {
            tokens.push_back({ASTTOK_LBRACK, TOK_TEXT()});
        };
        ']'                                 => {
            tokens.push_back({ASTTOK_RBRACK, TOK_TEXT()});
        };
        '{'                                 => {
            tokens.push_back({ASTTOK_LBRACE, TOK_TEXT()});
        };
        '}'                                 => {
            tokens.push_back({ASTTOK_RBRACE, TOK_TEXT()});
        };
        '('                                 => {
            if (  !tokens.empty()
               && tokens.back().type == ASTTOK_LIT)
            {
                tokens.back().type = ASTTOK_FUNC;
            }
            tokens.push_back({ASTTOK_LPAREN, TOK_TEXT()});
        };
        ')'                                 => {
            tokens.push_back({ASTTOK_RPAREN, TOK_TEXT()});
        };
        ','                                 => {
            tokens.push_back({ASTTOK_COMMA, TOK_TEXT()});
        };
        ';'                                 => {
            tokens.push_back({ASTTOK_SEMI, TOK_TEXT()});
        };

        # Whitespace runs (only ASCII space).
        #
        ' '+                                => {
            tokens.push_back({ASTTOK_SPACE, TOK_TEXT()});
        };

        # Literal text runs.
        #
        lit_char+                           => {
            tokens.push_back({ASTTOK_LIT, TOK_TEXT()});
        };

        # Standalone '#' (at end of input, or followed by structural
        # or special character that prevents a hash token or literal
        # continuation).
        #
        '#'                                 => {
            tokens.push_back({ASTTOK_LIT, TOK_TEXT()});
        };

    *|;
}%%

// Ragel state table data.
//
%% write data nofinal;

std::vector<ASTToken> ast_tokenize(const UTF8 *input, size_t nLen)
{
    std::vector<ASTToken> tokens;

    // Find actual length (stop at NUL or nLen).
    //
    size_t actualLen = 0;
    while (actualLen < nLen && input[actualLen] != '\0')
    {
        actualLen++;
    }

    if (0 == actualLen)
    {
        tokens.push_back({ASTTOK_EOF, ""});
        return tokens;
    }

    // Ragel scanner variables.
    //
    const UTF8 *p   = input;
    const UTF8 *pe  = input + actualLen;
    const UTF8 *eof = pe;
    const UTF8 *ts;
    const UTF8 *te;
    int cs;
    int act;

    %% write init;
    %% write exec;

    tokens.push_back({ASTTOK_EOF, ""});
    return tokens;
}
