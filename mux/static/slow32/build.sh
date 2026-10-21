#!/bin/bash
# Build muxscript.s32x and netmux.s32x: TinyMUX's static build
# (../Makefile) for SLOW-32, the CPU in ~/slow-32 (SLOW32_ROOT).
#
# The sources are copied to $OUT/tree first, with autoconf.h (beside this
# script) in place of configure's: headers in include/ find a quoted
# "autoconf.h" beside themselves, so the native build's would leak in.
# Then one object per source, a partial link per module as ../Makefile
# makes them (s32-ld -r --localize-hidden --redefine-sym), and the two
# final links.  Needs clang/llc with the SLOW32 backend, libc++ built for
# it (scripts/build-libcxx.sh) and PCRE2 (slow-32/pcre2/build.sh).
#
#   OUT=/some/dir mux/static/slow32/build.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)                 # mux/
OUT=${OUT:-$HERE/out}
B=$OUT
mkdir -p "$OUT"
M=$OUT/tree/mux
rsync -a --delete --exclude '*.o' --exclude '*.so' --exclude '*.a' --exclude '*.eo' --exclude '*.lo' \
    --exclude 'static/slow32/out' "$SRC/" "$M/"
cp "$HERE/autoconf.h" "$M/include/autoconf.h"
O=$B/obj
R=${SLOW32_ROOT:-$HOME/slow-32}
LLVM=${LLVM_BIN:-$HOME/llvm-project/build/bin}
CXXR=${LIBCXX:-$HOME/llvm-project/build-slow32-libcxx/install}
AS=$R/tools/assembler/slow32asm
LD=$R/tools/linker/s32-ld
P2=$R/pcre2/out
INC="-I$M/include -I$M/modules -I$M/announce -I$M/ganl/include -I$M/sqlite -I$M/lua54 -I$M/src -I$P2"
RTI="-isystem $R/runtime/include"
COMMON="-target slow32-unknown-none -O2"
CXXF="$COMMON -std=c++17 -nostdinc++ -isystem $CXXR/include/c++/v1 $RTI -fexceptions -frtti -funwind-tables"
CF="$COMMON $RTI"
FEAT="-DSSL_ENABLED -DMUX_NO_THREADS -DMUX_STATIC_MODULES -DPCRE2_STATIC -DPCRE2_CODE_UNIT_WIDTH=8"
# SQLite as slow-32/sqlite/build.sh builds it, but with WAL: TinyMUX opens
# its database in WAL mode, and the SLOW-32 VFS has no shared memory, which
# WAL can do without only in exclusive locking mode -- so that is the
# default here.  One process owns the database, which is true of a game
# under the emulator (the native build keeps normal locking, so that a
# backup can copy the database while the game runs).
SQLOPTS="-DSQLITE_OS_OTHER=1 -DSQLITE_THREADSAFE=0 -DSQLITE_DEFAULT_LOCKING_MODE=1 -DSQLITE_OMIT_LOAD_EXTENSION=1
 -DSQLITE_OMIT_SHARED_CACHE=1 -DSQLITE_OMIT_DEPRECATED=1 -DSQLITE_OMIT_UTF16=1
 -DSQLITE_OMIT_COMPILEOPTION_DIAGS=1 -DSQLITE_DEFAULT_MEMSTATUS=0
 -DSQLITE_DQS=0 -DSQLITE_LIKE_DOESNT_MATCH_BLOBS -DSQLITE_CORE -DSQLITE_BYTEORDER=0 -DSQLITE_MAX_ATTACHED=125"

mkdir -p $O
# An object is rebuilt when its source is newer; no header dependencies are
# tracked, so a header newer than the last build rebuilds everything.
if [ -f $O/.stamp ] && [ -n "$(find $M $R/runtime/include -name '*.h' -newer $O/.stamp -print -quit)" ]; then
    find $O -name '*.s32o' -delete
