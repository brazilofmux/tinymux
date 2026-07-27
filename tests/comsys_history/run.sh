#!/bin/bash
#
#   run.sh — semantics of the channel_history schema and its queries (#1589).
#
#   Stage 1 replaces the HISTORY_%d attribute ring with rows.  The C++ around
#   it is thin -- bind, step, reset -- so the part that can actually be wrong
#   is the SQL: the ordering, the retention deletes, and the ts = 0 exemption
#   that stops an upgrade throwing away history it could not date.
#
#   Both the schema and the queries are EXTRACTED FROM THE SOURCE rather than
#   restated here.  A test that carries its own copy of the SQL passes
#   happily while the shipped query drifts underneath it, which is the same
#   shape of mistake as a probe whose control and subject are the same
#   object.
#
#   Needs the sqlite3 CLI; skips cleanly without it.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
SRC="$REPO_ROOT/mux/modules/engine/sqlitedb.cpp"
WORK="$SCRIPT_DIR/work"

if ! command -v sqlite3 >/dev/null 2>&1; then
    echo "SKIP: sqlite3 CLI not found."
    exit 0
fi
if [ ! -r "$SRC" ]; then
    echo "SKIP: $SRC not found."
    exit 0
fi

rm -rf "$WORK"; mkdir -p "$WORK"
DB="$WORK/t.db"

npass=0; nfail=0
ok()   { npass=$((npass+1)); echo "ok $((npass+nfail)) - $1"; }
nope() { nfail=$((nfail+1)); echo "not ok $((npass+nfail)) - $1"
         [ -n "${2:-}" ] && echo "  $2"; return 0; }

# Pull a C string literal block out of the source and concatenate it.
extract() {   # $1 = the identifier assigned, e.g. migration_v14
    python3 - "$SRC" "$1" <<'PY'
import re, sys
src, ident = sys.argv[1], sys.argv[2]
s = open(src, encoding='utf-8').read()
m = re.search(r'%s =\n(.*?);\n' % re.escape(ident), s, re.S)
if not m:
    sys.exit("could not find %s" % ident)
# Strip // comments FIRST.  Prose inside a comment can contain quotes -- the
# migration explains that 0 does not mean "keep everything" -- and a literal
# scanner run over the raw block splices that prose into the SQL.  Same
# treatment extract_stmt already applies, for the same reason.
body = re.sub(r'//[^\n]*', '', m.group(1))
print(''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', body)).replace('\\"', '"'))
PY
}

# Pull the SQL text of a Prepare(...) call by its statement member name.
extract_stmt() {   # $1 = e.g. m_stmtChanHistLoad
    python3 - "$SRC" "$1" <<'PY'
import re, sys
src, ident = sys.argv[1], sys.argv[2]
s = open(src, encoding='utf-8').read()
# Split on the call rather than regexing across the file: a non-greedy
# match still spans from the FIRST Prepare( to the named statement,
# silently concatenating every query in between.
found = None
for chunk in s.split('Prepare(m_db,')[1:]:
    m = re.match(r'\s*((?:.|\n)*?)&(m_stmt\w+)\)', chunk)
    if m and m.group(2) == ident:
        found = m.group(1)
        break
if found is None:
    sys.exit("could not find Prepare for %s" % ident)
body = re.sub(r'//[^\n]*', '', found)
print(''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', body)).replace('\\"', '"'))
PY
}

# --- Build a v13-shaped database, then migrate it with the real v14 SQL ----
sqlite3 "$DB" <<'SQL'
CREATE TABLE channels (
    name TEXT PRIMARY KEY, header TEXT NOT NULL DEFAULT '',
    type INTEGER NOT NULL DEFAULT 127, temp1 INTEGER NOT NULL DEFAULT 0,
    temp2 INTEGER NOT NULL DEFAULT 0, charge INTEGER NOT NULL DEFAULT 0,
    charge_who INTEGER NOT NULL DEFAULT -1, amount_col INTEGER NOT NULL DEFAULT 0,
    num_messages INTEGER NOT NULL DEFAULT 0, chan_obj INTEGER NOT NULL DEFAULT -1
) WITHOUT ROWID;
CREATE TABLE attributes (object INTEGER NOT NULL, attrnum INTEGER NOT NULL,
    value BLOB NOT NULL, owner INTEGER NOT NULL DEFAULT -1,
    flags INTEGER NOT NULL DEFAULT 0, mod_count INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (object, attrnum)) WITHOUT ROWID;
CREATE TABLE attrnames (attrnum INTEGER PRIMARY KEY, name TEXT NOT NULL,
    flags INTEGER NOT NULL DEFAULT 0);
CREATE TABLE metadata (key TEXT PRIMARY KEY, value INTEGER);

-- A WRAPPED ring: L=10, N=11.  Slot 1 holds message 11 (newest) while slot 2
-- still holds message 2 (oldest retained), so slot order is NOT time order.
INSERT INTO channels(name,num_messages,chan_obj) VALUES('wrapped',11,100);
INSERT INTO attrnames VALUES (200,'MAX_LOG',0);
INSERT INTO attributes(object,attrnum,value) VALUES(100,200,'10');
SQL

for s in 0 1 2 3 4 5 6 7 8 9; do
    case $s in 0) m=10 ;; 1) m=11 ;; *) m=$s ;; esac
    sqlite3 "$DB" "INSERT INTO attrnames VALUES ($((300+s)),'HISTORY_$s',0);
                   INSERT INTO attributes(object,attrnum,value)
                   VALUES(100,$((300+s)),'msg$m');"
