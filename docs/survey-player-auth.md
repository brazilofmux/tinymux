# Survey: player auth & password handling (player.cpp)

Audit of `mux/modules/engine/player.cpp` — the highest-stakes security surface:
password hashing/verification, the connect/login flow, password changes, and
login-history bookkeeping. Methodology matches the parser/JIT/wild campaign.

**Result: well-designed. One real (platform-dependent) auth-path crash found and
fixed (#842); the rest of the path is sound.**

## #842 (6f11ec0e9) — check_pass null-deref on crypt() NULL

`check_pass()` did `strcmp(mux_crypt(pPassword, pTarget, &iType), pTarget)` with
no null check. `mux_crypt()` can return NULL: for `$1$`/`$5$`/`$6$`/`$2a$`/`_…`
settings it falls through to `crypt(szPassword, szSetting)`, and **POSIX
`crypt(3)` returns NULL** on a malformed/unsupported salt setting (macOS/BSD,
older glibc, some musl). `strcmp(NULL, …)` then crashes the server on a login
attempt. Reachable by migrating a DB whose `A_PASS` hashes use a method the
destination `crypt()` can't compute (e.g. a `$6$` SHA-512 DB), or a corrupt
`A_PASS`. `ChangePassword()` already null-checks `mux_crypt()`; `check_pass()` —
the security-critical caller — did not. Fixed: null-check and **fail auth
closed**. (On modern glibc `crypt()` returns `"*0"` instead of NULL, so the crash
is platform-dependent, but the NULL return is POSIX-sanctioned.)

## Audited sound — no change

- **`mux_crypt`** dispatch parses the `$ALGO$salt$` prefix and bounds buffers
  (`thread_local buf[…]` sized for SHA1 prefix+salt+hash; `GenerateSalt`'s
  `szSalt[32]` ≥ the ≤20-byte max). SHA1/MD5/SHA256/SHA512 + DES + PennMUSH P6H
  import.
- **`check_pass` comparison**: `strcmp` is non-constant-time, but it compares a
  **hash** (the input is hashed with the stored salt first), so it is **not a
  practical timing oracle** — an attacker can't iteratively construct an input
  whose hash matches leading bytes without breaking the hash.
- **`connect_player`**: looks up the player, gates on `check_pass`, records failed
  logins (`record_login(..., false, ...)`), only pays salary / updates `A_LAST`
  on success.
- **`do_password`** (change own password): requires a stored password AND
  `check_pass(executor, oldpass)` before changing, then `ok_password(newpass)` —
  no hijack, new-password validated.
- **`ChangePassword`**: tries strong methods first (SHA512 → SHA256 → MD5 → SHA1
  → DES per `mudconf.password_methods`), null-checks `mux_crypt`, asserts a final
  fallback.
- **`GenerateSalt`**: per-method bounded buffers; salt randomness via
  `RandomINT32` (svdrand, seeded from `/dev/urandom` on Unix) — more than
  adequate for salts (uniqueness, not secrecy).
- **`decrypt_logindata`/`encrypt_logindata`/`record_login`**: `grabto` returns
  in-buffer pointers, fixed `NUM_GOOD`/`NUM_BAD` loops, `mux_sprintf`/`mux_strncpy`
  LBUF-bounded. A corrupt `A_LOGINDATA` parses/re-serializes without OOB.

## Documented legacy (config-gated, not bugs)

Cleartext-password fallback (`CRYPT_CLEARTEXT` when `ENABLE_PROPER_DES` is off and
the stored value isn't a recognized hash) and the DES fixed-salt `'XX'` are
deliberate legacy/migration behaviors, documented in the source.
