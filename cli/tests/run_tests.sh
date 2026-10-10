#!/usr/bin/env bash
# Runs the cuffsh test suites against a built cli/cuffsh (Linux/macOS).
#   ./cli/tests/run_tests.sh [path/to/cuffsh] [--fuzz N]
# Needs: python3, and `pip install pyte wcwidth` (a terminal emulator used to
# check what actually ends up on screen). The engine's own tests are separate:
# run tests/run.sh from the project root.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
BIN="${1:-../cuffsh}"
FUZZ=60
[ "${2:-}" = "--fuzz" ] && FUZZ="${3:-60}"
[ -x "$BIN" ] || { echo "build it first: ./cli/build.sh" >&2; exit 1; }
python3 -c "import pyte, wcwidth" 2>/dev/null || { echo "missing dependencies. Install python packages: pip install pyte wcwidth" >&2; exit 1; }
rc=0

# Shows every failing check (the suites print them as "FAIL <name>   <-- <detail>") followed by the
# suite's summary line. A suite that dies without reporting a failing check shows its last lines instead.
run_suite() {
    local out status failures
    out=$(python3 "$@" 2>&1)
    status=$?
    failures=$(printf '%s\n' "$out" | grep -E '^(FAIL|--- trial)' || true)
    [ -n "$failures" ] && printf '%s\n' "$failures"
    if [ "$status" -ne 0 ] && [ -z "$failures" ]; then
        printf '%s\n' "$out" | tail -15
    else
        printf '%s\n' "$out" | tail -1
    fi
    return "$status"
}

run_suite test_editor.py  "$BIN" || rc=1
run_suite test_session.py "$BIN" || rc=1
run_suite fuzz.py "$BIN" "$FUZZ" 1 || rc=1
exit $rc
