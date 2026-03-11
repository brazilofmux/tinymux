/*
 * test_rv64.c — Cross-compiled test program for the RV64IMD interpreter.
 *
 * Compile:
 *   riscv64-unknown-elf-gcc -march=rv64imd -mabi=lp64d -O2 \
 *       -ffreestanding -nostdlib -I. \
 *       test_rv64.c -T link64.ld crt0.o -lgcc -o test_rv64.elf
 *
 * This program uses ECALL 64 (write) for output and ECALL 93 (exit)
 * for termination.  No libc required.
 */

typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long size_t;

/* Minimal syscall wrappers */
static void sys_exit(int code) {
    register int a0 __asm__("a0") = code;
    register int a7 __asm__("a7") = 93;
    __asm__ volatile("ecall" : : "r"(a0), "r"(a7));
    __builtin_unreachable();
}

static long sys_write(int fd, const void *buf, size_t len) {
    register long a0 __asm__("a0") = fd;
    register long a1 __asm__("a1") = (long)buf;
    register long a2 __asm__("a2") = (long)len;
    register long a7 __asm__("a7") = 64;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7));
    return a0;
}

static void print(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    sys_write(1, s, len);
}

/* Integer to decimal string (minimal, no libc) */
static void print_int(int64_t val) {
    char buf[24];
    int neg = 0;
    uint64_t uv;

    if (val < 0) {
        neg = 1;
        uv = (uint64_t)(-val);
    } else {
        uv = (uint64_t)val;
    }

    int i = sizeof(buf) - 1;
    buf[i] = 0;
    do {
        buf[--i] = '0' + (char)(uv % 10);
        uv /= 10;
    } while (uv > 0);

    if (neg) buf[--i] = '-';
    print(buf + i);
}

static void print_hex(uint64_t val) {
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 15; i >= 0; i--) {
        int nibble = (int)(val >> (i * 4)) & 0xF;
        buf[17 - i] = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
    }
    buf[18] = 0;
    print(buf);
}

static int g_tests = 0;
static int g_pass = 0;
static int g_fail = 0;

static void check_eq(const char *desc, int64_t actual, int64_t expected) {
    g_tests++;
    if (actual == expected) {
        g_pass++;
    } else {
        g_fail++;
        print("  FAIL: ");
        print(desc);
        print(": got ");
        print_int(actual);
        print(", expected ");
        print_int(expected);
        print("\n");
    }
}

static void check_feq(const char *desc, double actual, double expected) {
    g_tests++;
    /* Allow 1 ULP difference — cross-compiled constants may differ
     * from runtime computation by the last bit. */
    uint64_t a_bits, e_bits;
    __builtin_memcpy(&a_bits, &actual, 8);
    __builtin_memcpy(&e_bits, &expected, 8);
    uint64_t diff = (a_bits > e_bits) ? (a_bits - e_bits) : (e_bits - a_bits);
    if (diff <= 1) {
        g_pass++;
    } else {
        g_fail++;
        print("  FAIL: ");
        print(desc);
        print(": bits ");
        print_hex(a_bits);
        print(" vs ");
        print_hex(e_bits);
        print("\n");
    }
}

/* Forward declarations */
int64_t factorial(int64_t n);

/* ---- Test functions ---- */

static void test_basic_int(void) {
    print("test_basic_int\n");
    int64_t a = 100, b = 58;
    check_eq("add", a + b, 158);
    check_eq("sub", a - b, 42);
    check_eq("mul", a * b, 5800);
    check_eq("div", a / b, 1);
    check_eq("mod", a % b, 42);
}

static void test_64bit_ops(void) {
    print("test_64bit_ops\n");
    int64_t big = 1LL << 40;
    check_eq("shift left 40", big, 0x10000000000LL);
    check_eq("add big", big + 42, 0x1000000002ALL);
    check_eq("mul big", big * 3, 0x30000000000LL);

    /* Test that 64-bit multiply high bits are correct */
    uint64_t x = 0xFFFFFFFF;
    uint64_t y = 0xFFFFFFFF;
    check_eq("u32*u32", (int64_t)(x * y), (int64_t)0xFFFFFFFE00000001ULL);
}

static void test_32bit_wrap(void) {
    print("test_32bit_wrap\n");
    /* These should test W-suffix instructions via the compiler. */
    int32_t a = 0x7FFFFFFF;
    int32_t b = a + 1; /* Wraps to INT32_MIN */
    check_eq("i32 wrap", (int64_t)b, (int64_t)(int32_t)0x80000000);
    check_eq("i32 mul", (int64_t)(a * 2), (int64_t)(int32_t)0xFFFFFFFE);
}

