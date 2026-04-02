# TinyMUX Function Matrix Survey

This survey maps the ~493 registered softcode functions across their
"units of operation":

- **Scalar arguments**: discrete values passed as comma-separated args
- **Word / list items**: space-delimited (or sep-delimited) tokens
- **Grapheme clusters**: user-perceived characters (Unicode-aware)
- **Bytes**: raw storage units (intentionally thin -- not a user-facing model)

The important design constraint is that TinyMUX is trying hard to avoid
exposing bytes as a first-class user model. `strmem()` exists as a
diagnostic escape hatch, not as the start of a byte-oriented API family.

**Current vs aspirational**: This document describes *actual current
semantics*. Where a function's implementation does not match its
aspirational unit model (e.g., `wordpos()` uses byte offsets rather than
grapheme offsets, `foreach()` iterates code points rather than grapheme
clusters), the table labels the actual behavior and notes the gap. The
"Gaps" sections describe what would be needed to reach full grapheme
coverage.

A secondary constraint: **vectors** (vadd, vsub, etc.) are a specialized
list sub-family, not a separate dimension. They operate on space-delimited
numeric lists with optional separator overrides.

---

## 1. Numeric and Logical Reductions

### 1a. Arithmetic

| Operation | Scalar (args) | Integer Scalar | List Reduction | Integer List | Vector |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Addition | `add()` | `iadd()` | `ladd()`, `lmath(sum)` | -- | `vadd()` |
| Subtraction | `sub()` | `isub()` | `lmath(sub)` | -- | `vsub()` |
| Multiplication | `mul()` | `imul()` | `lmath(mul)` | -- | `vmul()` (scalar-vector) |
| Division | `fdiv()` | `idiv()` | `lmath(div)` | -- | -- |
| Floor Division | `floordiv()` | -- | -- | -- | -- |
| Modulus | `mod()` | -- | `lmath(mod)` | -- | -- |
| Remainder | `remainder()` | -- | -- | -- | -- |
| Float Mod | `fmod()` | -- | -- | -- | -- |
| Power | `power()` | -- | -- | -- | -- |
| Abs Value | `abs()` | `iabs()` | -- | -- | -- |
| Sign | `sign()` | `isign()` | -- | -- | -- |
| Increment | `inc()` | -- | -- | -- | -- |
| Decrement | `dec()` | -- | -- | -- | -- |
| Min | `min()` | -- | `lmin()`, `lmath(min)` | -- | -- |
| Max | `max()` | -- | `lmax()`, `lmath(max)` | -- | -- |
| Mean | `mean()` | -- | `lmath(mean)`/`lmath(avg)` | -- | -- |
| Median | `median()` | -- | `lmath(median)` | -- | -- |
| Std Dev | `stddev()` | -- | -- | -- | -- |
| Dot Product | -- | -- | -- | -- | `vdot()` |
| Cross Product | -- | -- | -- | -- | `vcross()` |
| Magnitude | -- | -- | -- | -- | `vmag()` |
| Unit Vector | -- | -- | -- | -- | `vunit()` |
| Dimension | -- | -- | -- | -- | `vdim()` = `words()` |
| Distance | `dist2d()`, `dist3d()` | -- | -- | -- | -- |
| Sqrt | `sqrt()` | -- | -- | -- | -- |
| Exp / Log | `exp()`, `ln()`, `log()` | -- | -- | -- | -- |
| Trig | `sin()`, `cos()`, `tan()`, `asin()`, `acos()`, `atan()`, `atan2()` | -- | -- | -- | -- |
| Constants | `pi()`, `e()` | -- | -- | -- | -- |
| Base Convert | `baseconv()` | -- | -- | -- | -- |
| Rounding | `ceil()`, `floor()`, `round()`, `trunc()` | -- | -- | -- | -- |
| Spelling | `spellnum()`, `roman()` | -- | -- | -- | -- |
| Random | `rand()`, `die()`, `successes()` | -- | `lrand()`, `pickrand()` | -- | -- |
| Distribute | -- | -- | `distribute()` | -- | -- |

### 1b. Logical and Bitwise

