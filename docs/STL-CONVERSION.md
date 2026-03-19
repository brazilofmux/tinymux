# STL Conversion Tracker

This document tracks areas of the codebase that use C-era manual memory management
(calloc/realloc/free, intrusive linked lists, fixed-bucket hash tables, strdup/free)
and their conversion to STL containers.

---

## Completed

### Comsys Module (`mux/modules/comsys/`)
**Commit**: `a52f0b16` (2026-03-19, brazil)

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
**Commit**: (2026-03-19, brazil)

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

## Pending — High Priority

### 2. Mail Module — Remaining Conversions (`mux/modules/mail/`)

Already partially modernized (`unordered_map` for htab, `vector<dbref>` for
alias members). Three subsystems remain.

#### 2a. Mail Body Storage

| Pattern | Location | Replacement |
|---------|----------|-------------|
| `mail_body *m_mail_list` (calloc/realloc/free) | mail_mod.h:189, mail_mod.cpp:419–430 | `std::vector<mail_body>` |
| `m_mail_db_top` / `m_mail_db_size` counters | mail_mod.h:190–191 | `.size()` / `.capacity()` |
| `mail_db_grow()` manual resize | mail_mod.cpp:418 | Eliminated (vector auto-grows) |

#### 2b. Mail Message Linked List

| Pattern | Location | Replacement |
|---------|----------|-------------|
| `struct mail { mail *next; mail *prev; }` doubly-linked circular list | mail_mod.h:110–122 | `std::list<mail>` or `std::deque<mail>` per player |
| Manual `MailListAppend` / `MailListRemove` pointer surgery | mail_mod.cpp:5577–5620 | STL `.push_back()` / `.erase()` |
| Per-player head pointer in `m_mail_htab` | mail_mod.h:188 | `unordered_map<dbref, std::list<mail>>` |

#### 2c. Mail String Fields

| Pattern | Location | Replacement |
|---------|----------|-------------|
| `UTF8 *time`, `*subject`, `*tolist` (strdup/free) | mail_mod.h:113–115 | `std::string` |
| Manual free in ~6 cleanup sites | mail_mod.cpp:299–307, 620–641, 2476–2478 | Automatic destruction |

#### 2d. Alias Array

| Pattern | Location | Replacement |
|---------|----------|-------------|
| `malias_t **m_malias` (new[]/delete[]) | mail_mod.h:195 | `std::vector<std::unique_ptr<malias_t>>` |
| `m_ma_top` / `m_ma_size` counters | mail_mod.h:196–197 | `.size()` / `.capacity()` |
| `UTF8 *name`, `*desc` in malias_t (strdup/free) | mail_mod.h:143–144 | `std::string` |

---

## Pending — Medium Priority

### 3. UFUN Chain (`mux/include/functions.h` + `mux/modules/engine/functions.cpp`)

| Pattern | Location | Replacement |
|---------|----------|-------------|
| `UFUN *next` intrusive singly-linked list | functions.h:30 | Eliminate; use container |
| `UFUN *ufun_head` global list head | functions.cpp:44 | `std::list<UFUN>` or `std::vector<UFUN>` |
| Linear scan for name lookup | functions.cpp:13444, 13636 | Already have `ufunc_htab`; list is for ordered iteration |

**Notes**: The `ufun_head` chain is walked for `@list functions` and cleanup.
Name lookup already goes through a hash table (`ufunc_htab`). Converting the
chain to `std::list<UFUN>` would simplify insertion/deletion/cleanup. Low risk.

### 4. ADDENT Chain (`mux/include/command.h` + `mux/modules/engine/command.cpp`)

| Pattern | Location | Replacement |
|---------|----------|-------------|
| `ADDENT *next` intrusive singly-linked list | command.h:211 | Eliminate |
| `CMDENT.addent` chain head | command.h:227 | `std::vector<ADDENT>` |
| Manual walk in cleanup | command.cpp:1071–1075 | Automatic destruction |

**Notes**: Each `@addcommand` name can have multiple definitions chained
via `->next`. Small chains (typically 1–3 entries). Converting to
`std::vector<ADDENT>` would simplify cleanup. Low risk, small scope.

### 5. qsort → std::sort (`mux/modules/engine/functions.cpp`)

| Pattern | Location | Replacement |
|---------|----------|-------------|
| `qsort()` with C comparison functions | functions.cpp:8742–8800 | `std::sort()` with lambda/comparator |
| `qsort_record` union for sort keys | functions.cpp:8742 | Type-safe comparators |

**Notes**: Several `sortby()`/`sort()` softcode functions use C `qsort()`.
Mechanical conversion to `std::sort()` with typed comparators.

---

## Pending — Low Priority / Defer

### 6. Pool Allocator (`mux/lib/alloc.cpp`)

| Pattern | Location | Replacement |
|---------|----------|-------------|
| `POOLHDR *next` / `*nxtfree` intrusive linked lists | alloc.cpp:39–50 | — |
| 9 pool types with chain_head/free_head | alloc.h:9–18 | — |

**Rationale for deferral**: This is the core memory allocator used by the
entire codebase for LBUF/SBUF/MBUF buffers. It is performance-critical and
tightly coupled to the buffer lifecycle (`alloc_lbuf`/`free_lbuf` appear in
thousands of call sites). Converting it would require touching nearly every
file. The intrusive list design is intentional here — it avoids allocating
list nodes, which matters for an allocator. **Defer until profiling shows
the allocator is a bottleneck.**

### 7. Object Block Lists (`mux/modules/engine/walkdb.cpp`)

| Pattern | Location | Replacement |
|---------|----------|-------------|
| `objlist_block *next` intrusive linked list of dbref arrays | mudconf.h:392–405 | `std::vector<dbref>` |
| `objlist_stack *next` stack of block lists | mudconf.h:392 | `std::stack<std::vector<dbref>>` |

**Rationale for deferral**: These are transient structures used only during
`@search`/`@find` operations. Allocated and freed within a single function
call. Small blast radius but also small benefit.

### 8. DBT/JIT Fixed Arrays (`mux/modules/engine/dbt.cpp`)

| Pattern | Location | Replacement |
|---------|----------|-------------|
| `block_entry_t *cache` (calloc, fixed size) | dbt.cpp:2851 | `std::vector<block_entry_t>` |
| `patch_site_t *patches` (calloc, fixed size) | dbt.cpp:2862 | `std::vector<patch_site_t>` |

**Rationale for deferral**: Fixed-size allocations for JIT compiler internals.
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