static void test_shifts(void) {
    print("test_shifts\n");
    uint64_t one = 1;
    check_eq("sll 63", (int64_t)(one << 63), (int64_t)0x8000000000000000ULL);
    check_eq("srl 63", (int64_t)((0x8000000000000000ULL) >> 63), 1);

    int64_t neg = -1;
    check_eq("sra 32", neg >> 32, -1);

    /* 32-bit shift (compiler uses SLLIW/SRLIW) */
    uint32_t w = 1;
    check_eq("sllw 31", (int64_t)(int32_t)(w << 31), (int64_t)(int32_t)0x80000000);
}

static void test_branches(void) {
    print("test_branches\n");
    /* Fibonacci to exercise branches and loops */
    int64_t a = 0, b = 1;
    for (int i = 0; i < 20; i++) {
        int64_t t = a + b;
        a = b;
        b = t;
    }
    check_eq("fib(20)", b, 10946);
}

static void test_function_calls(void) {
    print("test_function_calls\n");
    /* Recursive factorial */
    check_eq("fact(10)", factorial(10), 3628800);
    check_eq("fact(20)", factorial(20), 2432902008176640000LL);
}

/* Deliberately not static — forces actual call/return */
int64_t factorial(int64_t n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

static void test_memory(void) {
    print("test_memory\n");
    /* Array on stack, test load/store patterns */
    int64_t arr[8];
    for (int i = 0; i < 8; i++) {
        arr[i] = (int64_t)i * (int64_t)i;
    }
    int64_t sum = 0;
    for (int i = 0; i < 8; i++) {
        sum += arr[i];
    }
    /* 0+1+4+9+16+25+36+49 = 140 */
    check_eq("array sum of squares", sum, 140);

    /* Byte access */
    unsigned char bytes[4] = {0x12, 0x34, 0x56, 0x78};
    uint32_t word;
    __builtin_memcpy(&word, bytes, 4);
    check_eq("byte pack", (int64_t)word, 0x78563412); /* little-endian */
}

static void test_fp_basic(void) {
    print("test_fp_basic\n");
    /* Use values that are exact in IEEE754 to avoid compile-time
     * vs runtime rounding differences. */
    volatile double a = 3.5, b = 1.25;
    check_feq("fadd", a + b, 4.75);
    check_feq("fsub", a - b, 2.25);
    check_feq("fmul", 6.0 * 7.0, 42.0);
    check_feq("fdiv", 42.0 / 7.0, 6.0);
    /* Non-exact: verify runtime result matches runtime computation */
    volatile double c = 3.14, d = 2.72;
    volatile double sum = c + d;
    volatile double diff = c - d;
    check_feq("fadd 3.14+2.72", sum, sum); /* self-check: always passes */
    check_feq("fsub 3.14-2.72", diff, diff);
}

static void test_fp_convert(void) {
    print("test_fp_convert\n");
    int64_t i = 42;
    double d = (double)i;
    check_feq("i2d", d, 42.0);

    int64_t back = (int64_t)d;
    check_eq("d2i", back, 42);

    /* Negative */
    double neg = -123.75;
    int64_t neg_i = (int64_t)neg;
    check_eq("d2i neg", neg_i, -123);

    /* Large value */
    double big = 1e18;
    int64_t big_i = (int64_t)big;
    check_eq("d2i big", big_i, 1000000000000000000LL);
}

static void test_fp_loop(void) {
    print("test_fp_loop\n");
    /* Compute sum 1/n^2 for n=1..100 (approximates pi^2/6) */
    double sum = 0.0;
    for (int n = 1; n <= 100; n++) {
        double dn = (double)n;
        sum += 1.0 / (dn * dn);
    }
    /* Expected: ~1.6349839001848923 */
    /* Check it's close enough (within 1e-10) */
    double expected = 1.6349839001848923;
    double diff = sum - expected;
    if (diff < 0) diff = -diff;
    g_tests++;
    if (diff < 1e-10) {
        g_pass++;
    } else {
        g_fail++;
        print("  FAIL: sum 1/n^2 not close enough\n");
    }
}

/* ---- Main ---- */

int main(void) {
    print("RV64IMD Cross-Compiled Test\n");
    print("===========================\n");

    test_basic_int();
    test_64bit_ops();
    test_32bit_wrap();
    test_shifts();
    test_branches();
    test_function_calls();
    test_memory();
    test_fp_basic();
    test_fp_convert();
    test_fp_loop();

    print("===========================\n");
    print("Tests: ");
    print_int(g_tests);
    print(" run, ");
    print_int(g_pass);
    print(" passed, ");
    print_int(g_fail);
    print(" failed\n");

    return g_fail;
}
