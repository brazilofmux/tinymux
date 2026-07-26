// Instruction-level differential fuzzer: RV64 interpreter vs DBT translator.
//
// testcases/tools/jit_diff fuzzes the *softcode* layer (softcode -> HIR ->
// JIT vs ast_eval).  Nothing fuzzed the layer below it, where every DBT
// defect this cycle actually lived -- #1147 (stale patch sites), #1148
// (SysV stack alignment), #1151 (unchecked guest pointers), #1152 (an x86
// decode applied to AArch64) and #1153 (block cache double insert).  The
// hand-assembled cases in dbt_test.cpp are the only coverage of that layer
// and only about 6 of its 39 test functions drive the DBT at all.
//
// So: generate random RV64 sequences, run each through the reference
// interpreter and through the host DBT, and compare all 32 integer
// registers.  A translator bug becomes a register mismatch.
//
// Two constraints keep "random" from meaning "crashes for uninteresting
// reasons", and both come from real properties of the system rather than
// convenience:
//
//   Memory operands are confined to a data window addressed through one
//   reserved base register.  The DBT's inline loads and stores are
//   deliberately unchecked -- #1151 bounded that fix to the intrinsic
//   stubs, because a compare on every guest access is the cost a JIT
//   exists to avoid -- so an unconstrained address is a genuine wild
//   access in the DBT and a clean refusal in the interpreter.  That is a
//   known, accepted difference, not a finding, and generating it would
//   produce nothing but noise and segfaults.
//
//   Control flow is structured, not random.  rv64_interp_run takes no
//   instruction cap, so an infinite loop hangs the fuzzer outright rather
//   than failing it -- random back edges are therefore unusable.  Instead
//   the generator emits a *counted* loop: a reserved counter register, a
//   body that cannot write it, and a decrement-and-branch epilogue.  That
//   gives real back edges -- which is the point, since block chaining,
//   superblock formation and side exits only exist once a branch does --
//   while termination stays a property of the construction.  Forward
//   branches inside the body are bounded so they can never skip the
//   epilogue, which would turn the loop infinite.
//
// A mismatch is shrunk by delta debugging before it is printed, so the
// report is a minimal sequence rather than the raw 24-instruction one.
//
// Deterministic by default: same seed, same corpus, so a failure found in
// CI reproduces exactly.  Env knobs:
//   DBT_FUZZ_SEED=<n>    corpus seed        (default 1)
//   DBT_FUZZ_ITERS=<n>   sequences to run   (default 300)

#include "dbt.h"
#include "dbt_interp.h"
#include "dbt_decoder.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>

// ---------------------------------------------------------------
// Instruction encoders (file-static in dbt_test.cpp, so re-stated here)
// ---------------------------------------------------------------

static uint32_t r_type(uint8_t op, uint8_t rd, uint8_t f3,
                       uint8_t rs1, uint8_t rs2, uint8_t f7) {
    return (uint32_t)op | ((uint32_t)rd << 7) | ((uint32_t)f3 << 12)
         | ((uint32_t)rs1 << 15) | ((uint32_t)rs2 << 20) | ((uint32_t)f7 << 25);
}

static uint32_t i_type(uint8_t op, uint8_t rd, uint8_t f3,
                       uint8_t rs1, int32_t imm) {
    return (uint32_t)op | ((uint32_t)rd << 7) | ((uint32_t)f3 << 12)
         | ((uint32_t)rs1 << 15) | ((uint32_t)(imm & 0xFFF) << 20);
}

static uint32_t s_type(uint8_t op, uint8_t f3, uint8_t rs1,
                       uint8_t rs2, int32_t imm) {
    return (uint32_t)op | ((uint32_t)(imm & 0x1F) << 7) | ((uint32_t)f3 << 12)
         | ((uint32_t)rs1 << 15) | ((uint32_t)rs2 << 20)
         | ((uint32_t)((imm >> 5) & 0x7F) << 25);
}

