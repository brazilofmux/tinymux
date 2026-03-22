# STL Conversion Tracker

This document tracks areas of the codebase that use C-era manual memory management
(calloc/realloc/free, intrusive linked lists, fixed-bucket hash tables, strdup/free)
and their conversion to STL containers.

---

## Completed

### Comsys Module (`mux/modules/comsys/`)
**Commit:** `a52f0b16` (2026-03-19, brazil)

| Old Pattern | New Pattern |
|-------------|-------------|
| `comuser **users` + `num_users`/`max_users` (realloc'd sorted array) | `std::map<dbref, comuser>` |
| `comuser *on_users` intrusive linked list | `bool bConnected` flag, iterate map |
| `UTF8 *title` (strdup/free) | `std::string title` |
| `comsys_t *comsys_table[500]` hash with `->next` chains | `std::unordered_map<dbref, comsys_t>` |
| Packed `ALIAS_SIZE`-stride alias array + parallel `UTF8 **channels` | `std::vector<com_alias>` with `std::string` members |
| `calloc(1, sizeof(channel))` | `std::make_unique<channel>()` |
| `map<..., channel *>` with manual free | `map<..., unique_ptr<channel>>` |
| Hand-rolled insertion sort | `std::sort` |

Net: −641 lines, +264 lines. All 551 smoke tests pass.

### Mail Aliases (`malias_t::list`)
**Prior work** (brazil branch)

| Old Pattern | New Pattern |
|-------------|-------------|
| `int *list` + `numrecep`/`maxrecep` (realloc'd) | `std::vector<dbref>` |
| `m_mail_htab` (custom hash) | `std::unordered_map<dbref, struct mail *>` |

---

### Engine-Side Comsys (`mux/modules/engine/comsys.cpp` + `mux/include/comsys.h`)
**Commit:** (2026-03-19, brazil)

| Old Pattern | New Pattern |
|-------------|-------------|
| `comsys_t *comsys_table[NUM_COMSYS]` (500-bucket hash, `->next` chains) | `std::unordered_map<dbref, comsys_t>` |
| `comuser **users` + `num_users`/`max_users` (calloc/realloc sorted array) | `std::map<dbref, comuser>` |
| `comuser *on_users` intrusive singly-linked list | `bool bConnected` flag, iterate map |
| `UTF8 *title` (MEMALLOC/MEMFREE/StringClone) | `std::string title` |
| Packed `ALIAS_SIZE`-stride alias + parallel `UTF8 **channels` | `std::vector<com_alias>` with `std::string` members |
| `MEMALLOC(sizeof(channel))` / `MEMFREE(ch)` | `new channel()` / `delete ch` |
| Hand-rolled bubble sorts (`sort_users`, `sort_com_aliases`) | `std::sort` / eliminated (map is ordered) |
| `create_new_comsys()`, `add_comsys()`, `destroy_comsys()` | Eliminated (default constructors, map operations) |
| `static int num_channels` | Eliminated (use `mudstate.channel_names.size()`) |
| Also updated: `funceval.cpp` `fun_cwho()` to iterate users map |

Net: −927 lines, +443 lines. All 551 smoke tests pass.

---

### Mail Module—Full Conversion (`mux/modules/mail/`)
**Commit:** (2026-03-19, brazil)

| Old Pattern | New Pattern |
|-------------|-------------|
| `struct mail { *next; *prev; }` doubly-linked list | `std::list<mail>` per player |
| `unordered_map<dbref, mail *>` head pointers | `unordered_map<dbref, std::list<mail>>` |
| `MailListFirst`/`MailListNext`/`MailListAppend`/`MailListRemove` | `MailList()` + range-for / iterator-erase |
| `UTF8 *time`, `*subject`, `*tolist` (strdup/free) | `std::string` |
| `mail_body *m_mail_list` (calloc/realloc/free) | `std::vector<mail_body>` |
| `UTF8 *m_pMessage` + `m_nMessage` (malloc/free) | `std::string m_pMessage` |
| `m_mail_db_top` / `m_mail_db_size` counters | `.size()` |
| `malias_t **m_malias` (calloc/free array) | `std::vector<std::unique_ptr<malias_t>>` |
| `UTF8 *name`, `*desc` in malias_t (strdup/free) | `std::string` |
| `m_ma_top` / `m_ma_size` counters | `.size()` |
| `make_numlist`/`make_namelist` returning `strdup()` | Return `std::string` |

Net: −863 lines, +563 lines. All 551 smoke tests pass.

---

### UFUN Chain, ADDENT Chain, qsort—std:: sort
**Commit:** (2026-03-19, brazil)

| Old Pattern | New Pattern |
|-------------|-------------|
| `UFUN *next` intrusive linked list + `ufun_head` | `std::list<UFUN> ufun_list` |
| `UTF8 *name` in UFUN (StringClone/delete) | `std::string name` |
| `ADDENT *next` intrusive linked list | `std::vector<ADDENT> *` per CMDENT |
| `UTF8 *name` in ADDENT (MEMALLOC/MEMFREE) | `std::string name` |
| `qsort()` with C comparator in mail.cpp | `std::sort()` with typed comparator |
| Files: functions.h/.cpp, command.h/.cpp, predicates.cpp, mail.cpp |

Net: −171 lines, +105 lines. All 551 smoke tests pass.

---

### Pool Allocator (`mux/lib/alloc.cpp`)
**Commit:** (2026-03-19, brazil)

| Old Pattern | New Pattern |
|-------------|-------------|
| `POOLHDR *next` intrusive chain (all buffers) | `std::vector<char*> all_buffers` |
| `POOLHDR *nxtfree` intrusive freelist | `std::vector<char*> free_stack` (push/pop back) |
| `POOL::free_head` / `chain_head` pointers | Eliminated |
| `pool_vfy`: corrupt—silently truncate chain (leak memory) | corrupt—report and continue |
| `pool_reset`: walk intrusive chain rebuilding | `unordered_set` of free ptrs + `remove_if` |

Public API (`alloc.h`) unchanged—zero callers affected. POOLHDR shrinks by
16 bytes (removed `next`/`nxtfree` pointers). Sentinel/magic diagnostics preserved.

Net: rewrite of alloc.cpp internals. All 551 smoke tests pass.

### Object List Stack (`mux/modules/engine/walkdb.cpp`)
**Commit:** (2026-03-23, codex)

| Old Pattern | New Pattern |
|-------------|-------------|
| `objlist_block *next` intrusive linked list of dbref arrays | `std::vector<dbref>` inside `ObjectListFrame` |
| `objlist_stack` manual struct with `head`/`tail`/`cblock` pointers | `std::stack<ObjectListFrame>` with RAII-managed frames |

`@search`/`@find` and other traversal helpers now rely on STL storage, eliminating bespoke `lbuf` block management and making nested scans exception-safe. No behavior changes were observed in smoke tests.

### 7. DBT/JIT Fixed Arrays (`mux/modules/engine/dbt.cpp`)

| Pattern | Location | Replacement |
|---------|----------|-------------|
| `block_entry_t *cache` (calloc, fixed size) | dbt.cpp:2851 | `std::vector<block_entry_t>` |
| `patch_site_t *patches` (calloc, fixed size) | dbt.cpp:2862 | `std::vector<patch_site_t>` |

**Rationale for deferral:** Fixed-size allocations for JIT compiler internals.
The JIT compiler is still evolving; converting these now risks churn.

---

## Conversion Guidelines

1. **Preserve artificial limits at input boundaries** — `MAX_CHANNEL_LEN`,
   `MAX_ALIAS_LEN`, `MAX_ALIASES_PER_PLAYER`, etc. remain enforced at
   validation points even when the underlying container can grow.

2. **Pointer stability** — When code holds a pointer/reference into a
   container across insertions of other elements, use `std::map` or
   `std::list` (which guarantee stability) rather than `std::vector`
   (which invalidates on reallocation).

3. **UTF8/string boundary** — All `std::string` members store char data.
   At COM interface boundaries expecting `UTF8 *`, use
   `reinterpret_cast<const UTF8 *>(str.c_str())`.

4. **Test coverage** — Every conversion must pass all existing smoke tests.
   Comsys and mail have dedicated test cases; engine-side changes affect
   dbconvert which is exercised by the test harness.