| Operation | Scalar (args) | Short-Circuit | Bool Variant | List Reduction |
| :--- | :--- | :--- | :--- | :--- |
| AND | `and()` | `cand()` | `andbool()`, `candbool()` | `land()` |
| OR | `or()` | `cor()` | `orbool()`, `corbool()` | `lor()` |
| XOR | `xor()` | -- | -- | **missing** |
| NOT | `not()` | -- | -- | -- |
| Bitwise AND | `band()` | -- | -- | **missing** |
| Bitwise OR | `bor()` | -- | -- | **missing** |
| Bitwise XOR | `bxor()` | -- | -- | **missing** |
| Bitwise NAND | `bnand()` | -- | -- | -- |
| Shift Left | `shl()` | -- | -- | -- |
| Shift Right | `shr()` | -- | -- | -- |

### 1c. Comparison

| Operation | Numeric | String | List |
| :--- | :--- | :--- | :--- |
| Equal | `eq()` | `strmatch()`, `match()` | -- |
| Not Equal | `neq()` | -- | -- |
| Greater Than | `gt()` | -- | -- |
| Greater or Equal | `gte()` | -- | -- |
| Less Than | `lt()` | -- | -- |
| Less or Equal | `lte()` | -- | -- |
| Compare (3-way) | -- | `comp()` | -- |
| Lexicographic Min | -- | `alphamin()` | -- |
| Lexicographic Max | -- | `alphamax()` | -- |
| Between | `between()` | -- | -- |
| Bound/Clamp | `bound()` | -- | -- |
| Edit Distance | -- | `strdistance()` | -- |
| Soundex | -- | `soundex()`, `soundlike()` | -- |

### Gaps in Numeric / Logical

1. **Integer list reductions**: `iadd`, `isub`, `imul`, `idiv` exist for
   scalars, but the list side (`ladd`, `lmath`) is float-only. No `limath()`
   or `liadd()` / `limul()` etc.
2. **Horizontal bitwise reducers**: `band()`, `bor()`, `bxor()` accept
   variadic args, but there is no list-reduction form (`lband()`, `lbor()`,
   `lbxor()`).
3. **`lxor()`**: boolean list parity reduction is missing.
4. **List comparison**: no element-wise compare that returns a list of
   results (like a vectorized `eq()`). This may be too specialized to
   matter; `mix()` can approximate it.
5. **`floordiv()`** has no integer or list variant.
6. **`remainder()` vs `mod()`**: two scalar modulus operations; neither
   has a list variant.

---

## 2. Spatial and Sequence Operations

### 2a. Counting

| What | Word / List | Grapheme | Byte |
| :--- | :--- | :--- | :--- |
| Count items | `words()` | `strlen()` | `strmem()` |

### 2b. Extraction / Slicing

| Operation | Word / List | Grapheme |
| :--- | :--- | :--- |
| Positional slice | `extract(str,first,len,isep,osep)` | `mid(str,start,len)` |
| First item | `first(str,sep)` | `left(str,len)` |
| All but first | `rest(str,sep)`, `lrest(str,sep)` | -- |
| Last item | `last(str,sep)` | `right(str,len)` |
| All but last | -- | -- |
| Truncate | -- | `strtrunc(str,len)` |
| Multi-index select | `elements(str,positions,isep,osep)` | -- |
| Range by delim | `index(str,token,sep,count)` | -- |
| Before/After token | `before(str,token)` / `after(str,token)` | -- |
| Delete-and-return | `delextract(str,first,len,sep)` | -- |

### 2c. Searching / Finding

| Operation | Word / List | Grapheme | Regex |
| :--- | :--- | :--- | :--- |
| Find first (exact) | `member(list,word,sep)` | `pos(pattern,str)` | `regmatch()` / `regmatchi()` |
| Find first (wild) | `match(list,pattern,sep)` | `strmatch(str,pattern)` | -- |
| Find all positions | `matchall(list,pattern,sep)` | `lpos(str,sub)` | -- |
| Grab matching item | `grab(list,pattern,sep)` | -- | `regrab()` / `regrabi()` |
| Grab all matching | `graball(list,pattern,sep)` | -- | `regraball()` / `regraballi()` |
| Filter by function | `filter(obj/attr,list,...)` | -- | -- |
| Filter by boolean | `filterbool(obj/attr,list,...)` | -- | -- |
| Grep attributes | `grep(obj,pattern,attr)` / `grepi()` | -- | `regrep()` / `regrepi()` |

