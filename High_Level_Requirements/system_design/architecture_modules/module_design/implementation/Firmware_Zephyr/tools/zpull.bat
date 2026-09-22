@echo off
rem SGC Zephyr relay pull: fetch the VPS mirror and merge into the current branch.
rem
rem WHY THIS EXISTS: the GitHub "origin" is ALWAYS BEHIND. Relay model is
rem   VPS (ZioClaw commits) -> THIS PC (you) -> GitHub (only when you push).
rem A plain "git pull" / TortoiseGit pull on origin fetches GitHub = "no changes".
rem
rem Requires: ssh alias "vps" in ~/.ssh/config (key id_ed25519_sgc) - already set up.
rem Usage: zpull.bat   (from anywhere; repo auto-discovered from this script's path)
git -C "%~dp0" fetch ssh://vps/root/.openclaw/workspace/ski_gate_chrono master
if errorlevel 1 (
echo [zpull] ERROR: fetch from VPS failed. Is the tunnel window up? ssh alias "vps" OK?
goto end
)
git -C "%~dp0" merge FETCH_HEAD
if errorlevel 1 (
echo [zpull] ERROR: merge failed - resolve conflicts, then commit.
goto end
)
echo [zpull] done, now at:
git -C "%~dp0" log --oneline -1
:end
