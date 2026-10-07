#!/usr/bin/env bash

set -uo pipefail
shopt -s nullglob
cd "$(dirname "$0")/.."

# CUFFC and UNIT_FLAGS let a sanitizer build be tested, e.g.
#   CUFFC=./cuffc-asan UNIT_FLAGS="-O1 -g -fsanitize=address,undefined" tests/run.sh
BIN=${CUFFC:-./cuffc}
UNIT_FLAGS=${UNIT_FLAGS:--O2}
PASS=0
FAIL=0

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

echo "== tests/unit (C++ unit tests) =="
for src in tests/unit/*.cpp; do
    bin="/tmp/cuff_unit_$(basename "${src%.cpp}")"
    if ! g++ -std=c++17 -Wall -Wextra $UNIT_FLAGS -I. "$src" -o "$bin" 2>/tmp/cuff_unit_build.log; then
        echo "FAIL (build): $src"
        cat /tmp/cuff_unit_build.log
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
for f in tests/cases/*.cuff; do
    run_success_case "$f"
done

echo "== tests/errors (error code check) =="
for f in tests/errors/*.cuff; do
    run_error_case "$f"
done

echo "== examples (output diff) =="
for f in examples/0*.cuff examples/1*.cuff; do
    run_success_case "$f"
done

echo "== examples/error_cases (error code check) =="
for f in examples/error_cases/*.cuff; do
    run_error_case "$f"
done

echo ""
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
