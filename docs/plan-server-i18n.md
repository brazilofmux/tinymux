# Plan: Server-message internationalization (libintl / gettext)

**Status:** feasibility epic — not an implementation commitment.  
**Scope:** TinyMUX **server** messages only (netmux + engine + modules that speak to players).  
**Out of scope:** in-game softcode / builder-authored text, client UI strings (Hydra console/web/etc.), help-file translation as a first milestone (optional later).

Related epic: **#1419** — server-message internationalization (libintl/gettext) feasibility.

---

## 1. Problem

TinyMUX already funnels most C/C++ string literals through `T(...)`:

```c
// mux/include/config.h
#define T(x)    (reinterpret_cast<const UTF8 *>(x))
```

Today `T()` is **only a cast to `UTF8*`**. It is *not* gettext. That is useful: it is the natural extraction / runtime hook if we ever wire libintl.

Players and wizards see two very different classes of English:

| Class | Examples | Audience | Localize? |
|-------|----------|----------|-----------|
| **Softcode / function diagnostics** | `#-1 OUT OF RANGE`, `#-1 PERMISSION DENIED`, `#-1 ARGUMENTS MUST BE INTEGERS` | Softcode authors, often matched by `$`/`switch`/tests | **No** (default) — protocol-stable |
| **Notify / command chatter** | `Sorry, you don't have enough coins.`, `Permission denied.`, `@email: Not configured` | Connected players / staff | **Yes** — primary target |
| **Logs / startup** | `db_read: object dbref #…`, INI/LOAD lines | Operators, log scrapers | **Optional / separate domain** |
| **Help / wizhelp** | `mux/game/text/*.txt` (~25k lines) | Players/staff | **Later phase** (content, not `libintl` alone) |

Measured over `mux/src`, `mux/lib` and `mux/modules` — **distinct** literals, not
line hits, since the same sentence appearing twice is one msgid:

| | count |
|---|---|
| distinct `T("…")` literals | **6,976** |
| of those, reachable from a `notify*` sink | **896** |
| of those, `#-1 …` softcode tokens | **170** |
| distinct `#-1 …` literals anywhere | **180** |

The gap between 6,976 and 896 is the whole argument for the `T`/`S_` split in
§6.1: flipping `T()` naively exposes seven times more strings than want
translating, and sweeps up 170 softcode ABI tokens on the way — including the
bare `#-1`.

Game **builders** already own world text (attributes, `@emit`, etc.). This plan does **not** replace that with gettext.

---

## 2. Goals

1. Decide whether **GNU gettext / libintl** is a viable way to ship **translated server notifies** on **Linux, Windows, and macOS**.
2. Preserve **API stability** of softcode error tokens (`#-1 …`) so existing softcode and the smoke suite do not break.
3. Prefer a path that is **optional at build time** (English-only remains valid).
4. Document phases, risks, and a go/no-go checklist before large-scale `.po` work.

---

## 3. Non-goals (explicit)

- Translating builder softcode or database content.
- Forcing every operator to install locale packs.
- Changing softcode error **codes** or introducing a parallel structured-error API (possible later; not required for notify i18n).
- Full help-file localization as part of the first vertical slice (may use the same machinery later).
- Client (Hydra) UI localization (separate epic if ever needed).

---

## 4. Taxonomy of “server messages”

Mark every `T("…")` (and globals like `NOPERM_MESSAGE`) with one of:

| Tag | Meaning | Default policy |
|-----|---------|----------------|
| `I18N_PLAYER` | Shown via `notify` / `notify_quiet` / pose-like paths to a character | Translate |
| `I18N_SOFTERR` | Function/command result starting with `#-1` (or fixed machine tokens) | **Do not translate** (or separate catalog with stable English keys) |
| `I18N_LOG` | `STARTLOG` / `Log.tinyprintf` / stderr | Optional domain `tinymux-log` |
| `I18N_INTERNAL` | Debug, assert, never shown in-game | No |

**Critical invariant:** smoke tests and softcode matchers key off English `#-1 …` text today. Localizing those by default would break the world. Treat them as an **ABI**.

### 4.1 The boundary is already structural

Worth knowing before designing the tagging: the split is not merely a
convention that has to be imposed. Of the literals that reach a `notify*`
sink, **zero** are `#-1 …` tokens. Those never travel the player-output path
at all — they are `safe_str`'d into evaluation buffers and returned to
softcode. So "reaches `notify*`" is already a near-exact mechanical selector
for `I18N_PLAYER`, which makes the Phase 2 bootstrap cheaper to scope than a
hand classification of 6,976 call sites suggests.

What the 896 actually costs, since "896 strings" and "896 translatable
sentences" are different problems:

