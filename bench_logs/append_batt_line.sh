#!/bin/sh
# SGC charge-watch log appender. Usage: append_batt_line.sh '<raw node exec JSON or output>'
# Extracts the batt_dbg: line (strips JSON wrapper and trailing CR), falls back to
# READ_FAIL_PORT on port-denied, READ_FAIL otherwise. Created 2026-11-14 after the
# exec output switched to a {status,exitCode,durationMs,aggregated} wrapper that
# corrupted three log entries when string-matched directly.
LOG=/home/node/.openclaw/workspace/ski_gate_chrono/bench_logs/battery_climb_20261028.log
TS=$(date -u +%Y-%m-%dT%H:%M:%SZ)
IN="$1"
LINE=$(printf '%s' "$IN" | grep -o 'batt_dbg:[^"\\r]*' | head -n1 | tr -d '\r' | sed 's/[[:space:]]*$//')
if [ -n "$LINE" ]; then
  printf '%s %s\n' "$TS" "$LINE" >> "$LOG"
elif printf '%s' "$IN" | grep -qiE 'denied|permission|could not open port'; then
  printf '%s READ_FAIL_PORT\n' "$TS" >> "$LOG"
else
  printf '%s READ_FAIL\n' "$TS" >> "$LOG"
fi
