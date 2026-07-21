#!/bin/bash
# run-tests.sh — run the whole a2ui-dali test suite.
#
#   tools/run-tests.sh
#
# 1. a2ui-conformance-test      — A2UI v0.9 message parsing / model population (no display)
# 2. a2ui-streaming-render-test — real renderer + real DALi view tree (needs a display,
#                                 so it runs under Xvfb like tools/capture.sh)
#
# Requires: . setenv sourced (DESKTOP_PREFIX, LD_LIBRARY_PATH, dali2-*).
set -u

HERE="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$HERE/bin"
status=0

echo "### conformance test"
if [ -x "$BIN/a2ui-conformance-test" ]; then
  (cd "$BIN" && ./a2ui-conformance-test) || status=1
else
  echo "[run-tests] not built: $BIN/a2ui-conformance-test" >&2
  status=1
fi

echo
echo "### streaming render test (Xvfb)"
if [ -x "$BIN/a2ui-streaming-render-test" ]; then
  xvfb-run -a -s "-screen 0 720x1080x24" "$BIN/a2ui-streaming-render-test" "$HERE" || status=1
else
  echo "[run-tests] not built: $BIN/a2ui-streaming-render-test" >&2
  status=1
fi

echo
if [ "$status" -eq 0 ]; then echo "### ALL TESTS PASSED"; else echo "### TESTS FAILED"; fi
exit "$status"
