# Inventory: multi-column player-facing tables (Phase 1)

**Epic:** #1667  
**Design:** [`design-tabular-notifies.md`](design-tabular-notifies.md)  
**Date:** 2026-07-28  
**Scope:** Player/staff **notifies** that look like tables (header + aligned columns). Not softcode `#-1` output, not log lines, not debug `printf` to stdout.

**Policy (unchanged):** leave alone or do it right — no conversion in this inventory.

---

## Legend

| Tag | Meaning |
|-----|---------|
| **Blob header** | One msgid/string with spacing baked into English (`M_("…  …")` or same text via `T_`) |
| **Label+fmt header** | Header built from labels with `%-N.Ns` / field helpers (still dual: labels vs row math) |
| **No header / freeform** | Multi-field lines without a separate column header, or single-line “forms” |
| **Cell primitive** | How row cells are width-limited: `PadField`+`StripTabsAndTruncate`, `co_copy_field` / `append_ljust_field`, `%-N.Ns` printf, `LeftJustify*` |
| **Dual** | Engine + loadable module both implement (comsys/mail) |

**Translate?** “No (blob)” = do not fill `msgstr` until Phase 4 conversion.  
“Not yet / labels OK later” = header labels could become per-msgid when schema exists; still do not half-migrate.

---

## A. Dual path — comsys (highest NLS + parity priority)

### A1. `@clist/full` — full channel directory

