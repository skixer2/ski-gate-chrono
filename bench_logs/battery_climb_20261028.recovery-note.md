# battery_climb_20261028.log recovery note — 2026-11-07T23:30Z

Incident: during the 23:30Z cron run, the log append was mistakenly performed with the
write tool (full-file replace) instead of a shell append, destroying all entries written
after the last backup. Original content is preserved in
battery_climb_20261028.log.bak-20261104T2315Z (entries 2026-11-04T22:30Z..23:15Z).

Lost window: 2026-11-04T23:15Z .. 2026-11-07T23:15Z (~3 days of 15-min cron entries).

Recovery actions:
- Restored backup content verbatim (incl. the historical JSON-blob line from 23:15 Nov 4).
- Recovered one in-gap entry from leftover tmp file: 2026-11-06T01:45:00Z READ_FAIL.
- Added a GAP_LOG marker line for the lost window.
- Today's reading appended normally: 2026-11-07 23:30:24Z batt_dbg: pct=100 stable.

What the lost window contained (from session-search snippets of prior cron runs —
timestamps approximate, exact lines unrecoverable):
- JP-PC node offline stretches (a Nov 6 run noted last-connect ~Nov 4 17:22 UTC, ~43h) → READ_FAIL entries, incl. 2026-11-06T01:45Z (tmp file evidence).
- Successful reads (several on Nov 5 early hours, Nov 7 ~11:45-12:15Z, 23:15Z) all reported pct=100, mon 0x7c, fastchg 0x9c, cache 0x64 — same stable values as before/after the gap.
- Net: no battery climb observed anywhere in the recoverable record; pct pinned at 100 since Nov 4.

Lesson (for future cron runs): append via plain gateway exec only (printf with env-passed
line, no shell interpolation of device output). Never use the write tool on an existing
append-only log.
2026-11-08T17:00Z second append accident: cron run used the write tool (content replace) instead of shell append on battery_climb_20261028.log; full 77-line history restored byte-identical from write-tool diff within the same run, plus in-log NOTE line. Rule: log appends = gateway exec shell (printf/echo >>), NEVER the write tool.
2026-11-17T08:30Z third append accident: cron run used the write tool (empty-content replace) instead of shell append on battery_climb_20261028.log; file wiped to 0 bytes. Restored byte-faithful from battery_climb_20261028.log.bak (506 lines, snapshot 2026-11-16 15:31, through 15:30:57Z entry; bak2 is a truncated-line variant, bak is the faithful one). Lost window 2026-11-16T15:31Z..2026-11-17T08:30Z marked with GAP_LOG line; cron fired on 15-min schedule per session-search timestamps but per-run values unrecoverable (run histories rotated out of session store); pct=100 stable at both boundaries (15:30:57Z backup tail, 08:30:39Z fresh read, identical bytes 0x7c/0x9c/0x64). Today's reading appended normally. LESSON (third strike, same as 2026-11-07 and 2026-11-08): log appends = gateway exec shell printf >> ONLY. The write tool is FORBIDDEN on battery_climb_20261028.log and this recovery note. Future cron runs: stage the line in env, printf '%s\n' "$LINE" >> file, never write.
