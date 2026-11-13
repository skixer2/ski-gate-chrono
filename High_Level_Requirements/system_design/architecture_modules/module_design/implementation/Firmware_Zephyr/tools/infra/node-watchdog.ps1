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
#
# v2.7 (2026-11-13, after the 14 h outage of 11-12/13): the tunnel
# probe (local TCP accept) cannot see a DEAD TRANSPORT — the -L
# listener lives in the local ssh process and keeps accepting even
# when the far end is gone. That blind spot left an alive-but-
# disconnected node host unrecycled for 14 h. Fixes:
#   A. single-instance guard (duplicate loops duplicate daemons),
#   B. tunnel restart decision now uses the END-TO-END /healthz probe,
#   C. node liveness = direct ESTABLISHED-link check to the tunnel
#      port (no more early-return on "gateway unreachable").

$ErrorActionPreference = 'SilentlyContinue'

# ── Configuration ───────────────────────────────────────────────────
$USER_DIR   = 'C:\Users\v17ni'
$SSH_EXE    = 'C:\Windows\System32\OpenSSH\ssh.exe'
$PY_EXE     = "$USER_DIR\AppData\Local\Programs\Python\Python313\python.exe"
$NODE_EXE   = 'C:\Program Files\nodejs\node.exe'
$LOG        = "$USER_DIR\node-watchdog.log"
# v2.6 knobs: node self-heal bounds (minutes)
$STALE_MIN = 30
$FORCE_MIN = 90
$TUN_PORT   = 18789
$WEB_PORT   = 8080
$CHECK_EVERY_SEC  = 30
$FAILS_BEFORE_FIX = 2
# v2.7: alive-but-unlinked node cycles before recycle (3 x 30 s = 90 s)
$DEAD_CYCLES_BEFORE_RECYCLE = 3

# v2.4: the $SSH_ARGS array was deleted — unused since v2.2 moved the
# spawn into tunnel-run.cmd, and its -F config + alias approach is
# exactly what failed under SYSTEM.

function Log($msg) {
    $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $msg
    Add-Content -Path $LOG -Value $line
    if ((Get-Item $LOG).Length -gt 1MB) {
        Set-Content -Path $LOG -Value $line
    }
}

# ── v2.7 A: single-instance guard ───────────────────────────────────
# Duplicate watchdog loops duplicate every daemon and fight over the
# log; a stuck old loop also blocks recycle decisions. Any sibling
# powershell running this same script dies at startup.
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

# ── Daemon 1: the tunnel ────────────────────────────────────────────
function Ensure-Tunnel {
    # NOTE (bug 09-27): the counter MUST be $script:-scoped on BOTH the
    # increment and the read — v2 incremented Script: but read the local
    # (always 0) and never restarted the tunnel. Explicit scope only.
    # v2.7 B: end-to-end healthz probe decides. A zombie ssh still
    # ACCEPTS local TCP (the -L listener lives in the local process),
    # so Test-Port alone said "ok" for 14 h on 11-12/13 while the
    # transport was dead.
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
        # v2.3: spawn through the cmd-file VIA THE JUNCTION. Short,
        # SYSTEM-proven (the diag ran from C:\sgc\tools fine), and
        # immune to the v2.2 escape-mangling that corrupted the long
        # profile path (\a -> bell, \t -> tab — the spawn never ran).
        Start-Process cmd -ArgumentList @(
            '/c', 'C:\sgc\tools\infra\tunnel-run.cmd'
        ) -WindowStyle Hidden
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
    # v2.5 FIX: the old cmd /c set-and-chain spawn was broken — PS 5.1
    # Start-Process joins ArgumentList WITHOUT quoting, so the space in
    # "C:\Program Files\nodejs" reached cmd bare ('C:\Program' is not
    # recognized) and the host crash-looped on every check. Latent since
    # v2.1: never fired while a manually-started host was alive.
    # Now: Start-Process node.exe directly + env inherited from us +
    # native file redirects. No cmd parser in the way.
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

# ── v2.6: node CONNECTION-liveness (JP directive 11-05: no human intervention) ──
# 2026-11-05 failure mode: gateway rejected the node during a config incident;
# the node-host process stayed alive but silently gave up retrying. v2.5 only
# checked "process exists" -> stuck for hours. Fix: if BOTH nodehost logs are
# stale (no writes for $STALE_MIN) while the tunnel+gateway probe is OK, the
# node is presumed disconnected-stuck -> kill it; the loop respawns it fresh.
# Backstop: unconditional recycle every $FORCE_MIN regardless of log activity.
# Worst case cost: a healthy node blips once every $FORCE_MIN (reconnects ~2 s).
#
# v2.7: the stale-log heuristic missed the 11-12/13 case (logs can look
# fresh while the socket is half-dead, and the gateway probe early-return
# skipped the whole check when the tunnel hung). Ground truth is now the
# socket itself: an alive node host with NO ESTABLISHED connection to the
# tunnel port is disconnected, full stop.
$script:LastNodeRecycle = Get-Date
$script:NodeDeadStreak = 0
function Test-GatewayViaTunnel {
    try {
        $r = Invoke-WebRequest -UseBasicParsing -TimeoutSec 6 `
             -Uri 'http://127.0.0.1:18789/healthz'
        return ($r.StatusCode -eq 200)
    } catch { return $false }
}
function Get-NewestLogWrite {
    $newest = [datetime]::MinValue
    foreach ($f in @("$USER_DIR\nodehost.log", "$USER_DIR\nodehost.err.log")) {
        if (Test-Path $f) {
            $t = (Get-Item $f -ErrorAction SilentlyContinue).LastWriteTime
            if ($t -and $t -gt $newest) { $newest = $t }
        }
    }
    return $newest
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
    # v2.7 C: does any node pid hold an ESTABLISHED connection to the
    # tunnel port? Ground truth of "connected", seen from the PC.
    foreach ($p in $procIds) {
        $conns = Get-NetTCPConnection -OwningProcess $p -State Established -ErrorAction SilentlyContinue
        if ($conns | Where-Object { $_.RemotePort -eq $TUN_PORT }) { return $true }
    }
    # netstat fallback (older OS / cmdlet unavailable)
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
    # v2.7 C: while the gateway is unreachable the TUNNEL daemon (now
    # healthz-based) owns the problem — don't churn the node while the
    # pipe itself is being rebuilt. Once healthz passes, an unlinked
    # node is disconnected no matter what its logs say.
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
    # v2.6 backstops unchanged: stale-log + periodic force recycle.
    $ageMin = if ((Get-NewestLogWrite) -eq [datetime]::MinValue) {
        [int]::MaxValue
    } else {
        [int]((Get-Date) - (Get-NewestLogWrite)).TotalMinutes
    }
    $sinceMin = [int]((Get-Date) - $script:LastNodeRecycle).TotalMinutes
    if ($ageMin -ge $STALE_MIN) {
        Log ("nodehost: logs stale {0} min while gateway reachable -> recycle" -f $ageMin)
        Restart-NodeHost
    } elseif ($sinceMin -ge $FORCE_MIN) {
        Log ("nodehost: periodic recycle ({0} min)" -f $sinceMin)
        Restart-NodeHost
    }
}

# ── Main loop ───────────────────────────────────────────────────────
Log 'watchdog v2.7: starting (boot-proof; tunnel healthz, node link-check, self-heal 30/90 min)'
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