| | count | note |
|---|---|---|
| full sentences (≥20 chars) | 655 | translate directly, no code change |
| carrying a `%`-spec | 386 | need positional args (`%1$s`) before a translator can reorder |
| short fragments (<8 chars) | 35 | `'--- '`, `'Conn'`, `'Exits:'`, `'%s: %s'` — concatenation sites |

The expensive part is not extraction; it is the 386 format strings needing
positional arguments and the 35 concatenation sites needing restructuring.
Both are worth doing on their own merits — positional args and whole messages
are better code whether or not a `.mo` ever ships — so they can be paid for
before the go/no-go rather than after it.

### 4.2 Not every `#-1` message is a literal

`S_()` marking freezes *literals*. Two shapes on the `#-1` surface are not
literals, and marking cannot reach them:

**Assembled from pieces.** `[qqnofn(1)]` produces its diagnostic as
`T("#-1 FUNCTION (")` + the name + `T(") NOT FOUND")`. The tail fragment does
not begin with `#-1`, so any inventory built by grepping for `#-1` omits it
while still appearing complete.

**Spliced from third-party libraries.** Two sites embed another project's
wording verbatim:

```c
safe_str(T("#-1 REGEXP ERROR "), buff, bufc);
safe_str(errbuf, buff, bufc);                                  // PCRE2's text
snprintf(pResult, nResultMax, "#-1 LUA ERROR: %s", errmsg);    // Lua's text
```

That text varies with library version, and library version varies with
platform packaging (Homebrew vs vcpkg vs distro). So the parts of the `#-1`
surface most likely to differ between two builds are the two places TinyMUX
quotes someone else — not TinyMUX's own wording. Freezing the ABI has to
account for them explicitly, or the freeze is narrower than it reads.

If a future product wants localized softcode errors, that must be a **conf switch** defaulting off, and tests must run with English.

---

## 5. Platform feasibility (libintl)

### 5.1 Linux

| Item | Notes |
|------|--------|
| Library | GNU **libintl** via glibc gettext, or standalone gettext |
| Build | `AM_GNU_GETTEXT` / pkg-config; widely packaged |
| Runtime | `bindtextdomain("tinymux", LOCALEDIR)`; `textdomain("tinymux")` |
| Locales | System locales + `.mo` under e.g. `/usr/share/locale` or game-relative `game/locale` |
| Risk | Low |

### 5.2 macOS

| Item | Notes |
|------|--------|
| Library | **libintl** from Homebrew `gettext` (not always in base SDK); or link against gettext keg |
| Build | Detect via `configure` (`AC_CHECK_LIB` / `pkg-config`); ship optional |
| Runtime | Same GNU gettext API if linked against Homebrew gettext |
| Risk | Medium — must not require Homebrew for English-only builds; document rpath for optional dylib |

Apple’s own “locale” story is not a drop-in replacement for gettext `.po` workflows. Prefer **GNU gettext compatibility** for one message catalog format across all three OS families.

The same holds on Windows: the native equivalent would be `.mui` resource
DLLs, but those exist for Win32 resource tables, which a server that owns its
own string handling does not otherwise need. GNU gettext runs on Windows via
`libintl` from vcpkg or MSYS2, so `.po` can be the *only* catalog format on
all three platforms. That reduces the Windows question to a dependency and
packaging one, rather than a second mechanism to build and maintain.

### 5.3 Windows

| Item | Notes |
|------|--------|
| Library | Options: **gettext-runtime** (MinGW/vcpkg), **libintl** from GnuWin32/MSYS2, or a small vendored gettext-runtime |
| MSVC | Historically painful; vcpkg `gettext` is the modern path for `netmux.sln` |
| Runtime | `bindtextdomain` needs a real filesystem path next to the binary or under `game/locale` |
| UTF-8 | TinyMUX is UTF-8 throughout. Catalogs must be **UTF-8**; Windows console code pages are a **display** concern, not storage. Prefer UTF-8 `.mo` + existing UTF-8 game output. |
| Risk | Medium–high for packaging; low if i18n is optional and English is compiled-in |

### 5.3.1 What actually varies across OSes

Measured rather than assumed, since the ABI-freeze decision depends on it. Of
the 180 distinct `#-1 …` literals, **5** appear under any conditional
compilation, and all five are guarded by *build configuration*, not by OS:

| token | guard |
|---|---|
| `#-1 END OF TABLE`, `#-1 NO RESULTS SET` | `STUB_SLAVE` |
| `#-1 HMAC FAILED`, `#-1 UNSUPPORTED DIGEST TYPE` | `UNIX_DIGEST` |
| `#-1 PERMISSION DENIED` | `HAVE_WORKING_FORK` |

