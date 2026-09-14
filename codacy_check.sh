#!/usr/bin/env bash
# Run Codacy analysis locally (same as the CI codacy-check job) and fail on issues.
set -euo pipefail

cd "$(dirname "$0")"

OUT="codacy-results.sarif"

get_pull_request_changed_files_json() {
    local event_path="$1"
    local api_url="${GITHUB_API_URL:-https://api.github.com}"
    local page=1
    local page_files=""
    local pr_number=""
    local repo="${GITHUB_REPOSITORY:-}"
    local response=""
    local results='[]'

    if [ -z "${GITHUB_TOKEN:-}" ]; then
        echo "Missing GITHUB_TOKEN for Codacy pull request scope" >&2
        return 1
    fi

    pr_number=$(jq -r '.number // .pull_request.number // empty' "$event_path")
    if [ -z "$pr_number" ] || [ -z "$repo" ]; then
        echo "Unable to determine pull request metadata for Codacy scope" >&2
        return 1
    fi

    while :; do
        if ! response=$(curl --fail --silent --show-error \
            -H "Accept: application/vnd.github+json" \
            -u "x-access-token:${GITHUB_TOKEN}" \
            "$api_url/repos/$repo/pulls/$pr_number/files?per_page=100&page=$page"); then
            echo "Unable to query pull request files for Codacy scope: $repo#$pr_number" >&2
            return 1
        fi

        page_files=$(jq '[.[].filename]' <<<"$response")
        results=$(jq -cs '.[0] + .[1]' <(printf '%s\n' "$results") <(printf '%s\n' "$page_files"))

        if [ "$(jq 'length' <<<"$page_files")" -lt 100 ]; then
            break
        fi

        page=$((page + 1))
    done

    printf '%s\n' "$results"
}

get_changed_files_json() {
    local event_path="${GITHUB_EVENT_PATH:-}"
    local event_name="${GITHUB_EVENT_NAME:-}"
    local base_sha=""
    local head_sha=""
    local diff_range=""
    local changed_files=""

    case "$event_name" in
    pull_request)
        if [ ! -f "$event_path" ]; then
            echo "Missing GitHub event payload for Codacy pull request scope" >&2
            return 1
        fi

        get_pull_request_changed_files_json "$event_path"
        return
        ;;
    push)
        if [ ! -f "$event_path" ]; then
            echo "Missing GitHub event payload for Codacy push scope" >&2
            return 1
        fi

        base_sha=$(jq -r '.before // empty' "$event_path")
        head_sha=$(jq -r '.after // empty' "$event_path")
        if [ "$base_sha" != "0000000000000000000000000000000000000000" ]; then
            diff_range="$base_sha..$head_sha"
        fi
        ;;
    esac

    if [ "$event_name" = "push" ]; then
        if [ -z "$base_sha" ] || [ -z "$head_sha" ]; then
            echo "Unable to determine changed files for Codacy scope from GitHub event metadata" >&2
            return 1
        fi

        if [ "$base_sha" = "0000000000000000000000000000000000000000" ]; then
            if ! changed_files=$(git ls-tree -r --name-only "$head_sha"); then
                echo "Unable to determine changed files for Codacy scope from pushed tree: $head_sha" >&2
                return 1
            fi
        else
            if [ -z "$diff_range" ]; then
                echo "Unable to determine changed files for Codacy scope from GitHub event metadata" >&2
                return 1
            fi

            if ! changed_files=$(git diff --name-only "$diff_range"); then
                echo "Unable to determine changed files for Codacy scope using range: $diff_range" >&2
                return 1
            fi
        fi

        jq -Rsc 'split("\n") | map(select(length > 0))' <<<"$changed_files"
        return
    fi

    printf 'null\n'
}

./.codacy/cli.sh install
./.codacy/cli.sh analyze --format sarif --output "$OUT"

if ! command -v jq >/dev/null 2>&1; then
    echo "jq is required to evaluate results (sudo apt-get install jq)" >&2
    exit 2
fi

CHANGED_FILES_JSON=$(get_changed_files_json)
RESULTS_FILTER='
  .runs[] as $r
  | $r.results[]?
  | ((.locations[0].physicalLocation.artifactLocation.uri // "") | ltrimstr("./")) as $uri
  | select(
      ($uri | startswith("third-party/") | not)
      and ($changed_files == null or ($changed_files | index($uri)))
    )
  | {
      uri: $uri,
      line: (.locations[0].physicalLocation.region.startLine // 0),
      tool: ($r.tool.driver.name // "Codacy"),
      message: (.message.text // .ruleId // "Issue")
    }
'

if [ "$CHANGED_FILES_JSON" = "null" ]; then
    SCOPE="entire repository"
else
    SCOPE="$(jq 'length' <<<"$CHANGED_FILES_JSON") changed file(s)"
fi

ISSUES=$(jq --argjson changed_files "$CHANGED_FILES_JSON" "[$RESULTS_FILTER] | length" "$OUT")
echo "Codacy found $ISSUES issue(s) in $SCOPE (third-party/ excluded). Full report: $OUT"

if [ "$ISSUES" -gt 0 ]; then
    jq -r --argjson changed_files "$CHANGED_FILES_JSON" "$RESULTS_FILTER | \"\(.uri):\(.line) [\(.tool)] \(.message)\"" "$OUT"
    exit 1
fi
