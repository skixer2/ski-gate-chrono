#!/usr/bin/env bash
# push_test_results.sh — push smoke/core/full results to the OpenClaw (VPS) host over SSH.
#
# No secrets in this file: auth is via an ed25519 key (path only). Nothing here
# ever reads or accepts a password.
#
# Usage (from unit_tests/):
#   ./push_test_results.sh                        # newest run_*.md + its *.log
#   ./push_test_results.sh run_20260814_0900      # that specific run
#   ./push_test_results.sh --all                  # every run_* file in tmp_test_results
#
# Config precedence: env var > documented default (see below).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_DIR="${LOCAL_DIR:-$HERE/tmp_test_results}"

REMOTE_HOST="${SGC_RESULTS_HOST:-72.60.88.60}"   # example only
REMOTE_PORT="${SGC_RESULTS_PORT:-22}"
REMOTE_USER="${SGC_RESULTS_USER:-node}"          # "ubuntu" is an alternative
REMOTE_PATH="${SGC_RESULTS_PATH:-/home/node/.openclaw/workspace/ski_gate_chrono/High_Level_Requirements/system_design/architecture_modules/module_design/unit_tests/tmp_test_results}"
IDENTITY_FILE="${SGC_RESULTS_KEY:-$HOME/.ssh/id_ed25519_sgc}"

ALL=0
RUN_ID=""
if [[ "${1:-}" == "--all" ]]; then
  ALL=1
else
  RUN_ID="${1:-}"
fi

# ---- prerequisites ----------------------------------------------------------
command -v scp >/dev/null 2>&1 || { echo "error: scp not found" >&2; exit 1; }
command -v ssh >/dev/null 2>&1 || { echo "error: ssh not found" >&2; exit 1; }
[[ -d "$LOCAL_DIR" ]] || { echo "error: local results dir not found: $LOCAL_DIR" >&2; exit 1; }
[[ -f "$IDENTITY_FILE" ]] || {
  echo "error: SSH key not found: $IDENTITY_FILE" >&2
  echo "generate once: ssh-keygen -t ed25519 -f \"$IDENTITY_FILE\" -C sgc-results" >&2
  exit 1
}

SSH_OPTS=(-i "$IDENTITY_FILE" -p "$REMOTE_PORT" -o BatchMode=yes -o StrictHostKeyChecking=accept-new)
SCP_OPTS=(-i "$IDENTITY_FILE" -P "$REMOTE_PORT" -o BatchMode=yes -o StrictHostKeyChecking=accept-new)

# ---- select files -----------------------------------------------------------
if [[ $ALL -eq 1 ]]; then
  mapfile -t FILES < <(find "$LOCAL_DIR" -maxdepth 1 -type f -name 'run_*' | sort)
else
  if [[ -z "$RUN_ID" ]]; then
    NEWEST="$(find "$LOCAL_DIR" -maxdepth 1 -type f -name 'run_*.md' -printf '%T@ %p\n' | sort -nr | head -n1 | cut -d' ' -f2-)"
    [[ -n "$NEWEST" ]] || { echo "error: no run_*.md found in $LOCAL_DIR" >&2; exit 1; }
    RUN_ID="$(basename "$NEWEST" .md)"
  fi
  mapfile -t FILES < <(find "$LOCAL_DIR" -maxdepth 1 -type f \( -name "${RUN_ID}.md" -o -name "${RUN_ID}_*.log" \) | sort)
fi

[[ ${#FILES[@]} -gt 0 ]] || { echo "error: no files matched in $LOCAL_DIR (RUN_ID=$RUN_ID)" >&2; exit 1; }

# ---- push -------------------------------------------------------------------
# 1) ensure remote dir exists
ssh "${SSH_OPTS[@]}" "${REMOTE_USER}@${REMOTE_HOST}" "mkdir -p ${REMOTE_PATH}"

# 2) scp the files
scp "${SCP_OPTS[@]}" "${FILES[@]}" "${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_PATH%/}/"

echo "OK: ${#FILES[@]} file(s) uploaded -> ${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_PATH}"
for f in "${FILES[@]}"; do
  echo "  $(basename "$f")"
done