// B-type: the immediate is scattered across four fields and is in
// multiples of 2, so it is easy to get wrong -- hence the explicit shifts.
static uint32_t b_type(uint8_t f3, uint8_t rs1, uint8_t rs2, int32_t imm) {
    uint32_t i = (uint32_t)imm;
    return (uint32_t)OP_BRANCH
         | (((i >> 11) & 1u) << 7)
         | (((i >> 1) & 0xFu) << 8)
         | ((uint32_t)f3 << 12)
         | ((uint32_t)rs1 << 15)
         | ((uint32_t)rs2 << 20)
         | (((i >> 5) & 0x3Fu) << 25)
         | (((i >> 12) & 1u) << 31);
}

// OP_FP funct7 = funct5 << 2 | format; format 1 is double.
static uint32_t fp_op(uint8_t funct5, uint8_t rd, uint8_t f3,
                      uint8_t rs1, uint8_t rs2) {
    return r_type(OP_FP, rd, f3, rs1, rs2,
                  (uint8_t)((funct5 << 2) | FP_FMT_D));
}

static uint32_t ECALL_INSN(void) { return 0x00000073u; }

// ---------------------------------------------------------------
// Layout
// ---------------------------------------------------------------

static const size_t MEM_SIZE  = 64 * 1024;
// Must fit a 12-bit signed immediate: the base register is set with a
// single ADDI, and a wider value silently truncates.  0x2000 did exactly
// that, giving base 0 -- every generated store then overwrote the code,
// which the interpreter re-reads and the DBT has already translated.  That
// manufactured divergences on self-modifying code, not translator bugs.
// 0x400 clears the longest sequence this generator can emit (~300 bytes).
static const uint64_t DATA_BASE = 0x400;
static const int      BASE_REG  = 9;        // holds DATA_BASE; never written
static const int      LOOP_REG  = 8;        // loop counter; never written by the body
static const int      SP_REG    = 2;

// Registers the generator may write. x0 is hardwired zero, x2 is the stack
// pointer, x9 is the reserved data base.
static bool writable_reg(int r) {
    return r >= 1 && r < 32 && r != SP_REG && r != BASE_REG && r != LOOP_REG;
}

// ---------------------------------------------------------------
// Deterministic PRNG (xorshift64*) -- std::rand would vary by libc.
// ---------------------------------------------------------------

static uint64_t g_rng;
static uint64_t rnd(void) {
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return g_rng * 0x2545F4914F6CDD1DULL;
}
static uint32_t rnd_below(uint32_t n) { return (uint32_t)(rnd() % n); }

// Operand values worth hitting far more often than uniform choice would.
static int64_t interesting_imm(void) {
    static const int64_t POOL[] = { 0, 1, -1, 2, -2, 7, -7, 255, -256,
                                    2047, -2048, 1023, -1024 };
    return POOL[rnd_below(sizeof(POOL) / sizeof(POOL[0]))];
}

// ---------------------------------------------------------------
// Generator
// ---------------------------------------------------------------

static int pick_reg(void) {
    for (;;) {
        int r = (int)rnd_below(32);
        if (writable_reg(r)) return r;
    }
}

// Any register may be read, including x0 and the base register.
static int pick_src(void) { return (int)rnd_below(32); }

static void emit_alu_imm(std::vector<uint32_t> &out) {
    static const uint8_t F3[]  = { ALU_ADDI, ALU_SLTI, ALU_SLTIU, ALU_XORI,
                                   ALU_ORI, ALU_ANDI };
    int rd = pick_reg(), rs1 = pick_src();
    out.push_back(i_type(OP_IMM, (uint8_t)rd, F3[rnd_below(6)],
                         (uint8_t)rs1, (int32_t)interesting_imm()));
}

