# Titan Web Client -- Open Issues

Updated: 2026-03-29

## Fixed In This Audit

- **Hydra stream framing and prompt handling** -- Fixed.
  `HydraConnection` now buffers `GameOutput.text`, emits complete newline-
  terminated lines to the UI, and treats `GameOutput.end_of_record` as a
  prompt boundary instead of rendering arbitrary stream chunks as full lines.
  This applies to both the WebSocket GameSession path and the grpc-web
  Subscribe fallback.

- **Hydra protobuf field-map drift** -- Fixed.
  The local decode maps now match current `mux/proxy/hydra.proto` for
  `GameOutput.end_of_record`, `ServerMessage.notice`, and `ServerMessage.link_event`.

- **`/hrestart` only matched with a trailing space** -- Fixed.
  Bare `/hrestart` now reaches the existing usage/error handling path instead
  of falling through as normal game input.

- **Quick-connect autosave dropped Hydra world settings** -- Fixed.
  Connecting a Hydra world now persists `transport`, `character`, `password`,
  and `game`, instead of collapsing the saved world back to a plain websocket
  entry.

## ~~Open — Bugs (New, 2026-04-04)~~ FIXED

- **ANSI 256-color/truecolor parser missing bounds checks** — **FIXED**
  - **File:** `client/web/js/terminal.js`
  - The `renderAnsiLine()` SGR parser now validates each palette index and RGB component against `Number.isInteger(v) && v >= 0 && v <= 255` before using it for `\e[38;5;Nm` (256-color fg), `\e[38;2;R;G;Bm` (truecolor fg), and the matching `48;...` background forms. Short/malformed sequences fall through to plain text instead of leaking `undefined` or `NaN` into CSS, and out-of-range component values are also rejected. Verified with `node --check` and a 15-case harness covering well-formed, short, and out-of-range inputs against the extracted `renderAnsiLine` function.

## Open

- **Saved world passwords live in browser localStorage** *(deferred)*
  The settings store persists world passwords directly in localStorage.
  That is convenient, but it means any script running in the origin can read
  them. The native clients now use platform credential stores (Keychain,
  EncryptedSharedPreferences, Windows Credential Manager, chmod 0600).
  The web client's XSS exposure is a different threat model — a future pass
  could encrypt with a user-derived key via Web Crypto, but the priority is
  lower since the web client is not the primary deployment target.

- **No automated browser-level regression coverage**
  The current audit verified JavaScript syntax and inspected the runtime
  integration paths, but there is no checked-in browser harness covering Hydra
  auth, session resume, WebSocket GameSession, grpc-web fallback, prompts, or
  reconnect behavior end-to-end.

## Verification

- `node --check client/web/js/hydra_connection.js`
- `node --check client/web/js/main.js`

## Notes

- `client/web` has no prior `ISSUES.md`; this file starts the current audited
  baseline.
- The Hydra path is materially closer to usable than it looked from memory.
  The main problems found here were integration drift and framing bugs, not a
  missing client architecture.
