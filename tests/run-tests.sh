#!/usr/bin/env sh
set -eu

OHSH=${1:-./ohsh}
ROOT=$(pwd)
WORK=$(mktemp -d)
cleanup() {
    rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

fail() {
    echo "test failed: $1" >&2
    exit 1
}

assert_file() {
    [ -f "$1" ] || fail "expected file $1"
}

assert_dir() {
    [ -d "$1" ] || fail "expected folder $1"
}

assert_missing() {
    [ ! -e "$1" ] || fail "expected $1 to be absent"
}

cat > "$WORK/.ohshrc" <<'CONFIG'
tips = false
alias docs = goto Documents
alias files = show files
CONFIG

cat > "$WORK/basic.osh" <<'SCRIPT'
make folder Documents
docs
make file "notes one.txt"
go back
make folder Backup
copy "Documents/notes one.txt" to Backup --yes
move Backup/"notes one.txt" to Backup/moved.txt --yes
files
SCRIPT

(
    cd "$WORK"
    HOME="$WORK" "$ROOT/$OHSH" run basic.osh --yes
)

assert_dir "$WORK/Documents"
assert_file "$WORK/Backup/moved.txt"
assert_missing "$WORK/Backup/notes one.txt"

cat > "$WORK/fallback.osh" <<'SCRIPT'
printf 'one\ntwo\n' > words.txt
cat *.txt | grep two > result.txt
SCRIPT

(
    cd "$WORK"
    HOME="$WORK" "$ROOT/$OHSH" run fallback.osh
)

grep -q "two" "$WORK/result.txt" || fail "fallback shell did not preserve pipe/glob/redirection"

cat > "$WORK/deny-delete.osh" <<'SCRIPT'
delete Backup/moved.txt
SCRIPT

if (
    cd "$WORK"
    HOME="$WORK" "$ROOT/$OHSH" run deny-delete.osh
); then
    fail "script delete without --yes unexpectedly succeeded"
fi

assert_file "$WORK/Backup/moved.txt"

cat > "$WORK/delete.osh" <<'SCRIPT'
delete Backup/moved.txt --yes
SCRIPT

(
    cd "$WORK"
    HOME="$WORK" "$ROOT/$OHSH" run delete.osh
)

assert_missing "$WORK/Backup/moved.txt"

printf 'examples\nwhere am i\nexit\n' | "$ROOT/$OHSH" >/dev/null

echo "tests passed"
