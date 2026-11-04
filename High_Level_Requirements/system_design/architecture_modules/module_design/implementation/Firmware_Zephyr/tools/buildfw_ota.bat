@echo off
rem One-shot detached OTA (sysbuild+MCUboot) FW build. Args: <builddir>. Example: buildfw_ota C:\sgc\build\p0240a
rem Example: buildfw C:\sgc\build\p0180
set "BD=%~1"
if "%BD%"=="" set "BD=C:\sgc\build\latest"
if not exist C:\sgc\build mkdir C:\sgc\build
cmd /c "C:\sgc\tools\zbuild_ota.bat C:\sgc\apps\03_pull_proto arduino_nicla_sense_me %BD% >%BD%.log 2>&1 <NUL"
echo BUILD_EXIT_%errorlevel% >>%BD%.log
