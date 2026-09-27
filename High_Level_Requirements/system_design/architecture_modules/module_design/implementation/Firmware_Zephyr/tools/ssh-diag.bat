@echo off
rem ssh-diag.bat — run ONCE as admin. Registers a one-shot SYSTEM task
rem that executes ssh-diag-run.cmd (the watchdog's exact tunnel ssh with
rem -vvv), waits, cleans up. Logs: ssh-diag.log + ssh-diag-stdout.log
rem NOTE: diag uses ports 18790/9998 so it cannot collide with the live
rem manual bridge (18789/9999) — a clean error message is all we need.
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: run as Administrator.
    pause
    exit /b 1
)
schtasks /create /tn SGC-SshDiag /tr "C:\sgc\tools\ssh-diag-run.cmd" /sc once /st 23:59 /ru SYSTEM /rl highest /f
schtasks /run /tn SGC-SshDiag
timeout /t 10 >nul
schtasks /delete /tn SGC-SshDiag /f
echo DONE - read C:\Users\v17ni\ssh-diag.log and ssh-diag-stdout.log
pause