### 2d. Modification (Insert / Delete / Replace)

| Operation | Word / List | Grapheme | Regex |
| :--- | :--- | :--- | :--- |
| Delete by position | `ldelete(list,pos,isep,osep)` | `delete(str,pos,len)`, `strdelete(str,pos,len)` | -- |
| Insert at position | `linsert(list,pos,word,sep)`, `insert(list,pos,word,isep,osep)` | `strinsert(str,pos,text)` | -- |
| Replace by position | `lreplace(list,pos,word,isep,osep)`, `replace(list,pos,word,isep,osep)` | `strreplace(str,start,len,new)` | -- |
| Conditional replace | `splice(list,old,new,isep,osep)` | -- | -- |
| List-aware edit | `ledit(str,from,to,isep,osep)` | -- | -- |
| Remove by value | `remove(list,word,isep,osep)` | -- | -- |
| Regex replace | -- | -- | `regedit()` / `regediti()` / `regeditall()` / `regeditalli()` |

The following are **string-level** operations. They work on raw strings
or character sets, not on grapheme clusters or list words. They are
listed separately to keep the unit model clean.

| Operation | Function | Notes |
| :--- | :--- | :--- |
| Substring replace | `edit(str,from,to,...)` | Whole-string find-and-replace, not grapheme-indexed |
| Strip characters | `strip(str,chars)` | Removes characters from a set; not grapheme-cluster-aware |
| Transliterate | `tr(str,from,to)` | Character-set translation; maps single characters, not clusters |
| Case translate | `translate(str,mode)` | Case/mode transform on the whole string |

### 2e. Reordering

| Operation | Word / List | Grapheme |
| :--- | :--- | :--- |
| Reverse | `revwords(str,sep,osep)` | `reverse(str)` |
| Shuffle | `shuffle(list,sep,osep)` | `scramble(str)` |
| Sort | `sort(list,type,sep,osep)` | -- |
| Sort by function | `sortby(obj/attr,list,sep,osep)` | -- |
| Sort by key | `sortkey(obj/attr,list,sep,osep,count)` | -- |
| Merge (sorted) | `merge(list1,list2,type)` | -- |

### 2f. Set Operations

| Operation | Word / List | Grapheme |
| :--- | :--- | :--- |
| Union | `setunion(l1,l2,isep,osep,type)` | **missing** |
| Intersection | `setinter(l1,l2,isep,osep,type)` | **missing** |
| Difference | `setdiff(l1,l2,isep,osep,type)` | **missing** |
| Unique | `unique(list,isep,osep,type)` | **missing** |

### Gaps in Sequence Operations

1. **"All but last" extraction**: `rest()` gives all-but-first for words;
   there is no `butlast()` or grapheme equivalent. (Achievable with
   `extract()` or `mid()` + `strlen()`, but not as a one-liner.)
2. **Grapheme-level sort**: `sort()` operates on words; there is no
   `strsort()` to sort the grapheme clusters within a string.
3. **Grapheme set operations**: no `strunion()`, `strdiff()`, `strinter()`,
   `strunique()`. Useful for anagram/character-set tasks.
4. **Grapheme-to-list bridge**: no function to explode a string into a list
   of grapheme clusters (`graphemes(str, osep)`). This would unlock all
   existing list functions for character-level work.
5. **Conditional grapheme replace**: `splice()` does conditional replace
   at word level; there is no grapheme-level equivalent (replace grapheme
   cluster X with Y everywhere).  `edit()` does this for substrings, but
   `splice()` compares whole words.
6. **Nth occurrence search**: `pos()` finds the first substring;
   `lpos()` finds all positions. There is no `posn(str, sub, n)` to find
   the nth occurrence directly.
