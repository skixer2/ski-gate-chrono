@echo off
rem Fires the FW build as a SYSTEM-owned scheduled task (survives node-host
rem invoke kills; wmic/start/schtasks-default all failed from exec context).
rem The app repo must already be pulled (buildfw itself does no git).
rem
rem Usage:  fwbuild_sched.bat <tag>
rem   e.g.  fwbuild_sched.bat b0264   -> builds into C:\sgc\build\b0264
rem
rem 2026-11-23 fix (JP): this script used to hardcode C:\sgc\build\p0240a and
rem silently ignore whatever tag the caller passed, so build directories stopped
rem matching their tags. The tag argument is now forwarded to buildfw_ota.bat,
rem and the build dir is derived from it. Omitting the tag keeps the old
rem default so nothing that already calls this argument-less breaks.
setlocal
set "TAG=%~1"
if "%TAG%"=="" set "TAG=p0240a"
set "BD=C:\sgc\build\%TAG%"
if not exist "C:\sgc\build" mkdir "C:\sgc\build"
schtasks /create /tn ZioClaw_FWBuild /tr "cmd /c C:\sgc\tools\buildfw_ota.bat %BD%" /sc once /st 23:59 /f /ru SYSTEM
schtasks /run /tn ZioClaw_FWBuild
echo FWSCHED_LAUNCHED %TAG%
endlocal
