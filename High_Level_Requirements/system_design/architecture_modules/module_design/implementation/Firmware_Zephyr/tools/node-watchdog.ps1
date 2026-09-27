# node-watchdog.ps1 — SGC bench watchdog v2: BOOT-PROOF (JP GO 09-27)
# ====================================================================
# One loop, THREE daemons — designed to run as a SYSTEM scheduled task
# at boot (nobody logged in):
#
#   1. TUNNEL   ssh -L 18789 (node connection) + -R 9999 (file channel)
#               Functional probe: TCP connect to 127.0.0.1:18789.
#               Keepalives so a dead link exits ssh within ~45 s.
#   2. FILES    py -m http.server 8080 (serves C:\Users\v17ni — the
#               gateway side pulls via the -R forward). Log to FILE:
#               a server whose stdout died with its parent console
#               kills every request (09-26 lesson).
#   3. NODE HOST  node openclaw.mjs node run — runs with USERPROFILE
#               redirected to C:\Users\v17ni so it finds its identity
#               (pairing keys/config) even under SYSTEM.
#
# Environment note (why the odd explicit paths): as SYSTEM, HOME and
# APPDATA point at the system profile — ssh would miss the key/config,
# openclaw would miss its node identity. We hand each process the
# user-profile paths explicitly. No stored passwords anywhere.
#
# Log: %USERPROFILE% is SYSTEM's under the task — we write to the
# fixed path C:\Users\v17ni\node-watchdog.log (rotates at 1 MB).

$ErrorActionPreference = 'SilentlyContinue'

# ── Configuration ───────────────────────────────────────────────────
$USER_DIR   = 'C:\Users\v17ni'
$SSH_EXE    = 'C:\Windows\System32\OpenSSH\ssh.exe'
$PY_EXE     = "$USER_DIR\AppData\Local\Programs\Python\Python313\python.exe"
$NODE_EXE   = 'C:\Program Files\nodejs\node.exe'
$LOG        = "$USER_DIR\node-watchdog.log"
$TUN_PORT   = 18789
$WEB_PORT   = 8080
$CHECK_EVERY_SEC  = 30
$FAILS_BEFORE_FIX = 2

$SSH_ARGS = @(
    '-N',
    '-L', "$TUN_PORT`:127.0.0.1:$TUN_PORT",
    '-R', '9999:127.0.0.1:8080',
    '-o', 'ServerAliveInterval=15',
    '-o', 'ServerAliveCountMax=3',
    '-o', 'ExitOnForwardFailure=yes',
    '-o', 'StrictHostKeyChecking=accept-new',
    '-F', "$USER_DIR\.ssh\config",          # SYSTEM home would miss it
    'vps'
)

function Log($msg) {
    $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $msg
    Add-Content -Path $LOG -Value $line
    if ((Get-Item $LOG).Length -gt 1MB) {
        Set-Content -Path $LOG -Value $line
    }
}

function Test-Port([int]$port) {
    $c = New-Object Net.Sockets.TcpClient
    try {
        $ok = $c.ConnectAsync('127.0.0.1', $port).Wait(3000)
        return ($ok -and $c.Connected)
    } finally { $c.Close() }
}

function Get-TunnelSsh {
    Get-CimInstance Win32_Process -Filter "Name='ssh.exe'" |
        Where-Object { $_.CommandLine -like "*$TUN_PORT`:127.0.0.1*" }
}

# ── Daemon 1: the tunnel ────────────────────────────────────────────
function Ensure-Tunnel {
    # NOTE (bug 09-27): the counter MUST be $script:-scoped on BOTH the
    # increment and the read — v2 incremented Script: but read the local
    # (always 0) and never restarted the tunnel. Explicit scope only.
    if (Test-Port $TUN_PORT) {
        $script:TunFails = 0
        return
    }
    $script:TunFails = [int]$script:TunFails + 1
    Log ("tunnel: probe FAIL #{0}" -f $script:TunFails)
    if ($script:TunFails -ge $FAILS_BEFORE_FIX) {
        Get-TunnelSsh | ForEach-Object {
            Log ("tunnel: killing pid {0}" -f $_.ProcessId)
            Stop-Process -Id $_.ProcessId -Force
        }
        Start-Sleep -Seconds 1
        Log 'tunnel: (re)starting ssh'
        Start-Process $SSH_EXE -ArgumentList $SSH_ARGS -WindowStyle Hidden
        Start-Sleep -Seconds 3
        $script:TunFails = 0
    }
}

# ── Daemon 2: the file server ───────────────────────────────────────
function Ensure-Web {
    if (Test-Port $WEB_PORT) { return }
    # NOTE (bug 09-27): 'py' is NOT on the SYSTEM PATH (it lives in the
    # user's AppData) — absolute python path or the spawn dies silently.
    $script:WebFails = [int]$script:WebFails + 1
    if ($script:WebFails -le 3 -or ($script:WebFails % 20) -eq 0) {
        Log ("web: starting http.server 8080 (attempt {0})" -f $script:WebFails)
    }
    Start-Process cmd -ArgumentList @(
        '/c', $PY_EXE, '-m', 'http.server', '8080',
        '--directory', $USER_DIR,
        '>', "$USER_DIR\http.log", '2>&1'
    ) -WindowStyle Hidden
    Start-Sleep -Seconds 2
    if (Test-Port $WEB_PORT) { $script:WebFails = 0 }
}

# ── Daemon 3: the openclaw node host ────────────────────────────────
function Ensure-NodeHost {
    $running = Get-CimInstance Win32_Process -Filter "Name='node.exe'" |
        Where-Object { $_.CommandLine -like '*openclaw.mjs*node run*' }
    if ($running) { return }
    Log 'nodehost: starting openclaw node run'
    # USERPROFILE redirect = the node identity lives in the user's
    # .openclaw; without this a SYSTEM-hosted node would pair as a
    # brand-new device.
    Start-Process cmd -ArgumentList @(
        '/c', 'set', "USERPROFILE=$USER_DIR",
        '&&', 'set', "APPDATA=$USER_DIR\AppData\Roaming",
        '&&', $NODE_EXE,
        "$USER_DIR\AppData\Roaming\npm\node_modules\openclaw\openclaw.mjs",
        'node', 'run',
        '>', "$USER_DIR\nodehost.log", '2>&1'
    ) -WindowStyle Hidden
    Start-Sleep -Seconds 3
}

# ── Main loop ───────────────────────────────────────────────────────
Log 'watchdog v2.1: starting (boot-proof, 3 daemons)'
$script:TunFails = 0
$script:WebFails = 0
$Beat = 0
while ($true) {
    Ensure-Tunnel
    Ensure-Web
    Ensure-NodeHost
    if ((++$Beat % (600 / $CHECK_EVERY_SEC)) -eq 0) {
        Log 'heartbeat: all daemons ok'
    }
    Start-Sleep -Seconds $CHECK_EVERY_SEC
}
