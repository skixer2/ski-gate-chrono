# SGC Phone App (Flutter) — README

Current version: **1.72.0** (`lib/config/app_version.dart` — single source of truth).

## Structure map (`lib/`)

| Path | Contents |
|---|---|
| `main.dart` | App entry: ApiClient / SyncWorker / DeviceDeleteBacklog init (all fire-and-forget), HomeScreen |
| `ble/` | Device link: `ble_manager.dart` (FlutterBluePlus path), `native_ble_downloader.dart` (Nordic BleManager Kotlin lane), `file_transfer.dart` (FT pull protocol), `sgc_service.dart` (GATT facade: console `D`/`s`/`d`, config, battery) |
| `cloud/` | sgc-api integration: `api_client.dart` (I07 bootstrap, refresh mutex, typed errors), `auth_store.dart` (flutter_secure_storage), `upload_queue.dart` (durable queue, uuid idempotency, backoff 1 m→1 h), `sync_worker.dart` (single-flight; renames-then-pushes) |
| `storage/` | `local_storage.dart` (runs), `local_db.dart` (merges), `device_registry.dart` (device letters A–Z→AA…), `device_delete_backlog.dart` (mac-stamped pending device deletes) |
| `config/` | `app_version.dart` — **bump on every code change** (convention mirrors FW `config.h`); full per-version history lives in its comments |
| `models/` | run, gate timestamps, sensor frames, baro points, course gates… |
| `processing/` | decompressor, impact detector, cross-correlator, gate-time estimator, run merger |
| `ui/screens/` | home, run_list (day pager), run_detail, merged_run, cloud_runs, cloud_run_detail, cloud_account, profile, ble_devices, course_setup, settings |
| `ui/widgets/` | altitude/speed charts, gate timestamp table |

## Version convention

`APP_VERSION` in `lib/config/app_version.dart` is bumped on **every** change (no exceptions), with a dated history comment per bump. The build lane greps it into logs; cloud bootstrap gates `min_app_version` against it.

## Naming + day pager (reqs v6.5, App 1.69+)

- Every distinct device MAC gets a **permanent letter** (A, B, …; re-pair keeps it). Default run name = `<letter>#<device_counter>` (e.g. `A#32`) — **computed, never stored**; a custom F45 name overrides.
- Run list shows **one day at a time** (◀ EEE d MMM ▶, launch = newest day, ends disabled). Within a day: sorted (timestamp, R-before-L, id), numbered 1..N **display-only** (deletes renumber; never stored/uploaded). Merges live on their first source run's day.
- Merged default name = `<A#x> + <B#y>` from the sources' defaults; cloud `name` always carries the effective string, so local and cloud titles match.

## Synch-as-state model (App 1.72)

Per run/merge, long-press menu: **Rename / Synch to cloud (switch) / Delete**.

- **Synch checked** = the run IS on the cloud. **Desync (ON→OFF)** = the *only* cloud-removal path: cloud `DELETE`, queue entry → DESYNCED (dead — backfill can never re-upload), pending renames dropped; phone + device untouched.
- **Re-sync (OFF→ON)** = explicit narrow upload of just that run (same `client_run_uuid` → server creates a NEW row), bypassing the global push_to_cloud gate.
- **Delete = phone + device legs only** (never the cloud). Device leg: live `d <id>` console command, or the durable `device_delete_backlog` applied on every connect *before* listing/download (idempotent, mac-stamped). Unsynced run → double-confirm total-loss warning; synced run → honest "cloud copy kept" scoping.
- **Cloud Runs screen:** rows carry Synch switches; orphan rows (no local copy) get a delete icon + strong warning — no auto-resurrection anywhere.

## Build & install lane (JP-PC)

`scripts/app_build_install.bat [tag]` — on JP-PC: git pull (`ZioClaw` remote) → `flutter analyze` → `flutter build apk --debug` → `adb install`, log to `zio_claw\sgc_bench\logs\appbuild_<tag>.log` with `PULL_OK` / `BUILD_OK` / `INSTALL_EXIT_0` / `APPBUILD_DONE` markers. `appbuild_sched.bat` wraps it as a scheduled task for long builds. Package: `com.skigatechono.sgc_phone`.