7. **List-aware regex**: `regedit()` family operates on the whole string.
   There is no `lregedit()` that applies a regex to each list element.

---

## 3. Higher-Order / Iteration Functions

These bridge the scalar and list worlds by applying scalar logic across
list elements.

| Function | Input | Semantics |
| :--- | :--- | :--- |
| `iter(list, pattern, isep, osep)` | list | Evaluate pattern for each element; `##` = value, `#@` = index |
| `citer(list, pattern, osep)` | list | Column-iter: iterate with comma separation |
| `list(list, pattern, sep)` | list | Like `iter` but output is newline-separated (for side effects) |
| `map(obj/attr, list, ...)` | list | Call a softcode function for each element |
| `filter(obj/attr, list, ...)` | list | Keep elements where function returns true |
| `filterbool(obj/attr, list, ...)` | list | Keep elements where function returns non-empty |
| `fold(obj/attr, list, base, sep)` | list | Left fold: accumulate via softcode function |
| `mix(obj/attr, l1, l2, ..., sep)` | multi-list | Zip + map: call function with parallel elements |
| `step(obj/attr, list, step, isep, osep)` | list | Map with step-size windowing |
| `foreach(obj/attr, str, isep, osep)` | UTF-8 code point | Call function for each code point (not grapheme-cluster-aware) |
| `while(cond, body, init, limit, isep, osep)` | scalar | Iterate while condition is true |
| `munge(obj/attr, l1, l2, sep)` | list | Reorder l2 by sorted-order of l1 |
| `sortby(obj/attr, list, sep, osep)` | list | Sort list by comparison function |
| `sortkey(obj/attr, list, sep, osep, count)` | list | Sort list by key-extraction function |

### Gaps in Iteration

1. **`foreach()` iterates UTF-8 code points, not grapheme clusters.**
   It walks by lead-byte length, so a multi-code-point cluster (e.g.,
   family emoji) is split into separate calls. All other iterators work
   at the word/list level. A `strmap()` or `graphemes()`-based approach
   would be more compositional and correctly grapheme-aware.
2. **No `reduce()` alias**: `fold()` is the standard left-fold, but
   some MU* platforms also offer `reduce()`. Low priority since `fold()`
   already exists.
3. **No `zip()` without function application**: `mix()` zips and applies;
   there is no pure zip that interleaves two lists into a single list.
4. **No `enumerate()`**: returns `index:value` pairs. Achievable with
   `iter()` + `#@` but not directly available.

---

## 4. Bridging Word Units and Grapheme Units

This is the sparsest and most strategically useful part of the matrix.
TinyMUX has strong support inside each dimension but fewer tools for
moving between them.

| Bridge | Current Support | Status |
| :--- | :--- | :--- |
| Word index -> word text | `extract()`, `first()`, `rest()`, `last()` | covered |
| Grapheme offset -> grapheme slice | `mid()`, `strdelete()`, `strinsert()`, `strreplace()` | covered |
| Byte offset -> containing word | `wordpos(str, charpos, sep)` | present (byte-offset, not grapheme) |
| Word index -> grapheme start/end | -- | **missing** |
| String -> grapheme list | -- | **missing** |
| Grapheme list -> string | -- | **missing** (no list-consuming joiner; `strcat()` is variadic-args only) |

Note: `wordpos()` indexes into the color-stripped UTF-8 buffer by byte
position (`cp[charpos - 1]`), not by grapheme cluster. Despite the
documentation saying "character position", the implementation is
byte-oriented. For ASCII strings the distinction is invisible, but for
multi-byte UTF-8 the results will be wrong if the caller passes a
grapheme count instead of a byte offset.

### Best Additions

1. **`wordstart(str, word, sep)` / `wordend(str, word, sep)`** -- return
   the grapheme offset where a given word begins/ends.
2. **`graphemes(str, osep)`** -- explode string into a list of grapheme
   clusters. This immediately unlocks `sort()`, `setunion()`, `filter()`,
   etc. for character-level work.
3. **`posn(str, sub, n)`** -- find the nth occurrence of a substring.
4. **`wordspan(str, word, sep)`** -- return `start end` as a structured
   pair (lower priority; `wordstart`/`wordend` are simpler).

