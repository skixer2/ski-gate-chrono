@echo off
rem Repoint + fire the FW build SYSTEM task at the autoflash harness.
schtasks /create /tn ZioClaw_FWBuild /tr "cmd /c C:\sgc\tools\autoflash.bat C:\sgc\build\b0258 0.25.8" /sc once /st 23:59 /f /ru SYSTEM
if errorlevel 1 ( echo SCHED_CREATE_FAIL & exit /b 1 )
schtasks /run /tn ZioClaw_FWBuild
if errorlevel 1 ( echo SCHED_RUN_FAIL & exit /b 1 )
echo FWSCHED_LAUNCHED
