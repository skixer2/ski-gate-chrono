@echo off
rem One-shot detached FW build (wrapper-safe launcher; args: <builddir>).
rem Example: buildfw C:\sgc\build\p0180
set "BD=%~1"
if "%BD%"=="" set "BD=C:\sgc\build\latest"
if not exist C:\sgc\build mkdir C:\sgc\build
cmd /c "C:\sgc\tools\zbuild.bat C:\sgc\apps\03_pull_proto arduino_nicla_sense_me %BD% >%BD%.log 2>&1 <NUL"
echo BUILD_EXIT_%errorlevel% >>%BD%.log
