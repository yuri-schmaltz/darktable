#!/bin/sh
# Re-parse the --results document with an independent JSON parser.
#
# The C test asserts on substrings, which cannot catch a missing key: the
# document still contains every expected substring while being invalid JSON.
# That is not hypothetical - it is how the schema shipped broken during the
# first pass of this feature ("exit_code": 1,"job_failed", with the key
# dropped). This test renders a document and hands it to python3, which either
# parses it or does not.
#
# Requires python3. Skips (exit 77) when python3 is absent rather than failing,
# so a minimal CI image does not report a false red.

set -eu

if ! command -v python3 >/dev/null 2>&1; then
  echo "SKIP: python3 not available to independently validate the JSON"
  exit 77
fi

# ctest passes $<TARGET_FILE:test_cli_results_emit>, which lives in the build
# tree; fall back to a sibling binary for a manual run from the source tree.
BIN="${1:-$(dirname "$0")/test_cli_results_emit}"
if [ ! -x "$BIN" ]; then
  echo "SKIP: $BIN not built"
  exit 77
fi

"$BIN" > /tmp/dt_cli_results.json

python3 - <<'PYEOF'
import json, sys
d = json.load(open('/tmp/dt_cli_results.json'))
assert d['schema'] == 'darktable-cli-results/1', d['schema']
assert d['schema_version'] == 1
assert d['inputs'] == 3, d['inputs']
assert d['failed'] == 2, d['failed']
assert d['exit_code'] == 3, d['exit_code']
assert d['exit_name'] == 'library', d['exit_name']
r = d['results']
assert len(r) == 3
assert r[0]['status'] == 'ok' and r[0]['error'] is None
assert r[1]['error'] == 'no such file\twith tab\nand newline'
assert r[1]['input'] == 'in/b"quote\\back.cr3'
assert r[2]['input'] == 'ctl\x01\x02'
assert abs(d['seconds'] - 2.0) < 1e-6
print("JSON valid and schema-correct; escaping round-trips")
PYEOF
