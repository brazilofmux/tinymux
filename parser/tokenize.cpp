/*
 * tokenize.cpp - MUX expression tokenizer study tool.
 *
 * Reads MUX expressions from stdin (one per line) and emits a token
 * stream to stdout. Stage 1 of the parser study.
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
