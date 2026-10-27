# SGC On-Snow Field Test — First Full-Loop Validation

*Prepared 2026-10-27. Scope: FW 0.21.5+ (or 0.22.x if per-run delete flashed) → S22 App 1.69/1.70 → sgc-api.vytsolutions.com. Goal: validate the whole chain in real conditions before the season. Single device (A); merges are bench-only until device B arrives.*

## The night before (bench, 10 min)

- [ ] Device on USB → charging ⚡ shows in app (ABC5)
- [ ] Battery ≥ 90%, device name set (F21), app shows letter **A** (device registry)
- [ ] FW version: serial `?` → `ver` ≥ 0.21.5
- [ ] App ≥ 1.69 (check title bar), phone storage ≥ 1 GB free
- [ ] Cloud: Settings → Cloud Account signed in; Cloud Runs loads (newest first)
- [ ] Free slots on device: serial `b` → budget OK for planned run count
- [ ] Phone: Bluetooth ON, mobile data or planned chalet Wi-Fi (offline queue covers gaps — F43)

## On the mountain (per session)

1. [ ] **Arm**: press button → ARMED indication; device worn as configured (pole/arm, same side as configured discipline)
2. [ ] **Run normally.** Drop-detect should fire from the start-gate motion (baro drop ≥ 2 m equivalent) — if the run starts with a gentle push-off, watch for missed auto-start (log it)
3. [ ] **At the lift / after the run**: app open in pocket is enough — auto-connect + auto-download of new runs (paired device A). Verify: new tile appears, name `A#n`, per-day numbering 1..N, day pager shows today
4. [ ] **Sync card**: queue drains → "uploaded" status; spot-check one run in Cloud Runs from the phone browser or later
5. [ ] Battery % after each hour in the cold (log it — first real cold-weather battery data)
6. [ ] If anything wedges: note the exact moment + last visible state; serial console via PC at home recovers diagnostics

## Cold-weather watch list (first exposure)

- [ ] Baro altitude sanity vs known elevation (temp drift)
- [ ] Gate count detected vs gates actually run (video one run for truth)
- [ ] BLE reliability through jacket layers / pole mount distance
- [ ] Charging: **never charge below 0 °C** (Li-ion) — bring it indoors, let it warm, then USB
- [ ] Screen-off behavior: auto-sync + upload still drain the queue (wake-lock path)

## After the session (30 min, warm)

- [ ] All runs present locally; count vs runs actually skied
- [ ] Cloud complete: same count, names `A#x` consistent with local
- [ ] Open 2–3 run details: gates + altitude/speed charts render; timestamps plausible (no negative/absurd deltas)
- [ ] Delete one junk run (once App 1.70 + FW 0.22 are on): 3-way delete works; listing renumbers (display-only numbering — expected)
- [ ] Rename one run → appears in cloud within a sync cycle (App 1.70)
- [ ] Export/save the day's serial log if anything odd happened (`notes/` the observations same day)

## Known-good fallbacks

- Offline → queue uploads later (airplane-mode test is a valid substitute for no-signal)
- Device full → oldest slots recycle automatically; nothing to do
- App confusion → restart app; queue is durable
- Device unresponsive → USB serial console (COM3 bench) — never wipe unless certain
