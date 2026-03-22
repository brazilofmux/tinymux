# Softcode Accessor Redesign: Comsys & Mail

## Motivation

Both comsys and mail store rich structured data in SQLite, but the softcode
accessor layer exposes only a fraction of it. Common complaints:

- Fields present in the schema (timestamps, recipient lists, flags, headers)
  return "cannot get that information" or simply have no accessor at all.
- Permission models are inconsistent—a channel *subscriber* cannot query
  basic metadata on a non-public channel they belong to.
- The `mail()` function is overloaded three ways (count / stats / body),
  making it hard to use and harder to extend.
- Getting info about N channels or N messages requires N separate calls per
  field (the N+1 problem).
- Adding a new column to the schema requires adding a whole new C++ function,
  registering it, writing docs, etc.

This document proposes a small set of generic, field-based accessors that
replace the current patchwork and expose everything the schema stores.

---

## Guiding Principles

1. **If it is in the schema, it is queryable** (subject to permissions).
2. **Per-field permissions** — each field documents its own access rule,
   because not all fields in a subsystem share the same sensitivity.
3. **Bulk-friendly** — list functions return delimited sets; field-based
   accessors avoid the need for dozens of single-purpose functions.
4. **Intentional behavior changes** — where new accessors widen or narrow
   access relative to old functions, this is called out explicitly. Legacy
   wrappers preserve old behavior; they do not silently adopt new rules.

---

## Part 1—Comsys Accessors

### Current Functions (13)

| Function | What It Returns | Key Limitation |
|----------|----------------|----------------|
| `channels([player][,sep])` | Channel list | Subscribers to non-public channels don't see their own channel |
| `comalias(player, chan)` | Player's alias | Wizard or self/owner+Inherits |
| `comtitle(player, chan)` | Player's comtitle | Wizard or subscriber (select_user entry, regardless of bUserIsOn) |
| `chanobj(chan)` | Channel object dbref | **CA_WIZARD registration** |
| `cowner(chan)` | Owner dbref | Non-public: hidden from subscribers |
| `cmogrifier(chan)` | Same as chanobj | Duplicate; also requires visibility |
| `cusers(chan)` | User count | Hidden from own subscribers |
| `cmsgs(chan)` | Message count | Hidden from own subscribers |
| `cbuffer(chan)` | Buffer size | Reads attr from chanobj, not schema |
| `cdesc(chan)` | Description | Reads attr from chanobj, not schema |
| `cflags(chan[,player])` | Flag letters | Chan: P/L/S. User: O/G/Q. Raw type int inaccessible |
| `cstatus(chan, player)` | On/Off/Gag | Hidden from own subscribers |
| `crecall(chan[,n][,sep])` | Message history | Must be on channel |

### Current Permission Rules (Exact)

These are the *actual* rules in the code today, documented here so that
legacy wrappers can preserve them precisely.

| Function | Registration | Runtime Check |
|----------|-------------|---------------|
| `chanobj()` | **CA_WIZARD** | (none beyond registration) |
| `comalias()` | CA_PUBLIC | Wizard, or self, or Owner(executor)==victim && Inherits(executor) |
| `comtitle()` | CA_PUBLIC | Wizard, or executor has a `select_user()` entry (subscriber, regardless of bUserIsOn) |
| `cowner/cmogrifier/cusers/cmsgs/cbuffer/cdesc/cflags/cstatus` | CA_PUBLIC | PUBLIC channel, or Comm_All, or Controls(charge_who) |
| `channels()` | CA_PUBLIC | Lists channels where: PUBLIC, or Comm_All, or Controls(charge_who) |
| `crecall()` | CA_PUBLIC | Same as cowner + executor must be on channel |

### Schema Fields Not Exposed

| Column | Table | Currently Accessible? |
|--------|-------|-----------------------|
| `header` | channels | **No** |
| `type` (raw int) | channels | Only as letters via `cflags()` |
| `charge` | channels | **No** |
| `chan_obj` | channels | Wizard-only via `chanobj()` |
| `gag_join_leave` | channel_users | **No** |
| `comtitle_status` | channel_users | Only indirectly (Q flag in `cflags(chan,player)`) |