static void emit_shift_imm(std::vector<uint32_t> &out) {
    int rd = pick_reg(), rs1 = pick_src();
    uint32_t sh = rnd_below(64);
    // SLLI/SRLI share funct3 with SRAI distinguished by bit 30.
    uint8_t f3 = (rnd_below(2) == 0) ? ALU_SLLI : ALU_SRLI;
    uint32_t insn = i_type(OP_IMM, (uint8_t)rd, f3, (uint8_t)rs1, (int32_t)sh);
    if (f3 == ALU_SRLI && rnd_below(2) == 0) insn |= (1u << 30);  // SRAI
    out.push_back(insn);
}

static void emit_alu_reg(std::vector<uint32_t> &out) {
    struct Op { uint8_t f3, f7; };
    static const Op OPS[] = {
        { ALU_ADD, 0x00 }, { ALU_ADD, 0x20 },   // ADD / SUB
        { ALU_SLL, 0x00 }, { ALU_SLT, 0x00 }, { ALU_SLTU, 0x00 },
        { ALU_XOR, 0x00 }, { ALU_SRL, 0x00 }, { ALU_SRL, 0x20 },  // SRL / SRA
        { ALU_OR,  0x00 }, { ALU_AND, 0x00 },
    };
    const Op &o = OPS[rnd_below(sizeof(OPS) / sizeof(OPS[0]))];
    out.push_back(r_type(OP_REG, (uint8_t)pick_reg(), o.f3,
                         (uint8_t)pick_src(), (uint8_t)pick_src(), o.f7));
}

// M extension: the div/rem edge cases (INT64_MIN / -1, x / 0) are defined
// by the RV64 spec and are exactly where a backend is likely to diverge.
static void emit_muldiv(std::vector<uint32_t> &out) {
    static const uint8_t F3[] = { 0, 1, 2, 3, 4, 5, 6, 7 };  // MUL..REMU
    out.push_back(r_type(OP_REG, (uint8_t)pick_reg(), F3[rnd_below(8)],
                         (uint8_t)pick_src(), (uint8_t)pick_src(), 0x01));
}

static void emit_alu_word(std::vector<uint32_t> &out) {
    // 32-bit ops sign-extend their result into 64 bits; a translator that
    // forgets the extension only shows up on negative or >2^31 values.
    if (rnd_below(2) == 0) {
        out.push_back(i_type(OP_IMM32, (uint8_t)pick_reg(), 0,
                             (uint8_t)pick_src(), (int32_t)interesting_imm()));
    } else {
        uint8_t f7 = (rnd_below(2) == 0) ? 0x00 : 0x20;   // ADDW / SUBW
        out.push_back(r_type(OP_REG32, (uint8_t)pick_reg(), 0,
                             (uint8_t)pick_src(), (uint8_t)pick_src(), f7));
    }
}

// Memory, always through the reserved base register with a small offset,
// so every access is in range for both routes (see the header comment).
static void emit_mem(std::vector<uint32_t> &out) {
    int32_t off = (int32_t)(rnd_below(64) * 8);
    if (rnd_below(2) == 0) {
        static const uint8_t F3[] = { 0, 1, 2, 3, 4, 5, 6 };  // LB..LWU
        out.push_back(i_type(OP_LOAD, (uint8_t)pick_reg(), F3[rnd_below(7)],
                             (uint8_t)BASE_REG, off));
    } else {
        static const uint8_t F3[] = { 0, 1, 2, 3 };           // SB..SD
        out.push_back(s_type(OP_STORE, F3[rnd_below(4)],
                             (uint8_t)BASE_REG, (uint8_t)pick_src(), off));
    }
}

// --- D extension --------------------------------------------------
//
// Seeded from the integer registers via FMV.D.X so the FP inputs inherit
// the same awkward bit patterns (INT64_MIN, 2^53 neighbours) rather than
// being small tidy doubles.  Rounding mode is pinned to RNE (funct3 0)
// instead of dynamic, so a divergence means the arithmetic differs rather
// than the two routes merely disagreeing about fcsr.
static void emit_fp_seed(std::vector<uint32_t> &out) {
    out.push_back(fp_op(FP_FMVDX, (uint8_t)rnd_below(32), 0,
                        (uint8_t)pick_src(), 0));
}

