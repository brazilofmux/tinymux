# Hydra Proxy — Open Issues

(none)

## Closed

- **GANL Integration** — Front/back doors already use GANL exclusively. The facade classes delegate to SessionManager which handles all networking via GANL.
- **Status Dumps** — Implemented `SessionManager::dumpStatus()`, wired to SIGUSR1 signal. Logs session count, front/back door counts, per-session state, links, and scrollback metrics.
- **Duration Strings** — `parseDuration()` in config.cpp supports bare integers (seconds) and suffixed values (s/m/h/d). Applied to all timeout fields.
- **Scrollback Re-encryption** — `ScrollBack::reencryptInDb()` decrypts with old key and re-encrypts with new key. Called from `changePassword()` for all sessions owned by the account.
