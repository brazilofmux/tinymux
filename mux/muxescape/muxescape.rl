/*! \file muxescape.rl
 * \brief Escape plain text for use in TinyMUX/MUSH command arguments.
 *
 * Reads UTF-8 or ASCII text from stdin or a single file and writes a form
 * that survives MUX softcode parsing while preserving spacing:
 *
 * - spaces -> %b or [space(n)]
 * - tabs -> %t or [repeat(%t,n)]
 * - newlines -> %r or [repeat(%r,n)]
 * - structural characters like []{}(),;#%\\ are backslash-escaped
 *
 * This is intended for preparing message bodies for commands such as @mail,
 * page, +job, or softcode package interfaces where the text will be parsed.
 *
 * Build:
 *   ragel -G2 -o muxescape.cpp muxescape.rl
 *   c++ -std=c++17 -O2 -Wall -Wextra -o muxescape muxescape.cpp
 *
 * Usage:
 *   muxescape [file]
 *   cat message.txt | muxescape
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <string>
#include <vector>

static void emit_spaces_run(const unsigned char *ts, const unsigned char *te)
{
    size_t count = static_cast<size_t>(te - ts);
    if (count <= 3) {
        for (size_t i = 0; i < count; ++i) {
            std::fputs("%b", stdout);
        }
    } else {
        std::fprintf(stdout, "[space(%zu)]", count);
    }
}

static void emit_repeat_run(const char *token, size_t count)
{
    if (count <= 3) {
        for (size_t i = 0; i < count; ++i) {
            std::fputs(token, stdout);
        }
    } else {
        std::fprintf(stdout, "[repeat(%s,%zu)]", token, count);
    }
}

static size_t logical_newlines(const unsigned char *ts, const unsigned char *te)
{
    size_t count = 0;
    const unsigned char *p = ts;
    const unsigned char *end = te;
    while (p < end) {
        if (*p == '\r') {
            ++count;
            ++p;
            if (p < end && *p == '\n') {
                ++p;
            }
        } else if (*p == '\n') {
            ++count;
            ++p;
        } else {
            ++p;
        }
    }
    return count;
}

static void emit_escaped_segment(const unsigned char *ts, const unsigned char *te)
{
    for (const unsigned char *p = ts; p < te; ++p) {
        std::fputc('\\', stdout);
        std::fputc(*p, stdout);
    }
}

static void emit_literal_segment(const unsigned char *ts, const unsigned char *te)
{
    if (te > ts) {
        std::fwrite(ts, 1, static_cast<size_t>(te - ts), stdout);
    }
}

static bool read_all(FILE *fp, std::vector<char> &buf)
{
    char chunk[4096];
    while (true) {
        size_t n = std::fread(chunk, 1, sizeof(chunk), fp);
        if (n > 0) {
            buf.insert(buf.end(), chunk, chunk + n);
        }
        if (n < sizeof(chunk)) {
            if (std::ferror(fp)) {
                return false;
            }
            break;
        }
    }
    return true;
}

%%{
    machine muxescape;
    alphtype unsigned char;

    structural = [\[\]\{\}\(\),;#%\\];
    newline = '\r\n' | '\r' | '\n';
    other = any - [ \t\r\n\[\]\{\}\(\),;#%\\];

    action emit_spaces {
        emit_spaces_run(ts, te);
    }

    action emit_tabs {
        emit_repeat_run("%t", static_cast<size_t>(te - ts));
    }

    action emit_newlines {
        emit_repeat_run("%r", logical_newlines(ts, te));
    }

    action emit_escaped {
        emit_escaped_segment(ts, te);
    }

    action emit_literal {
        emit_literal_segment(ts, te);
    }

    main := |*
        ' '+        => emit_spaces;
        '\t'+       => emit_tabs;
        newline+    => emit_newlines;
        structural+ => emit_escaped;
        other+      => emit_literal;
    *|;
}%%

%% write data;

static void process_buffer(const unsigned char *data, size_t len)
{
    const unsigned char *p = data;
    const unsigned char *pe = data + len;
    const unsigned char *eof = pe;
    const unsigned char *ts = nullptr;
    const unsigned char *te = nullptr;
    int cs = 0;
    int act = 0;

    (void)eof;
    (void)act;

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    %% write init;
    %% write exec;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}

int main(int argc, char *argv[])
{
    const char *path = nullptr;
    if (argc == 2) {
        path = argv[1];
        if (0 == std::strcmp(path, "--help") || 0 == std::strcmp(path, "-h")) {
            std::fprintf(stderr, "Usage: muxescape [file]\n");
            return 0;
        }
    } else if (argc > 2) {
        std::fprintf(stderr, "Usage: muxescape [file]\n");
        return 1;
    }

    FILE *fp = stdin;
    if (path) {
        fp = std::fopen(path, "rb");
        if (!fp) {
            std::perror(path);
            return 1;
        }
    }

    std::vector<char> data;
    if (!read_all(fp, data)) {
        if (path) {
            std::fclose(fp);
        }
        std::fprintf(stderr, "muxescape: failed to read input\n");
        return 1;
    }
    if (path) {
        std::fclose(fp);
    }

    if (!data.empty()) {
        process_buffer(reinterpret_cast<const unsigned char *>(data.data()), data.size());
    }

    return std::ferror(stdout) ? 1 : 0;
}