fi
# job: <obj-name> <source> <kind>; kinds pick flags
compile() {
    local name=$1 src=$2 kind=$3 cc flags
    case $kind in
        lib)     cc=clang++; flags="$CXXF -DHAVE_CONFIG_H $INC $FEAT" ;;
        libmux)  cc=clang++; flags="$CXXF $INC $FEAT -fvisibility=hidden" ;;
        libc)    cc=clang;   flags="$CF -std=c11 -DHAVE_CONFIG_H $INC $FEAT" ;;
        engine)  cc=clang++; flags="$CXXF -DHAVE_CONFIG_H $INC $FEAT -fvisibility=hidden" ;;
        module)  cc=clang++; flags="$CXXF $INC $FEAT -fvisibility=hidden" ;;
        main)    cc=clang++; flags="$CXXF -DHAVE_CONFIG_H $INC $FEAT" ;;
        ganl)    cc=clang++; flags="$CXXF -DHAVE_CONFIG_H -I$M/include -I$M/ganl/include -I$M/ganl $FEAT" ;;
        sqlite)  cc=clang;   flags="$CF $SQLOPTS -fvisibility=hidden" ;;
        vfs)     cc=clang;   flags="$CF $SQLOPTS -I$M/sqlite -fvisibility=hidden" ;;
        lua)     cc=clang;   flags="$CF -std=gnu99 -fvisibility=hidden" ;;
    esac
    [ $O/$name.s32o -nt $src ] && return 0
    if ! $LLVM/$cc $flags -S -emit-llvm $src -o $O/$name.ll 2>$O/$name.err ||
       ! $LLVM/llc -mtriple=slow32-unknown-none $O/$name.ll -o $O/$name.s 2>>$O/$name.err ||
       ! $AS $O/$name.s $O/$name.s32o >/dev/null 2>>$O/$name.err; then
        echo "FAIL $name"; head -5 $O/$name.err; return 1
    fi
    rm -f $O/$name.ll
}
export -f compile; export O M CXXF CF COMMON INC FEAT SQLOPTS LLVM AS

# The source lists come from the Makefile.am files, so a file the native
# build gains is built here too rather than silently left out.
# am_list <Makefile.am> <VARIABLE>: the words of a (possibly continued)
# assignment, one per line.  A one-line assignment ends on its own line.
am_list() {
    awk -v v="$2" '
        !f && $0 ~ "^" v " = " { f = 1; sub("^" v " = ", "") }
        f { c = ($0 ~ /\\$/); sub(/\\$/, ""); print; if (!c) exit }
    ' "$1" | tr -s ' \t' '\n' | grep -v '^$'
}
for v in "$M/lib/Makefile.am LIBMUX_SRC" "$M/lib/Makefile.am LIBMUX_C_SRC" \
         "$M/src/Makefile.am DRIVER_SRC" "$M/ganl/Makefile.am libganl_a_SOURCES" \
         "$M/modules/engine/Makefile.am ENGINE_CXX_SRC"; do
    [ -n "$(am_list $v)" ] || { echo "build.sh: no source list for $v"; exit 1; }
done

