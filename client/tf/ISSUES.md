# TinyFugue C++ Client Review Issues

This file was rebuilt from the current `client/tf` code after reviewing the
import in commit `191a98a17473187009b9c886773957730641bceb`. The original
imported issue list was stale and no longer matched the implementation.

## High-Risk Correctness Bugs

- **The display model is still one shared output buffer, not per-world screens**  
  Background worlds are prefixed and dumped into the same `Terminal::output_lines_`
  buffer, and `/fg` only changes `app.fg` plus scroll position. It does not
  restore that world's own view. This is a major behavioral gap from classic
  TinyFugue's per-screen model.  
  Refs: `client/tf/src/main.cpp:269`, `client/tf/src/command.cpp:213`,
  `client/tf/src/terminal.cpp:448`  
  Comparison: `/home/sdennis/tinyfugue/src/tfio.h`, `/home/sdennis/tinyfugue/src/output.c`

## Broken Or Misleading Commands

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

- **`/status_add`, `/status_edit`, `/status_rm` are explicit stubs**  
  They still report planned behavior instead of mutating status bar layout.  
  Refs: `client/tf/src/command.cpp:1256`

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
