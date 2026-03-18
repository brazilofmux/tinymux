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

- **Status bar attributes: classic TF's single-char syntax not supported**
  The full word-based attribute syntax is supported (bold, dim,
  underline, blink, reverse, all 8 color names, bright variants,
  bg variants, xterm-256 via color0..color255 / bg_color0..bg_color255).
  Classic TF's single-character codes (`B`=bold, `Cred`=fg red, etc.)
  are not recognized — only the long-form names work.

## Telnet Protocol Issues

- **Richer charset handling**
  The client negotiates UTF-8 via `CHARSET` (telnet option 42) but
  does not handle other charsets (e.g., ISO-8859-1 → UTF-8 conversion).
  The `./utf` pipeline machinery could potentially be leveraged for this.

## Opportunities for Improvement

- **TrueColor fallback now uses CIE97 perceptual distance**
  When TrueColor is not available, `ESC[38;2;R;G;Bm` falls back to the
  nearest xterm-256 color via libmux's `co_nearest_xterm256()` which
  uses CIE97 perceptual distance with K-d tree search. The old
  Euclidean-in-RGB `rgb_to_xterm()` is retained as a static utility
  but no longer used for rendering.

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
