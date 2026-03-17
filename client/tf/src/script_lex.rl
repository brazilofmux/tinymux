// script_lex.rl — Ragel -G2 expression tokenizer.
//
// Tokenizes TF expression language: numeric/string literals, identifiers,
// operators (arithmetic, comparison, logical, assignment, string),
// grouping, and substitution markers.
//
// Build: ragel -G2 -o script_lex.cpp script_lex.rl

#include "script.h"
#include <cstdlib>
#include <cstring>

%%{
    machine script_lex;
    alphtype char;

    # --- Character classes ---
    d  = '0'..'9' ;
    xd = d | 'a'..'f' | 'A'..'F' ;
    al = 'a'..'z' | 'A'..'Z' | '_' ;
    an = al | d ;

    # --- Numeric literals ---
    hex_int   = '0' [xX] xd+ ;
    dec_int   = d+ ;
    float_lit = d+ '.' d* ([eE] [+\-]? d+)?
              | d* '.' d+ ([eE] [+\-]? d+)?
              | d+ [eE] [+\-]? d+ ;

    # --- String literals ---
    dq_char = [^"\\] | '\\' any ;
    sq_char = [^'\\] | '\\' any ;
    dq_string = '"' dq_char* '"' ;
    sq_string = "'" sq_char* "'" ;

    # --- Identifiers ---
    ident = al an* ;

    write data nofinal;

    main := |*

        # Whitespace — skip
        [ \t\r\n]+ => { /* skip */ };

        # Float before int for longest match
        float_lit => { emit_float(ts, te); };
        hex_int   => { emit_int(ts, te); };
        dec_int   => { emit_int(ts, te); };

        # String literals
        dq_string => { emit_string(ts, te); };
        sq_string => { emit_string(ts, te); };

        # Multi-char operators (longest match)
        ':='  => { emit(Tok::ASSIGN); };
        '+='  => { emit(Tok::PLUS_ASSIGN); };
        '-='  => { emit(Tok::MINUS_ASSIGN); };
        '*='  => { emit(Tok::STAR_ASSIGN); };
        '/='  => { emit(Tok::SLASH_ASSIGN); };
        '=='  => { emit(Tok::EQ); };
        '!='  => { emit(Tok::NE); };
        '<='  => { emit(Tok::LE); };
        '>='  => { emit(Tok::GE); };
        '=~'  => { emit(Tok::STREQ); };
        '!~'  => { emit(Tok::STRNE); };
        '=/'  => { emit(Tok::MATCH); };
        '!/'  => { emit(Tok::NMATCH); };
        '++'  => { emit(Tok::INC); };
        '--'  => { emit(Tok::DEC); };

        # Substitution
        '%{'  => { emit(Tok::PERCENT_LBRACE); };

        # Single-char operators
        '+'   => { emit(Tok::PLUS); };
        '-'   => { emit(Tok::MINUS); };
        '*'   => { emit(Tok::STAR); };
        '/'   => { emit(Tok::SLASH); };
        '<'   => { emit(Tok::LT); };
        '>'   => { emit(Tok::GT); };
        '&'   => { emit(Tok::AND); };
        '|'   => { emit(Tok::OR); };
        '!'   => { emit(Tok::NOT); };
        '('   => { emit(Tok::LPAREN); };
        ')'   => { emit(Tok::RPAREN); };
        '}'   => { emit(Tok::RBRACE); };
        ','   => { emit(Tok::COMMA); };
        '?'   => { emit(Tok::QUESTION); };
        ':'   => { emit(Tok::COLON); };

        # Bare '=' treated as '==' (TF compatibility)
        '='   => { emit(Tok::EQ); };

        # Identifiers (after operators so keywords aren't eaten)
        ident => { emit_ident(ts, te); };

        # Catch-all
        any   => { emit(Tok::ERROR); };

    *|;
}%%

void ScriptLexer::emit(Tok t) {
    tokens_.push_back({t, {}, 0, 0.0});
}

void ScriptLexer::emit_int(const char* ts, const char* te) {
    Token tok;
    tok.type = Tok::INT_LIT;
    tok.ival = strtoll(ts, nullptr, 0);  // handles 0x prefix
    tokens_.push_back(tok);
}

void ScriptLexer::emit_float(const char* ts, const char* te) {
    Token tok;
    tok.type = Tok::FLOAT_LIT;
    tok.fval = strtod(ts, nullptr);
    tokens_.push_back(tok);
}

void ScriptLexer::emit_string(const char* ts, const char* te) {
    // Strip quotes, process escape sequences
    Token tok;
    tok.type = Tok::STRING_LIT;
    const char* p = ts + 1;       // skip opening quote
    const char* end = te - 1;     // before closing quote
    std::string& s = tok.sval;
    s.reserve(end - p);
    while (p < end) {
        if (*p == '\\' && p + 1 < end) {
            p++;
            switch (*p) {
                case 'n':  s += '\n'; break;
                case 't':  s += '\t'; break;
                case 'r':  s += '\r'; break;
                case '\\': s += '\\'; break;
                case '"':  s += '"';  break;
                case '\'': s += '\''; break;
                default:   s += '\\'; s += *p; break;
            }
        } else {
            s += *p;
        }
        p++;
    }
    tokens_.push_back(std::move(tok));
}

void ScriptLexer::emit_ident(const char* ts, const char* te) {
    Token tok;
    tok.type = Tok::IDENT;
    tok.sval.assign(ts, te - ts);
    tokens_.push_back(std::move(tok));
}

void ScriptLexer::tokenize(const char* data, size_t len) {
    tokens_.clear();

    if (len == 0) {
        tokens_.push_back({Tok::END, {}, 0, 0.0});
        return;
    }

    const char* p = data;
    const char* pe = data + len;
    const char* eof = pe;
    const char* ts;
    const char* te;
    int cs, act;

    (void)script_lex_en_main;  // suppress unused warning

    %% write init;
    %% write exec;

    // Append END sentinel
    tokens_.push_back({Tok::END, {}, 0, 0.0});
}
