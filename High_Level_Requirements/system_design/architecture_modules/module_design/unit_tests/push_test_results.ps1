# push_test_results.ps1 - push smoke/core/full results to the OpenClaw host over SSH.
#
# No secrets in this file: auth is via an ed25519 key (path only). Nothing here
# ever reads or accepts a password.
#
# Usage (from unit_tests/):
#   .\push_test_results.ps1
#   .\push_test_results.ps1 -RunId run_20260814_0900
#   .\push_test_results.ps1 -All
#
# Config precedence: parameter > env var > default.
#   SGC_RESULTS_HOST / SGC_RESULTS_PORT / SGC_RESULTS_USER / SGC_RESULTS_PATH / SGC_RESULTS_KEY
#
# Hostinger note: OpenClaw runs in Docker. On the VPS HOST the workspace is under
# /root/.openclaw/workspace/... (bind-mounted to /home/node/.openclaw inside the container).
# Default user/path below target that host layout. Override with env if needed.
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
if (-not $RemoteHost) {
  if ($env:SGC_RESULTS_HOST) { $RemoteHost = $env:SGC_RESULTS_HOST }
  else { $RemoteHost = "72.60.88.60" }
}
if ($RemotePort -le 0) {
  if ($env:SGC_RESULTS_PORT) { $RemotePort = [int]$env:SGC_RESULTS_PORT }
  else { $RemotePort = 22 }
}
if (-not $RemoteUser) {
  if ($env:SGC_RESULTS_USER) { $RemoteUser = $env:SGC_RESULTS_USER }
  else { $RemoteUser = "root" }
}
if (-not $RemotePath) {
  if ($env:SGC_RESULTS_PATH) { $RemotePath = $env:SGC_RESULTS_PATH }
  else {
    $RemotePath = "/root/.openclaw/workspace/ski_gate_chrono/High_Level_Requirements/system_design/architecture_modules/module_design/unit_tests/tmp_test_results"
  }
}
if (-not $IdentityFile) {
  if ($env:SGC_RESULTS_KEY) { $IdentityFile = $env:SGC_RESULTS_KEY }
  else { $IdentityFile = (Join-Path $env:USERPROFILE ".ssh\id_ed25519_sgc") }
}
if (-not $LocalDir) {
  $LocalDir = Join-Path $PSScriptRoot "tmp_test_results"
}

if (-not (Test-Path -LiteralPath $LocalDir)) {
  Write-Error "Local results dir not found: $LocalDir"
  exit 1
}
$LocalDir = (Resolve-Path -LiteralPath $LocalDir).Path

# ---- prerequisites ----------------------------------------------------------
if (-not (Get-Command scp -ErrorAction SilentlyContinue)) {
  Write-Error "scp not found. Install OpenSSH Client (Windows Optional Features)."
  exit 1
}
if (-not (Get-Command ssh -ErrorAction SilentlyContinue)) {
  Write-Error "ssh not found. Install OpenSSH Client (Windows Optional Features)."
  exit 1
}
if (-not (Test-Path -LiteralPath $IdentityFile)) {
  $hint = "ssh-keygen -t ed25519 -f `"$IdentityFile`" -C sgc-results"
  Write-Error "SSH key not found: $IdentityFile. Generate once: $hint"
  exit 1
}

# ---- select files -----------------------------------------------------------
if ($All) {
  $files = @(Get-ChildItem -Path $LocalDir -Filter "run_*" -File -ErrorAction Stop)
} else {
  if (-not $RunId) {
    $newest = Get-ChildItem -Path $LocalDir -Filter "run_*.md" -File -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending |
              Select-Object -First 1
    if (-not $newest) {
      Write-Error "No run_*.md found in $LocalDir - run the harness first or pass -RunId."
      exit 1
    }
    $RunId = $newest.BaseName
  }
  $files = @(Get-ChildItem -Path $LocalDir -File -ErrorAction Stop | Where-Object {
    $_.Name -eq ($RunId + ".md") -or $_.Name -like ($RunId + "_*.log")
  })
}

if ($files.Count -eq 0) {
  Write-Error "No files matched in $LocalDir (RunId=$RunId)."
  exit 1
}

# ---- push -------------------------------------------------------------------
$sshTarget = $RemoteUser + "@" + $RemoteHost
$remoteDir = $RemotePath.TrimEnd("/")
$dest = $sshTarget + ":" + $remoteDir + "/"

$sshArgs = @(
  "-i", $IdentityFile,
  "-p", "$RemotePort",
  "-o", "BatchMode=yes",
  "-o", "StrictHostKeyChecking=accept-new",
  $sshTarget,
  "mkdir -p $remoteDir"
)
& ssh @sshArgs
if ($LASTEXITCODE -ne 0) {
  Write-Error "ssh mkdir failed (exit $LASTEXITCODE). Check host/user/key."
  exit 1
}

$paths = @($files | ForEach-Object { $_.FullName })
$scpArgs = @(
  "-i", $IdentityFile,
  "-P", "$RemotePort",
  "-o", "BatchMode=yes",
  "-o", "StrictHostKeyChecking=accept-new"
) + $paths + @($dest)
& scp @scpArgs
if ($LASTEXITCODE -ne 0) {
  Write-Error "scp failed (exit $LASTEXITCODE)."
  exit 1
}

Write-Host ("OK: {0} file(s) uploaded -> {1}:{2}" -f $files.Count, $sshTarget, $remoteDir)
foreach ($f in $files) {
  Write-Host ("  " + $f.Name)
}
