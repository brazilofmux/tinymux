# RhostMUSH Features for Adoption

Source: `/tmp/rhostmush/Server/src/`

## Tier 1 — Quick Wins

| # | Function | What it does | Source | Status |
|---|----------|-------------|--------|--------|
| 1 | `between(lo,hi,val[,inclusive])` | Boolean range test | functions.c | DONE |
| 2 | `delextract(list,pos,count[,sep])` | Delete a range of elements from a list | functions.c | DONE |
| 3 | `garble(string,percent)` | Garble text by percentage (RP: drunk, distance, languages) | functions.c | DONE |
| 4 | `caplist(list[,sep,osep])` | Title-case with smart article/conjunction handling | functions.c | DONE |
| 5 | `moon(secs[,mode])` | Moon phase (% illumination) | functions.c | DONE |
| 6 | `soundex()`/`soundlike()` | Phonetic encoding and fuzzy name matching | functions.c | DONE |
| 7 | `lnum2(start,end,step)` | lnum with step increment | functions.c | SKIP — lnum() already has 4th arg for step |
| 8 | `while(eval,cond,list,compval[,isep,osep])` | Iterate while condition is true | functions.c | DONE |

## Tier 2 — Moderate Effort, Good Value

| # | Feature | What it does | Source |
|---|---------|-------------|--------|
| 9 | `@pipe[/on\|/off\|/tee]` | Capture command output to attribute | command.c | SKIP — architecturally invasive |
| 10 | `crc32obj(object)` | CRC32 across all attributes on an object | functions.c | DONE |
| 11 | `@label`/`@goto` | Named jump targets in action lists | command.c | SKIP — Rhost impl is text-annotation hack |
| 12 | `sandbox(expr,funclist[,reverse])` | Evaluate with restricted function set | functions.c | DONE |
| 13 | `wrapcolumns(text,width,cols,...)` | Multi-column text wrapping | functions.c | DONE |
| 14 | `subnetmatch(ip,cidr)` | IP subnet membership test | functions.c | DONE |
| 15 | `@selfboot` | Players boot own stale connections | command.c | SKIP — can be softcoded |
| 16 | `@freeze`/`@thaw` | Freeze queue entry by PID | wiz.c | SKIP — niche |

## Tier 3 — Larger Features

| # | Feature | What it does | Source |
|---|---------|-------------|--------|
| 17 | Totem system | User-definable flag-like system (factions, species, custom states) | flags.c |
| 18 | Depower system | Inverse of powers — restrict what wizards can do | flags.c |
| 19 | Room logging | LOGROOM toggle — auto file logging of room activity | conf.c |
| 20 | CPU abuse tiers | Multi-level CPU protection with throttling | conf.c |

## Skip

- `@sudo` — MUX has `@force/now`
- `@snoop` — security/privacy concerns
- `@cluster` — MUX SQLite backend solves attribute limits differently
- Door system — MUX Lua module is more modern
- Senses (smell/taste/touch) — easily done in softcode
- News/BBS — best left to softcode
- `mask()` — MUX has band/bor/bxor/bnand
- `privatize()`/`pushregs()` — `@include/localize` covers this
- Toggle system — overlaps with MUX flag/power system
- WebSocket — MUX already has it
