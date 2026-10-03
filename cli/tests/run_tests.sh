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
python3 -c "import pyte, wcwidth" 2>/dev/null || { echo "missing deps: pip install pyte wcwidth" >&2; exit 1; }
rc=0
python3 test_editor.py  "$BIN" | tail -1 || rc=1
python3 test_session.py "$BIN" | tail -1 || rc=1
python3 fuzz.py "$BIN" "$FUZZ" 1 | tail -1 || rc=1
exit $rc