done

# An UNWRAPPED ring: L=10, N=3, so slots 1..3 are messages 1..3 in order.
sqlite3 "$DB" "INSERT INTO channels(name,num_messages,chan_obj) VALUES('partial',3,101);
               INSERT INTO attributes(object,attrnum,value) VALUES(101,200,'10');"
for s in 1 2 3; do
    sqlite3 "$DB" "INSERT INTO attributes(object,attrnum,value)
                   VALUES(101,$((300+s)),'p$s');"
done

extract migration_v14 > "$WORK/v14.sql" || { echo "Bail out!  no v14 SQL"; exit 1; }
if ! sqlite3 "$DB" < "$WORK/v14.sql" 2>"$WORK/err"; then
    echo "Bail out!  v14 migration failed:"; sed 's/^/    /' "$WORK/err"; exit 1
fi

# --- 1. The ring is reconstructed in time order, not slot order -----------
got=$(sqlite3 "$DB" "SELECT group_concat(message,',') FROM
                     (SELECT message FROM channel_history
                       WHERE channel_name='wrapped' ORDER BY id);")
want="msg2,msg3,msg4,msg5,msg6,msg7,msg8,msg9,msg10,msg11"
[ "$got" = "$want" ] \
    && ok "wrapped ring imports in chronological order" \
    || nope "wrapped ring imports in chronological order" "got=$got"

got=$(sqlite3 "$DB" "SELECT group_concat(message,',') FROM
                     (SELECT message FROM channel_history
                       WHERE channel_name='partial' ORDER BY id);")
[ "$got" = "p1,p2,p3" ] \
    && ok "unwrapped ring imports in order" \
    || nope "unwrapped ring imports in order" "got=$got"

got=$(sqlite3 "$DB" "SELECT history_max_count FROM channels WHERE name='wrapped';")
[ "$got" = "10" ] \
    && ok "retention count is seeded from MAX_LOG" \
    || nope "retention count is seeded from MAX_LOG" "got=$got"

got=$(sqlite3 "$DB" "SELECT ts FROM channel_history WHERE channel_name='wrapped' LIMIT 1;")
[ "$got" = "0" ] \
    && ok "imported rows carry ts=0 (age unknown)" \
    || nope "imported rows carry ts=0 (age unknown)" "got=$got"

got=$(sqlite3 "$DB" "SELECT speaker FROM channel_history WHERE channel_name='wrapped' LIMIT 1;")
[ "$got" = "-1" ] \
    && ok "imported rows carry speaker=-1 (unknown)" \
    || nope "imported rows carry speaker=-1 (unknown)" "got=$got"

# NULL rather than 0: zero is a perfectly good offset (UTC), so collapsing
# the two would claim an imported row was written in UTC when nothing knows.
got=$(sqlite3 "$DB" "SELECT COUNT(*) FROM channel_history
                      WHERE channel_name='wrapped' AND tz_offset IS NULL;")
[ "$got" = "10" ] \
    && ok "imported rows carry tz_offset NULL, distinct from 0" \
    || nope "imported rows carry tz_offset NULL, distinct from 0" "got=$got"

# --- 2. The load query: newest N, oldest-first, with optional filters -----
LOAD=$(extract_stmt m_stmtChanHistLoad)

# $1=chan $2=limit $3=since $4=speaker -- ANY_SPEAKER is -2, no-time is 0.
load() { sqlite3 "$DB" "$(printf '%s' "$LOAD" \
         | sed "s/?1/'$1'/g; s/?2/$2/g; s/?3/$3/g; s/?4/$4/g")"; }

got=$(load wrapped 3 0 -2 | cut -d'|' -f5 | paste -sd, -)
[ "$got" = "msg9,msg10,msg11" ] \
    && ok "load returns the newest N, oldest-first" \
    || nope "load returns the newest N, oldest-first" "got=$got"

got=$(load wrapped -1 0 -2 | wc -l | tr -d ' ')
[ "$got" = "10" ] \
    && ok "limit -1 means all (what limit<=0 binds to)" \
    || nope "limit -1 means all" "got=$got rows"

# Filters, against rows with real ts/speaker rather than the imported ones.
sqlite3 "$DB" "INSERT INTO channel_history(channel_name,ts,tz_offset,speaker,message)
               VALUES('wrapped',5000,-18000,7,'from7early'),
                     ('wrapped',6000,0,9,'from9'),
                     ('wrapped',7000,3600,7,'from7late');"

got=$(load wrapped -1 6000 -2 | cut -d'|' -f5 | paste -sd, -)
[ "$got" = "from9,from7late" ] \
    && ok "since filter bounds by time" \
    || nope "since filter bounds by time" "got=$got"

got=$(load wrapped -1 0 7 | cut -d'|' -f5 | paste -sd, -)
[ "$got" = "from7early,from7late" ] \
    && ok "speaker filter selects one speaker" \
    || nope "speaker filter selects one speaker" "got=$got"

got=$(load wrapped -1 6000 7 | cut -d'|' -f5 | paste -sd, -)
[ "$got" = "from7late" ] \
    && ok "since and speaker filters combine" \
    || nope "since and speaker filters combine" "got=$got"

# The sentinel has to be distinguishable from a real speaker of -1, which
# every imported row genuinely has.
got=$(load wrapped -1 0 -1 | cut -d'|' -f5 | paste -sd, -)
case "$got" in
    *msg*) case "$got" in
               *from7*) nope "speaker=-1 filters rather than meaning 'any'" "got=$got" ;;
               *) ok "speaker=-1 selects imported rows, not everything" ;;
           esac ;;
    *) nope "speaker=-1 selects imported rows, not everything" "got=$got" ;;
