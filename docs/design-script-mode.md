# Script Mode: TinyMUX as a Unix Scripting Language

## Status: Proposal

Branch: `brazil`

## Motivation

TinyMUX softcode is a complete programming language: string manipulation,
arithmetic, control flow (if/switch/iter), regular expressions, a
persistent object database, and 300+ built-in functions.  But the only
way to execute it is to connect via telnet/WebSocket to a running server.

Script mode makes netmux a Unix command-line tool:

```bash
#!/usr/local/bin/netmux --script
think add(2,3)
think iter(a b c, strlen(##))
@dolist lnum(1,10)=think mul(##,##)
```

```
$ chmod +x example.mux && ./example.mux
5
1 1 1
1
4
9
...
```

### Benefits

 - **Simpler testing.**  The current smoke test infrastructure requires
   expect(1), a TCP listener on port 2860, telnet protocol negotiation,
   and log file parsing.  Script mode replaces that with:
   ```
   echo 'think sha1(hello)' | netmux --script | grep aaf4c61d
   ```

 - **Shell integration.**  Pipe MUX output into grep/awk/jq.  Use
   softcode for string processing in shell scripts.  The database
   becomes a persistent environment across invocations.

 - **Batch administration.**  Run maintenance scripts without a network
   connection: `netmux --script < cleanup.mux`

 - **Embeddable evaluation.**  Other tools can invoke netmux as a
   subprocess for softcode evaluation, similar to how `jq` is used
   for JSON processing.

### Non-Goals

 - This is NOT a standalone DBT/JIT test harness.  Script mode tests
   the full pipeline (softcode -> AST -> RV64 -> x86-64 -> output).
   Instruction-level DBT testing (load raw RV64 binary, verify
   translation of individual instructions) is a separate tool that
   operates below the softcode layer.

 - This does not replace the network server.  Script mode is an
   additional invocation mode, not a replacement for the telnet/
   WebSocket game server.

 - No multiplayer.  Script mode is single-user, single-threaded,
   no connections.  The DESC descriptor chain is empty (or contains
   one synthetic entry).

## Interface

### Invocation

```
netmux --script [file]          # execute file (or stdin if omitted)
netmux --script -e 'expr'       # evaluate single expression
netmux --script -c conf.conf    # specify config file (default: netmux.conf)
netmux --script --db path/      # specify game directory
netmux --script --who player    # execute as player (default: #1)
netmux --script --readonly      # don't modify database on exit
```

Shebang support:
```
#!/usr/local/bin/netmux --script
```

### Input

Each line of input is processed as if typed by the executing player
at the MUX command prompt.  All standard commands work:

```
think [expression]        → evaluate, print result to stdout
@set obj=attr:value       → modify database
@create Thing             → create objects
@dolist list=command      → iterate (queued)
@trigger obj/attr         → fire attribute (queued)
@wait 0=command           → queue for next cycle
@fo player=command        → force another object
&attr obj=value           → set attribute
```

Lines beginning with `#` (that are not object references like `#123`)
are treated as comments and ignored.  Blank lines are ignored.

### Output

Text that would normally be sent to the player's connection is written
to stdout:

 - `think` output → stdout
 - `@pemit` to the executing player → stdout
 - `say`, `pose` in the player's location → stdout
 - Page/mail notifications → stdout
 - Error messages (`Huh?`, permission errors) → stderr

All ANSI color codes are stripped by default (plain text).  Use
`--ansi` to preserve them.

### Exit

Script mode exits when:
 1. Input is exhausted (EOF on stdin or end of file), AND
 2. The command queue has drained (all @dolist/@trigger/@wait 0
    complete).

Exit code:
 - 0: normal completion
 - 1: database load failure
 - 2: script error (configurable via `@shutdown/exit=N`)

The database is saved on exit unless `--readonly` is specified.

### Environment Variables

```
TINYMUX_DB      — game directory (overridden by --db)
TINYMUX_CONF    — config file path (overridden by -c)
```

## Architecture

### Boot Sequence

Script mode shares the normal boot path up to the networking split:

```
main()
  ├─ Parse argv: detect --script flag
  ├─ FLOAT_Initialize(), pools, modules, COM interfaces
  ├─ LoadGame()  — full database + evaluator init
  │   ├─ cf_init(), init_functab(), init_cmdtab()
  │   ├─ cf_read() — config file
  │   └─ database load (SQLite or flatfile)
  ├─ Startup()  — dbck, process_preload
  │
  ├─ if (script_mode) {
  │     script_main(executor, input_stream);
  │     // does NOT call ganl_initialize()
  │  } else {
  │     ganl_initialize();
  │     ganl_main_loop();
  │     ganl_shutdown();
  │  }
  │
  ├─ DumpDatabase()  — save (unless --readonly)
  └─ Shutdown()
```

### Output Interception

The notify pathway currently requires a DESC:

```
think "hello"
  → do_think() → notify(player, "hello")
    → raw_notify() → send_text_to_player()
      → for each DESC of player: queue_string(d, msg)
```

Script mode needs to intercept at the `raw_notify` level.  Two options:

**Option A: Notify hook (preferred)**

Add a function pointer `mudstate.script_notify_fn` that, when non-null,
is called instead of `send_text_to_player()`:

```cpp
// In raw_notify():
if (mudstate.script_notify_fn) {
    mudstate.script_notify_fn(target, msg, len);
    return;
}
// ... normal DESC path
```

