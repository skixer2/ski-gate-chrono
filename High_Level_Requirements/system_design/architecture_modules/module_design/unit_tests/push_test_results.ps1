# push_test_results.ps1 — push smoke/core/full results to the OpenClaw (VPS) host over SSH.
#
# No secrets in this file: auth is via an ed25519 key (path only). Nothing here
# ever reads or accepts a password.
#
# Usage (from unit_tests/):
#   .\push_test_results.ps1                          # newest run_*.md + its *.log
#   .\push_test_results.ps1 -RunId run_20260814_0900 # that specific run
#   .\push_test_results.ps1 -All                     # every run_* file in tmp_test_results
#
# Config precedence: parameter > env var > documented default (see body).
#   SGC_RESULTS_HOST / SGC_RESULTS_PORT / SGC_RESULTS_USER / SGC_RESULTS_PATH / SGC_RESULTS_KEY
param(
  [string]$RunId        = "",
  [string]$RemoteHost   = "",
  [int]   $RemotePort   = 0,
  [string]$RemoteUser   = "",
  [string]$RemotePath   = "",
  [string]$IdentityFile = "",
  [string]$LocalDir     = "",
  [switch]$All
)
$ErrorActionPreference = "Stop"

# ---- resolve config ---------------------------------------------------------
if (-not $RemoteHost)   { $RemoteHost   = if ($env:SGC_RESULTS_HOST) { $env:SGC_RESULTS_HOST } else { "72.60.88.60" } }  # example only
if ($RemotePort -le 0)  { $RemotePort   = if ($env:SGC_RESULTS_PORT) { [int]$env:SGC_RESULTS_PORT } else { 22 } }
if (-not $RemoteUser)   { $RemoteUser   = if ($env:SGC_RESULTS_USER) { $env:SGC_RESULTS_USER } else { "node" } }        # "ubuntu" is an alternative
if (-not $RemotePath)   { $RemotePath   = if ($env:SGC_RESULTS_PATH) { $env:SGC_RESULTS_PATH } else { "/home/node/.openclaw/workspace/ski_gate_chrono/High_Level_Requirements/system_design/architecture_modules/module_design/unit_tests/tmp_test_results" } }
if (-not $IdentityFile) { $IdentityFile = if ($env:SGC_RESULTS_KEY)  { $env:SGC_RESULTS_KEY }  else { (Join-Path $env:USERPROFILE ".ssh\id_ed25519_sgc") } }
if (-not $LocalDir)     { $LocalDir     = Join-Path $PSScriptRoot "tmp_test_results" }

if (-not (Test-Path $LocalDir)) {
  Write-Error "Local results dir not found: $LocalDir"
  exit 1
}
$LocalDir = (Resolve-Path $LocalDir).Path

# ---- prerequisites ----------------------------------------------------------
if (-not (Get-Command scp -ErrorAction SilentlyContinue)) {
  Write-Error "scp not found. Install OpenSSH Client (Settings > Optional Features > OpenSSH Client)."
  exit 1
}
if (-not (Get-Command ssh -ErrorAction SilentlyContinue)) {
  Write-Error "ssh not found. Install OpenSSH Client (Settings > Optional Features > OpenSSH Client)."
  exit 1
}
if (-not (Test-Path $IdentityFile)) {
  Write-Error "SSH key not found: $IdentityFile`nGenerate once: ssh-keygen -t ed25519 -f `"$IdentityFile`" -C sgc-results"
  exit 1
}

# ---- select files -----------------------------------------------------------
if ($All) {
  $files = @(Get-ChildItem -Path $LocalDir -Filter "run_*" -File)
} else {
  if (-not $RunId) {
    $newest = Get-ChildItem -Path $LocalDir -Filter "run_*.md" -File |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $newest) {
      Write-Error "No run_*.md found in $LocalDir — run the harness first or pass -RunId."
      exit 1
    }
    $RunId = $newest.BaseName
  }
  $files = @(Get-ChildItem -Path $LocalDir -File | Where-Object {
    $_.Name -eq "$RunId.md" -or $_.Name -like "${RunId}_*.log"
  })
}

if ($files.Count -eq 0) {
  Write-Error "No files matched in $LocalDir (RunId=$RunId)."
  exit 1
}

# ---- push -------------------------------------------------------------------
$sshOpts = @("-i", $IdentityFile, "-p", "$RemotePort", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=accept-new")
$scpOpts = @("-i", $IdentityFile, "-P", "$RemotePort", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=accept-new")
$dest = "${RemoteUser}@${RemoteHost}:$($RemotePath.TrimEnd('/'))/"

# 1) ensure remote dir exists
& ssh @sshOpts "${RemoteUser}@${RemoteHost}" "mkdir -p $RemotePath"
if ($LASTEXITCODE -ne 0) {
  Write-Error "ssh mkdir -p failed (exit $LASTEXITCODE). Check host/user/key reachability."
  exit 1
}

# 2) scp the files
& scp @scpOpts $files.FullName $dest
if ($LASTEXITCODE -ne 0) {
  Write-Error "scp failed (exit $LASTEXITCODE)."
  exit 1
}

Write-Host "OK: $($files.Count) file(s) uploaded -> ${RemoteUser}@${RemoteHost}:${RemotePath}"
$files | ForEach-Object { Write-Host "  $($_.Name)" }
