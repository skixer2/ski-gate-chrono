# cleanup_user_home.ps1 - JP directive 2026-10-15
# ZioClaw home on JP-PC = C:\Users\v17ni\zio_claw
# Groups loose ZioClaw artifacts by project/theme, deletes temp files,
# writes README.md documentation per folder. Idempotent (safe re-run).
# Does NOT touch: harness live logs (stay in root until harness v2.6),
# .ssh/.gitconfig, JP's own files (jpv_brain*, develop), Windows profile dirs.
$ErrorActionPreference = 'Continue'
$root = 'C:\Users\v17ni'
$zc   = Join-Path $root 'zio_claw'

$dirs = @(
  'sgc_bench\logs','sgc_bench\hex','sgc_bench\captures',
  'sgc_bench\screens','sgc_bench\ui','sgc_bench\data',
  'harness\tools','digital_product_factory','archive'
)
foreach ($d in $dirs) { New-Item -ItemType Directory -Path (Join-Path $zc $d) -Force | Out-Null }

$moved = 0; $deleted = 0
function Move-Match([string]$pattern, [string]$dest) {
  $files = Get-ChildItem -LiteralPath $script:root -File | Where-Object { $_.Name -like $pattern }
  foreach ($f in $files) {
    Move-Item -LiteralPath $f.FullName -Destination (Join-Path $script:zc $dest) -Force
    $script:moved++
  }
}

# --- sgc_bench (ski_gate_chrono bench artifacts) ---
Move-Match 'appbuild*.log'        'sgc_bench\logs'
Move-Match 'ard_serial.log'       'sgc_bench\logs'
Move-Match 'bhi_*.log'            'sgc_bench\logs'
Move-Match 'bl_flash.log'         'sgc_bench\logs'
Move-Match 'zbhi2b.log'           'sgc_bench\logs'
Move-Match 'zbuild_v*.log'        'sgc_bench\logs'
Move-Match 'bhy_probe*.hex'       'sgc_bench\hex'
Move-Match 'nicla_bootloader.hex' 'sgc_bench\hex'
Move-Match 'ble_cap*.txt'         'sgc_bench\captures'
Move-Match 'dl*.txt'              'sgc_bench\captures'
Move-Match 'lgt*.txt'             'sgc_bench\captures'
Move-Match 'final*.txt'           'sgc_bench\captures'
Move-Match 'm3run.txt'            'sgc_bench\captures'
Move-Match 'savertest.txt'        'sgc_bench\captures'
Move-Match 'sgcf*.txt'            'sgc_bench\captures'
Move-Match 'sgcflutter.txt'       'sgc_bench\captures'
Move-Match 'sgclog*.txt'          'sgc_bench\captures'
Move-Match 'v2lines.txt'          'sgc_bench\captures'
Move-Match 'westdbg*.txt'         'sgc_bench\captures'
Move-Match 'logcat.txt'           'sgc_bench\captures'
Move-Match 'dlok*.png'            'sgc_bench\screens'
Move-Match 'dlv*.png'             'sgc_bench\screens'
Move-Match 'fin*.png'             'sgc_bench\screens'
Move-Match 'ph*.png'              'sgc_bench\screens'
Move-Match 'phone*.png'           'sgc_bench\screens'
Move-Match 'rot.png'              'sgc_bench\screens'
Move-Match 'sgc_screen*.png'      'sgc_bench\screens'
Move-Match 'ui*.xml'              'sgc_bench\ui'
Move-Match 'run*.bin'             'sgc_bench\data'
Move-Match 'mruns.json'           'sgc_bench\data'
Move-Match 'lc*.txt'              'sgc_bench\data'
Move-Match 'pidlog.txt'           'sgc_bench\data'
# --- harness tools ---
Move-Match 'hd*.py'               'harness\tools'
Move-Match 'kckill.ps1'           'harness\tools'
Move-Match 'wd-owner.ps1'         'harness\tools'
# --- digital product factory ---
Move-Match 'SyncAllCurrencies.vba' 'digital_product_factory'
# --- archive ---
Move-Match 'path-backup_*.txt'    'archive'

