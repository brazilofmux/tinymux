# Design: NLS-safe multi-column notifies

**Status:** **implemented** for columnar player/staff tables and B3 mail list lines (Phases 0–5 + B3).  
Mail multi-line **forms** (inventory B4) remain a separate form-layout concern.  
**Related:** #1648, #1649, #1419, #1614, #1667.

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

## 4. Dual implementation (#1614) and layout ownership (Phase 2)

Two different “ownership” questions. Mixing them is how partial migrations
happen. Phase 2 answers **both**.

### 4.1 Product ownership (already decided on #1614)

Maintainer decision (2026-07-27 on #1614), abbreviated:

> Finish the migration. The comsys and mail modules become the shipped default
> in 2.14. (**B-by-completion** — modules are the implementation; built-ins
> remain the fallback until directives land.)

Sequence already set there: close known gaps → Windows loadable → **then**
ship `module comsys_mod` / `module mail_mod` in `netmux.conf`. Directives last.

Implications for this epic:

| Fact | Consequence for tables |
|------|------------------------|
| Modules are the **destination** for comsys/mail commands | Long-term convert those tables on the **module** path first when Phase 4 runs |
| Built-ins remain **fallback** until directives land | Engine `@clist` / mail alias paths still execute on stock installs **today** |
| Softcode functions stay engine-side and read module state | Not a layout issue; handoff harness owns it |
| Inventory section **C** tables are engine-only forever | Staff lists never “move to a module” |

**Do not** re-open #1614’s A/B/C product choice here. That is settled.

### 4.2 Layout ownership (this epic’s Phase 2 decision)

Options from the original design sketch:

| Option | Implication |
|--------|-------------|
| **A. Shared layout in libmux** | Engine and modules call the same emit API |
| **B. Convert only the product owner** | Modules only for A/B inventory; engine staff tables somehow separate |
| **C. Lockstep dual forever** | Rejected — permanent dual tax |

**Decision: A — layout API lives in libmux** (on top of existing `co_copy_field`).

Reasons, given 4.1:

1. **Stock installs still run built-in tables** until #1614 directives land. Converting only the module would leave the default English path on `PadField`/blob headers — a partial migration.
2. **Inventory C\*** (guests, site, hashstat, bufstats, …) is engine-only and never becomes a module. Without libmux layout, those tables stay a second layout culture forever.
3. **Modules already depend on libmux** for `co_copy_field` / `mux_sprintf` / `mux_format.h`. Promoting `append_ljust_field` (today copy-pasted in comsys_mod and mail_mod) into libmux is the natural next step, not a new dependency direction.
4. After modules become default, the same libmux API remains the single implementation; the engine fallback path can call it too, so “fallback” is not a second table stack.
5. Aligns with [`design-libmux-modules.md`](design-libmux-modules.md) phase 3 (“grow libmux.so — foundational code modules can use directly”).

**Rejected:**

- **B alone** for layout (“only convert modules”) — partial while dual life continues; abandons C\*.
- **C** — already rejected.
- Putting layout only in `engine.so` — modules cannot link it (same wall that produced raw `snprintf` before `mux_format.h`).

### 4.3 What Phase 2 does *not* unlock

| Still blocked | Until |
|---------------|--------|
| Deleting engine comsys/mail built-ins | #1614 sequence (directives after gaps) |
| Translating blob headers | Phase 4 per family |
| Assuming stock games run modules | `netmux.conf` still has zero `module` lines |

### 4.4 What Phase 2 *does* unlock

Phase **3** may design and implement the layout API **in libmux** (headers, helpers, tests). Phase **4** may convert tables **using that API** on whatever path currently emits them (module *and* engine for dual families; engine for C\*), so English output stays one construction, not two.

Conversion order in the inventory still prefers dual families first; both sides of a dual family switch in the **same** change (or immediately paired), both calling libmux — never “module done, engine later.”

### 4.5 Phase 2 exit checklist

- [x] Product ownership not re-litigated; #1614 B-by-completion cited
- [x] Layout home chosen: **libmux** (not module-only, not engine.so)
- [x] Rationale covers dual life, C\* staff tables, and existing module→libmux dependency
- [x] Explicit rejections recorded (B-alone layout, C lockstep, engine-only)
- [x] Phase 3/4 boundaries stated (API yes; blob translation no; dual families paired)

**Phase 2 is complete when this section is on master.** No code is required for Phase 2.

### 4.6 Phase 3 handoff constraints (ownership only — not the API design)

Phase 3 designs the API. Phase 2 only fixes **where it must live** and **who may call it**:

| Constraint | Detail |
|------------|--------|
| **Library** | Implementation links into **libmux** (same binary modules already load). |
| **Headers** | Declarations must be module-includable without `stringutil.h` / game-state headers — same pattern as `mux_format.h` and `color_ops.h` (e.g. a dedicated `mux_table.h` or an extension of an existing module-safe header). |
| **Cell primitive** | Built on **`co_copy_field`** already exported from libmux; do not invent a parallel truncator. |
| **Callers** | At least: `comsys_mod`, `mail_mod`, engine `comsys.cpp` / `mail.cpp` (dual families), and one engine-only C\* path as a smoke of “staff tables can use it.” |
| **Duplication** | After Phase 3 lands helpers, module-local `append_ljust_field` copies are removed or become thin wrappers — not a third independent implementation. |
| **NLS** | API composes labels the caller already resolved (`M_()` / `T_()` outside the layout layer). Layout does not call gettext itself. |
| **Out of Phase 3** | Converting pot blob headers; shipping `module` directives; deleting built-ins. |

---

## 5. Phased plan

Slow by design. Each phase is a separate decision to start.

| Phase | Deliverable | Exit criteria |
|-------|-------------|----------------|
| **0 — Policy (this doc)** | Binary rule written; pot entries known as non-translatable | Maintainers agree leave-alone until right |
| **1 — Inventory** | Full list of multi-column player tables | Done — [`inventory-tabular-notifies.md`](inventory-tabular-notifies.md) |
| **2 — Ownership** | Product ownership + layout ownership recorded | Done — §4 (#1614 B-by-completion + layout in libmux) |
| **3 — Layout API** | Column descriptor + emit-header / emit-cell on `co_copy_field` in **libmux**; retire duplicated `append_ljust_field` | Done — `mux/include/mux_table.h`, `mux/lib/mux_table.c`; modules use it; `tests/table` |
| **4 — Convert by family** | One table family per change; both dual paths in the same change; delete blob msgids from pot | **Done** for columnar tables (A*, B1–B2, C1–C9) and B3 mail folder/review list lines. B4 multi-line read/detail forms remain out of grid scope. |
| **5 — Guardrails** | `check_nls.py` ban on pre-spaced multi-column header msgids | Done — `looks_like_table_header_blob` over pot + `M_()` sources |

**Do not** open a PR that only rewrites `@clist/full`’s header string.

---

## 6. Policy for translators (now)

1. **Do not reintroduce** pre-spaced multi-column header msgids — Phase 4 converted the known grids; Phase 5 fails the NLS guard if a new one appears.
2. Translate **per-label** msgids only (`Channel`, `Owner`, `Name`, …); chrome (`*** `, ` | `) stays in C.
3. Mail multi-line **forms** (B4: From/At/Fldr/Subject detail blocks) are not columnar tables; they may still use format templates with fixed English labels until a separate form-layout pass. B3 list lines use `mux_table` for the From/Sub fields.

---

## 7. Relationship map

```text
#1419 NLS epic
  └── presentation rules (plan-server-i18n.md)
        ├── #1623 positional %N$          (formats — separate)
        ├── #1622 morphology / wrong side (same family as #1648)
        └── tabular notifies (this doc)
              ├── #1648 pre-spaced headers     (constraint; leave alone until Phase 4)
              ├── #1649 cell primitive         (co_copy_field; mostly done)
              ├── #1614 product: modules default  (decided; directives last)
              └── layout API in libmux         (Phase 2; unlocks Phase 3)
```

---

## 8. Decision log

| Date | Decision |
|------|----------|
| 2026-07-28 | Adopt leave-alone vs do-right binary. No partial header migrations. Design captured here; implementation waits on inventory + ownership. |
| 2026-07-28 | Phase 1 inventory written (`inventory-tabular-notifies.md`). |
| 2026-07-28 | Phase 2: adopt #1614 product decision (modules → 2.14 default; built-in fallback until directives). Layout API lives in **libmux** (option A), not module-only and not engine-only. Phase 3 unblocked for API design/impl only — not blob translation. Exit checklist §4.5; Phase 3 handoff constraints §4.6. |
| 2026-07-28 | Phase 3: `mux_table_*` in libmux (`mux_table.h` / `mux_table.c`); comsys_mod and mail_mod drop local `append_*` copies; unit tests in `tests/table`. Phase 4 still owns blob-header conversion and engine dual-path table rewrites. |
| 2026-07-28 | Phase 4 start: A2 `@clist` / `@clist/headers` — engine + module use shared `mux_table_*` schema (name 13 / owner 15 / third 45 / pad 79); blob headers replaced by `M_("Channel")` etc. |
| 2026-07-28 | Phase 4 A1: `@clist/full` — engine + module; name 13 / header 15 / owner 15 / access 6 + freeform Users/Msgs; blob full-line header removed. |
| 2026-07-28 | Phase 4 A3: comlist — engine + module; alias 15 / channel 18 + freeform Status/Title; blob header removed. |
| 2026-07-28 | Phase 4 A4: @cwho — engine left printf codepoint width; both now mux_table display cols 29/6/6 with Name/Status/Player labels. |
| 2026-07-28 | Phase 4 B1/B2: mail alias lists — engine left printf+Spaces(desc_width); both now mux_table 12/40/15 (+ num 4 for admin). |
| 2026-07-28 | Phase 4 C1/C2/C3/C4/C8: staff tables (guests, site access, hashstat aligned header, bufstats, day report). |
| 2026-07-28 | Phase 4 C5–C7/C9: refs, UDF list, @hook list, reality levels → mux_table. |
| 2026-07-28 | Phase 5: `check_nls.py` rejects pre-spaced multi-column table header msgids in pot and `M_()` sources. Epic #1667 complete for columnar tables. |
| 2026-07-28 | Phase 4 B3: mail folder/review list lines — engine + module; From 16 display cols + Sub trunc 25 via mux_table; B4 multi-line forms still open. |
