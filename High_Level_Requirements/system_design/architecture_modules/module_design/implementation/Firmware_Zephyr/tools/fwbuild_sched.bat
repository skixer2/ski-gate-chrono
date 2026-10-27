@echo off
rem Fires the FW build as a SYSTEM-owned scheduled task (survives node-host
rem invoke kills; wmic/start/schtasks-default all failed from exec context).
rem The app repo must already be pulled (buildfw itself does no git).
schtasks /create /tn ZioClaw_FWBuild /tr "cmd /c C:\sgc\tools\buildfw.bat C:\sgc\build\p0221a" /sc once /st 23:59 /f /ru SYSTEM
schtasks /run /tn ZioClaw_FWBuild
echo FWSCHED_LAUNCHED
