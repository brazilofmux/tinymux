# TinyFugue C++ Client Review Issues

This file was rebuilt from the current `client/tf` code after reviewing the
import in commit `191a98a17473187009b9c886773957730641bceb`. Updated after
code review of commits `6ceb3ecf..HEAD` (2026-03-17).

## Bugs

(None currently known.)

## Stubbed Or Partially Implemented Interfaces

- **`@read` and `@mail` internal status fields are stubs**
  `@active` and `@log` are functional. `@read` needs background
  unread line tracking. `@mail` needs a mail indicator source.

- **Format variable evaluation does not cache compiled expressions**
  `status_int_*` and `status_var_*` variables are now evaluated via
  `expand_subs()` (supporting `%{var}` and `$[expr]` substitutions),
  but the expression is re-parsed on every status bar redraw. Classic
  TF caches the compiled program. This is unlikely to matter in
  practice since redraws are infrequent.

- **Status bar attributes limited to bold/underline/reverse**
  Classic TF allows arbitrary color/attribute combinations. Consider
  extending to support fg/bg color names or numeric ANSI codes.

## Telnet Protocol Issues

- **Limited Telnet Option Support**
  The client now handles `TTYPE`, basic `NAWS`, `BINARY`, and UTF-8
  `CHARSET` negotiation, but advanced features like MCCP compression and
  richer charset handling are still missing compared to classic TinyFugue.

## Opportunities for Improvement

- **TrueColor (24-bit) support**
  The current renderer converts RGB to xterm-256. Modern terminals support
  24-bit color, and the engine should support `ESC[38;2;R;G;Bm` sequences.

- **Shell process read loop doesn't distinguish EAGAIN from errors**
  (main.cpp:448-460) When `read()` returns -1, the loop breaks without
  checking `errno`. EAGAIN is handled implicitly (poll retries next
  iteration) but an actual error leaves the fd open. Consider closing
  on non-EAGAIN errors.

## Notes

- The codebase now implements far more than the original imported issue list
  claimed: `/def`, hooks, triggers, `/repeat`, `/bind`, many expression
  builtins, `/log`, `/save`, `/saveworld`, `/limit`, `/eval`, `/quote`, and
  more all exist in some form.
- The main gaps are no longer "feature absent" so much as "feature present
  but semantics incomplete, placeholder, or unlike TinyFugue."
- Status bar now supports: variable-driven fields (any `/set` variable
  displayed live), all classic internal fields (`@world`, `@more`,
  `@clock`, `@active`, `@log`, `@read`, `@mail`), format variables
  (`status_int_*`, `status_var_*`), dynamic attribute variables
  (`status_attr_int_*`, `status_attr_var_*`), right-justification
  (negative widths), and reactive redraw on `/set`.
