# Survey: mail system (mail.cpp)

Audit of `mux/modules/engine/mail.cpp` (6126 lines) — the `@mail` system: message
storage (`mail_list[]` body array with reference counting), the mail database
loaders (flatfile V5/V6 + SQLite), and the player-facing `@mail` commands.
Methodology matches the parser/JIT/wild/comsys campaign.

**Result: one real, verified heap OOB-write on a malformed mail DB (#843, fixed).
The runtime/player path is bounds-safe.**

## #843 (719e4ae58) — unchecked message numbers index mail_list[]

The loaders read message numbers straight from the (admin-supplied, possibly
corrupt) mail database and use them as `mail_list[]` indices with **no bounds
check**, in both the flatfile (`load_mail_V5`/`V6`) and SQLite
(`sqlite_load_mail`) paths:

- `mp->number = getref(fp); MessageReferenceInc(mp->number)` →
  `mail_list[number].m_nRefs++`.
- `new_mail_message(message, number) → MessageAddWithNumber(number)` →
  `mail_db_grow(number+1); mail_list[number] = …`.

A huge message number makes `mail_db_grow` fail its allocation and return without
growing, after which `MessageAddWithNumber` writes past the end of `mail_list[]`
— a heap OOB write. The reference accessors
(`MessageReferenceInc`/`Check`/`Dec`, `MessageFetch`, `MessageFetchSize`) are all
unguarded.

**Verified live:** a crafted SQLite DB (`mail_bodies(number=2000000000)` + the
`mail_db_top` meta to open the loader gate) **SIGSEGVs** the unpatched build on
load; the patched build loads it cleanly. Same reachability class as the flatfile
DB OOB (#806) and the comsys loader (#841) — corrupt/migrated/crafted mail DB.

**Fix:** `mail_index_valid(n)` (`0 <= n < mail_db_top`) guards every
`mail_list[]` accessor (protecting the load path **and** any runtime fetch of an
out-of-range number); `MessageAddWithNumber` rejects negative/absurd `i` (avoids
the `i+1` overflow) and re-checks validity after the grow; `mail_db_grow` refuses
an absurd `newtop` (> `MAIL_DB_LIMIT` = 64M) to avoid the `int` size-arithmetic
overflow. The accessor guards funnel-protect all three loaders.

## Audited sound — no change

- **`parse_msglist`** (player input "1-5,7,9-"): `ms->low`/`high` are filters over
  the player's own mailbox during iteration, **not** `mail_list[]` indices, so an
  absurd range value just means "match all" — no OOB. `ms->low > 0` /
  `ms->low <= ms->high` validated.
- **`MessageAdd`/`add_mail_message`**: scans for a free slot / `mail_db_grow`s as
  needed; the message body is `StringClone`d (heap) and bounded by LBUF
  (`mux_exec`/`tprintf`).
- **`new_mail_message`**: truncates an over-LBUF message body before storing.
- **`encrypt/decrypt_logindata`-style record keeping** lives in player.cpp, not
  here.