### New Channel Visibility Rule

A player can query channel metadata via the new accessors if **any** of:

- The channel is PUBLIC, **or**
- The player is a subscriber (on `channel_users`), **or**
- The player has the `Comm_All` power, **or**
- The player Controls the channel owner.

This is the single rule for `chaninfo()` and `chanusers()`. It adds
subscriber access—an intentional behavior change from the current model
where subscribers to non-public channels are locked out of metadata queries.

### New Functions

#### `chaninfo(channel, field)`

Returns the value of `field` for the named channel.

| Field | Source Column/Derivation | Return Type | Permission |
|-------|--------------------------|-------------|------------|
| `name` | `channels.name` | string | Channel visible |
| `header` | `channels.header` | string | Channel visible |
| `owner` | `channels.charge_who` | dbref | Channel visible |
| `object` | `channels.chan_obj` | dbref or `#-1` | **Wizard only** (see note) |
| `type` | `channels.type` | integer | Channel visible |
| `flags` | derived from `type` | flag letters (P, L, S, ...) | Channel visible |
| `charge` | `channels.charge` | integer | Channel visible |
| `users` | count of `channel_users` rows | integer | Channel visible |
| `msgs` | `channels.num_messages` | integer | Channel visible |
| `desc` | DESC attr on chan_obj (or empty) | string | Channel visible |
| `buffer` | MAX_LOG attr on chan_obj (or 0) | integer | Channel visible |
| `canjoin` | `test_join_access(executor)` | 0 / 1 | Channel visible |
| `cantransmit` | `test_transmit_access(executor)` | 0 / 1 | Channel visible |
| `canreceive` | `test_receive_access(executor)` | 0 / 1 | Channel visible |

**Note on `object` field:** `chanobj()` is currently CA_WIZARD. The
`chaninfo(chan, object)` field preserves this restriction. Channel objects
often carry privileged attributes and locks; exposing their dbref to
subscribers would be a security change we are not making here.

Error returns:

- `#-1 CHANNEL NOT FOUND` — channel does not exist or not visible.
- `#-1 INVALID FIELD` — unrecognized field name.
- `#-1 PERMISSION DENIED` — `object` field without Wizard.

#### `chanusers(channel[, separator])`

Returns a delimited list of dbrefs subscribed to the channel.

Permission: new channel visibility rule. Non-members of non-public
channels get `#-1 CHANNEL NOT FOUND`.

#### `chanuser(channel, player, field)`

Returns per-user information for a player on a channel.

| Field | Source Column | Return Type | Permission |
|-------|---------------|-------------|------------|
| `alias` | `player_channels.alias` | string | Self, or Owner+Inherits, or Wizard |
| `title` | `channel_users.title` | string | Self, or both on channel, or Wizard |
| `status` | derived from `is_on` | "On" / "Off" | Self, or both on channel, or Wizard |
| `flags` | derived from user fields | flag letters O/G/Q | Self, or both on channel, or Wizard |
| `gagjoin` | `channel_users.gag_join_leave` | 0 / 1 | Self, or both on channel, or Wizard |
| `comtitles` | `channel_users.comtitle_status` | 0 / 1 | Self, or both on channel, or Wizard |

**Note on `alias` permission:** `comalias()` today allows Wizard, self, or
`Owner(executor)==victim && Inherits(executor)`. The `alias` field
preserves this Owner+Inherits path. Other fields use a simpler co-member
rule since titles and status are already visible via channel output.

**Note on `flags` field:** This is new and returns the same compact O/G/Q
letter string that `cflags(chan, player)` returns today. It exists because
`cflags(chan, player)` and `cstatus(chan, player)` return *different*
formats: `cflags` returns flag letters (O, G, Q) while `cstatus` returns
words (On, Off, Gag). The `status` field maps to `cstatus` semantics;
the `flags` field maps to `cflags(chan, player)` semantics.

Error returns:

- `#-1 CHANNEL NOT FOUND` — not visible.
- `#-1 PLAYER NOT FOUND` — bad player argument.
- `#-1 NOT ON CHANNEL` — player not subscribed.
- `#-1 INVALID FIELD` — unrecognized field name.
- `#-1 PERMISSION DENIED` — insufficient access for the requested field.