static void emit_fp_arith(std::vector<uint32_t> &out) {
    static const uint8_t F5[] = { FP_FADD, FP_FSUB, FP_FMUL, FP_FDIV };
    out.push_back(fp_op(F5[rnd_below(4)], (uint8_t)rnd_below(32), 0,
                        (uint8_t)rnd_below(32), (uint8_t)rnd_below(32)));
}

static void emit_fp_misc(std::vector<uint32_t> &out) {
    switch (rnd_below(5)) {
    case 0:
        out.push_back(fp_op(FP_FSQRT, (uint8_t)rnd_below(32), 0,
                            (uint8_t)rnd_below(32), 0));
        break;
    case 1:
        out.push_back(fp_op(FP_FSGNJ, (uint8_t)rnd_below(32),
                            (uint8_t)rnd_below(3), (uint8_t)rnd_below(32),
                            (uint8_t)rnd_below(32)));
        break;
    case 2:  // FMIN/FMAX: NaN and signed-zero rules live here
        out.push_back(fp_op(FP_FMINMAX, (uint8_t)rnd_below(32),
                            (uint8_t)rnd_below(2), (uint8_t)rnd_below(32),
                            (uint8_t)rnd_below(32)));
        break;
    case 3:  // FEQ/FLT/FLE -> integer register
        out.push_back(fp_op(FP_FCMP, (uint8_t)pick_reg(),
                            (uint8_t)rnd_below(3), (uint8_t)rnd_below(32),
                            (uint8_t)rnd_below(32)));
        break;
    default:
        if (rnd_below(2) == 0) {
            out.push_back(fp_op(FP_FCVTW, (uint8_t)pick_reg(), 0,
                                (uint8_t)rnd_below(32), (uint8_t)rnd_below(4)));
        } else {
            out.push_back(fp_op(FP_FCLASS, (uint8_t)pick_reg(),
                                (uint8_t)rnd_below(2),
                                (uint8_t)rnd_below(32), 0));
        }
        break;
    }
}

// Build a wide, awkward constant in rd: small immediates alone would never
// reach the 2^31 / 2^53 / INT64_MIN neighbourhoods where the interesting
// divergences live.
static void emit_seed_const(std::vector<uint32_t> &out) {
    int rd = pick_reg();
    out.push_back(i_type(OP_IMM, (uint8_t)rd, ALU_ADDI, 0,
                         (int32_t)interesting_imm()));
    uint32_t sh = 1 + rnd_below(62);
    out.push_back(i_type(OP_IMM, (uint8_t)rd, ALU_SLLI, (uint8_t)rd,
                         (int32_t)sh));
    out.push_back(i_type(OP_IMM, (uint8_t)rd, ALU_ADDI, (uint8_t)rd,
                         (int32_t)interesting_imm()));
}

static void emit_one(std::vector<uint32_t> &code) {
    switch (rnd_below(10)) {
    case 0: emit_seed_const(code); break;
    case 1: emit_alu_imm(code);    break;
    case 2: emit_shift_imm(code);  break;
    case 3: emit_alu_reg(code);    break;
    case 4: emit_muldiv(code);     break;
    case 5: emit_alu_word(code);   break;
    case 6: emit_mem(code);        break;
    case 7: emit_fp_seed(code);    break;
    case 8: emit_fp_arith(code);   break;
    default: emit_fp_misc(code);   break;
    }
}

// Sprinkle forward-only branches into an already-built block.  The target
// is clamped to the end of the block, so a branch can never jump past the
// loop epilogue -- that would skip the decrement and hang the run.
static void add_forward_branches(std::vector<uint32_t> &block) {
    if (block.size() < 3) return;
    static const uint8_t F3[] = { BR_BEQ, BR_BNE, BR_BLT, BR_BGE,
                                  BR_BLTU, BR_BGEU };
    int n = (int)rnd_below(3);
    for (int k = 0; k < n; k++) {
        size_t at = rnd_below((uint32_t)block.size() - 1);
        uint32_t skip = 1 + rnd_below(3);
        if (at + skip >= block.size()) skip = (uint32_t)(block.size() - at - 1);
        if (!skip) continue;
        block.insert(block.begin() + (long)at,
                     b_type(F3[rnd_below(6)], (uint8_t)pick_src(),
                            (uint8_t)pick_src(), (int32_t)(skip * 4 + 4)));
    }
}

