@echo off
rem The actual SYSTEM-context ssh probe (launched BY the scheduled task;
rem kept in its own file because schtasks /tr caps at 261 chars).
C:\Windows\System32\OpenSSH\ssh.exe -vvv -N -L 18790:127.0.0.1:18789 -R 9998:127.0.0.1:8080 -o ServerAliveInterval=15 -o ServerAliveCountMax=3 -o ExitOnForwardFailure=yes -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile=C:\Users\v17ni\.ssh\known_hosts -i C:\Users\v17ni\.ssh\id_ed25519_sgc -F C:\Users\v17ni\.ssh\config -E C:\Users\v17ni\ssh-diag.log root@72.60.88.60 > C:\Users\v17ni\ssh-diag-stdout.log 2>&1
