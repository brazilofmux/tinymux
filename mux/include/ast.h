/*! \file ast.h
 * \brief AST-based expression parser and evaluator.
 *
 * This provides mux_parse() and mux_eval() as the future replacement
 * for mux_exec(). During transition, mux_exec() is a drop-in
 * replacement that combines parse + eval.
 *
 * Design constraints:
 *   - No ## / #@ / #$ substitutions (use %i0-%i9 instead)
 *   - No dynamic function calls (computed function names)
 *   - All function names are statically known at parse time
 *   - FN_NOEVAL function calls preserve deferred-region metadata
 *     alongside parse-time AST children
 *
 * The AST is cacheable: parsing is pure (no side effects, no database
 * access). The same cached AST can be evaluated with different contexts.
 */

#ifndef AST_H
#define AST_H

#include <string>
#include <string_view>
#include <vector>
#include <memory>

// ---------------------------------------------------------------
// AST node types
// ---------------------------------------------------------------

enum ASTNodeType {
    AST_SEQUENCE,       // Ordered list of children
    AST_LITERAL,        // Plain text
    AST_SPACE,          // Whitespace run
    AST_SUBST,          // %-substitution
    AST_ESCAPE,         // \-escape
    AST_FUNCCALL,       // function(args...) — name is static string
    AST_EVALBRACKET,    // [expression]
    AST_BRACEGROUP,     // {deferred expression}
    AST_SEMICOLON,      // ; command separator
};

enum ASTNoevalKind {
    ASTNOEVAL_NONE,
    ASTNOEVAL_IFELSE,
    ASTNOEVAL_SWITCH,
    ASTNOEVAL_CASE,
    ASTNOEVAL_SWITCHALL,
    ASTNOEVAL_CASEALL,
    ASTNOEVAL_ITER,
    ASTNOEVAL_CAND,
    ASTNOEVAL_CANDBOOL,
    ASTNOEVAL_COR,
    ASTNOEVAL_CORBOOL,
    ASTNOEVAL_ULAMBDA
};

struct ASTDeferredArg {
    std::string raw_text;
    bool is_deferred;

    ASTDeferredArg(std::string_view raw = "", bool deferred = false)
        : raw_text(raw), is_deferred(deferred) {}
};

struct ASTNode {
    ASTNodeType type;
    std::string text;
    std::vector<std::unique_ptr<ASTNode>> children;
    std::vector<ASTDeferredArg> deferred_args; // FUNCCALL arg metadata
    ASTNoevalKind noeval_kind;
    bool parser_known_noeval;
    bool has_close_paren;   // FUNCCALL: true if ')' was found
    bool has_close_bracket; // EVALBRACKET: true if ']' was found
    bool has_close_brace;   // BRACEGROUP: true if '}' was found

    ASTNode(ASTNodeType t, std::string_view s = "")
        : type(t), text(s), noeval_kind(ASTNOEVAL_NONE),
          parser_known_noeval(false), has_close_paren(true),
          has_close_bracket(true), has_close_brace(true) {}

    void addChild(std::unique_ptr<ASTNode> child) {
        children.push_back(std::move(child));
    }
};

// ---------------------------------------------------------------
// Source regions and lexer modes
// ---------------------------------------------------------------

enum ASTLexMode {
    ASTLEX_EVAL,
    ASTLEX_NOEVAL,
    ASTLEX_STRUCTURAL
};

struct ASTSourceSpan {
    const UTF8 *input;
    size_t nLen;

    ASTSourceSpan(const UTF8 *p = nullptr, size_t n = 0)
        : input(p), nLen(n) {}
};

// ---------------------------------------------------------------
// Token types (internal to parser, but exposed for testing)
// ---------------------------------------------------------------

enum ASTTokenType {
    ASTTOK_LIT,
    ASTTOK_FUNC,
    ASTTOK_LPAREN,
    ASTTOK_RPAREN,
    ASTTOK_LBRACK,
    ASTTOK_RBRACK,
    ASTTOK_LBRACE,
    ASTTOK_RBRACE,
    ASTTOK_COMMA,
    ASTTOK_SEMI,
    ASTTOK_PCT,
    ASTTOK_ESC,
    ASTTOK_SPACE,
    ASTTOK_EOF
};

struct ASTToken {
    ASTTokenType type;
    std::string_view text;
};

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------

// Tokenize a MUX expression string into a token stream.
//
std::vector<ASTToken> ast_tokenize(const UTF8 *input, size_t nLen);

// Tokenize a MUX expression region under an explicit lexer mode.
//
// Phase 1 note:
// This is currently a naming/API scaffold. The initial implementation
// may still share behavior with the legacy whole-input tokenizer until
// parser-controlled mode switching is wired through.
//
std::vector<ASTToken> ast_tokenize_mode(const UTF8 *input, size_t nLen,
                                        ASTLexMode mode);

// Parse a token stream into an AST.
//
std::unique_ptr<ASTNode> ast_parse(const std::vector<ASTToken> &tokens);

// Parse a MUX expression string directly into an AST.
// Convenience wrapper: tokenize + parse.
//
std::unique_ptr<ASTNode> ast_parse_string(const UTF8 *input, size_t nLen);

// Parse a source region directly into an AST under an explicit lexer
// mode. This is the entrypoint intended for deferred-region reparsing.
//
std::unique_ptr<ASTNode> ast_parse_region(ASTSourceSpan span,
                                          ASTLexMode mode);

// Reconstruct the raw source text from an AST subtree.
//
std::string ast_raw_text(const ASTNode *node);

// Print an AST tree for debugging.
//
void ast_dump(const ASTNode *node, int indent = 0);

// ---------------------------------------------------------------
// mux_exec — drop-in replacement for mux_exec
// ---------------------------------------------------------------

// Parse + evaluate in one call. Same signature as mux_exec.
// During transition, both mux_exec and mux_exec coexist.
//
void mux_exec(const UTF8 *pStr, size_t nStr,
               UTF8 *buff, UTF8 **bufc,
               dbref executor, dbref caller, dbref enactor,
               int eval, const UTF8 *cargs[], int ncargs);

// Parse + evaluate via the AST evaluator only, bypassing JIT.
//
void ast_exec(const UTF8 *pStr, size_t nStr,
              UTF8 *buff, UTF8 **bufc,
              dbref executor, dbref caller, dbref enactor,
              int eval, const UTF8 *cargs[], int ncargs);

#endif // AST_H
