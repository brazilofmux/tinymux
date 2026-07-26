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

Rough order-of-magnitude in `mux/modules/engine` (line hits, not unique strings):

- `#-1 …` style returns: ~**390**
- `notify*` with `T(...)`: ~**975**
- `safe_str(T(...))` (mixed diagnostics + rare prose): ~**470**

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

### 5.3 Windows

| Item | Notes |
|------|--------|
| Library | Options: **gettext-runtime** (MinGW/vcpkg), **libintl** from GnuWin32/MSYS2, or a small vendored gettext-runtime |
| MSVC | Historically painful; vcpkg `gettext` is the modern path for `netmux.sln` |
| Runtime | `bindtextdomain` needs a real filesystem path next to the binary or under `game/locale` |
| UTF-8 | TinyMUX is UTF-8 throughout. Catalogs must be **UTF-8**; Windows console code pages are a **display** concern, not storage. Prefer UTF-8 `.mo` + existing UTF-8 game output. |
| Risk | Medium–high for packaging; low if i18n is optional and English is compiled-in |

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

Evolve without renaming every call site:

```c
// English-only / no NLS
#define T(x)  (reinterpret_cast<const UTF8 *>(x))

// With NLS (illustrative)
#define T(x)  (mux_gettext(x))           // player-facing path
#define N_(x) (x)                        // mark-only for xgettext
#define S_(x) (reinterpret_cast<const UTF8 *>(x))  // softcode ABI: never translate
```

Migration strategy:

1. Introduce `S_()` (or `T_SOFTERR()`) for `#-1` and other ABI tokens.
2. Keep `T()` for notifies initially, then optionally flip `T` → gettext when NLS on.
3. Avoid translating format templates until `printf`-style args are catalog-safe (`ngettext` for plurals).

### 6.2 Extraction

- `xgettext` over `mux/**/*.{cpp,h,c}` with keywords `T`, `N_`, `S_` (S_ extracted but marked `softerr` / not shipped to player catalogs).
- Domain: **`tinymux`** for player notifies.
- Optional second domain: **`tinymux-log`**.
- CI job (Linux) that fails if new `notify*(T("…"))` strings are missing from the `.pot` (once the pipeline exists).

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
| Proxy / Hydra | Out of scope unless we extend the epic later |

---

## 9. Go / no-go checklist

Proceed past Phase 0 only if:

1. [ ] Softcode `#-1` strings are agreed **stable English ABI** (not translated by default).
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
