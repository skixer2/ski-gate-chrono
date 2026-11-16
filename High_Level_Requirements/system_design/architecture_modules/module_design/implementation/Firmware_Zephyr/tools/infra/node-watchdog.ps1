# node-watchdog.ps1 - SGC bench watchdog v2: BOOT-PROOF (JP GO 09-27)
# ====================================================================
# One loop, THREE daemons - designed to run as a SYSTEM scheduled task
# at boot (nobody logged in):
#   1. TUNNEL   ssh -L 18789 (node connection) + -R 9999 (file channel)
#   2. FILES    python http.server 8080 (serves C:/Users/v17ni)
#   3. NODE HOST  node openclaw.mjs node run (USERPROFILE redirected)
# Log: C:/Users/v17ni/node-watchdog.log (rotates at 1 MB).
#
# v2.7 (2026-11-13, after the 14 h outage of 11-12/13):
#   A. single-instance guard,
#   B. tunnel restart decision uses the END-TO-END /healthz probe,
#   C. node liveness = direct ESTABLISHED-link check to the tunnel port.
# v2.8 (2026-11-16, after the every-30-min flap JP hit during bench work):
#   The v2.6 stale-log backstop force-recycled a HEALTHY IDLE node every
#   30 min (a connected idle node writes nothing to nodehost.log, so the
#   log-age heuristic always tripped). The link check (C) IS the ground
#   truth of connection health, so the stale-log backstop is REMOVED and
#   the unconditional periodic recycle is relaxed 90 min -> 12 h as a
#   black-swan net (WS protocol wedge with open TCP eventually closes the
#   TCP via gateway heartbeats, which the link check then sees).

$ErrorActionPreference = 'SilentlyContinue'

# -- Configuration ---------------------------------------------------
$USER_DIR   = 'C:\Users\v17ni'
$PY_EXE     = "$USER_DIR\AppData\Local\Programs\Python\Python313\python.exe"
$NODE_EXE   = 'C:\Program Files\nodejs\node.exe'
$LOG        = "$USER_DIR\node-watchdog.log"
# v2.8 knobs
$FORCE_MIN = 720
$TUN_PORT   = 18789
$WEB_PORT   = 8080
$CHECK_EVERY_SEC  = 30
$FAILS_BEFORE_FIX = 2
# alive-but-unlinked node cycles before recycle (3 x 30 s = 90 s)
$DEAD_CYCLES_BEFORE_RECYCLE = 3

function Log($msg) {
    $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $msg
    Add-Content -Path $LOG -Value $line
    if ((Get-Item $LOG).Length -gt 1MB) {
        Set-Content -Path $LOG -Value $line
    }
}

