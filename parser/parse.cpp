/*
 * parse.cpp - MUX expression recursive-descent parser study tool.
 *
 * Reads MUX expressions from stdin (one per line), tokenizes them,
 * then builds an AST and prints it. Stage 2 of the parser study.
 */

#include "mux_parse.h"

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
        ast_print(ast.get(), 2);
        printf("\n");
    }
    return 0;
}
