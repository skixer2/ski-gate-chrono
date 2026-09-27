@echo off
rem tunnel-run.cmd — the bridge, as a file (spawned by the watchdog).
rem LAUNCH-METHOD MATTERS (09-27 lesson): schtasks->cmd-file->ssh WORKS
rem under SYSTEM (proven by ssh-diag); PowerShell Start-Process->ssh.exe
rem died pre-main with an empty -E log. Same ssh, different launcher.
rem Full output captured for forensics; overwritten per respawn.
C:\Windows\System32\OpenSSH\ssh.exe -N -L 18789:127.0.0.1:18789 -R 9999:127.0.0.1:8080 -o ServerAliveInterval=15 -o ServerAliveCountMax=3 -o ExitOnForwardFailure=yes -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile=C:\Users\v17ni\.ssh\known_hosts -i C:\Users\v17ni\.ssh\id_ed25519_sgc -F C:\Users\v17ni\.ssh\config root@72.60.88.60 > C:\Users\v17ni\tunnel-run.log 2>&1
