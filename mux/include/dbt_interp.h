/*! \file dbt_interp.h
 * \brief RV64IMD reference interpreter — state and public API.
 *
 * The interpreter is a straightforward fetch-decode-execute loop
 * over a 64-bit register file.  It executes from a flat memory
 * buffer provided by the caller.
 *
 * ECALL dispatches to a caller-provided callback function, which
 * is the extensibility point: for testing, the callback is a test
 * harness; for TinyMUX, it will be the EngineAPI dispatch.
 */

#ifndef DBT_INTERP_H
#define DBT_INTERP_H

#include <cstdint>
#include <cstddef>

// Guest memory — flat byte buffer provided by caller.
//
struct rv64_memory_t {
    uint8_t *data;
    size_t   size;
};

// Guest CPU state.
//
struct rv64_state_t {
    uint64_t x[32];       // General-purpose registers (x[0] always 0)
    uint64_t pc;          // Program counter
    uint64_t insn_count;  // Instructions executed
    uint64_t f[32];       // FP registers (64-bit double, no NaN-boxing)
    uint32_t fcsr;        // FP control/status: frm[7:5], fflags[4:0]
};

// ECALL handler callback.
//
// Called when the guest executes an ECALL instruction.
// Arguments are in state->x[10]-x[17] (a0-a7).
// The handler should place its return value in state->x[10] (a0).
//
// Return value:
//   < 0  — continue execution
//   >= 0 — halt; the value is the exit code
//
typedef int (*rv64_ecall_fn)(rv64_state_t *state, void *user_data);

// Run the interpreter until halt.
//
// Returns the exit code from the ECALL handler, or -1 on error
// (illegal instruction, memory fault, etc.).
//
int rv64_interp_run(rv64_state_t *state, rv64_memory_t *mem,
                    rv64_ecall_fn ecall_handler, void *ecall_user_data);

#endif // DBT_INTERP_H