---

## 5. Case Conversion and Text Formatting

| Operation | Scalar / String | List |
| :--- | :--- | :--- |
| Lower case | `lcstr()` | -- |
| Upper case | `ucstr()` | -- |
| Capitalize first | `capstr()` | `caplist(list, sep, osep)` |
| Case all words | `caseall()` | -- |
| Accent | `accent(str, pattern)` | -- |
| Trim whitespace | `trim(str, side, chars)` | -- |
| Squish whitespace | `squish(str, sep)` | -- |
| Pad left | `lpad(str, width, fill)`, `ljust(str, width, fill)` | -- |
| Pad right | `rpad(str, width, fill)`, `rjust(str, width, fill)` | -- |
| Center | `center(str, width, fill)`, `cpad(str, width, fill)` | -- |
| Repeat | `repeat(str, count)` | -- |
| Space | `space(count)` | -- |
| Columns | `columns(list, width, sep, osep)` | -- |
| Table | `table(list, width, ...)` | -- |
| Wrap | `wrap(str, width, ...)` | -- |
| Wrap columns | `wrapcolumns(str, cols, width, ...)` | -- |
| Printf | `printf(fmt, args...)` | -- |
| Itemize | `itemize(list, sep, conj, punct)` | -- |
| ANSI color | `ansi(codes, str, ...)` | -- |
| Strip ANSI | `stripansi(str)` | -- |
| Strip accents | `stripaccents(str)` | -- |
| Garble | `garble(str, type)` | -- |
| Color depth | `colordepth(dbref)` | -- |

### Gaps in Formatting

1. **List-level case conversion**: `lcstr()` and `ucstr()` operate on
   whole strings. There is no `lclist()` or `uclist()` that lowercases
   each list element independently. (Low priority -- `iter()` + `lcstr()`
   covers it.)
2. **`title()`**: capitalize each word -- `caseall()` may cover this
   depending on semantics, but it is worth verifying.

---

## 6. Encoding, Hashing, and Type Conversion

| Operation | Functions |
| :--- | :--- |
| Base64 | `encode64()`, `decode64()` |
| URL encoding | `url_escape()`, `url_unescape()` |
| Crypt | `encrypt()`, `decrypt()` |
| Hash | `sha1()`, `digest()`, `hmac()`, `crc32()`, `crc32obj()` |
| Char <-> Code | `chr()`, `ord()` |
| Pack/Unpack | `pack()`, `unpack()` |
| Escape | `escape()`, `secure()` |
| JSON | `json()`, `json_mod()`, `json_query()`, `isjson()` |
| CTU (unit conv) | `ctu()` |
| Type testing | `isint()`, `isnum()`, `israt()`, `isword()`, `isdbref()`, `isobjid()`, `isalpha()`, `isalnum()`, `isdigit()` |
| Subnet match | `subnetmatch()` |

### Gaps

- No `isjsonpath()` or JSON-to-list bridge (e.g., `json_keys()`,
  `json_values()`). `json_query()` partially covers this.
- Type-testing functions are scalar-only. No `lisint()` or
  `lisnum()` for testing every element in a list. (Low priority --
  `filter()` + `isnum()` works.)

---

## 7. Control Flow and Evaluation

| Pattern | Scalar | Short-Circuit | List-Oriented |
| :--- | :--- | :--- | :--- |
| If/Else | `if()`, `ifelse()` | -- | -- |
| Switch | `switch()`, `switchall()` | -- | -- |
| Case | `case()`, `caseall()` | -- | -- |
| Choose | `choose()` | -- | -- |
| Default | `default()`, `edefault()` | -- | -- |
| First non-empty | `firstof()`, `strfirstof()` | yes (FN_NOEVAL) | -- |
| All non-empty | `allof()`, `strallof()` | yes (FN_NOEVAL) | -- |
| Call user fn | `u()`, `ulocal()`, `udefault()`, `ulambda()` | -- | `map()`, `filter()`, `fold()` |
| Eval string | `eval()`, `s()`, `subeval()`, `asteval()` | -- | -- |
| Obj-context eval | `objeval()` | -- | -- |
| Localize | `localize()`, `letq()` | -- | -- |
| Sandbox | `sandbox()` | -- | -- |
| Trace | `trace()` | -- | -- |
| Null | `null()`, `@@()` | -- | -- |
| Literal | `lit()` | -- | -- |
| Error | `error()` | -- | -- |
| Registers | `setq()`, `setr()`, `unsetq()`, `listq()`, `r()` | -- | -- |
| Counters | `inc()`, `dec()` | -- | -- |
| Iter state | `itext()`, `inum()`, `ilev()` | -- | -- |
| Function depth | `fdepth()`, `fcount()` | -- | -- |
| Benchmark | `benchmark()`, `astbench()`, `rvbench()` | -- | -- |

