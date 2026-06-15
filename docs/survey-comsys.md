# Survey: comsys channel system (comsys.cpp)

Audit of `mux/modules/engine/comsys.cpp` (5354 lines) — the channel/chat system:
channel data load/save, channel commands (addcom/delcom/create/destroy),
message formatting and broadcast (`do_processcom`, `BuildChannelMessage`,
`SendChannelMessage`, `do_cemit`). Threat surfaces: the persisted-file
deserializers (same class as flatfile DB #806 / Lua undumper #833) and the
player-facing message path. Methodology matches the parser/JIT/wild campaign.

**Result: memory-safe and well-hardened. One minor legacy correctness fix
(#841); one noted robustness design choice (mux_assert-abort on malformed file).**

## Deserializers (load_comsystem_V5/V4/V0123, load_channels_*) — safe

- Channel **name**/**header**/**title** are each clamped to `MAX_CHANNEL_LEN`(50)/
  `MAX_HEADER_LEN`(100)/`MAX_TITLE_LEN`(200) before `memcpy` into the correctly
  sized `channel::name[51]`/`header[101]` / `std::string` title. No OOB.
- `GetLineTrunc` returns a **minimum of 1** (substitutes `"\n"` on EOF/empty), so
  the ubiquitous `if (temp[nChannel-1] == '\n')` is never `temp[-1]` — no
  underflow OOB read.
- `ReadListOfNumbers(fp, cnt, anum)` reads into a fixed `buffer[200]` via bounded
  `fgets` and writes `anum[0..cnt)`; **every** caller sizes `anum[] >= cnt`
  (`anum[10]` with cnt ≤ 10, `anum[2]` with cnt 2). No fixed-array OOB.
- `MakeCanonicalComAlias` / `ParseChannelLine`: alias clamped to `MAX_ALIAS_LEN`,
  name `StringClone`d (heap).

**#841 (132c197e9):** the V0123 channel-name UTF-8 boundary backoff read `temp`
(the pre-conversion line) instead of `pBufferUnicode` (the converted bytes
actually copied) — a copy-paste slip vs. the adjacent header path. Not OOB
(in-bounds, count never exceeds source), but could truncate a legacy channel
name mid-character → invalid UTF-8. Fixed to read `pBufferUnicode`.

**Noted (not changed):** the loaders use `mux_assert(ReadListOfNumbers(...))`,
i.e. a controlled **abort** on a malformed/truncated comsys file. Safe (no UB),
fail-fast — a defensible design choice; a graceful skip-and-log would be friendlier
but is invasive to retrofit across V5/V4/V0123.

## Message path — safe

- `BuildChannelMessage` builds `messNormal`/`messNoComtitle` (alloc_lbuf) entirely
  via `safe_str`/`safe_chr` (LBUF-bounded) and `mux_exec(..., LBUF_SIZE-1, ...)`.
  The eval-comtitle path runs as `user->who` (their own code — the documented
  `eval_comtitle` feature, not a privilege issue).
- `do_processcom` caps the message at 3500 chars; small fixed buffers
  (`chattype[2]`, `sdrBuf[32]`) use bounded `mux_sprintf(sizeof, …)`.
- `do_cemit` `mux_strncpy`s into an alloc_lbuf (LBUF-bounded) and is permission-
  gated (`Controls`/`Comm_All`). It passes one buffer as both `msgNormal` and
  `msgNoComtitle`, but `SendChannelMessage` guards `free_lbuf(msgNoComtitle)` with
  `msgNoComtitle != msgNormal` — **no double-free**.
