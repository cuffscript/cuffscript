#!/usr/bin/env bash

set -uo pipefail
shopt -s nullglob
cd "$(dirname "$0")/.."

# CUFFC, CXX and UNIT_FLAGS let a sanitizer build or another compiler be tested, e.g.
#   CUFFC=./cuffc-asan UNIT_FLAGS="-O1 -g -fsanitize=address,undefined" tests/run.sh
BIN=${CUFFC:-./cuffc}
CXX=${CXX:-g++}
UNIT_FLAGS=${UNIT_FLAGS:--O2}
PASS=0
FAIL=0

# EXCLUDED_DLC names the libraries the binary was built without, e.g.
#   make -B EXCLUDE_DLC="network hardware" && EXCLUDED_DLC="network hardware" tests/run.sh
# A test that stops because one of them is unknown (`use DLC:<name>` is E5-004, or a flag that only it reads is not an
# option) is counted as left out, not failed; every other test must pass as in the full build. The unit tests that
# name one of them are not built.
EXCLUDED=${EXCLUDED_DLC:-}
DLC_NAMES=$(sed -n 's/^DLC_NAMES = //p' engine/dlc/libraries.txt | tr -d '\r') # the libraries that can be left out
if [ -z "$DLC_NAMES" ]; then
    echo "could not read the DLC_NAMES line of engine/dlc/libraries.txt" >&2
    exit 1
fi
LEFT_OUT=0
for d in $EXCLUDED; do
    case " $DLC_NAMES " in
    *" $d "*) UNIT_FLAGS="$UNIT_FLAGS -DCUFF_EXCLUDE_DLC_$(printf '%s' "$d" | tr '[:lower:]' '[:upper:]')" ;;
    *)
        echo "EXCLUDED_DLC cannot name $d (those that can be left out are: $DLC_NAMES)" >&2
        exit 1
        ;;
    esac
done

# The flags that only one library reads (engine/EngineOptionsCli.h).
dlc_flags() {
    case "$1" in
    network) echo "--no-network --allow-private-network" ;;
    filesystem) echo "--no-filesystem" ;;
    hardware) echo "--hardware" ;;
    esac
}

# Whether a test's output says that it stopped at a library left out of the build.
left_out() {
    local d flag
    for d in $EXCLUDED; do
        if grep -qF "unknown DLC library 'DLC:$d'" <<<"$1"; then
            return 0
        fi
        for flag in $(dlc_flags "$d"); do
            if grep -qF "unknown option '$flag'" <<<"$1"; then
                return 0
            fi
        done
    done
    return 1
}

# macOS has no `timeout` unless coreutils is installed (as `gtimeout`); without either, run unbounded.
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD=timeout
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_CMD=gtimeout
else
    TIMEOUT_CMD=
fi

with_timeout() {
    local seconds="$1"
    shift
    if [ -n "$TIMEOUT_CMD" ]; then
        "$TIMEOUT_CMD" "$seconds" "$@"
    else
        "$@"
    fi
}

check_bin() {
    if [ ! -x "$BIN" ]; then
        echo "cuffc not found -- run 'make' first" >&2
        exit 1
    fi
}

run_success_case() {
    local cuff="$1"
    local expected="${cuff%.cuff}.expected"
    local actual
    actual=$(with_timeout 10 "$BIN" "$cuff" 2>&1)
    local code=$?
    if [ $code -ne 0 ] && left_out "$actual"; then
        LEFT_OUT=$((LEFT_OUT + 1))
        return
    fi
    if [ $code -ne 0 ]; then
        echo "FAIL (exit $code): $cuff"
        echo "$actual"
        FAIL=$((FAIL + 1))
        return
    fi
    if [ ! -f "$expected" ]; then
        PASS=$((PASS + 1))
        return
    fi
    if [ "$actual" != "$(cat "$expected")" ] && left_out "$actual"; then
        LEFT_OUT=$((LEFT_OUT + 1))
        return
    fi
    if [ "$actual" != "$(cat "$expected")" ]; then
        echo "FAIL (output mismatch): $cuff"
        diff <(echo "$actual") "$expected"
        FAIL=$((FAIL + 1))
        return
    fi
    PASS=$((PASS + 1))
}

run_error_case() {
    local cuff="$1"
    local expected_code_file="${cuff%.cuff}.expected_code"
    local args_file="${cuff%.cuff}.args"
    local extra_args=()
    if [ -f "$args_file" ]; then
        # shellcheck disable=SC2207
        extra_args=($(cat "$args_file"))
    fi
    local actual
    if [ "${#extra_args[@]}" -gt 0 ]; then
        actual=$(with_timeout 10 "$BIN" "${extra_args[@]}" "$cuff" 2>&1)
    else
        actual=$(with_timeout 10 "$BIN" "$cuff" 2>&1)
    fi
    local code=$?
    if [ $code -eq 0 ]; then
        echo "FAIL (expected nonzero exit): $cuff"
        FAIL=$((FAIL + 1))
        return
    fi
    if [ -f "$expected_code_file" ]; then
        local expected_code
        expected_code=$(cat "$expected_code_file")
        # Here-string, not `echo | grep -q`: under `set -o pipefail`, grep -q exits at
        # its first match and can make the upstream echo die of SIGPIPE, which the
        # pipeline then reports as "no match" -- a rare, timing-dependent false failure
        # (measured ~1 in 1500 on ~450-byte messages) that grows with message length.
        if ! grep -q "\[$expected_code\]" <<<"$actual" && left_out "$actual"; then
            LEFT_OUT=$((LEFT_OUT + 1))
            return
        fi
        if ! grep -q "\[$expected_code\]" <<<"$actual"; then
            echo "FAIL (expected $expected_code not found): $cuff"
            echo "$actual"
            FAIL=$((FAIL + 1))
            return
        fi
    fi
    PASS=$((PASS + 1))
}