---

## 8. Complete Gap Summary and Recommendations

### Tier 1 -- High Value, Low Risk

These fill the most commonly felt gaps and have clean semantics.

| Proposed Function | What It Does | Why |
| :--- | :--- | :--- |
| `graphemes(str, osep)` | Explode string into grapheme-cluster list | Unlocks all list functions for char-level work |
| `wordstart(str, word, sep)` | Grapheme offset of word N start | Missing direction: word index -> grapheme position |
| `wordend(str, word, sep)` | Grapheme offset of word N end | Same; also note `wordpos()` currently uses byte offsets |
| `lxor(list, sep)` | Boolean parity reduction over a list | Completes the `land()`/`lor()` family |

### Tier 2 -- Solid Value, Straightforward

| Proposed Function | What It Does | Why |
| :--- | :--- | :--- |
| `lband(list, sep)` | Bitwise AND across list | Completes horizontal bitwise family |
| `lbor(list, sep)` | Bitwise OR across list | Same |
| `lbxor(list, sep)` | Bitwise XOR across list | Same |
| `limath(op, list, sep)` | Integer-only list reduction | Parallel to `lmath()` for 64-bit integers |
| `posn(str, sub, n)` | Find nth occurrence of substring | Common need; `pos()` only finds first |
| `strsort(str)` | Sort grapheme clusters within a string | Anagram, dedup, character-set work |
| `strunique(str)` | Deduplicate grapheme clusters | Character-set extraction |

### Tier 3 -- Nice to Have

| Proposed Function | What It Does | Why |
| :--- | :--- | :--- |
| `strunion(s1, s2)` | Union of grapheme cluster sets | Set algebra at character level |
| `strdiff(s1, s2)` | Difference of grapheme cluster sets | Same |
| `strinter(s1, s2)` | Intersection of grapheme cluster sets | Same |
| `butlast(str, sep)` | All but last word | Symmetric complement to `rest()` |
| `zip(l1, l2, sep)` | Interleave two lists (no function call) | Pure structural operation; `mix()` requires a function |

### What NOT to Add

- **Byte-level API family** (`strmid`, `bpos`, `blpos`): cuts against
  TinyMUX's current direction. Bytes are an implementation detail.
- **Code-point API** (`cpcount`, `cpmid`): introduces a third indexing
  model between bytes and grapheme clusters. Confusing for users.
- **List-level type testers** (`lisint`, `lisnum`): `filter()`+`isnum()`
  already covers this with no new surface area.
- **`lregedit()`**: regex-per-list-element can be composed with `iter()`+
  `regedit()`. Not common enough to justify a dedicated function.

---

## Appendix A: Complete Function-to-Dimension Map

Legend: **S** = scalar/args, **W** = word/list, **G** = grapheme,
**V** = vector, **R** = regex, **O** = object/db, **T** = time,
**C** = channel/comms, **M** = mail, **J** = JSON, **X** = system/admin

Functions omitted from the matrix body above (object, time, channel, mail,
system) are categorized here for completeness.

### Object / Database (O)

