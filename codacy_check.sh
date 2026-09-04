#!/usr/bin/env bash
# Run Codacy analysis locally (same as the CI codacy-check job) and fail on issues.
set -euo pipefail

cd "$(dirname "$0")"

OUT="codacy-results.sarif"

./.codacy/cli.sh install
./.codacy/cli.sh analyze --format sarif --output "$OUT"

if ! command -v jq >/dev/null 2>&1; then
    echo "jq is required to evaluate results (sudo apt-get install jq)" >&2
    exit 2
fi

ISSUES=$(jq '[.runs[].results[]? | select((.locations[0].physicalLocation.artifactLocation.uri // "") | startswith("third-party/") | not)] | length' "$OUT")
echo "Codacy found $ISSUES issue(s) (third-party/ excluded). Full report: $OUT"

if [ "$ISSUES" -gt 0 ]; then
    jq -r '.runs[] as $r | $r.results[]? | select((.locations[0].physicalLocation.artifactLocation.uri // "") | startswith("third-party/") | not) | "\(.locations[0].physicalLocation.artifactLocation.uri // "?"):\(.locations[0].physicalLocation.region.startLine // 0) [\($r.tool.driver.name)] \(.message.text // .ruleId)"' "$OUT"
    exit 1
fi