# --- whole dirs -> archive ---
foreach ($src in @('sgcsync','sgcsnoop','sgc_cleanup_20260927','nicla_app01','nicla_app02','nicla_beacon','nicla_hw','nicla_led')) {
  $from = Join-Path $root $src
  if (Test-Path $from) {
    Move-Item $from (Join-Path $zc ('archive\' + $src)) -Force
    $script:moved++
  }
}

# --- temp deletions ---
foreach ($pattern in @('*.tmp','wd.b64','wd-task-utf16.xml','$null')) {
  $files = Get-ChildItem -LiteralPath $root -File | Where-Object { $_.Name -like $pattern }
  foreach ($f in $files) {
    Remove-Item -LiteralPath $f.FullName -Force
    $script:deleted++
  }
}

# --- READMEs ---
$readmeRoot = @'
# zio_claw - ZioClaw home on JP-PC (JP directive 2026-10-15)

All ZioClaw-generated files on this PC live here, grouped by project/theme.
Every permanent folder carries a README.md describing what it is and why it exists.
Temporary files are deleted once useless. ZioClaw does NOT touch anything outside
this folder (exception: C:\sgc, explicitly allowed by JP).

## Map
- sgc_bench\    - ski_gate_chrono bench artifacts (logs, hex dumps, BLE/serial captures, phone screenshots, UI dumps, run data)
- harness\      - boot-proof harness helpers (tools); live harness logs are still in C:\Users\v17ni root (hardcoded paths, migration pending harness v2.6)
- digital_product_factory\ - deliverable helpers (currency-sync VBA for the Excel products)
- archive\      - dated leftovers kept for forensics; safe to delete wholesale after ~6 months

## Left in C:\Users\v17ni root ON PURPOSE
- node-watchdog.log, nodehost.log, nodehost.err.log, tunnel-run.log, tunnel-aux.log, http.log
  (live harness logs; paths hardcoded in node-watchdog.ps1 / tunnel-run.cmd until v2.6)
- .ssh, .gitconfig (keys/config required by the tunnel + git relay)
- jpv_brain*, develop\ (JP's own - off limits)

Rule for all ZioClaw chats: new PC artifacts go to C:\Users\v17ni\zio_claw\<project>\.
'@
Set-Content -Path (Join-Path $zc 'README.md') -Value $readmeRoot -Encoding UTF8

$readmeSgc = @'
# sgc_bench - ski_gate_chrono bench artifacts

Artifacts dumped in C:\Users\v17ni during ZioClaw-driven device bench sessions
(Nicla Sense ME / Zephyr firmware, S22 phone via adb). Grouped Oct 2026.
- logs\     firmware/app build logs (zbuild_*, appbuild*, bhi_*, bl_flash, zbhi2b, ard_serial)
- hex\      firmware binaries pulled for inspection (bhy_probe*, nicla_bootloader)
- captures\ BLE/serial captures and debug dumps (ble_cap*, sgclog*, sgcf*, lgt*, dl*, westdbg*, logcat, m3run, savertest, final*, v2lines)
- screens\  S22 phone screenshots from UI debugging (phone*, ph*, sgc_screen*, dlok*, dlv*, fin*, rot)
- ui\       uiautomator dumps (ui*.xml) used for tap-coordinate work
- data\     decoded run payloads and session data (run*.bin, mruns.json, lc*.txt, pidlog)
NEW artifacts from bench sessions belong HERE (e.g. zio_claw\sgc_bench\logs\...).
'@
Set-Content -Path (Join-Path $zc 'sgc_bench\README.md') -Value $readmeSgc -Encoding UTF8

$readmeHarness = @'
# harness - boot-proof bench harness helpers

- tools\kckill.ps1   kills stuck nrf-kconfig python processes (VS Code Kconfig ext holds zephyr\.cache); used before west builds
- tools\hd.py, hd2.py  one-shot hex-dump helpers (serial FT debugging, Oct 2026)
- tools\wd-owner.ps1  watchdog process forensics one-shot (Oct 5, 2026 incident)
LIVE harness logs (node-watchdog.log, nodehost*.log, tunnel-run.log, tunnel-aux.log,
http.log) still live in C:\Users\v17ni root - paths are hardcoded in
node-watchdog.ps1 / tunnel-run.cmd; they move here in harness v2.6.
The task + scripts source of truth: C:\sgc (repo Firmware_Zephyr/tools/infra).
'@
Set-Content -Path (Join-Path $zc 'harness\README.md') -Value $readmeHarness -Encoding UTF8

$readmeDpf = @'
# digital_product_factory - deliverable helpers

- SyncAllCurrencies.vba - currency-sync macro shipped with the Excel products
  (JP imports it and attaches a button; convention: see TOOLS.md Digital Product
  Factory section). Dated Sep 2026.
'@
Set-Content -Path (Join-Path $zc 'digital_product_factory\README.md') -Value $readmeDpf -Encoding UTF8

$readmeArch = @'
# archive - dated leftovers (kept for forensics, safe to delete after ~6 months)

- sgcsync_20260910\            Sep 10 app-era build logs + source snapshots + run0.bin (Arduino FW era)
- sgcsnoop_20260915\           S22 phone forensics (dumpstate zip + FS pull), Sep 14-15
- sgc_cleanup_20260927\        the Sep 27 sweep: Zephyr bring-up logs (zbuild03-33, ncs_*), app build logs, b64 transports
- nicla_apps_20260918\         first Zephyr app experiments (nicla_app01/02, beacon, hw, led) - superseded by repo apps\
- path-backup_20260922_091938.txt  PATH backup from the Sep 22 PATH surgery
'@
Set-Content -Path (Join-Path $zc 'archive\README.md') -Value $readmeArch -Encoding UTF8

Write-Output ("DONE moved={0} deleted={1}" -f $moved, $deleted)
