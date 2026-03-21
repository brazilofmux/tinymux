/*
 * tokenize.cpp - MUX expression tokenizer study tool.
 *
 * Reads MUX expressions from stdin (one per line) and emits a token
 * stream to stdout. Stage 1 of the parser study.
 */

#include "mux_parse.h"

int main(int argc, char *argv[])
{
    ParserProfile profile = PROFILE_MUX214_AST;
    for (int i = 1; i < argc; i++) {
        if (0 == strcmp(argv[i], "--profile")) {
            if (i + 1 >= argc || !parse_profile_string(argv[i + 1], profile)) {
                fprintf(stderr, "usage: %s [--profile mux214|mux213|penn]\n", argv[0]);
                return 2;
            }
            i++;
        } else if (0 == strncmp(argv[i], "--profile=", 10)) {
            if (!parse_profile_string(argv[i] + 10, profile)) {
                fprintf(stderr, "usage: %s [--profile mux214|mux213|penn]\n", argv[0]);
                return 2;
            }
        } else {
            fprintf(stderr, "usage: %s [--profile mux214|mux213|penn]\n", argv[0]);
            return 2;
        }
    }

    char line[8192];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        printf("INPUT: %s\n", line);
        printf("PROFILE: %s\n", profile_name(profile));
        auto tokens = tokenize(line, profile);
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
