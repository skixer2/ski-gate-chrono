@echo off
rem ssh-diag.bat — run ONCE as admin: executes the watchdog's exact
rem tunnel ssh AS SYSTEM and captures everything to C:\Users\v17ni\ssh-diag.log
schtasks /create /tn SGC-SshDiag /tr "cmd /c C:\Windows\System32\OpenSSH\ssh.exe -vvv -N -L 18789:127.0.0.1:18789 -R 9999:127.0.0.1:8080 -o ServerAliveInterval=15 -o ServerAliveCountMax=3 -o ExitOnForwardFailure=yes -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile=C:\Users\v17ni\.ssh\known_hosts -i C:\Users\v17ni\.ssh\id_ed25519_sgc -F C:\Users\v17ni\.ssh\config -E C:\Users\v17ni\ssh-diag.log root@72.60.88.60 > C:\Users\v17ni\ssh-diag-stdout.log 2>&1" /sc once /st 23:59 /ru SYSTEM /rl highest /f
schtasks /run /tn SGC-SshDiag
timeout /t 8 >nul
schtasks /delete /tn SGC-SshDiag /f
echo DONE - logs at C:\Users\v17ni\ssh-diag.log and ssh-diag-stdout.log
pause