The script mode sets this to a function that writes to stdout (fd 1).
This requires zero changes to the DESC infrastructure.  No synthetic
descriptors, no fake sockets.

**Option B: Synthetic DESC**

Create a single DESC with `socket = STDOUT_FILENO` and special
`DS_CONSOLE` flag.  Output goes through the normal queue_string path
but process_output writes to fd 1 instead of a socket.

Option A is simpler and avoids touching the DESC/GANL code.  Option B
is more compatible with code that checks `Connected(player)` or
iterates descriptors.

### Command Processing

Script mode runs a synchronous loop:

```cpp
void script_main(dbref executor, FILE *input) {
    UTF8 line[LBUF_SIZE];
    while (fgets(line, sizeof(line), input)) {
        // Skip comments and blank lines.
        if (line[0] == '#' || line[0] == '\n') continue;

        // Process as if typed by executor.
        process_command(executor, executor, executor,
                        0, true, line, nullptr, 0);

        // Drain the command queue (handles @dolist, @trigger, etc.)
        drain_queue();
    }
    // Final drain for any remaining queued commands.
    drain_queue();
}
```

The `drain_queue()` function runs queued commands until the queue is
empty.  This replaces the GANL event loop's periodic `RunTasks()` call.
It must handle:

 - Immediate queue entries (@dolist, @trigger fired during processing)
 - @wait 0 entries (deferred to next "cycle")
 - Recursive triggers (A triggers B triggers C — all drain)
 - Infinite loop protection (configurable limit, default 10000 commands)

Timed @wait (e.g., `@wait 5=command`) is problematic in script mode.
Options:
 - Ignore timed waits (skip, warn to stderr)
 - Treat all @wait as @wait 0 (immediate)
 - Actually sleep (makes script mode slow but correct)

Recommended: treat `@wait 0` as immediate, warn on `@wait N>0`.

### Database Considerations

Script mode loads the full database.  Changes made by the script
(object creation, attribute modification, etc.) are persisted on exit
unless `--readonly` is specified.

For disposable/test usage:
```bash
# Copy database, run script, discard
cp -r game/ /tmp/test-game/
netmux --script --db /tmp/test-game/ < test.mux
```

Or with `--readonly`, which skips the final DumpDatabase().

### Connected() and Descriptor Checks

Many softcode functions and commands check `Connected(player)`.  In
script mode with Option A (notify hook), the executing player has no
DESC, so `Connected(#1)` returns false.  This affects:

 - `@pemit` to self (checks Connected) — would silently fail
 - `lwho()` — returns empty
 - `conn()` — returns -1
 - `idle()` — returns -1

These are acceptable for scripting.  If full compatibility is needed,
Option B (synthetic DESC) makes the player appear connected.

## Implementation Plan

### Phase 1: Minimal Viable Script Mode

 1. Add `--script` flag parsing in driver.cpp main()
 2. Add `mudstate.script_notify_fn` hook in raw_notify()
 3. Implement `script_main()` loop: read lines, process_command, drain
 4. Wire into main() as alternative to ganl_main_loop()
 5. Test with `echo 'think add(2,3)' | netmux --script`

Estimated: ~150 lines of new code (mostly in driver.cpp).

### Phase 2: Usability

 6. Comment and blank line handling
 7. `--readonly` flag (skip DumpDatabase)
 8. `-e 'expr'` single expression mode
 9. `--who player` to set executor
 10. ANSI stripping (default) / `--ansi` passthrough
 11. Error messages to stderr
 12. Shebang support validation

### Phase 3: Smoke Test Integration

 13. Write a `script_test.sh` that runs key tests via `--script`
 14. Compare output against expected (like `diff` or `cmp`)
 15. Optionally: parallel to existing expect-based Smoke, eventually
     replace it

### Phase 4: Queue and Timing

 16. Proper drain_queue() with infinite loop protection
 17. @wait 0 support
 18. @wait N warning/handling policy
 19. @halt support (script halts the queue)

## Relationship to Other Work

### DBT/JIT Testing

Script mode exercises the JIT through normal softcode:
```
think rveval(add(mul(3,4),5))
```

This tests the full compilation pipeline but NOT individual RV64
instructions.  A standalone DBT test harness (Stage 1e-ii in
design-jit-compiler.md) would operate at a lower level:

```cpp
// Hypothetical DBT test:
uint32_t code[] = { rv_ADDI(10, 0, 42), rv_ECALL() };
memcpy(memory, code, sizeof(code));
int rc = dbt_run(&dbt, 0, STACK_TOP);
assert(dbt.ctx.x[10] == 42);
```

The two are complementary: script mode for end-to-end, DBT harness
for instruction-level.

### Engine/Driver Split (Phase 5)

Script mode benefits from the engine/driver split.  With engine.so
as a COM server, script mode is essentially a different driver that
loads the same engine — one that reads stdin instead of sockets.
Long-term, script mode could be a separate binary (`muxeval`) that
links engine.so directly, without any of the networking code.

### Smoke Test Migration

The current smoke test infrastructure (expect + telnet + port 2860)
has several pain points:
 - Port conflicts with running servers
 - Race conditions in Makesmoke (dump timing)
 - Fragile expect scripting
 - Slow startup/shutdown per test run

Script mode eliminates all of these.  A test becomes:
```bash
result=$(echo 'think sha1(hello world)' | netmux --script --readonly)
[ "$result" = "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed" ] || fail
```

Migration can be incremental: new tests use script mode, existing
tests continue with Smoke until ported.
