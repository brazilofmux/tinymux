/*! \file ast.h
 * \brief AST-based expression parser and evaluator.
 *
 * This provides mux_parse() and mux_eval() as the future replacement
 * for mux_exec(). During transition, mux_exec2() is a drop-in
 * replacement that combines parse + eval.
 *
 * Design constraints:
 *   - No ## / #@ / #$ substitutions (use %i0-%i9 instead)
 *   - No dynamic function calls (computed function names)
 *   - All function names are statically known at parse time
 *   - FN_NOEVAL functions receive AST subtrees, not raw text
 *
 * The AST is cacheable: parsing is pure (no side effects, no database
 * access). The same cached AST can be evaluated with different contexts.
 */

#ifndef AST_H
#define AST_H

#include <string>
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

struct ASTNode {
    ASTNodeType type;
    std::string text;
    std::vector<std::unique_ptr<ASTNode>> children;

    ASTNode(ASTNodeType t, const std::string &s = "")
        : type(t), text(s) {}

    void addChild(std::unique_ptr<ASTNode> child) {
        children.push_back(std::move(child));
    }
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
    std::string text;
};

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------

// Tokenize a MUX expression string into a token stream.
//
std::vector<ASTToken> ast_tokenize(const UTF8 *input, size_t nLen);

// Parse a token stream into an AST.
//
std::unique_ptr<ASTNode> ast_parse(const std::vector<ASTToken> &tokens);

// Parse a MUX expression string directly into an AST.
// Convenience wrapper: tokenize + parse.
//
std::unique_ptr<ASTNode> ast_parse_string(const UTF8 *input, size_t nLen);

// Reconstruct the raw source text from an AST subtree.
//
std::string ast_raw_text(const ASTNode *node);

// Print an AST tree for debugging.
//
void ast_dump(const ASTNode *node, int indent = 0);

// ---------------------------------------------------------------
// mux_exec2 — drop-in replacement for mux_exec
// ---------------------------------------------------------------

// Parse + evaluate in one call. Same signature as mux_exec.
// During transition, both mux_exec and mux_exec2 coexist.
//
void mux_exec2(const UTF8 *pStr, size_t nStr,
               UTF8 *buff, UTF8 **bufc,
               dbref executor, dbref caller, dbref enactor,
               int eval, const UTF8 *cargs[], int ncargs);

#endif // AST_H