No token is *spelled* differently on a different platform. What differs is
whether it is reachable in a given build — which is the same reason a Windows
smoke run reports build-configuration failures for the `hmac`/`digest` cases.
So the cross-platform question is **availability, not wording**, and that is a
much weaker claim to have to defend. The exceptions are the library-spliced
messages in §4.2, which can genuinely differ in text.

### 5.4 Build matrix recommendation

| Config | Behavior |
|--------|----------|
| `--disable-nls` / no libintl found | `T(x)` remains cast; `N_(x)` marks only; English only |
| `--enable-nls` + libintl | `T(x)` → `gettext(x)` (or wrapper that returns `UTF8*`); load `.mo` at startup |
| Windows without gettext | Same as disable-nls; document vcpkg recipe for translators’ builds |

**Do not** make libintl a hard dependency of a release binary unless packaging is proven on all three.

---

## 6. Technical approach (if we proceed)

### 6.1 Macro layer

**Decision (#1444):** opt **in** for prose, do not flip tree-wide `T()` to gettext.
`T()` stays the UTF-8 cast it always was (command/function/attr names, formats,
internals). Translators only see strings someone deliberately marked — cheaper
to maintain, and a miss leaves English rather than breaking `abs()` / `@cron`.

```c
#define T(x)  (reinterpret_cast<const UTF8 *>(x))  // cast only — never gettext
#define M_(x) (mux_gettext(x))                     // player/staff prose (NLS on)
#define N_(x) (x)                                  // mark-only for xgettext / static msgid
#define S_(x) (reinterpret_cast<const UTF8 *>(x))  // softcode ABI: never translate
```

Migration strategy:

1. Introduce `S_()` for `#-1` and other ABI tokens (English forever by default).
2. Leave `T()` as cast everywhere (static tables stay statically initialisable).
3. Mark notify prose with **`M_()`** incrementally (~896 notify-reachable sentences).
4. Avoid format templates until `printf`-style args are catalog-safe (`ngettext` for plurals).

Each `T` → `M_` step is safe in isolation. A complete `T` → `S_` sweep for `#-1`
is still good hygiene but is no longer load-bearing for catalog safety.

### 6.2 Extraction

- `xgettext` over `mux/**/*.{cpp,h,c}` with keywords **`M_`**, **`N_`** only for the player domain (not `T`).
- Domain: **`tinymux`** for player notifies.
- Optional second domain: **`tinymux-log`**.
- CI job (Linux) that fails if new `notify*(M_("…"))` strings are missing from the `.pot` (once the pipeline exists).

### 6.3 Runtime

- On startup (after paths known):  
  `bindtextdomain("tinymux", <game>/locale)`  
  `textdomain("tinymux")`  
  `bind_textdomain_codeset("tinymux", "UTF-8")` where available.
- Locale selection: process environment (`LANG` / `LC_MESSAGES`) first; optional conf `locale` / `language` for hosting multiple games on one host.
- Per-player locale is a **phase 2** product decision (descriptor attribute? `@language`?) and multiplies complexity (thread/TLS locale in gettext).

**Phase 1 recommendation:** process-wide locale only (one language per netmux process).

### 6.4 Packaging

```
game/locale/
  en/LC_MESSAGES/tinymux.mo   # optional; English can be msgid passthrough
  es/LC_MESSAGES/tinymux.mo
  …
```

Ship at least: empty or English catalog, and one sample language for CI (e.g. `xx` pseudo-locale or `es` stub).

### 6.5 Plurals and formatting

- Prefer whole sentences in `T("…")` rather than concatenating translated fragments.
- Use `ngettext` for true plurals (`1 coin` / `N coins`).
- Existing `tprintf(T("…%s…"), …)` stays valid if translators keep format specifiers; add a CI check for `%` consistency.
- Positional arguments (`%N$`) are a separate runtime/toolchain problem (#1623); do not conflate them with layout.

### 6.6 Multi-column tables (presentation constraint)

Some notifies are **tables**: a header line plus aligned data rows. Today a few
headers ship as **one msgid with column spacing baked into English** (e.g.
`*** Channel       Header          Owner …`), while rows use independent
`PadField` / `co_copy_field` stops in C. A translator cannot keep those aligned
under CJK or after a stop change — see #1648.

**Policy until converted:**

1. **Do not translate** pre-spaced multi-column header msgids; leave `msgstr ""`
   so English passthrough preserves layout.
2. **Do not** “fix” one header in isolation or translate with hand-counted
   spaces — that is a partial migration and extra work later.
3. **Do it right** means: column schema (shared widths), one msgid per label,
   same cell primitive for header and rows (`co_copy_field`), layout API in
   **libmux** (so modules, engine fallback, and engine-only staff tables share
   one path). Product ownership of comsys/mail is #1614 (modules → default;
   not re-decided here).

Full design and phases: [`design-tabular-notifies.md`](design-tabular-notifies.md).  
Phase 1 inventory: [`inventory-tabular-notifies.md`](inventory-tabular-notifies.md).

---

## 7. Phased roadmap

| Phase | Deliverable | Exit criteria |
|-------|-------------|----------------|
| **0 — Feasibility** (this doc + epic) | Taxonomy, platform matrix, go/no-go | Agreement on softcode ABI freeze |
| **1 — Plumbing** | Configure detection; `mux_gettext` stub; optional link; `bindtextdomain` to `game/locale`; English passthrough | English-only CI green on Linux/Win/macOS with NLS on and off |
| **2 — Catalog bootstrap** | `.pot` extraction; mark `S_()` for `#-1` family; first `.po` for a slice of **notify** strings only (e.g. create/destroy/quota) | One non-English sample; smoke suite still English |
| **3 — Coverage** | Expand notify coverage; docs for translators; packaging | Most `I18N_PLAYER` strings extracted |
| **4 — Optional** | Help-file localization strategy; per-player locale; log domain | Only if Phase 3 paid off |

**Estimated size (order of magnitude):**

- Plumbing: small (configure + thin wrapper + startup).
- Correct `S_` vs `T` classification: medium (hundreds of call sites; mechanical for `#-1`, judgment for mixed).
- Full notify catalog: large (many unique English sentences; ongoing maintenance).

---

## 8. Risks and open questions

| Risk / question | Notes |
|-----------------|--------|
| Softcode matching `#-1 …` | Must stay English by default |
| Smoke suite | Runs English; never depend on host locale |
| Concatenated strings | Hard to translate; prefer whole messages |
| `T()` already used for non-messages | Cast used for any UTF-8 literal; extraction needs keyword discipline |
| Windows packaging | vcpkg/MSYS path must be documented or NLS stays off |
| macOS dylib | Homebrew gettext rpath |
| Who maintains `.po`? | Upstream ships infrastructure + English; community translations |
| Modules (comsys/mail) | Same rules; extract with core |
| Pre-spaced table headers | Do not translate; convert only via tabular layout design (#1648 / `design-tabular-notifies.md`) |
| Proxy / Hydra | Out of scope unless we extend the epic later |

---

## 9. Go / no-go checklist

Proceed past Phase 0 only if:

1. [ ] Softcode `#-1` strings are agreed **stable English ABI** (not translated by default).
   Cheapest way to make this checkable rather than asserted: add a corpus that
   *provokes* each reachable token and logs what was emitted, so confirming the
   ABI across platforms is a `diff` of two smoke logs. That catches the
   assembled and library-spliced messages of §4.2, which no static inventory
   can, and it yields the `S_()` inventory as an observed artifact rather than
   a grep.
2. [ ] NLS is **optional** on Linux, Windows, and macOS English builds.
3. [ ] A Phase 1 prototype links gettext on at least **Linux + one of {Windows, macOS}** without breaking `make test`.
4. [ ] Translator workflow (`.pot` → `.po` → `.mo` → `game/locale`) is documented for a single sample language.
5. [ ] Owner capacity exists for catalog churn (new notifies need `xgettext` discipline).

If (1)–(2) fail, stop: document “not feasible under current softcode contracts” and keep `T()` as cast-only.

---

## 10. Suggested first experiments (spike, not full epic)

1. Build with `--enable-nls` on Linux only: `T` → gettext for a **single** notify path (e.g. one `create.cpp` message); prove locale switch.
2. Convert one `#-1` site to `S_()` and prove smoke still matches.
3. On Windows: attempt vcpkg gettext link **or** document “NLS unsupported on MSVC for now”.
4. On macOS: Homebrew gettext link smoke.

Spike success → Phase 1 PR. Spike failure on two platforms → revise (e.g. runtime-only English tables, or drop Windows NLS).

---

## 11. Alternatives considered

| Approach | Pros | Cons |
|----------|------|------|
| **GNU gettext / libintl** | Industry standard, `.po` tools, plurals | Packaging pain on Windows/macOS |
| Hand-rolled string tables | Full control, UTF-8 easy | Reinvent plurals, tooling, contributor flow |
| ICU MessageFormat | Powerful | Heavy dependency for this problem |
| Translate help files only | High player value | Different pipeline (large text, not `T()`) |

Recommendation: **gettext if Phase 1 spikes clear**; otherwise stay English with optional later help-file project.

---

## 12. References (in-tree)

- `mux/include/config.h` — `T()` cast
- `mux/src/driver_bridge.cpp` — `FUNC_NOPERM_MESSAGE = T("#-1 PERMISSION DENIED")`
- `mux/game/text/*.txt` — help content (later phase)
- Smoke suite — English `#-1` and notify matchers
- Softcode error style used throughout `functions.cpp` / `funceval*.cpp` / `funmath.cpp`