### Legacy Wrappers

Old functions are reimplemented as wrappers but **preserve their original
permission checks**. They do not adopt the new visibility rules.

| Old Function | Dispatches To | Permission Preserved |
|-------------|---------------|----------------------|
| `cowner(chan)` | `chaninfo(chan, owner)` | PUBLIC or Comm_All or Controls(charge_who) |
| `chanobj(chan)` | `chaninfo(chan, object)` | **CA_WIZARD** (registration-level) |
| `cmogrifier(chan)` | **kept as own implementation** | PUBLIC or Comm_All or Controls(charge_who)—returns chan_obj without Wizard gate |
| `cusers(chan)` | `chaninfo(chan, users)` | PUBLIC or Comm_All or Controls(charge_who) |
| `cmsgs(chan)` | `chaninfo(chan, msgs)` | PUBLIC or Comm_All or Controls(charge_who) |
| `cbuffer(chan)` | `chaninfo(chan, buffer)` | PUBLIC or Comm_All or Controls(charge_who) |
| `cdesc(chan)` | `chaninfo(chan, desc)` | PUBLIC or Comm_All or Controls(charge_who) |
| `cflags(chan)` | `chaninfo(chan, flags)` | PUBLIC or Comm_All or Controls(charge_who) |
| `cflags(chan, player)` | **kept as own implementation** | Same old check + returns O/G/Q letters |
| `cstatus(chan, player)` | **kept as own implementation** | PUBLIC or Comm_All or Controls(charge_who); returns "Off" for non-subscribers (not #-1) |
| `comalias(player, chan)` | `chanuser(chan, player, alias)` | Wizard or self or Owner+Inherits |
| `comtitle(player, chan)` | `chanuser(chan, player, title)` | Wizard or subscriber (select_user entry exists) |

**Note on kept implementations:** Three legacy functions cannot safely
dispatch through the new accessors without changing semantics:

- `cflags(chan, player)` — uses the old visibility check (not co-member)
  and returns O/G/Q letters (not On/Off/Gag words).
- `cstatus(chan, player)` — returns `"Off"` for non-subscribers, while
  `chanuser()` returns `#-1 NOT ON CHANNEL`. Callers depend on `"Off"`.
- `cmogrifier(chan)` — exposes `chan_obj` to anyone passing the standard
  channel visibility check (PUBLIC/Comm_All/Controls). `chaninfo(chan,
  object)` is Wizard-gated. `cmogrifier()` predates `chanobj()` and has
  different semantics; simplest to keep its own implementation.

`channels()` and `crecall()` are kept as-is (with the subscriber
visibility fix applied to `channels()`).

### Behavior Changes Summary (Comsys)

| Change | Old Behavior | New Behavior | Affects |
|--------|-------------|--------------|---------|
| `channels()` subscriber visibility | Subscribers to non-public channels not listed | Subscribers see their own channels | `channels()` |
| `chaninfo()` subscriber access | N/A (new function) | Subscribers can query metadata | New code only |
| `chaninfo(chan, object)` | N/A | Wizard-only (matches `chanobj()`) | New code only |
| Legacy wrappers | Original permission logic | **Unchanged** — wrappers preserve old checks | No breakage |

---

## Part 2—Mail Accessors

### Current Functions (9)

| Function | What It Returns | Key Limitation |
|----------|----------------|----------------|
| `mail()` | Message count | |
| `mail(player)` | read/unread/cleared | Overloaded; self or Wizard |
| `mail(player, num)` | Message body | Overloaded; self (with nObjEvalNest guard) or **God only** |
| `mailsize([player])` | Total bytes | Aggregate only; self or Wizard |
| `mailsubj([player,] num)` | Subject | Self or Wizard |
| `mailfrom([player,] num)` | Sender dbref | Self or Wizard |
| `mailreview(player[, num])` | Sent-mail count/body | Only own sent mail |
| `mailsend(recip, subj, body)` | 1 on success | |
| `malias([player])` | List of alias names | Self or ExpMail |

### Current Permission Rules (Exact)

| Function | Cross-Player Access |
|----------|-------------------|
| `mail(player)` stats | Wizard |
| `mail(player, num)` body | **God only** (not Wizard) + nObjEvalNest guard for self |
| `mailsubj(player, num)` | Wizard |
| `mailfrom(player, num)` | Wizard |
| `mailsize(player)` | Wizard |
| `malias(player)` | **ExpMail** (not Wizard) |

The God-only restriction on `mail(player, num)` body access is the most
notable divergence. This was likely a deliberate security decision: message
bodies are the most sensitive mail data.

### Schema Fields Not Exposed

| Column | Table | Currently Accessible? |
|--------|-------|-----------------------|
| `time_str` | mail_headers | **No** — no `mailtime()` exists |
| `tolist` | mail_headers | **No** — original recipient list hidden |
| `read_flags` | mail_headers | **No** — M_ISREAD, M_URGENT, M_CLEARED, M_TAG, M_FORWARD, M_REPLY all invisible |
| per-message size | mail_bodies | **No** — only aggregate via `mailsize()` |

### New Functions

#### `mailcount([player])`

Returns total message count for the player. Replaces the no-arg form of
`mail()`.

Permission: self always; Wizard for others.

#### `mailstats([player])`

Returns `read unread cleared` as three space-separated integers. Replaces
the one-arg-player form of `mail()`.

Permission: self always; Wizard for others.

#### `maillist([player])`

Returns a space-separated list of valid message numbers for the player.
This is new—currently there is no way to enumerate messages without
probing sequentially.

Permission: self always; Wizard for others.

#### `mailinfo(msg#, field[, player])`

Returns any per-message field by name. The optional third argument lets
an admin query another player's mail, **with per-field permission rules**.

| Field | Source | Return Type | Cross-Player Access |
|-------|--------|-------------|---------------------|
| `from` | `mail_headers.from_player` | dbref | Wizard |
| `to` | `mail_headers.to_player` | dbref | Wizard |
| `tolist` | `mail_headers.tolist` | string (original recipients) | Wizard |
| `subject` | `mail_headers.subject` | string | Wizard |
| `time` | `mail_headers.time_str` | datetime string | Wizard |
| `body` | `mail_bodies.message` | string | **God only** |
| `size` | `length(mail_bodies.message)` | integer (bytes) | Wizard |
| `flags` | `mail_headers.read_flags` | flag letters (see below) | Wizard |
| `read` | derived from `read_flags` | 0 / 1 | Wizard |

**Note on `body` cross-player access:** The current `mail(player, num)`
restricts cross-player body reads to God. `mailinfo(num, body, player)`
preserves this restriction. Message bodies are the most sensitive mail
data; this is an intentional security boundary. The nObjEvalNest guard
on self-access is also preserved.

Flag letters for the `flags` field:

| Letter | Constant | Meaning |
|--------|----------|---------|
| `R` | M_ISREAD | Read |
| `U` | M_URGENT | Urgent / priority |
| `C` | M_CLEARED | Cleared (pending purge) |
| `T` | M_TAG | Tagged by user |
| `F` | M_FORWARD | Forwarded |
| `P` | M_REPLY | Replied-to |

Error returns:

- `#-1 NO SUCH MESSAGE` — invalid number or filtered by recycled-dbref guard.
- `#-1 INVALID FIELD` — unrecognized field name.
- `#-1 PERMISSION DENIED` — insufficient access for the requested field/player.

#### `mailflags(msg#[, player])`

Shorthand for `mailinfo(msg#, flags[, player])`. Returns the flag letter
string. Provided because flag checking is a common operation in softcode
that currently requires no fewer than zero accessors (it simply cannot be
done).

Permission: self always; Wizard for cross-player.

#### `mailalias(name[, player])`

Returns the member list (dbrefs) of the named alias.

Permission: self always; ExpMail for cross-player (matches `malias()`).

### Legacy Wrappers

Old functions are reimplemented as wrappers but **preserve their original
permission checks**.

| Old Function | Dispatches To | Permission Preserved |
|-------------|---------------|----------------------|
| `mail()` | `mailcount()` | Self only |
| `mail(player)` | `mailstats(player)` | Self or Wizard |
| `mail(player, num)` | `mailinfo(num, body, player)` | Self (+ nObjEvalNest) or **God only** |
| `mailsubj([player,] num)` | `mailinfo(num, subject[, player])` | Self or Wizard |
| `mailfrom([player,] num)` | `mailinfo(num, from[, player])` | Self or Wizard |
| `malias([player])` | **kept as-is** | Self or ExpMail |
| `mailreview(player[, num])` | **kept as-is** | Own sent mail only |

`mailsend()` and `mailsize()` are kept as-is.

### Behavior Changes Summary (Mail)

| Change | Old Behavior | New Behavior | Affects |
|--------|-------------|--------------|---------|
| `mailinfo()` metadata fields | N/A (new) | Wizard cross-player | New code only |
| `mailinfo(body)` | N/A (new) | God cross-player (matches `mail(p,n)`) | New code only |
| `mailalias()` | N/A (new) | ExpMail cross-player (matches `malias()`) | New code only |
| Legacy wrappers | Original permission logic | **Unchanged** | No breakage |

---

## Implementation Plan

### Phase 1—Comsys (chaninfo / chanuser / chanusers)

1. Fix `channels()` visibility to include subscribers.
2. Implement `chaninfo()` with field dispatch table and per-field permissions.
3. Implement `chanusers()`.
4. Implement `chanuser()` with field dispatch table and per-field permissions.
5. Add `flags` field to `chanuser()` for O/G/Q letters.
6. Rewrite old functions as wrappers (preserving original permissions).
7. Keep `cflags(chan, player)` as its own implementation.
8. Smoke tests for each new function and field.

### Phase 2—Mail (mailinfo / mailcount / mailstats / maillist)

1. Implement `mailcount()` and `mailstats()` (trivial extractions from `mail()`).
2. Implement `maillist()`.
3. Implement `mailinfo()` with field dispatch table and per-field permissions.
4. Implement `mailflags()`.
5. Implement `mailalias()` (complements existing `malias()`).
6. Rewrite old functions as wrappers (preserving original permissions).
7. Smoke tests for each new function and field.

### Phase 3—Deprecation & Docs

1. Add `@list functions` annotations marking old names as deprecated.
2. Update `docs/FUNCTIONS.md` or equivalent.
3. Wizhelp / helptext entries for new functions.

---

## Checklist

- [x] Phase 1.1 — `channels()` subscriber visibility fix
- [x] Phase 1.2 — `chaninfo()` with per-field permissions (14 fields incl. canjoin/cantransmit/canreceive)
- [x] Phase 1.3 — `chanusers()` with bulk field output (8 modes)
- [x] Phase 1.4 — `chanuser()` with per-field permissions (including `flags` field)
- [x] Phase 1.5 — `chanfind()` reverse header—name lookup
- [x] Phase 1.6 — `@clist/full` redesign (header, owner names, effective access)
- [x] Phase 1.7 — `@clist` subscriber visibility fix
- [x] Phase 1.8—Comsys smoke tests (30 tests)
- [x] Phase 2.1 — `mailcount()` and `mailstats()`
- [x] Phase 2.2 — `maillist()`
- [x] Phase 2.3 — `mailinfo()` with per-field permissions (body=God, rest=Wizard)
- [x] Phase 2.4 — `mailflags()`
- [ ] ~~Phase 2.5 — `mailalias()`~~ (deferred—existing `malias()` covers the need)
- [ ] ~~Phase 2.6—Mail legacy wrappers~~ (deferred—old functions kept as-is, no wrapper rewrite needed)
- [x] Phase 2.7—Mail smoke tests (13 tests)
- [ ] ~~Phase 3.1—Deprecation annotations~~ (deferred—no deprecation mechanism exists in `@list functions`)
- [x] Phase 3.2—Documentation (design doc kept current throughout)
- [x] Phase 3.3—Helptext (help.txt entries for all 9 new functions + FUNCTION CLASSES/LIST updates)
