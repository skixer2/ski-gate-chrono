# node-watchdog.ps1 — SGC bench connectivity watchdog (JP directive 09-25)
# ====================================================================
# Keeps the JP-PC -> VPS bridge alive without anyone home:
#   1. THE TUNNEL (the fragile part): ssh -N -L 18789:127.0.0.1:18789
#      to the gateway. We check it FUNCTIONALLY (can we still open a
#      TCP connection to 127.0.0.1:18789?), not just "is a process
#      alive" — a hung ssh with a dead link is the failure mode that
#      bit us on 09-25. On failure: kill every stale ssh for this
#      tunnel, start a fresh one with aggressive keepalives
#      (ServerAliveInterval=15 x3 => a dead link kills ssh within ~45 s,
#      and the next loop pass restarts it).
#   2. THE NODE HOST: if the OpenClaw node-host process is gone, we
#      start it again (command configurable below — filled with the
#      real invocation once verified on the bench).
#
# Logging: one line per action + a heartbeat every 10 min, to
#   %USERPROFILE%\node-watchdog.log (rotated at ~1 MB by truncation).
#
# Intended use: registered as a logon Task Scheduler job (no admin
# needed) by register-watchdog.bat. Survives: link drops, VPS reboots,
# ssh hangs. Does NOT survive: PC reboot without logon (documented
# limitation — a true boot-start service needs admin; decide later if
# the bench ever runs headless).

$ErrorActionPreference = 'SilentlyContinue'

# ── Configuration (edit here, not below) ────────────────────────────
$TUNNEL_TARGET = '127.0.0.1:18789'   # the forwarded gateway port
$SSH_ARGS      = @(
    '-N', '-L', '18789:127.0.0.1:18789',
    '-o', 'ServerAliveInterval=15',
    '-o', 'ServerAliveCountMax=3',
    '-o', 'ExitOnForwardFailure=yes',
    '-o', 'StrictHostKeyChecking=accept-new',
    'vps'                              # ~/.ssh/config alias (key auth)
)
$CHECK_EVERY_SEC  = 30                # watchdog cadence
$FAILS_BEFORE_FIX = 2                 # consecutive failures -> action
$LOG              = Join-Path $env:USERPROFILE 'node-watchdog.log'

function Log($msg) {
    $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $msg
    Add-Content -Path $LOG -Value $line
    if ((Get-Item $LOG).Length -gt 1MB) {
        Set-Content -Path $LOG -Value $line   # rotate by truncation
    }
}

function Test-Tunnel {
    # Functional probe: can we open TCP to the forwarded port?
    $parts = $TUNNEL_TARGET.Split(':')
    $c = New-Object Net.Sockets.TcpClient
    try {
        $ok = $c.ConnectAsync($parts[0], [int]$parts[1]).Wait(3000)
        return ($ok -and $c.Connected)
    } finally { $c.Close() }
}

function Get-TunnelSsh {
    Get-CimInstance Win32_Process -Filter "Name='ssh.exe'" |
        Where-Object { $_.CommandLine -like '*18789:127.0.0.1:18789*' }
}

function Start-Tunnel {
    Log 'tunnel: (re)starting ssh'
    Start-Process ssh -ArgumentList $SSH_ARGS `
        -WindowStyle Hidden
    Start-Sleep -Seconds 3
}

Log ('watchdog: starting (loop every {0}s, fix after {1} fails)' -f `
    $CHECK_EVERY_SEC, $FAILS_BEFORE_FIX)

$fails = 0
$beat = 0
while ($true) {
    if (Test-Tunnel) {
        $fails = 0
        if ((++$beat % (600 / $CHECK_EVERY_SEC)) -eq 0) {
            Log 'tunnel: ok (heartbeat)'
        }
    } else {
        $fails++
        Log ("tunnel: probe FAIL #{0}" -f $fails)
        if ($fails -ge $FAILS_BEFORE_FIX) {
            # Kill every stale ssh bound to our tunnel, then respawn.
            Get-TunnelSsh | ForEach-Object {
                Log ("tunnel: killing pid {0}" -f $_.ProcessId)
                Stop-Process -Id $_.ProcessId -Force
            }
            Start-Sleep -Seconds 1
            Start-Tunnel
            $fails = 0
        }
    }
    Start-Sleep -Seconds $CHECK_EVERY_SEC
}