// Where the counted loop sits, so the shrinker can preserve its shape.
// -1 means no loop in this sequence.
struct Seq {
    std::vector<uint32_t> code;
    long init_idx;     // index of the counter initialiser
    long loop_start;   // index of the first body instruction
    long dec_idx;      // index of the counter decrement
    long br_idx;       // index of the backward branch
};

static Seq generate(void) {
    Seq sq; sq.init_idx = sq.loop_start = sq.dec_idx = sq.br_idx = -1;
    std::vector<uint32_t> &code = sq.code;
    code.push_back(i_type(OP_IMM, (uint8_t)BASE_REG, ALU_ADDI, 0,
                          (int32_t)DATA_BASE));

    // A counted loop most of the time.  Back edges are the whole point of
    // this increment: block chaining, superblock formation and side exits
    // do not exist until a branch does, and those are exactly the paths
    // #1147 and #1152 lived on.  Termination is a property of the shape --
    // the body cannot write LOOP_REG, and no forward branch can reach past
    // the epilogue -- rather than of a timeout.
    if (rnd_below(4) != 0) {
        int trips = 2 + (int)rnd_below(6);
        sq.init_idx = (long)code.size();
        code.push_back(i_type(OP_IMM, (uint8_t)LOOP_REG, ALU_ADDI, 0, trips));

        std::vector<uint32_t> body;
        int bn = 3 + (int)rnd_below(8);
        for (int i = 0; i < bn; i++) emit_one(body);
        add_forward_branches(body);

        size_t loop_start = code.size();
        code.insert(code.end(), body.begin(), body.end());
        sq.loop_start = (long)loop_start;
        sq.dec_idx = (long)code.size();
        code.push_back(i_type(OP_IMM, (uint8_t)LOOP_REG, ALU_ADDI,
                              (uint8_t)LOOP_REG, -1));
        sq.br_idx = (long)code.size();
        int32_t back = ((int32_t)loop_start - (int32_t)code.size()) * 4;
        code.push_back(b_type(BR_BNE, (uint8_t)LOOP_REG, 0, back));
    }

    std::vector<uint32_t> tail;
    int n = 4 + (int)rnd_below(12);
    for (int i = 0; i < n; i++) emit_one(tail);
    add_forward_branches(tail);
    code.insert(code.end(), tail.begin(), tail.end());

    code.push_back(ECALL_INSN());
    return sq;
}

// ---------------------------------------------------------------
// Execution
// ---------------------------------------------------------------

static int interp_ecall(rv64_state_t *, void *) { return 1; }  // halt

static rv64_ctx_t g_dbt_ctx;
static bool g_dbt_ecall_fired;
static int dbt_ecall(rv64_ctx_t *ctx, void *) {
    g_dbt_ctx = *ctx;
    g_dbt_ecall_fired = true;
    return 0;  // halt
}

static void load(std::vector<uint8_t> &mem, const std::vector<uint32_t> &code) {
    std::fill(mem.begin(), mem.end(), (uint8_t)0);
    for (size_t i = 0; i < code.size(); i++) {
        memcpy(mem.data() + i * 4, &code[i], 4);
    }
}

static bool run_interp(const std::vector<uint32_t> &code,
                       uint64_t out[32], uint64_t fout[32]) {
    std::vector<uint8_t> mem(MEM_SIZE, 0);
    load(mem, code);
    rv64_memory_t m = { mem.data(), MEM_SIZE };
    rv64_state_t st = {};
    st.pc = 0;
    st.x[SP_REG] = MEM_SIZE - 16;
    rv64_interp_run(&st, &m, interp_ecall, nullptr);
    memcpy(out, st.x, sizeof(st.x));
    memcpy(fout, st.f, sizeof(st.f));
    return true;
}