check_bin

# A binary that still has a library named in EXCLUDED_DLC would pass every test without showing anything.
for d in $EXCLUDED; do
    if ! grep -qF "unknown DLC library 'DLC:$d'" <<<"$(printf 'use DLC:%s\n' "$d" | "$BIN" 2>&1)"; then
        echo "$BIN still has DLC:$d: build it with EXCLUDE_DLC first" >&2
        exit 1
    fi
done

echo "== tests/unit (C++ unit tests) =="
UNIT_DIR=$(mktemp -d "${TMPDIR:-/tmp}/cuff_unit.XXXXXX")
trap 'rm -rf "$UNIT_DIR"' EXIT
for src in tests/unit/*.cpp; do
    skip=
    for d in $EXCLUDED; do
        if grep -qF "DLC:$d" "$src"; then
            skip=$d
        fi
    done
    if [ -n "$skip" ]; then
        echo "  $(basename "$src"): left out (DLC:$skip)"
        LEFT_OUT=$((LEFT_OUT + 1))
        continue
    fi
    bin="$UNIT_DIR/$(basename "${src%.cpp}")"
    if ! "$CXX" -std=c++17 -Wall -Wextra -pthread $UNIT_FLAGS -I. "$src" -o "$bin" 2>"$UNIT_DIR/build.log"; then
        echo "FAIL (build): $src"
        cat "$UNIT_DIR/build.log"
        FAIL=$((FAIL + 1))
        continue
    fi
    if out=$(with_timeout 60 "$bin" 2>&1); then
        echo "  $(basename "$src"): $(echo "$out" | tail -1)"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $src"
        echo "$out"
        FAIL=$((FAIL + 1))
    fi
done

echo "== tests/cases (output diff) =="
while IFS= read -r f; do
    run_success_case "$f"
done < <(find tests/cases -name '*.cuff' ! -path '*/fixtures/*' | LC_ALL=C sort)

echo "== tests/errors (error code check) =="
while IFS= read -r f; do
    run_error_case "$f"
done < <(find tests/errors -name '*.cuff' ! -path '*/fixtures/*' | LC_ALL=C sort)

echo "== examples (output diff) =="
for f in examples/0*.cuff examples/1*.cuff; do
    run_success_case "$f"
done

echo "== examples/error_cases (error code check) =="
for f in examples/error_cases/*.cuff; do
    run_error_case "$f"
done

echo "== cuffc command line =="
# The same cases as in tests/run.ps1.
# cli_case NAME EXPECTED_STATUS EXPECTED_TEXT ARGS... : stdin is empty unless CLI_STDIN is set.
cli_case() {
    local name="$1" want_code="$2" want_text="$3"
    shift 3
    local actual code
    actual=$(printf '%s' "${CLI_STDIN:-}" | with_timeout 10 "$BIN" "$@" 2>&1)
    code=$?
    if [ "$code" -ne "$want_code" ] || ! grep -qF -- "$want_text" <<<"$actual"; then
        echo "FAIL (cuffc command line): $name: exit $code, wanted $want_code and '$want_text'"
        echo "$actual"
        FAIL=$((FAIL + 1))
        return
    fi
    PASS=$((PASS + 1))
}
CLI_DIR=$(mktemp -d "${TMPDIR:-/tmp}/cuff_cli.XXXXXX")
trap 'rm -rf "$UNIT_DIR" "$CLI_DIR"' EXIT
mkdir "$CLI_DIR/한글 폴더"
printf 'print("ok")\n' >"$CLI_DIR/한글 폴더/스크립트.cuff"
printf 'print(args())\nexit(length(args()))\n' >"$CLI_DIR/args.cuff"
cli_case "a folder is not a script" 1 "Cannot open file '$CLI_DIR'" "$CLI_DIR"
cli_case "a file that is not there" 1 "Cannot open file '$CLI_DIR/none.cuff'" "$CLI_DIR/none.cuff"
cli_case "a name with Korean and a space" 0 "ok" "$CLI_DIR/한글 폴더/스크립트.cuff"
cli_case "a file name after --" 0 "ok" -- "$CLI_DIR/한글 폴더/스크립트.cuff"
CLI_STDIN='print("from stdin")' cli_case "the script from stdin (-)" 0 "from stdin" -
cli_case "an unknown option" 2 "unknown option '--nope'" --nope
cli_case "arguments after the script, and exit()" 3 '["a b", "--nope", "한글"]' "$CLI_DIR/args.cuff" "a b" --nope 한글
CLI_STDIN='print(args())' cli_case "arguments after - (stdin)" 0 '["x"]' - x

echo ""
if [ -n "$EXCLUDED" ]; then
    echo "$PASS passed, $FAIL failed, $LEFT_OUT left out (built without: $EXCLUDED)"
else
    echo "$PASS passed, $FAIL failed"
fi
[ "$FAIL" -eq 0 ]
