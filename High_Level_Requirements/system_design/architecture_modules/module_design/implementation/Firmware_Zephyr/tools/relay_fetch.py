#!/usr/bin/env python3
"""Relay fetch via explicit ssh identity (node host running as SYSTEM).
Usage: py relay_fetch.py
"""
import subprocess

REPO = r'C:\Users\v17ni\Documents\projects\ski-gate-chrono'
URL = 'ssh://root@72.60.88.60/root/.openclaw/workspace/ski_gate_chrono'
KEY = 'C:/Users/v17ni/.ssh/id_ed25519_sgc'
SSH = ['ssh', '-i', KEY, '-o', 'StrictHostKeyChecking=accept-new']

for cmd in (['git', 'fetch', URL, 'master'], ['git', 'merge', 'FETCH_HEAD']):
    r = subprocess.run(cmd, cwd=REPO, env={'GIT_SSH_COMMAND': ' '.join(SSH),
                                           'PATH': r'C:\Windows\System32;'
                                                   r'C:\Program Files\Git\cmd',
                                           'SYSTEMROOT': r'C:\Windows'},
                       capture_output=True, text=True)
    print(cmd[1], '->', r.returncode)
    if r.stdout.strip():
        print(r.stdout.strip()[:500])
    if r.returncode:
        print(r.stderr.strip()[:500])
        raise SystemExit(1)
print('relay fetch OK')
