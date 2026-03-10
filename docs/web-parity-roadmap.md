# Web Parity Roadmap

Tracking features identified in the server surveys (docs/survey-*.md) to close
the gap between TinyMUX and PennMUSH/RhostMUSH on web integration.

## Tier 1 — High Value, Implement

| # | Feature | Status | Commit | Notes |
|---|---------|--------|--------|-------|
| 1 | base64 encode/decode | **Done** | 7f9e8cb0 | encode64(), decode64() in funcweb.cpp |
| 2 | JSON support | **Done** | 7f9e8cb0 | isjson() native RFC 8259; json(), json_query(), json_mod() via SQLite JSON1 |
| 3 | HMAC | **Done** | 7f9e8cb0 | hmac() via OpenSSL, default sha256, any digest() algo |
| 4 | WebSocket support | **Done** | a3fe85bb, b1833c20 | RFC 6455 same-port auto-detect. ws:// and wss:// via deferred protocol detection. |
| 5 | Connection logging | TODO | | SQLite table for connect/disconnect audit trail. connlog()/addrlog() softcode functions. Penn has this. |
| 6 | printf() | **Done** | | ANSI-aware formatted output. %s/%d/%f/%c with width, alignment (-/=), precision, zero-pad. |

## Tier 2 — Medium Value, Consider

| # | Feature | Status | Notes |
|---|---------|--------|-------|
| 7 | letq() | TODO | Scoped named registers (Penn) |
| 8 | sortkey() | TODO | Sort by computed key (Penn) |
| 9 | Account system | TODO | Multi-character accounts (Rhost) |
| 10 | url_escape/url_unescape | **Done** | RFC 3986 percent-encoding. + decoded as space for form compat. |
| 11 | Template system | TODO | template() presentation/logic separation (Rhost) |
| 12 | strdistance() | **Done** | Levenshtein edit distance, Unicode-aware. |
| 13 | lockencode/lockdecode | TODO | Programmatic lock manipulation (Rhost) |
| 14 | Regex attr matching | TODO | reglattr/regnattr family (Penn) |
| 15 | Character classification | **Partial** | isalpha(), isdigit(), isalnum() done via DFA. isunicode()/isutf8() deferred. |
| 16 | dynhelp() | TODO | Dynamic help from object attributes (Rhost) |
| 17 | mailsend() | TODO | Send mail from softcode. @mail exists as command. |

## Tier 3 — Long-Term

| # | Feature | Status | Notes |
|---|---------|--------|-------|
| 18 | HTTP server | TODO | Built-in HTTP request handling (Penn). Big attack surface. |
| 19 | Scripting language | TODO | Lua or leverage AST/JIT from ~/slow-32 and ~/risv |
| 20 | @cron | TODO | Cron-style scheduling (TinyMUSH) |
| 21 | Totem/tag system | TODO | User-definable markers beyond MARKER0-9 (Rhost) |
| 22 | Shared-memory debugging | TODO | Live debugging via shmem IPC (Rhost) |
| 23 | PCG RNG | TODO | Modern random number generator (Penn) |

## Architecture Notes

- WebSocket goes in the driver (networking layer). Needs HTTP upgrade
  handshake detection in the connection accept path, then frame codec
  for ongoing I/O. Output channels: text, JSON, HTML, prompt.
- Connection logging is a natural fit for SQLite — separate table or
  separate database file. R-tree index for time-range queries (Penn's
  approach).
- printf() is pure engine-side string formatting. No dependencies.
- Tier 2 items are mostly self-contained engine functions.
- Tier 3 items are architectural and need design work first.
