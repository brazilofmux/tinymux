/*! \file dbt_x64_div_harness.c
 * \brief Execute the x86-64 DBT div/rem guard sequences under qemu.
 *
 * Part of the x86-64 idiv regression check (dbt_x64_div_qemu_test.sh).
 * Cross-compiled to x86-64 (static) and run under qemu-x86_64 on the
 * AArch64 dev box, this loads the byte blobs produced by dbt_x64_div_gen,
 * wraps each with a prologue (RAX<-arg0 dividend, RCX<-arg1 divisor) and an
 * epilogue (return RAX for div forms, RDX for rem forms), and runs the real
 * idiv with edge operands.  Each case runs in a forked child so an
 * unguarded #DE (SIGFPE) is reported as a TRAP instead of killing the run.
 *
 * Usage: dbt_x64_div_harness <blob_dir>
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *g_dir;

/* Build an executable thunk: prologue + (blob minus trailing RET) + epilogue. */
static void *make_thunk(const char *name, int is_rem) {
    char path[512];
    snprintf(path, sizeof path, "%s/blob_%s.bin", g_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    uint8_t blob[256];
    int len = (int)fread(blob, 1, sizeof blob, f);
    fclose(f);
    if (len > 0 && blob[len - 1] == 0xC3) len--;  /* drop blob's RET */

    uint8_t code[320]; int n = 0;
    code[n++] = 0x48; code[n++] = 0x89; code[n++] = 0xF8;  /* mov rax,rdi (dividend) */
    code[n++] = 0x48; code[n++] = 0x89; code[n++] = 0xF1;  /* mov rcx,rsi (divisor)  */
    memcpy(code + n, blob, len); n += len;
    if (is_rem) { code[n++] = 0x48; code[n++] = 0x89; code[n++] = 0xD0; } /* mov rax,rdx */
    code[n++] = 0xC3;                                       /* ret */

    void *m = mmap(0, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) { perror("mmap"); exit(2); }
    memcpy(m, code, n);
    return m;
}

typedef int64_t (*divfn)(int64_t, int64_t);

/* Returns 0 and sets *out on success; returns 128+signal on a trap. */
static int run_one(const char *name, int is_rem, int64_t a, int64_t b, int64_t *out) {
    void *thunk = make_thunk(name, is_rem);
    int fds[2];
    if (pipe(fds) != 0) { perror("pipe"); exit(2); }
    pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]);
        int64_t r = ((divfn)thunk)(a, b);
        ssize_t w = write(fds[1], &r, sizeof r);
        (void)w;
        _exit(0);
    }
    close(fds[1]);
    int st; waitpid(pid, &st, 0);
    munmap(thunk, 4096);
    if (WIFSIGNALED(st)) { close(fds[0]); return 128 + WTERMSIG(st); }
    int64_t r = 0;
    ssize_t got = read(fds[0], &r, sizeof r);
    close(fds[0]);
    if (got != (ssize_t)sizeof r) return 129;  /* no result == abnormal */
    *out = r;
    return 0;
}

struct T { const char *name; int is_rem; int64_t a, b; int64_t expect; };

int main(int argc, char **argv) {
    g_dir = (argc > 1) ? argv[1] : ".";
    const int64_t I64MIN = INT64_MIN;
    const int64_t I32MIN = (int64_t)(int32_t)0x80000000;  /* sign-extended */

    struct T tests[] = {
        /* 64-bit: both #DE conditions (x/0, INT64_MIN/-1) plus a normal case */
        { "div64",  0, 42, 0,            -1 },
        { "div64",  0, I64MIN, -1,        I64MIN },
        { "div64",  0, -100, 7,           -14 },
        { "rem64",  1, 42, 0,             42 },
        { "rem64",  1, I64MIN, -1,        0 },
        { "rem64",  1, -100, 7,           -2 },
        { "divu64", 0, 42, 0,             -1 },   /* UINT64_MAX */
        { "remu64", 1, 42, 0,             42 },
        /* W-forms: sign-extended 32-bit operands and result */
        { "divw",   0, 42, 0,             -1 },
        { "divw",   0, I32MIN, -1,        I32MIN },
        { "divw",   0, -100, 7,           -14 },
        { "remw",   1, 42, 0,             42 },
        { "remw",   1, I32MIN, -1,        0 },
        { "divuw",  0, 42, 0,             -1 },
        { "remuw",  1, 42, 0,             42 },
        /* W-form divisor with zero low 32 bits but nonzero upper bits — the
         * exact shape that broke the AArch64 backend; the zero check must be
         * 32-bit. */
        { "divw",   0, 42, 0x100000000LL, -1 },
        { "remw",   1, 42, 0x100000000LL, 42 },
        { "divuw",  0, 42, 0x100000000LL, -1 },
        /* W-form divisor low 32 = -1 with nonzero upper bits — overflow guard
         * must be 32-bit. */
        { "divw",   0, 0x80000000LL, 0xFFFFFFFFLL, I32MIN },
    };

    int n = (int)(sizeof tests / sizeof tests[0]), fails = 0, traps = 0;
    for (int i = 0; i < n; i++) {
        struct T *t = &tests[i];
        int64_t out = 0;
        int rc = run_one(t->name, t->is_rem, t->a, t->b, &out);
        if (rc >= 128) {
            printf("TRAP %-6s a=%lld b=%lld -> signal %d (UNGUARDED idiv!)\n",
                   t->name, (long long)t->a, (long long)t->b, rc - 128);
            traps++;
            continue;
        }
        int ok = (out == t->expect);
        if (!ok) fails++;
        printf("%-4s %-6s a=%lld b=%lld -> %lld (expect %lld)\n",
               ok ? "OK" : "FAIL", t->name, (long long)t->a, (long long)t->b,
               (long long)out, (long long)t->expect);
    }
    printf("\n%d tests, %d fail, %d TRAP\n", n, fails, traps);
    return (fails || traps) ? 1 : 0;
}
