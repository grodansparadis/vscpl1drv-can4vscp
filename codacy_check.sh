#!/usr/bin/env bash
# Run Codacy analysis locally (same as the CI codacy-check job) and fail on issues.
set -euo pipefail

cd "$(dirname "$0")"

OUT="codacy-results.sarif"
REPO_ROOT="$PWD"
REPO_NAME="$(basename "$REPO_ROOT")"

./.codacy/cli.sh install
./.codacy/cli.sh analyze --format sarif --output "$OUT"

if ! command -v jq >/dev/null 2>&1; then
    echo "jq is required to evaluate results (sudo apt-get install jq)" >&2
    exit 2
fi

RESULTS_FILTER='
  .runs[] as $r
  | $r.results[]?
  | (
      (.locations[0].physicalLocation.artifactLocation.uri // "")
      | sub("^file://"; "")
      | if startswith($repo_root) then ltrimstr($repo_root)
        elif startswith("./") then ltrimstr("./")
        elif startswith($repo_name) then ltrimstr($repo_name)
        else .
        end
    ) as $uri
  | select($uri | startswith("third-party/") | not)
  | {
      uri: $uri,
      line: (.locations[0].physicalLocation.region.startLine // 0),
      tool: ($r.tool.driver.name // "Codacy"),
      message: (.message.text // .ruleId // "Issue")
    }
'

SCOPE="entire repository"
ISSUES=$(jq --arg repo_root "$REPO_ROOT/" --arg repo_name "$REPO_NAME/" "[$RESULTS_FILTER] | length" "$OUT")
echo "Codacy found $ISSUES issue(s) in $SCOPE (third-party/ excluded). Full report: $OUT"

if [ "$ISSUES" -gt 0 ]; then
    jq -r --arg repo_root "$REPO_ROOT/" --arg repo_name "$REPO_NAME/" "$RESULTS_FILTER | \"\(.uri):\(.line) [\(.tool)] \(.message)\"" "$OUT"
    exit 1
fi
