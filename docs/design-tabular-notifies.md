# Design: NLS-safe multi-column notifies

**Status:** design only — **leave alone in code until “do it right” is unblocked.**  
**Related:** #1648 (pre-spaced header msgids), #1649 (cell primitive), #1419 (NLS epic), #1614 (dual comsys/mail).

---

## 1. Binary rule

| Choice | Meaning |
|--------|---------|
| **Leave alone** | Keep pre-spaced English multi-column headers. **Do not translate** those msgids. Do not half-migrate one table. |
| **Do it right** | One layout system for **header + rows**, **shared column widths**, **one msgid per label**, **visual-width padding**, **engine and module on the same path** (or a single remaining implementation). |

**No partials.** Fixing three headers, translating with “careful spaces,” or converting only the module while the engine still uses `PadField` + blob headers is two migrations and a permanent dual story. That is extra work with no lasting NLS win.

Until “do it right” is achievable, **leave alone is the correct engineering choice**: design, issues, comments, wait.

---

## 2. The defect (#1648)

Some player-facing tables ship the header as **one msgid with spacing baked into English**:

```c
M_("*** Channel       Header          Owner           Access  Users Msgs");
```

Data rows ignore that string and use independent column stops (`PadField` or `co_copy_field` widths). Alignment lives in **C**; labels live in the **catalogue**; nothing connects them.

A translator cannot fix this from their side:

1. They must reverse-engineer stops that never appear in the msgid.
2. CJK labels are double-width; character count ≠ display columns.
3. Changing a stop silently desynchronises every `.po`; `check_nls.py` only checks `%` conversions, and these msgids have none.

**Phase 1 inventory (complete listing, pot blobs, dual map, conversion order):**
[`inventory-tabular-notifies.md`](inventory-tabular-notifies.md).

Leave pre-spaced multi-column header msgids **untranslated** (`msgstr ""`) until
Phase 4 converts that family. Filling them in is how a catalogue looks
“broken” when the architecture is.

---

## 3. What “right” is

A table is a **schema**, not a string:

```text
columns[] = { width_display_cols, label_msgid, align }
header    = chrome in C + each label through the same field primitive as cells
row cells = values through that same primitive
chrome    = fixed punctuation (***, spaces between fields) — not in the catalogue
```

### 3.1 Cell layer (#1649)

`co_copy_field` is the intended cell primitive: hard byte cap, display columns, color close on truncate, grapheme-cluster cuts. Module comsys/mail already route many **data** columns through helpers on top of it. Engine comsys list rows still largely use `PadField` + magic stops.

**#1649’s “no primitive” inventory is largely answered for the cell.** Remaining work is **composition and ownership**, not inventing another truncator.

### 3.2 Composition layer (this design)

| Requirement | Detail |
|-------------|--------|
| Shared widths | Named constants or one column-descriptor table used by header **and** rows |
| One msgid per label | `M_("Channel")`, `M_("Header")`, … — not a pre-spaced grid |
| One emit path | “Walk the column array” so header/row drift is impossible by construction |
| Shared implementation | Layout helpers live where **both** engine and modules can call them (libmux / module-safe header), **or** #1614 has collapsed dual comsys/mail to one owner |
| Checks | Prefer construction over post-hoc tests; optional: inventory / ban on new pre-spaced header msgids |

### 3.3 Explicit non-goals for this epic

- Implementing `%N$` / positional args (#1623) — independent; headers are not multi-arg formats.
- Translating every notify in the game.
- Softcode `#-1` ABI.
- Client UI layout.

---

## 4. Dual implementation (#1614)

“Do it right” for comsys/mail tables **depends on ownership**:

| Option | Implication |
|--------|-------------|
| **A. Shared layout in libmux** | Both engine and module call the same emit API; #1614 can stay open longer |
| **B. Single owner** | #1614 decides which implementation lives; convert tables only there |
| **C. Convert both in lockstep forever** | Rejected — permanent dual tax |

Until A or B is chosen, **do not start table conversion PRs**.

---

## 5. Phased plan (when unblocked)

Slow by design. Each phase is a separate decision to start.

| Phase | Deliverable | Exit criteria |
|-------|-------------|----------------|
| **0 — Policy (this doc)** | Binary rule written; pot entries known as non-translatable | Maintainers agree leave-alone until right |
| **1 — Inventory** | Full list of multi-column player tables (comsys, mail, others): header msgid, row stops, engine vs module | Done — [`inventory-tabular-notifies.md`](inventory-tabular-notifies.md) |
| **2 — Ownership** | #1614 decision **or** explicit “layout in libmux” ADR | One path to implement against |
| **3 — Layout API** | Column descriptor + emit-header / emit-cell on `co_copy_field`; single definition of helpers (no third copy-paste of `append_ljust_field`) | Unit tests on width/color/CJK fixtures |
| **4 — Convert by family** | One table family per change (e.g. `@clist*`, then mail list, …); delete blob msgids from pot; re-bless conformance if needed | Header and rows share descriptors; English output acceptable parity |
| **5 — Guardrails (optional)** | `check_nls.py` or docs: ban new pre-spaced multi-column header msgids | Regressions fail CI |

**Do not** open a PR that only rewrites `@clist/full`’s header string.

---

## 6. Policy for translators (now)

1. **Do not translate** multi-column pre-spaced header msgids listed in §2 (and any found in Phase 1).
2. Prefer leaving `msgstr ""` so English passthrough keeps layout working.
3. When a table is converted (Phase 4), translate **per-label** msgids only; chrome stays in C.

---

## 7. Relationship map

```text
#1419 NLS epic
  └── presentation rules (plan-server-i18n.md)
        ├── #1623 positional %N$          (formats — separate)
        ├── #1622 morphology / wrong side (same family as #1648)
        └── tabular notifies (this doc)
              ├── #1648 pre-spaced headers     (constraint; leave alone)
              ├── #1649 cell primitive         (co_copy_field; mostly done)
              └── #1614 dual comsys/mail       (gate for Phase 2/4)
```

---

## 8. Decision log

| Date | Decision |
|------|----------|
| 2026-07-28 | Adopt leave-alone vs do-right binary. No partial header migrations. Design captured here; implementation waits on inventory + ownership. |
| 2026-07-28 | Phase 1 inventory written (`inventory-tabular-notifies.md`). Next gate: Phase 2 ownership (#1614 / libmux layout). |