`attrcnt`, `attrib_set`, `children`, `clone`, `con`, `controls`,
`create`, `destroy`, `elock`, `entrances`, `exit`, `findable`, `flags`,
`fullname`, `get`, `get_eval`, `grep`, `grepi`, `hasattr`, `hasattrp`,
`hasflag`, `haspower`, `hasquota`, `hastype`, `home`, `inzone`,
`lastcreate`, `lattr`, `lattrcmds`, `lattrp`, `lcon`, `lexits`,
`lflags`, `link`, `lparent`, `loc`, `locate`, `lock`, `lockdecode`,
`lockencode`, `lplayers`, `lrooms`, `lthings`, `money`, `moniker`,
`name`, `ncon`, `nearby`, `nexits`, `next`, `nplayers`, `nthings`, `num`,
`obj`, `objid`, `objmem`, `owner`, `parent`, `pfind`, `pmatch`, `room`,
`rloc`, `route`, `search`, `set`, `tel`, `trigger`, `type`, `valid`,
`visible`, `where`, `wipe`, `xget`, `zone`, `zchildren`, `zexits`,
`zfun`, `zrooms`, `zthings`, `zwho`

### Time (T)

`convsecs`, `convtime`, `ctime`, `digittime`, `etimefmt`, `exptime`,
`moon`, `mtime`, `secs`, `singletime`, `startsecs`, `starttime`,
`restartsecs`, `restarttime`, `restarts`, `time`, `timefmt`, `writetime`

### Channel / Communication (C)

`cbuffer`, `cdesc`, `cemit`, `cflags`, `chanfind`, `chaninfo`,
`channels`, `chanobj`, `chanuser`, `chanusers`, `cmogrifier`, `cmsgs`,
`cowner`, `crecall`, `cstatus`, `cusers`, `cwho`, `comalias`,
`comtitle`, `emit`, `nsemit`, `nsoemit`, `nspemit`, `nsremit`, `oemit`,
`pemit`, `prompt`, `remit`

### Mail (M)

`mail`, `mailcount`, `mailflags`, `mailfrom`, `mailinfo`, `maillist`,
`mailreview`, `mailsend`, `mailsize`, `mailstats`, `mailsubj`, `malias`

### System / Admin (X)

`addrlog`, `beep`, `bittype`, `cmds`, `config`, `conn`, `connlast`,
`connleft`, `connlog`, `connmax`, `connnum`, `connrecord`, `conntotal`,
`doing`, `dumping`, `dynhelp`, `gmcp`, `height`, `host`, `idle`,
`jitstats`, `lcmds`, `lports`, `lwho`, `mudname`, `motd`, `objmem`,
`playmem`, `pocvm2`, `poll`, `ports`, `rvbench`, `siteinfo`, `stats`,
`terminfo`, `textfile`, `version`, `width`

### Pronouns / English (used with O)

`aposs`, `art`, `obj`, `poss`, `pose`, `subj`, `verb`

### Lua

`lua()`

### Internal (GOD-only, JIT support)

`_check_u_perm`, `_default_get`, `_edefault_get`, `_restore_cargs`,
`_restore_qregs`, `_save_cargs`, `_save_qregs`, `_set_ncargs`,
`_write_carg`

---

## Appendix B: Cross-Platform Comparison Notes

Functions present in PennMUSH or RhostMUSH but absent from TinyMUX that
would fit naturally into the matrix:

| Function | Platform | Dimension | Notes |
| :--- | :--- | :--- | :--- |
| `nattr()` | Penn | O | Count of attributes (TinyMUX has `attrcnt()`) |
| `lsearch()` | Penn | O | List-returning search (TinyMUX `search()` already returns a list) |
| `reswitch()` | Penn | S | Regex switch (achievable with `switch()`+`regmatch()`) |
| `speak()` | Penn | S | Say/pose formatter |
| `textentries()` | Penn | S | Count entries in a text file |
| `ordinal()` | Penn | S | 1st, 2nd, 3rd... |
| `nameq()` / `isname()` | Rhost | O | Named q-register operations |
| `mix()` multi-list | Penn | W | TinyMUX already has this |

Most cross-platform gaps are either already covered by TinyMUX equivalents
or are domain-specific enough to not warrant adoption. The structural gaps
identified in Section 8 (grapheme bridge, integer list reducers, bitwise
list reducers) are TinyMUX-specific and not addressed by copying functions
from other platforms.