{
for s in $(am_list $M/lib/Makefile.am LIBMUX_SRC); do
    f=${s%.cpp}
    # libmux.cpp is the one built with hidden visibility (kind libmux)
    if [ "$f" = libmux ]; then echo "lib_libmux $M/lib/libmux.cpp libmux"; continue; fi
    echo "lib_$f $M/lib/$s lib"; done
for s in $(am_list $M/lib/Makefile.am LIBMUX_C_SRC); do echo "lib_${s%.c} $M/lib/$s libc"; done
for s in $(am_list $M/modules/engine/Makefile.am ENGINE_CXX_SRC); do
    # the JIT's files are part of every engine (jit_compiler.cpp also holds
    # softcode functions the evaluator calls); configure's default backend
    [ "$s" = "@DBT_BACKEND@.cpp" ] && s=dbt_x64_sysv.cpp
    echo "eng_${s%.cpp} $M/modules/engine/$s engine"; done
echo "eng_sqlitedb $M/modules/engine/sqlitedb.cpp engine"
echo "eng_sqlite_backend $M/modules/engine/sqlite_backend.cpp engine"
echo "sqlite3 $M/sqlite/sqlite3.c sqlite"
echo "slow32_vfs $R/sqlite/slow32_vfs.c vfs"
for f in $M/lua54/*.c; do b=$(basename $f .c); case $b in lua|luac) continue ;; esac; echo "lua_$b $f lua"; done
echo "mod_comsys $M/modules/comsys/comsys_mod.cpp module"
echo "mod_mail $M/modules/mail/mail_mod.cpp module"
echo "mod_exp3 $M/modules/exp3/exp3.cpp module"
echo "mux_main $M/script/mux_main.cpp main"
echo "static_modules $M/src/static_modules.cpp main"
# netmux: the driver, and ganl without its TLS transport (no libssl here)
# (its slave spawner compiles; fork says ENOSYS when it is asked)
for s in $(am_list $M/src/Makefile.am DRIVER_SRC) version.cpp; do echo "drv_${s%.cpp} $M/src/$s main"; done
# ganl's unconditional sources (epoll and kqueue are added only where
# configure finds them), less the TLS transport: there is no libssl here.
for s in $(am_list $M/ganl/Makefile.am libganl_a_SOURCES); do
    f=$(basename ${s%.cpp})
    [ "$f" = openssl_transport ] && continue
    echo "ganl_$f $M/ganl/$s ganl"; done
} > $O/jobs.txt
xargs -P 8 -L 1 bash -c 'compile "$@"' _ < $O/jobs.txt | tee $O/compile.log
grep -q '^FAIL' $O/compile.log && exit 1
touch $O/.stamp

ENTRIES="mux_GetClassObject mux_CanUnloadNow mux_Register mux_Unregister"
partial() {   # partial <module> <objects...>
    local m=$1; shift
    local ren=""; for e in $ENTRIES; do ren="$ren --redefine-sym $e=${m}_$e"; done
    $LD -r --localize-hidden $ren -o $O/part_$m.s32o "$@"
}
partial engine $O/eng_*.s32o $O/sqlite3.s32o $O/slow32_vfs.s32o $O/lua_*.s32o || exit 1
partial comsys_mod $O/mod_comsys.s32o || exit 1
partial mail_mod $O/mod_mail.s32o || exit 1
partial exp3 $O/mod_exp3.s32o || exit 1

$LD --print-map -o $B/muxscript.s32x --mmio 64K --code-size 16M --heap-size 64M --stack-size 8M ${LDOPTS:-} $R/runtime/crt0.s32o $O/mux_main.s32o $O/static_modules.s32o \
    $O/part_engine.s32o $O/part_comsys_mod.s32o $O/part_mail_mod.s32o $O/part_exp3.s32o $O/lib_*.s32o \
    $P2/libpcre2-8.s32a $CXXR/lib/libc++.a $CXXR/lib/libc++abi.a \
    $R/runtime/libc_mmio.s32a $R/runtime/libs32.s32a

$LD -o $B/netmux.s32x --mmio 64K --code-size 16M --heap-size 64M --stack-size 8M ${LDOPTS:-} $R/runtime/crt0.s32o $O/drv_*.s32o $O/static_modules.s32o \
    $O/part_engine.s32o $O/part_comsys_mod.s32o $O/part_mail_mod.s32o $O/part_exp3.s32o $O/lib_*.s32o $O/ganl_*.s32o \
    $P2/libpcre2-8.s32a $CXXR/lib/libc++.a $CXXR/lib/libc++abi.a \
    $R/runtime/libc_mmio.s32a $R/runtime/libs32.s32a
