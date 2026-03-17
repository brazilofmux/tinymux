# TinyFugue C++ Client Review Issues

This file was rebuilt from the current `client/tf` code after reviewing the
import in commit `191a98a17473187009b9c886773957730641bceb`. The original
imported issue list was stale and no longer matched the implementation.

## High-Risk Correctness Bugs

- **Nonblocking socket writes are not handled correctly**  
  `Connection::connect()` puts sockets into `O_NONBLOCK`, but outbound paths
  still call `write()` / `SSL_write()` as if they were blocking. `send_line()`
  fails on `EAGAIN` or OpenSSL `WANT_*`, telnet negotiation helpers ignore
  short writes entirely, and most callers ignore the return value. This can
  drop user input, scripted `send()`, auto-login traffic, and telnet option
  replies under load or on slow links.  
  Refs: `client/tf/src/connection.cpp:78`, `client/tf/src/connection.cpp:169`,
  `client/tf/src/connection.cpp:368`, `client/tf/src/script_parse.cpp:596`

- **SSL reads can be treated as disconnects on retryable conditions**  
  `read_lines()` calls `SSL_read()` on a nonblocking socket, but on `<= 0` it
  checks `errno` instead of `SSL_get_error()`. `SSL_ERROR_WANT_READ` and
  `SSL_ERROR_WANT_WRITE` are therefore liable to look like hard failures and
  tear down a live TLS session.  
  Refs: `client/tf/src/connection.cpp:197`, `client/tf/src/connection.cpp:203`

- **Prompt handling duplicates text and does not behave like an inline prompt**  
  `check_prompt()` returns the current partial line without clearing it, and the
  event loop renders that prompt through `print_line()`, which appends a normal
  scrollback line. When the server later finishes the line with `\n`, the full
  line is printed again, so prompts show up as standalone lines plus the final
  completed line.  
  Refs: `client/tf/src/connection.h:41`, `client/tf/src/connection.cpp:415`,
  `client/tf/src/main.cpp:296`, `client/tf/src/terminal.cpp:448`

- **The display model is still one shared output buffer, not per-world screens**  
  Background worlds are prefixed and dumped into the same `Terminal::output_lines_`
  buffer, and `/fg` only changes `app.fg` plus scroll position. It does not
  restore that world's own view. This is a major behavioral gap from classic
  TinyFugue's per-screen model.  
  Refs: `client/tf/src/main.cpp:269`, `client/tf/src/command.cpp:213`,
  `client/tf/src/terminal.cpp:448`  
  Comparison: `/home/sdennis/tinyfugue/src/tfio.h`, `/home/sdennis/tinyfugue/src/output.c`

## Broken Or Misleading Commands

- **`/trigger` is not a working alias for trigger definitions**  
  The implementation rewrites `/trigger ...` to `cmd_def("-t'" + args + "'")`,
  which turns the entire argument string into the trigger pattern and leaves no
  valid macro name/body to parse.  
  Refs: `client/tf/src/command.cpp:742`

- **`/watchdog` ignores the command it claims to schedule**  
  The command comment says it should run a command after a pattern/idle event,
  but the created macro body is always empty, so nothing executes.  
  Refs: `client/tf/src/command.cpp:1298`

- **`/saveworld` can emit invalid world files**  
  Empty `mfile` is serialized as bare `foo`, not a quoted empty string, and the
  field is emitted without quoting or escaping either way. Reloading the saved
  file is therefore unreliable.  
  Refs: `client/tf/src/command.cpp:1090`

- **`/shift` does not operate on macro positional parameters**  
  Positional parameters live in the per-execution `ScriptEnv` created by
  `exec_body()`, but `/shift` edits `app.vars`. Inside a macro it therefore
  modifies the wrong storage and does not shift `%{1}`, `%{2}`, or `%{#}` for
  the current call frame.  
  Refs: `client/tf/src/macro.cpp:433`, `client/tf/src/command.cpp:1026`

- **`/localecho` is currently a no-op**  
  The command toggles a variable, but outbound input/rendering paths never read
  that variable, so local echo behavior does not actually change.  
  Refs: `client/tf/src/command.cpp:1232`, `client/tf/src/main.cpp:108`,
  `client/tf/src/macro.cpp:423`

- **`/limit` and `/relimit` only affect new lines**  
  Filtering is applied in the live receive path only. `/relimit` does not
  re-render from stored scrollback; it just prints a status line.  
  Refs: `client/tf/src/main.cpp:260`, `client/tf/src/command.cpp:1142`,
  `client/tf/src/command.cpp:1166`

- **Input history is global, not per-world**  
  There is a single `Terminal::input_history_`, so switching worlds shares one
  recall ring across all sessions. The old imported issue was still correct on
  this point.  
  Refs: `client/tf/src/terminal.h:100`, `client/tf/src/terminal.cpp:287`

## Stubbed Or Partially Implemented Interfaces

- **`/input`, `/status_add`, `/status_edit`, `/status_rm` are explicit stubs**  
  They report planned behavior or echo arguments instead of mutating the input
  buffer or status bar layout.  
  Refs: `client/tf/src/command.cpp:1160`, `client/tf/src/command.cpp:1256`

- **The keyboard-buffer scripting functions are placeholders**  
  `kblen`, `kbpoint`, `kbhead`, `kbtail`, `kbgoto`, `kbdel`, `kbmatch`,
  `kbwordleft`, and `kbwordright` return canned values instead of exposing the
  real editable line state. This blocks TF scripts such as completion helpers.  
  Refs: `client/tf/src/script_parse.cpp:909`  
  Comparison: `/home/sdennis/tinyfugue/tf-lib/complete.tf`

- **Pager and more-related APIs are placeholders**  
  `morepaused`, `morescroll`, and `moresize` are stubs, while the old client has
  a substantial pager/more implementation.  
  Refs: `client/tf/src/script_parse.cpp:953`  
  Comparison: `/home/sdennis/tinyfugue/src/output.c`

- **`substitute()` and `prompt()` do not match TF semantics**  
  `substitute()` returns success without modifying output, and `prompt()` just
  prints a normal line into the terminal buffer.  
  Refs: `client/tf/src/script_parse.cpp:982`, `client/tf/src/script_parse.cpp:987`

- **`fake_recv()` bypasses the real receive pipeline**  
  It prints directly to the terminal before running triggers, only appends to
  the foreground world's scrollback, and does not give gag/hilite logic a chance
  to control display.  
  Refs: `client/tf/src/script_parse.cpp:994`, `client/tf/src/main.cpp:252`

- **Status-bar scripting APIs are not wired up**  
  `status_fields()` always returns `""`, even though status width is exposed and
  commands imply a future multi-field status bar.  
  Refs: `client/tf/src/script_parse.cpp:968`, `client/tf/src/command.cpp:1256`

## Notes

- The codebase now implements far more than the original imported issue list
  claimed: `/def`, hooks, triggers, `/repeat`, `/bind`, many expression
  builtins, `/log`, `/save`, `/saveworld`, `/limit`, `/eval`, `/quote`, and
  more all exist in some form.
- The main gaps are no longer "feature absent" so much as "feature present but
  semantics incomplete, placeholder, or unlike TinyFugue."
