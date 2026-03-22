# PennMUSH Features for Adoption

Source: `/tmp/pennmush`

## Tier 1—High Value, Moderate Effort

### Softcode Functions

| Function | What it does | PennMUSH source |
|----------|-------------|-----------------|
| `mean()` | Arithmetic mean of a list | funmath.c |
| `median()` | Median of a list | funmath.c |
| `stddev()` | Standard deviation of a list | funmath.c |
| `bound()` | Clamp value to min/max range | funmath.c |
| `unique()` | Remove duplicates from a list | funlist.c |
| `firstof()` | First non-false argument (short-circuit) | funmisc.c |
| `allof()` | All non-false arguments concatenated | funmisc.c |
| `strfirstof()` | First non-empty string argument | funmisc.c |
| `strallof()` | All non-empty strings concatenated | funmisc.c |
| `linsert()` | Insert element at position in list | funlist.c |
| `lreplace()` | Replace element at position in list | funlist.c |
| `strdelete()` | Delete characters from string by position | funstr.c |
| `strinsert()` | Insert string at character position | funstr.c |
| `strreplace()` | Replace characters in string by position | funstr.c |
| `unsetq()` | Clear specific q-registers | funmisc.c |
| `listq()` | List all set q-registers | funmisc.c |
| `lplayers()` | List player dbrefs in a location | fundb.c |
| `lthings()` | List thing dbrefs in a location | fundb.c |
| `ncon()` | Count contents of a location | fundb.c |
| `nexits()` | Count exits from a location | fundb.c |
| `nplayers()` | Count players in a location | fundb.c |
| `nthings()` | Count things in a location | fundb.c |
| `prompt()` | Send telnet GA-terminated prompt | funmisc.c |

### Channel Functions

| Function | What it does | PennMUSH source |
|----------|-------------|-----------------|
| `cbuffer()` | Channel buffer size | extchat.c |
| `crecall()` | Recall channel message history | extchat.c |
| `cowner()` | Channel owner dbref | extchat.c |
| `cstatus()` | Player's on/off/gag status on channel | extchat.c |
| `ctitle()` | Player's channel title | extchat.c |
| `cusers()` | Count users on a channel | extchat.c |
| `cdesc()` | Channel description | extchat.c |
| `cflags()` | Channel flags | extchat.c |

### Commands

| Command | What it does | PennMUSH source |
|---------|-------------|-----------------|
| `@chatformat` | Per-player channel output format attribute | extchat.c |
| `@include` | Inline attribute evaluation (not queued) | command.c |

## Tier 2—High Value, Higher Effort

| Feature | What it does | PennMUSH source |
|---------|-------------|-----------------|
| `#lambda` | Anonymous inline attributes for filter/map/etc | funufun.c, parse.c |
| `regrep()` / `regrepi()` | Regex-based attribute value searching | fundb.c |
| `mapsql()` | Map SQL results through softcode function | fundb.c |
| Queue PIDs | Per-entry process IDs, getpids(), @halt/pid | cque.c |
| Channel mogrifiers | Object transforms all channel messages | extchat.c |
| `@hook` enhancements | /inline, /override, /extend switches | command.c |
| Nospoof emits | NS* family (nsemit, nspemit, etc.) | cmds.c |
| Built-in HTTP server | Serve web pages/REST from softcode | http.c |

## Tier 3—Nice to Have

| Feature | What it does |
|---------|-------------|
| Runtime custom flags | @flag/add without recompiling |
| Lock introspection | llocks(), lockflags(), lockowner() |
| Additional lock types | interact, follow, command, listen, control |
| speak() | Structured speech formatting |
| @select | Pattern-matching command variant |

## Skip

- decompose(), soundex(), soundslike()—niche
- Penn-specific naming variants—not worth compatibility
- pe_regs_dump()—internal debugging
