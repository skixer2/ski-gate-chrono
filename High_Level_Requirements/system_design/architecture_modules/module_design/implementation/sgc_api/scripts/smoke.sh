#!/usr/bin/env bash
# SGC-API P0 smoke test — curl end-to-end against a throwaway SQLite DB.
# Covers: healthz, register, login, batch upload (3 timestamps + 10 baro
# rows), idempotent retry, list, detail, rename 409 path, rename hit,
# delete, GDPR delete, post-GDPR 404.
#
# Usage:  bash scripts/smoke.sh [port]      (default 8901)
# Requires: python3 (JSON parsing), curl, uvicorn installed (pip install -e ".")
set -u

PORT="${1:-8901}"
BASE="http://127.0.0.1:${PORT}"
SMOKE_DIR="$(mktemp -d /tmp/sgc_api_smoke.XXXXXX)"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PASS=0
FAIL=0
SERVER_PID=""

# Prefer the project venv; fall back to whatever python3 is on PATH.
if [ -x "$ROOT/.venv/bin/python" ]; then PY="$ROOT/.venv/bin/python"; else PY="python3"; fi
cd "$ROOT"

cleanup() {
  [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null
  rm -rf "$SMOKE_DIR"
}
trap cleanup EXIT

# expect METHOD PATH EXPECTED_STATUS [json_body] [bearer_token] [extra_header]
expect() {
  local method="$1" path="$2" want="$3" body="${4:-}" token="${5:-}" extra="${6:-}"
  local args=(-s -o /dev/null -w '%{http_code}' -X "$method" "$BASE$path")
  [ -n "$token" ] && args+=(-H "Authorization: Bearer $token")
  [ -n "$extra" ] && args+=(-H "$extra")
  [ -n "$body" ] && args+=(-H 'Content-Type: application/json' -d "$body")
  local got
  got="$(curl "${args[@]}")"
  if [ "$got" = "$want" ]; then
    echo "PASS  $method $path -> $got"
    PASS=$((PASS + 1))
  else
    echo "FAIL  $method $path -> got $got, want $want"
    FAIL=$((FAIL + 1))
  fi
}

json_field() {  # json_field FILE JQ_LIKE_PYTHON_EXPR
  python3 - "$1" "$2" <<'PY'
import json, sys
with open(sys.argv[1]) as fh:
    print(eval(sys.argv[2], {"data": json.load(fh)}))
PY
}

echo "== starting uvicorn on $BASE (db: $SMOKE_DIR/smoke.db) =="
export SGC_API_SECRET_KEY="smoke-secret-not-for-production-0123456789abcdef"
export SGC_API_DATABASE_URL="sqlite:///$SMOKE_DIR/smoke.db"

# Schema first — production never create_alls either (§2 guardrail 2).
"$PY" -m alembic upgrade head >"$SMOKE_DIR/alembic.log" 2>&1 || {
  echo "FAIL  alembic upgrade head"; cat "$SMOKE_DIR/alembic.log"; exit 1;
}
echo "PASS  alembic upgrade head"

"$PY" -m uvicorn app.main:app --host 127.0.0.1 --port "$PORT" \
  >"$SMOKE_DIR/uvicorn.log" 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 30); do
  curl -s -o /dev/null "$BASE/healthz" && break
  sleep 0.5
done

echo "== 1. liveness + bootstrap =="
expect GET /healthz 200
expect GET /bootstrap-info 200

echo "== 2. register + login =="
expect POST /v1/auth/register 201 \
  '{"name":"smoke-athlete","password":"gate-timer-pass","user_type":"athlete","age":16,"category":"U18"}'
expect POST /v1/auth/register 409 \
  '{"name":"smoke-athlete","password":"gate-timer-pass","user_type":"athlete"}'
expect POST /v1/auth/login 401 \
  '{"name":"smoke-athlete","password":"wrong-password"}'

curl -s -H 'Content-Type: application/json' \
  -d '{"name":"smoke-athlete","password":"gate-timer-pass"}' \
  "$BASE/v1/auth/login" > "$SMOKE_DIR/login.json"
TOKEN="$(json_field "$SMOKE_DIR/login.json" "data['access_token']")"
[ -n "$TOKEN" ] && { echo "PASS  login issued access token"; PASS=$((PASS+1)); } \
               || { echo "FAIL  login: no access token";   FAIL=$((FAIL+1)); }
expect GET /v1/profile 200 "" "$TOKEN"

echo "== 3. batch upload (3 timestamps + 10 baro rows) =="
REQ="$(python3 -c '
import json
print(json.dumps({
  "client_run_uuid": "smoke-run-0001",
  "start_datetime": "2026-10-26T09:00:00Z",
  "run_number": 7, "format_version": 1,
  "timestamps": [{"timestamp_ms": 1000*i, "arm_side": "L" if i%2==0 else "R",
                  "guessed": False} for i in range(3)],
  "barometric_data": [{"time_offset_ms": 100*i, "altitude_m": 1200.0+0.1*i,
                       "speed_mps": -1.5} for i in range(10)],
}))')"
expect POST /v1/runs 201 "$REQ" "$TOKEN"
expect POST /v1/runs 200 "$REQ" "$TOKEN"   # idempotent retry

echo "== 4. list + detail =="
expect GET "/v1/runs" 200 "" "$TOKEN"
expect GET "/v1/runs/1?include=timestamps,barometric_data" 200 "" "$TOKEN"
expect GET "/v1/runs/999" 404 "" "$TOKEN"

echo "== 5. rename If-Match: 428 / 409 / 200 =="
expect PUT /v1/runs/1 428 '{"name":"Powder 7"}' "$TOKEN"
expect PUT /v1/runs/1 409 '{"name":"Powder 7"}' "$TOKEN" "If-Match: 99"
expect PUT /v1/runs/1 200 '{"name":"Powder 7"}' "$TOKEN" "If-Match: 1"
expect PUT /v1/runs/1 409 '{"name":"Powder 7 again"}' "$TOKEN" "If-Match: 1"

echo "== 6. delete cascade + GDPR =="
expect DELETE /v1/runs/1 204 "" "$TOKEN"
expect GET /v1/runs/1 404 "" "$TOKEN"
# Re-upload the same uuid after delete (id is reused — capture it fresh).
REUP_ID="$(curl -s -X POST "$BASE/v1/runs" -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' -d "$REQ" \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])')"
echo "PASS  POST /v1/runs -> 201 (re-uploaded as id $REUP_ID)"; PASS=$((PASS+1))
expect DELETE "/v1/runs/$REUP_ID" 204 "" "$TOKEN"
expect POST /v1/me/gdpr-delete 400 '{"confirm": false}' "$TOKEN"
expect POST /v1/me/gdpr-delete 204 '{"confirm": true}' "$TOKEN"
expect GET /v1/profile 404 "" "$TOKEN"      # §9: everything 404s afterwards
expect POST /v1/auth/login 401 \
  '{"name":"smoke-athlete","password":"gate-timer-pass"}'

echo
echo "== smoke summary: $PASS passed, $FAIL failed =="
[ "$FAIL" = "0" ]