static bool run_dbt(const std::vector<uint32_t> &code,
                    uint64_t out[32], uint64_t fout[32]) {
    std::vector<uint8_t> mem(MEM_SIZE, 0);
    load(mem, code);
    dbt_state_t dbt;
    if (dbt_init(&dbt, mem.data(), MEM_SIZE, dbt_ecall, nullptr) != 0) {
        return false;
    }
    dbt.max_dispatch = 100000;
    memset(&g_dbt_ctx, 0, sizeof(g_dbt_ctx));
    g_dbt_ecall_fired = false;
    dbt_run(&dbt, 0, MEM_SIZE - 16);
    dbt_cleanup(&dbt);
    // dbt_run returns without calling the ecall handler when translation
    // fails or the dispatch cap trips, leaving g_dbt_ctx zeroed.  Reading
    // that as "the DBT computed zeros" would manufacture a divergence for
    // every such run -- a false finding, and the first thing this harness
    // did before the guard was added.  Declining to compare is correct:
    // a translate failure is not a wrong answer.
    if (!g_dbt_ecall_fired) return false;
    memcpy(out, g_dbt_ctx.x, sizeof(g_dbt_ctx.x));
    // ctx.f is double[32] and state.f is uint64_t[32]; compare the bits,
    // not the values, so a NaN payload difference is a finding rather than
    // silently equal-by-comparison.
    memcpy(fout, g_dbt_ctx.f, sizeof(g_dbt_ctx.f));
    return true;
}

// Returns the first differing register, or -1.
static long g_declined = 0;

// Returns the differing register (0..31 integer, 32..63 FP), or -1.
static int compare(const std::vector<uint32_t> &code) {
    uint64_t a[32], b[32], fa[32], fb[32];
    if (!run_interp(code, a, fa)) return -1;
    if (!run_dbt(code, b, fb)) { g_declined++; return -1; }
    for (int r = 1; r < 32; r++) {
        // x2 is the stack pointer: both routes are handed it separately
        // rather than computing it, so it is not evidence either way.
        if (r == SP_REG) continue;
        if (a[r] != b[r]) return r;
    }
    for (int r = 0; r < 32; r++) {
        if (fa[r] != fb[r]) return 32 + r;
    }
    return -1;
}

// ---------------------------------------------------------------
// Shrink: drop instructions while the divergence survives.
// ---------------------------------------------------------------

// Delta debugging that understands the loop.
//
// A naive shrinker deletes any instruction, which for a counted loop can
// delete the decrement or move the back-branch target -- turning it into an
// infinite loop.  rv64_interp_run has no instruction cap, so that does not
// fail the run, it hangs the fuzzer.  (It did: one sequence in 200.)
//
// So the decrement and the branch are never deleted, and after any deletion
// inside the loop the branch offset is re-encoded to point at the same
// instruction it did before.  The loop survives shrinking as a loop.
static Seq shrink(Seq sq) {
    bool progress = true;
    while (progress) {
        progress = false;
        for (size_t i = 1; i + 1 < sq.code.size(); i++) {
            // The initialiser is as load-bearing as the decrement: delete
            // it and the counter starts at 0, the decrement makes it -1,
            // and BNE against x0 then runs ~2^64 times.  That is not a
            // slow shrink, it is a hang, and it is what one sequence in
            // 200 actually did.
            if ((long)i == sq.init_idx || (long)i == sq.dec_idx
                || (long)i == sq.br_idx) continue;

            Seq cand = sq;
            cand.code.erase(cand.code.begin() + (long)i);
            if (cand.init_idx   > (long)i) cand.init_idx--;
            if (cand.loop_start > (long)i) cand.loop_start--;
            if (cand.dec_idx   > (long)i) cand.dec_idx--;
            if (cand.br_idx    > (long)i) cand.br_idx--;
            if (cand.br_idx >= 0) {
                int32_t back = (int32_t)((cand.loop_start - cand.br_idx) * 4);
                cand.code[(size_t)cand.br_idx] =
                    b_type(BR_BNE, (uint8_t)LOOP_REG, 0, back);
            }
            if (compare(cand.code) >= 0) { sq = cand; progress = true; break; }
        }
    }
    return sq;
}

