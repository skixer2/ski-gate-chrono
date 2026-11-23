@echo off
rem One-shot detached FW build (wrapper-safe launcher; args: <builddir>).
rem Example: buildfw C:\sgc\build\p0180
set "BD=%~1"
rem
rem 2026-11-23 (JP): never default to "latest". Two sessions proved that
rem C:\sgc\build\latest is the one path that keeps disappearing (a stale
rem detached cleanup chain hardcoded to it). Versioned dirs survive.
rem Asking for a build with no tag is now an error, not a silent 'latest'.
if "%BD%"=="" (
    echo [buildfw] ERROR: no build dir given. Pass a versioned tag:
    echo   buildfw C:\sgc\build\bNNNN
    exit /b 2
)
if not exist C:\sgc\build mkdir C:\sgc\build
cmd /c "C:\sgc\tools\zbuild.bat C:\sgc\apps\03_pull_proto arduino_nicla_sense_me %BD% >%BD%.log 2>&1 <NUL"
echo BUILD_EXIT_%errorlevel% >>%BD%.log