esac

# An offset of 0 must round-trip as 0 and not be mistaken for "unknown".
got=$(sqlite3 "$DB" "SELECT tz_offset FROM channel_history WHERE message='from9';")
[ "$got" = "0" ] \
    && ok "tz_offset 0 is stored as 0, not as NULL" \
    || nope "tz_offset 0 is stored as 0, not as NULL" "got=$got"

got=$(sqlite3 "$DB" "SELECT tz_offset FROM channel_history WHERE message='from7early';")
[ "$got" = "-18000" ] \
    && ok "negative tz_offset round-trips" \
    || nope "negative tz_offset round-trips" "got=$got"

sqlite3 "$DB" "DELETE FROM channel_history WHERE ts IN (5000,6000,7000);"

# --- 3. Expiry by count ---------------------------------------------------
EXPC=$(extract_stmt m_stmtChanHistExpireCount)
sqlite3 "$DB" "$(printf '%s' "$EXPC" | sed "s/?1/'wrapped'/g; s/?2/4/")"
got=$(sqlite3 "$DB" "SELECT group_concat(message,',') FROM
                     (SELECT message FROM channel_history
                       WHERE channel_name='wrapped' ORDER BY id);")
[ "$got" = "msg8,msg9,msg10,msg11" ] \
    && ok "count expiry keeps the newest and drops the rest" \
    || nope "count expiry keeps the newest and drops the rest" "got=$got"

got=$(sqlite3 "$DB" "SELECT COUNT(*) FROM channel_history WHERE channel_name='partial';")
[ "$got" = "3" ] \
    && ok "expiring one channel leaves another alone" \
    || nope "expiring one channel leaves another alone" "got=$got"

# --- 4. Expiry by age, and the ts=0 exemption ----------------------------
sqlite3 "$DB" "INSERT INTO channel_history(channel_name,ts,message)
               VALUES('wrapped',1000,'old'),('wrapped',9000,'new');"
EXPA=$(extract_stmt m_stmtChanHistExpireAge)
# now=10000, max_age=2000 -> cutoff 8000: 'old' goes, 'new' stays.
sqlite3 "$DB" "$(printf '%s' "$EXPA" | sed "s/?1/'wrapped'/g; s/?2/8000/")"

got=$(sqlite3 "$DB" "SELECT COUNT(*) FROM channel_history
                      WHERE channel_name='wrapped' AND message='old';")
[ "$got" = "0" ] \
    && ok "age expiry drops a row older than the cutoff" \
    || nope "age expiry drops a row older than the cutoff" "got=$got"

got=$(sqlite3 "$DB" "SELECT COUNT(*) FROM channel_history
                      WHERE channel_name='wrapped' AND message='new';")
[ "$got" = "1" ] \
    && ok "age expiry keeps a row newer than the cutoff" \
    || nope "age expiry keeps a row newer than the cutoff" "got=$got"

# The one that matters most: imported rows have ts=0 and must survive, or an
# upgrade silently destroys a game's channel history.
got=$(sqlite3 "$DB" "SELECT COUNT(*) FROM channel_history
                      WHERE channel_name='wrapped' AND ts=0;")
[ "$got" = "4" ] \
    && ok "age expiry EXEMPTS imported rows (ts=0)" \
    || nope "age expiry EXEMPTS imported rows (ts=0)" \
            "got=$got, expected the 4 surviving imported rows"

# --- 5. Cascade: dropping a channel drops its history --------------------
sqlite3 "$DB" "PRAGMA foreign_keys=ON; DELETE FROM channels WHERE name='partial';"
got=$(sqlite3 "$DB" "SELECT COUNT(*) FROM channel_history WHERE channel_name='partial';")
[ "$got" = "0" ] \
    && ok "deleting a channel cascades to its history" \
    || nope "deleting a channel cascades to its history" "got=$got"

echo "=== comsys history schema: $npass passed, $nfail failed ==="
[ "$nfail" -eq 0 ] || exit 1
rm -rf "$WORK"
exit 0