static void report(const std::vector<uint32_t> &code, int reg) {
    uint64_t a[32], b[32], fa[32], fb[32];
    run_interp(code, a, fa);
    run_dbt(code, b, fb);
    // Shrinking can change which register diverges, so trust the sequence
    // in hand rather than the index found before it was minimised.
    int found = compare(code);
    if (found >= 0) reg = found;
    if (reg >= 32) {
        printf("\nDIVERGENCE in f%d: interpreter=0x%016llX dbt=0x%016llX\n",
               reg - 32, (unsigned long long)fa[reg - 32],
               (unsigned long long)fb[reg - 32]);
    } else {
        printf("\nDIVERGENCE in x%d: interpreter=0x%016llX dbt=0x%016llX\n",
               reg, (unsigned long long)a[reg], (unsigned long long)b[reg]);
    }
    printf("minimal sequence (%zu instructions):\n", code.size());
    for (size_t i = 0; i < code.size(); i++) {
        printf("  [%2zu] 0x%08X\n", i, code[i]);
    }
}

int main(void) {
    const char *s = getenv("DBT_FUZZ_SEED");
    const char *n = getenv("DBT_FUZZ_ITERS");
    uint64_t seed = s ? strtoull(s, nullptr, 0) : 1;
    long iters = n ? strtol(n, nullptr, 0) : 300;
    if (!seed) seed = 1;  // xorshift dies at zero
    g_rng = seed;

    printf("=== DBT differential fuzzer: %ld sequences, seed %llu ===\n",
           iters, (unsigned long long)seed);

    int failures = 0;
    for (long i = 0; i < iters; i++) {
        Seq sq = generate();
        const std::vector<uint32_t> &code = sq.code;
        if (getenv("DBT_FUZZ_TRACE")) {
            fprintf(stderr, "[%ld] %zu insns\n", i, code.size());
            const char *d = getenv("DBT_FUZZ_DUMP");
            if (d && i == strtol(d, nullptr, 0)) {
                for (size_t q = 0; q < code.size(); q++)
                    fprintf(stderr, "  [%2zu] 0x%08X\n", q, code[q]);
                fflush(stderr);
                const char *only = getenv("DBT_FUZZ_ONLY");
                if (only && !strcmp(only, "interp")) {
                    uint64_t a[32], fa[32];
                    fprintf(stderr, "running interpreter...\n"); fflush(stderr);
                    run_interp(code, a, fa);
                    fprintf(stderr, "interpreter returned\n"); fflush(stderr);
                    return 0;
                }
                if (only && !strcmp(only, "dbt")) {
                    uint64_t b[32], fb[32];
                    fprintf(stderr, "running dbt...\n"); fflush(stderr);
                    run_dbt(code, b, fb);
                    fprintf(stderr, "dbt returned\n"); fflush(stderr);
                    return 0;
                }
            }
            fflush(stderr);
        }
        int reg = compare(code);
        if (reg >= 0) {
            failures++;
            report(shrink(sq).code, reg);
            if (failures >= 3) {
                printf("\n(stopping after 3 divergences)\n");
                break;
            }
        }
    }

    // Declines are runs where dbt_run never reached the ecall (translate
    // failure or dispatch cap).  Printed because a fuzzer quietly declining
    // most of its corpus would report a clean sweep while testing almost
    // nothing -- the same vacuous-pass shape this codebase keeps hitting.
    printf("\n=== dbt fuzz: %ld sequences, %d divergences, %ld declined ===\n",
           iters, failures, g_declined);
    return failures ? 1 : 0;
}
