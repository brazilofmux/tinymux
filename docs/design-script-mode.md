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

## Architecture: Separate Binary via COM

The key insight: **don't modify netmux.** netmux is the network driver —
GANL, telnet, SSL, descriptor management, charset conversion. Script
mode needs none of that. engine.so already contains the full evaluator,
database, commands, functions — everything script mode needs.

Build a new minimal binary (`mux` or `muxscript`) that loads engine.so
through the same COM interface that netmux uses. This is a second
driver for the same engine — one that reads stdin instead of sockets.

```
netmux (existing)           mux (new)
├─ ganl_adapter.cpp         ├─ mux_main.cpp (~300 lines)
├─ driver.cpp               ├─ modules_cli.cpp (COM loader, ~200 lines)
├─ net.cpp                  └─ (that's it)
├─ telnet.cpp
├─ sitemon.cpp
├─ signals.cpp
├─ modules.cpp (COM loader)
└─ (2000+ lines of networking)
```

### What `mux` Contains

 - **mux_main.cpp**: argv parsing, COM initialization, LoadGame,
   script loop (read stdin → process_command → drain queue),
   DumpDatabase, Shutdown. ~300 lines.
 - **modules_cli.cpp**: Stripped-down COM front-door. Loads engine.so
   via dlopen, calls mux_Register/mux_GetClassObject to get
   mux_IGameEngine. No connection manager, no server events source.
   ~200 lines — literally just the dlopen + COM query.
 - Links against: libmux.so (core), dl, sqlite3.
 - Does NOT link: GANL, epoll, OpenSSL, telnet, sitemon, signals.

### COM Interfaces Used

```
mux_IGameEngine         — LoadGame, Startup, Shutdown, RunTasks
mux_INotify             — notify/raw_notify (output interception)
```

The script binary acquires IGameEngine from engine.so via COM.
LoadGame loads the database. The script loop calls process_command
for each input line, then RunTasks to drain the queue.

### Output Interception

**Option A: INotify hook (preferred)**

The mux_INotify COM interface already exists. The script binary
provides its own INotify implementation that writes to stdout:

```cpp
class CScriptNotify : public mux_INotify {
    void RawNotify(dbref target, const UTF8 *msg, int len) override {
        fwrite(msg, 1, len, stdout);
        fputc('\n', stdout);
    }
};
```

Register this as the IServerEventsSink before calling LoadGame.
All notify/pemit/think output goes to stdout. Zero changes to
the DESC infrastructure. No synthetic descriptors.

**Option B: Synthetic DESC** — only if Connected() compatibility
is critical. Creates one DESC with socket=STDOUT_FILENO.

### Boot Sequence

```
mux main()
  ├─ Parse argv: -e, -c, --readonly, --who, files
  ├─ dlopen engine.so
  ├─ COM: GetClassObject → IGameEngine
  ├─ Register CScriptNotify as INotify sink
  ├─ IGameEngine::LoadGame(conf)
  ├─ IGameEngine::Startup()
  │
  ├─ script_loop(executor, input):
  │     for each line:
  │       process_command(executor, ...)
  │       IGameEngine::RunTasks(now)   // drain queue
  │     final RunTasks drain
  │
  ├─ IGameEngine::DumpDatabase()  (unless --readonly)
  └─ IGameEngine::Shutdown()
```

No ganl_initialize. No epoll. No listener. No connections.
No telnet negotiation. No charset conversion. No OpenSSL.

### Command Processing

Script mode runs a synchronous loop:

```cpp
void script_loop(dbref executor, FILE *input, mux_IGameEngine *engine) {
    UTF8 line[LBUF_SIZE];
    while (fgets(line, sizeof(line), input)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        process_command(executor, executor, executor,
                        0, true, line, nullptr, 0);
        engine->RunTasks(CLinearTimeAbsolute::GetUTC());
    }
    // Final drain.
    engine->RunTasks(CLinearTimeAbsolute::GetUTC());
}
```

### Database Considerations

Same as before: full database load. `--readonly` skips DumpDatabase.
For disposable usage:
```bash
cp -r game/ /tmp/test/ && mux -c /tmp/test/netmux.conf < test.mux
```

### Connected() and Descriptor Checks

With Option A, the executing player has no DESC. `Connected(#1)`
returns false. `lwho()` returns empty. `conn()` returns -1.
Acceptable for scripting. Option B (synthetic DESC) if needed.

## Implementation Plan

### Phase 1: Minimal Binary

 1. `mux/script/mux_main.cpp` — argv, COM init, script_loop
 2. `mux/script/modules_cli.cpp` — dlopen engine.so, COM query
 3. `mux/script/Makefile.in` — build `mux` binary, link libmux.so
 4. `mux/script/CScriptNotify.cpp` — INotify → stdout
 5. Test: `echo 'think add(2,3)' | ./bin/mux -c smoke.conf`

Estimated: ~500 lines total. No changes to engine.so or netmux.

### Phase 2: Usability

 6. `-e 'expr'` single expression mode
 7. `--who player` to set executor
 8. `--readonly` flag
 9. ANSI stripping (default) / `--ansi` passthrough
 10. Error messages to stderr
 11. Shebang: `#!/usr/local/bin/mux`
 12. Comment and blank line handling

### Phase 3: Smoke Test Integration

 13. `tools/ScriptSmoke` — runs key tests via `mux`
 14. Compare output against expected (`diff`)
 15. No port, no expect, no race conditions, no telnet
 16. Parallel to existing Smoke; eventually replace it

### Phase 4: Queue and Timing

 17. Proper drain with infinite loop protection
 18. @wait 0 → immediate, @wait N → warn
 19. @halt support

## Relationship to Other Work

### Engine/Driver Split (Phase 5)

This IS the payoff of the engine/driver split. engine.so is a COM
server. netmux is one client. `mux` is another. Same engine, different
front-ends. The split was designed for exactly this use case.

### DBT/JIT Testing

`mux` exercises the JIT through normal softcode:
```bash
echo 'think rveval(add(mul(3,4),5))' | mux -c smoke.conf
```

No port conflicts. No expect. No race conditions. Tests run in
milliseconds, not seconds. Parallel test execution is trivial.

### Smoke Test Migration

Current pain points eliminated:
 - Port conflicts → no port
 - Race conditions → synchronous
 - Fragile expect → simple pipe
 - Slow startup → one process, no connection setup

```bash
result=$(echo 'think sha1(hello world)' | mux -c smoke.conf --readonly)
[ "$result" = "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed" ] || fail
```
