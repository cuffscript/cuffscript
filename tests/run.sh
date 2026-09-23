#!/usr/bin/env bash

set -uo pipefail
shopt -s nullglob
cd "$(dirname "$0")/.."

BIN=./cuffc
PASS=0
FAIL=0

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
    actual=$(timeout 10 "$BIN" "$cuff" 2>&1)
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
    local actual
    actual=$(timeout 10 "$BIN" "$cuff" 2>&1)
    local code=$?
    if [ $code -eq 0 ]; then
        echo "FAIL (expected nonzero exit): $cuff"
        FAIL=$((FAIL + 1))
        return
    fi
    if [ -f "$expected_code_file" ]; then
        local expected_code
        expected_code=$(cat "$expected_code_file")
        if ! echo "$actual" | grep -q "\[$expected_code\]"; then
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
    if ! g++ -std=c++17 -Wall -Wextra -O2 -I. "$src" -o "$bin" 2>/tmp/cuff_unit_build.log; then
        echo "FAIL (build): $src"
        cat /tmp/cuff_unit_build.log
        FAIL=$((FAIL + 1))
        continue
    fi
    if out=$(timeout 60 "$bin" 2>&1); then
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