| | Engine | Module |
|--|--------|--------|
| File | `modules/engine/comsys.cpp` `do_listchannels` | `modules/comsys/comsys_mod.cpp` `ChanListFull` |
| Header | **Converted (#1667 Phase 4)** — `"*** "` + per-label `Channel` / `Header` / `Owner` / `Access` / freeform `Users` `Msgs` | Same via `T_()` labels |
| Cell primitive | `mux_table_*` on `co_copy_field` | `mux_table_*` (aliases) |
| Columns | PLS/`*** ` (4) · name **13** · header **15** · owner **15** · access **6** (JXR+pad) · freeform counts | Same |
| Translate? | **Per-label** — do not reintroduce the full-line blob | Same |
| Dual | **Yes** — both paths converted together |

### A2. `@clist` / `@clist/headers` — short channel directory

| | Engine | Module |
|--|--------|--------|
| File | `comsys.cpp` `do_chanlist` | `comsys_mod.cpp` `ChanList` |
| Header | **Converted (#1667 Phase 4)** — `"*** "` + `M_("Channel")` / `M_("Owner")` / `M_("Header"\|"Description")` via `mux_table_append_ljust` | Same construction with `T_()` labels |
| Cell primitive | `mux_table_*` on `co_copy_field` | `mux_table_*` (via module aliases) |
| Columns | PLS/`*** ` (4) · name **13** · owner **15** · third **45** · pad to **79** | Same |
| Translate? | **Per-label** `Channel`, `Owner`, `Header`, `Description` — do not reintroduce a pre-spaced blob | Same |
| Dual | **Yes** — both paths converted together |

### A3. `comlist` — player’s channel aliases

| | Engine | Module |
|--|--------|--------|
| File | `comsys.cpp` (~3029) | `comsys_mod.cpp` `ComList` |
| Header | **Blob** `M_("Alias           Channel            Status   Title")` | **Blob** `T_(same)` |
| Cell primitive | `tprintf` `%-15.15s %-18.18s` + free status/title | `append_ljust_field` 15 / 18 + `mux_sprintf` status trail |
| Columns | alias **15** · channel **18** · status/title freeform | Same |
| Translate? | **No (blob)** | **No (blob)** |
| Dual | **Yes** |

### A4. `@cwho` — who is on a channel

| | Engine | Module |
|--|--------|--------|
| File | `comsys.cpp` `do_channelwho` | `comsys_mod.cpp` `ChanWho` |
| Header | **Label+fmt** `tprintf(T("%-29.29s %-6.6s %-6.6s"), "Name", "Status", "Player")` — labels not `M_` | **Label+field** `append_ljust_field` with `T_("Name")` etc. (29 / 6 / 6) |
| Cell primitive | `%-29.29s` + `strip_color(unparse_object)` | `co_copy_field` path + `UnparseObject` / strip |
| Columns | name **29** · status **6** · player **6** | Same |
| Translate? | Labels should become `M_()` when converted; **not** a pre-spaced single msgid today | Same |
| Dual | **Yes** |
| Notes | Already half-modern on module; engine still printf-width (codepoints, not display cols). Conversion = shared schema for both. |

---

## B. Dual path — mail

### B1. Mail alias list (player-visible)

| | Engine | Module |
|--|--------|--------|
| File | `mail.cpp` `do_malias_list_all` | `mail_mod.cpp` `do_malias_list_all` |
| Header | **Blob** `M_("Name         Description                              Owner")` | **Blob** `T_(same)` |
| Cell primitive | `%-12s` + `Spaces(40 - desc_width)` + `%-15.15s` moniker | `append_ljust_field` 12 / **40** / 15 |
| Columns | name **12** · description **40** · owner **15** | Same intent |
| Translate? | **No (blob)** | **No (blob)** |
| Dual | **Yes** |

### B2. Mail alias admin list

| | Engine | Module |
|--|--------|--------|
| File | `mail.cpp` `do_malias_adminlist` | `mail_mod.cpp` `do_malias_adminlist` |
| Header | **Blob** `M_("Num  Name         Description                              Owner")` | **Blob** `T_(same)` |
| Cell primitive | `%-4d %-12s` + spaces/desc + `%-15.15s` | `%-4d` + fields 12 / 40 / 15 |
| Translate? | **No (blob)** | **No (blob)** |
| Dual | **Yes** |

### B3. Mail folder / review list lines

| | Engine | Module |
|--|--------|--------|
| File | `mail.cpp` list/review paths | `mail_mod.cpp` list helpers |
| Header | Folder banner `MAIL_LINE` / dash lines — not a column schema header | Similar banners |
| Rows | e.g. `[%s] %-3d (%4d) From: %s Sub: %s` with `trimmed_name` / STT subject | `append_ljust_field` for From (16), time (25), etc. on detail headers |
| Shape | **Labeled freeform** / semi-table — not the same “blob header vs PadField stops” bug, but still multi-field layout |
| Translate? | Format strings with mixed labels — convert later as forms, not Phase-4 “grid” first cut |
| Dual | **Yes** |

### B4. Mail read / detail block

Multi-line forms (`From:`, `At:`, `Fldr:`, `To:`, `Subject:`) with width on moniker/time fields. **Form layout**, not a columnar directory. Track under epic for completeness; **lower priority** than A/B blob grids.

---

## C. Engine-only staff / diagnostic tables

These are player-visible (wizard/staff) but **not** dual comsys/mail. Still subject to the blob rule if translated.

### C1. `@list guests`

| | |
|--|--|
| File | `mguests.cpp` `CGuests::ListAll` |
| Header | **Blob** `M_("*Guest #  : Name            dbref  Status     Last Site")` (+ rule lines) |
| Rows | `mux_sprintf` `Guest %-3d: %-15s #%-5d %-10s %s` |
| Translate? | **No (blob)** for header until converted |
| Dual | No |

### C2. Site access (`@list site` / subnet list)

| | |
|--|--|
| File | `src/net.cpp` `mux_subnets::listinfo` |
| Header | **Blob** `M_("Address                                            Status")` |
| Rows | `%-50s %s` address/control |
| Translate? | **No (blob)** |
| Dual | No |

### C3. `@list hashstat` (abbreviated)

| | |
|--|--|
| File | `command.cpp` `list_hashstats` |
| Header | **Blob** `M_("Hash Stats    Size    Num     Del       Lookups          Hits        Probes Long")` |
| Rows | `LeftJustifyString` name 13 + entry count — **header columns do not match abbreviated rows** (header is legacy full-stat wording) |
| Translate? | **No (blob)**; note header/row mismatch even in English |
| Dual | No |

### C4. Buffer pool stats

| | |
|--|--|
| File | `lib/alloc.cpp` `list_bufstats` |
| Header | **Blob** `M_("Buffer Stats  Size      InUse      Total           Allocs   Lost")` |
| Rows | `LeftJustifyString` / `RightJustifyNumber` fixed widths 12 / 5 / 10 / 10 / 16 / 6 |
| Translate? | **No (blob)** |
| Dual | No |

### C5. `@list ref` / reference list

| | |
|--|--|
| File | `predicates.cpp` |
| Header | **Label+fmt** `tprintf(T("%-12s %-20s %-20s"), T("Reference"), T("Target"), T("Owner"))` |
| Rows | Same format |
| Translate? | Labels already separate strings — better shape; still printf-width not `co_copy_field` |
| Dual | No |

### C6. User-defined function list (`@list ufun` / FN_LIST)

| | |
|--|--|
| File | `functions.cpp` |
| Header | **Label+fmt** English literals in `tprintf` (`Function Name`, `DBref#`, `Attribute`) — not `M_` |
| Rows | `%-28.28s` / `#%-7d` / `%-30.30s` + flags |
| Translate? | Convert later; not pot-blob today if not marked `M_` |
| Dual | No |

### C7. `@list hooks` style command/attr columns

| | |
|--|--|
| File | `command.cpp` (~4947+) |
| Header | e.g. `%-32s | %s` with `"Built-in Command"` / `"Hook Mask Values"` via `T_` |
| Rows | `%-32.32s | %s` |
| Shape | Two-column report |
| Dual | No |

### C8. Activity report (`do_report` day bins)

| | |
|--|--|
| File | `walkdb.cpp` |
| Header | **Blob** `M_("Day   Hours     Players  Total")` |
| Rows | `%3d %03d - %03d: %6d %6d` |
| Translate? | **No (blob)** |
| Dual | No |

### C9. Level dump (command.cpp)

| | |
|--|--|
| File | `command.cpp` ~4189 |
| Shape | `Level: %-20.20s    Value: 0x%08x     Desc: %s` — labeled freeform row, no multi-col header blob |
| Dual | No |

---

## D. Out of inventory scope (recorded so they are not forgotten)

| Item | Why out |
|------|---------|
| Softcode function return tables | Not `notify` grids; `#-1` ABI |
| Log `tinyprintf` columns | `I18N_LOG` / operators |
| DBT / HIR / harness `printf` | Developer stdout |
| Single-line banners (`***** End of Mail Aliases *****`, `--- Site Access ---`) | Not multi-column alignment |
| Help text files | Separate content localization |

---

## E. Pot / catalogue summary (pre-spaced multi-word headers)

From `mux/po/tinymux.pot` at inventory time (grep for multi-space column-like msgids):

| msgid | Family |
|-------|--------|
| `Buffer Stats  Size      InUse      Total           Allocs   Lost` | C4 |
| ~~`*** Channel       Header          Owner           Access  Users Msgs`~~ | A1 — **removed** Phase 4 (per-label) |
| `Alias           Channel            Status   Title` | A3 |
| ~~`*** Channel       Owner           Header`~~ | A2 — **removed** Phase 4 (per-label) |
| ~~`*** Channel       Owner           Description`~~ | A2 — **removed** Phase 4 (per-label) |
| `Name         Description                              Owner` | B1 |
| `Num  Name         Description                              Owner` | B2 |
| `*Guest #  : Name            dbref  Status     Last Site` | C1 |
| `Day   Hours     Players  Total` | C8 |
| `Address                                            Status` | C2 |
| `Hash Stats    Size    Num     Del       Lookups          Hits        Probes Long` | C3 |

**Translator rule:** leave all of the above `msgstr ""` until that family is converted under #1667 Phase 4.

Module-side duplicates that use only `T_()` may be **missing from pot** while still English-only blobs at runtime — conversion should mark headers with `M_()` per label, not re-introduce blobs into pot.

---

## F. Cell-primitive map (as of inventory)

| Primitive | Used by |
|-----------|---------|
| `PadField` + `StripTabsAndTruncate` | Engine comsys `@clist*` |
| `append_ljust_field` / `co_copy_field` | Module comsys + mail (many columns) |
| `%-N.Ns` / `tprintf` | Engine comlist, cwho, mail aliases, ufun list, hooks, refs |
| `LeftJustifyString` / `RightJustifyNumber` | Buffer stats, hashstat abbreviated rows |
| `trimmed_name` + STT | Mail list/review monikers |

Phase 3 API should sit on **`co_copy_field`** (design doc); migration of engine `PadField` tables is part of “do it right,” not a separate partial.

---

## G. Suggested conversion order (when Phase 2–4 unblocked)

Order optimizes **dual-parity pain × NLS risk × player visibility**:

1. **A2** `@clist` / headers — dual, high visibility, blob  
2. **A1** `@clist/full` — dual, more columns  
3. **A3** comlist — dual blob  
4. **A4** `@cwho` — dual; labels already separable  
5. **B1–B2** mail aliases — dual blob  
6. **C1, C2, C4, C8, C3** staff tables — engine-only blobs  
7. **B3–B4, C5–C7, C9** freeform / forms — later  

**Do not** convert only the module or only the engine for A/B. Phase 2: both
sides of a dual family convert in the same change via the **libmux** layout
API. Product default may still be the built-in until #1614 directives land.

---

## H. Gaps / follow-ups for a later inventory refresh

- Full mail list/read format matrix (every `raw_notify` width in mail.cpp / mail_mod.cpp).  
- Any module-only tables outside comsys/mail.  
- Confirm which `T_` blob headers are extracted (or not) by `xgettext` keywords.  
- Re-measure after any large comsys/mail rewrite.

---

## I. Phase 1 exit criteria

- [x] Dual comsys tables A1–A4 listed with engine/module primitives and widths  
- [x] Dual mail alias tables B1–B2 listed  
- [x] Known pot blob headers catalogued  
- [x] Staff engine-only tables C1–C9 sampled  
- [x] Explicit non-goals (D)  
- [x] Conversion order proposal (G) without starting conversion  

**Phase 2 (done in design doc §4):** product ownership follows #1614
(modules become 2.14 default; built-ins fallback until directives). **Layout**
ownership is **libmux** so dual paths and engine-only staff tables share one
API. See `design-tabular-notifies.md` §4.

**Phase 3 (layout API):** `mux_table_append_*` / `mux_table_emit_fields` in
libmux; modules call them. **Not** yet: converting blob headers or engine
`PadField` tables (Phase 4).

**Next:** Phase 4 — convert table families (both dual paths in one change).