# -- v2.7 A: single-instance guard -----------------------------------
$me = $PID
Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" |
    Where-Object { $_.ProcessId -ne $me -and $_.CommandLine -like '*node-watchdog.ps1*' } |
    ForEach-Object {
        Log ("guard: killing stale watchdog pid {0}" -f $_.ProcessId)
        Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
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

# -- Daemon 1: the tunnel --------------------------------------------
function Ensure-Tunnel {
    # v2.7 B: end-to-end healthz probe decides. A zombie ssh still
    # ACCEPTS local TCP (listener lives in the local process).
    $up = $false
    if (Test-Port $TUN_PORT) { $up = Test-GatewayViaTunnel }
    if ($up) {
        $script:TunFails = 0
        return
    }
    $script:TunFails = [int]$script:TunFails + 1
    if ($script:TunFails -le 3 -or ($script:TunFails % 20) -eq 0) {
        Log ("tunnel: probe FAIL #{0}" -f $script:TunFails)
    }
    if ($script:TunFails -ge $FAILS_BEFORE_FIX) {
        Get-TunnelSsh | ForEach-Object {
            Log ("tunnel: killing pid {0}" -f $_.ProcessId)
            Stop-Process -Id $_.ProcessId -Force
        }
        Start-Sleep -Seconds 1
        Log 'tunnel: (re)starting via tunnel-run.cmd'
        Start-Process cmd -ArgumentList @(
            '/c', 'C:\sgc\tools\infra\tunnel-run.cmd'
        ) -WindowStyle Hidden
        Start-Sleep -Seconds 3
        $script:TunFails = 0
    }
}

# -- Daemon 2: the file server ---------------------------------------
function Ensure-Web {
    if (Test-Port $WEB_PORT) { return }
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

# -- Daemon 3: the openclaw node host --------------------------------
function Ensure-NodeHost {
    $running = Get-CimInstance Win32_Process -Filter "Name='node.exe'" |
        Where-Object { $_.CommandLine -like '*openclaw.mjs*node run*' }
    if ($running) { return }
    Log 'nodehost: starting openclaw node run'
    # v2.5: Start-Process node.exe directly + USERPROFILE redirect + native
    # file redirects. No cmd parser in the way.
    $oldUP = $env:USERPROFILE; $oldAD = $env:APPDATA
    $env:USERPROFILE = $USER_DIR
    $env:APPDATA     = "$USER_DIR\AppData\Roaming"
    try {
        Start-Process -FilePath $NODE_EXE -ArgumentList @(
            "$USER_DIR\AppData\Roaming\npm\node_modules\openclaw\openclaw.mjs",
            'node', 'run'
        ) -RedirectStandardOutput "$USER_DIR\nodehost.log" -RedirectStandardError "$USER_DIR\nodehost.err.log" -WindowStyle Hidden -ErrorAction Stop
    } finally {
        $env:USERPROFILE = $oldUP; $env:APPDATA = $oldAD
    }
    Start-Sleep -Seconds 3
}

# -- Connection liveness ---------------------------------------------
# v2.7/2.8: ground truth is the SOCKET: an alive node host with no
# ESTABLISHED connection to the tunnel port is disconnected. While the
# gateway is unreachable the TUNNEL daemon owns the problem.
$script:LastNodeRecycle = Get-Date
$script:NodeDeadStreak = 0
function Test-GatewayViaTunnel {
    try {
        $r = Invoke-WebRequest -UseBasicParsing -TimeoutSec 6 `
             -Uri 'http://127.0.0.1:18789/healthz'
        return ($r.StatusCode -eq 200)
    } catch { return $false }
}
function Restart-NodeHost {
    Log 'nodehost: FORCE-RECYCLE (stuck or periodic)'
    Get-CimInstance Win32_Process -Filter "Name='node.exe'" |
        Where-Object { $_.CommandLine -like '*openclaw.mjs*node run*' } |
        ForEach-Object {
            Log ("nodehost: killing pid {0}" -f $_.ProcessId)
            Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
        }
    $script:LastNodeRecycle = Get-Date
}
function Test-NodeTunnelLink($procIds) {
    foreach ($p in $procIds) {
        $conns = Get-NetTCPConnection -OwningProcess $p -State Established -ErrorAction SilentlyContinue
        if ($conns | Where-Object { $_.RemotePort -eq $TUN_PORT }) { return $true }
    }
    $lines = netstat -ano | Select-String 'ESTABLISHED'
    foreach ($ln in $lines) {
        $parts = ($ln.ToString() -split '\s+') | Where-Object { $_ -ne '' }
        if ($parts.Count -ge 5 -and $parts[3] -eq 'ESTABLISHED') {
            if ($procIds -contains [int]$parts[4] -and $parts[2] -like "*:$TUN_PORT") { return $true }
        }
    }
    return $false
}
function Ensure-NodeLiveness {
    $running = Get-CimInstance Win32_Process -Filter "Name='node.exe'" |
        Where-Object { $_.CommandLine -like '*openclaw.mjs*node run*' }
    if (-not $running) { $script:NodeDeadStreak = 0; return }
    $procIds = @($running | ForEach-Object { $_.ProcessId })
    if (Test-GatewayViaTunnel) {
        if (Test-NodeTunnelLink $procIds) {
            $script:NodeDeadStreak = 0
        } else {
            $script:NodeDeadStreak = [int]$script:NodeDeadStreak + 1
            if ($script:NodeDeadStreak -ge $DEAD_CYCLES_BEFORE_RECYCLE) {
                Log ("nodehost: alive but no tunnel link {0} cycles -> recycle" -f $script:NodeDeadStreak)
                Restart-NodeHost
                $script:NodeDeadStreak = 0
                return
            }
        }
    } else {
        $script:NodeDeadStreak = 0
    }
    # v2.8: stale-log backstop REMOVED (false-recycled healthy idle nodes
    # every 30 min). Periodic black-swan net only.
    $sinceMin = [int]((Get-Date) - $script:LastNodeRecycle).TotalMinutes
    if ($sinceMin -ge $FORCE_MIN) {
        Log ("nodehost: periodic recycle ({0} min)" -f $sinceMin)
        Restart-NodeHost
    }
}

# -- Main loop -------------------------------------------------------
Log 'watchdog v2.8: starting (link-truth liveness; tunnel healthz; periodic net 12 h)'
$script:TunFails = 0
$script:WebFails = 0
$Beat = 0
while ($true) {
    Ensure-Tunnel
    Ensure-Web
    Ensure-NodeHost
    Ensure-NodeLiveness
    if ((++$Beat % (600 / $CHECK_EVERY_SEC)) -eq 0) {
        Log 'heartbeat: all daemons ok'
    }
    Start-Sleep -Seconds $CHECK_EVERY_SEC
}
