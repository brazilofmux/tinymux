# tests/profile — profiling a live netmux, one phase at a time

**This is not `tests/perf`, and the difference is the point.** `tests/perf`
(#2048) tracks rvbench microbenchmark timings against a committed per-machine
baseline and gates on regressions — it answers *"did this commit make the
evaluator slower?"* This directory answers *"where does a live game spend its
time?"*, across the paths `muxscript` cannot reach at all: descriptors, the
network, attribute writes, and the dump. It is #2046's step 4, and it is
**attribution, not regression detection** — see "Not a gate" below.

`testcases/tools/PerfSmoke` profiles `muxscript` running the smoke suite. That
is a fine **compiler** benchmark and a misleading **game** benchmark: no
network, no descriptors, no database writes, no dump. Measured 2026-08-04,
46–50% of that profile is JIT compilation and 4.3% is JIT-generated code
executing, because 1605 one-shot test expressions compile once and run once —
the opposite of what a game does.

This harness is the other workload. It spins a throwaway netmux on a
kernel-assigned free port, attaches `perf record`, and drives it through
phases that each do one kind of work:

| phase | what it exercises |
|---|---|
| `boot` | database load, listener up |
| `create` | object creation |
| `attrwrite` | attribute store, spread over every object |
| `dump` | `@dump` — a path `muxscript` cannot reach at all |
| `attrread` | `get()` across the object set |
| `idle` | event-loop / timer floor with 32 sessions attached |
| `dispatch` | built-in command, no softcode evaluation |
| `softcode_arith` | JIT-friendly arithmetic |
| `softcode_list` | `iter()` / list functions |

Phases are never blended. The mix that approximates any particular game is a
modelling assumption and belongs to whoever reads the numbers.

## Running

```
tests/profile/run.sh [--scale N] [--keep]
PERF_QUIET=1 tests/profile/run.sh          # discard the server's stderr
```

Opt-in, and deliberately **not** part of `make test`: it takes minutes, it is
timing-sensitive, and a benchmark that fails the build on a noisy box is worse
than no benchmark.

## Reading the output

**Read the `usr=` / `sys=` columns, not the rates.** They come from
`/proc/<pid>/stat` and are exact. Wall-clock throughput saturates around
6000 cmd/s with `PERF_QUIET=1` — at the *same* rate for every phase, which
means the driver rather than netmux is the limit there.

**The percentages are of user time only.** `kptr_restrict` blocks
`/proc/kallsyms` on most hosts, so kernel samples cannot be symbolized; the
first version of this harness reported a 72–100% `[unknown]` bucket per phase,
which is unreadable and looks like a quiet server. Sampling is therefore
`task-clock:u`, and kernel time is counted exactly in the `sys=` column
instead of being sampled badly.

## Not a gate

#2048 measured a socket-driven harness at **82% run-to-run spread** and deleted
it in favour of parsing the smoke log. That finding is real and it applies
here too — just at a different scale, because that harness was timing rvbench
legs at nanoseconds per call while these phases run for seconds and read exact
CPU counters.

Three consecutive runs, `--scale 0.5`, same build, Linux/x86-64:

| phase | wall spread | server-user-CPU spread |
|---|---:|---:|
| `create` | 2.2% | 5.4% |
| `attrwrite` | 3.8% | **1.0%** |
| `attrread` | 3.4% | 4.5% |
| `dispatch` | 4.1% | 15.6% |
| `softcode_arith` | 5.3% | 18.8% |
| `softcode_list` | 8.2% | **29.0%** |
| `dump` | **238%** | — (rounds to zero) |

The pattern is that stability tracks how much work the phase does: `attrwrite`
spends 1.0s of CPU and repeats to 1.0%, `softcode_list` spends 0.10s and
repeats to 29%. `dump` costs essentially nothing under the SQLite backend, so
its wall time is pure noise.

So: good enough to say *where the time goes*, not good enough to fail a build
on. Use `tests/perf` for that.

## Two things this harness had to learn the hard way

**`perf` attaches to the wrong process.** `( cd X && cmd ) &` leaves the
subshell as `cmd`'s parent, and `$!` is the subshell. `perf record -p` on it
collects nothing, because perf's inherit only follows children created *after*
the attach. A `perf.data` with a header and no samples slices into "no samples
in window" for every phase, which reads exactly like an idle server. `run.sh`
now resolves the real pid and fails loudly on an empty recording.

**`@dump` forks.** `do_dump()` calls `fork_and_dump()`, which returns as soon
as the child exists, so the phase timed a `fork` (0.016s regardless of database
size). The generated config sets `fork_dump no` so the dump happens inline and
the phase measures the work.
