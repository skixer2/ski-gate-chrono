@echo off
rem Creates + fires the one-shot ZioClaw_AppBuild scheduled task (headless-safe:
rem node-host exec cannot hold long jobs, and `start` hangs without a desktop).
rem The task runs app_build_install.bat detached under Task Scheduler.
set "BAT=C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\module_design\implementation\Phone_app_prototype\scripts\app_build_install.bat"
schtasks /create /tn ZioClaw_AppBuild /tr "cmd /c %BAT% sched" /sc once /st 23:59 /f
schtasks /run /tn ZioClaw_AppBuild
echo SCHED_LAUNCHED
