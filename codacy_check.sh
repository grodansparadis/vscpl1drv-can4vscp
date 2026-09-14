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

RESULTS_FILTER='
  .runs[] as $r
  | $r.results[]?
  | select((.locations[0].physicalLocation.artifactLocation.uri // "") | startswith("third-party/") | not)
  | {
      uri: (.locations[0].physicalLocation.artifactLocation.uri // ""),
      line: (.locations[0].physicalLocation.region.startLine // 0),
      tool: ($r.tool.driver.name // "Codacy"),
      message: (.message.text // .ruleId // "Issue")
    }
'

ISSUES=$(jq "[$RESULTS_FILTER] | length" "$OUT")
echo "Codacy found $ISSUES issue(s) in entire repository (third-party/ excluded). Full report: $OUT"

if [ "$ISSUES" -gt 0 ]; then
    jq -r "$RESULTS_FILTER | \"\(.uri):\(.line) [\(.tool)] \(.message)\"" "$OUT"
    exit 1
fi
