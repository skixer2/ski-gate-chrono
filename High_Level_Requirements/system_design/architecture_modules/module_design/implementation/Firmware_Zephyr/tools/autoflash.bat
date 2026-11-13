@echo off
rem AUTOFLASH harness: build(OTA) -> version gates -> hexsafe copy -> flash -> boot banner gate
rem Args: %1 = build dir   %2 = version (e.g. 0.25.8). Runs as ZioClaw_FWBuild SYSTEM task.
setlocal
set "BD=%~1"
if "%BD%"=="" set "BD=C:\sgc\build\b0258"
set "VER=%~2"
if "%VER%"=="" set "VER=0.25.8"
set "PY=C:\Users\v17ni\AppData\Local\Programs\Python\Python313\python.exe"
set "HL=C:\sgc\harness.log"
set "HS=C:\sgc\hexsafe\%VER:.=%.signed.hex"
set "FL=%BD%_flash.log"
echo === HARNESS %date% %time% start bd=%BD% ver=%VER% ===>>"%HL%"
del "%FL%" 2>nul
rem 1) build (sysbuild + MCUboot)
call C:\sgc\tools\buildfw_ota.bat %BD%>>"%HL%" 2>&1
findstr /c:"BUILD_EXIT_0" "%BD%.log" >nul
if errorlevel 1 goto :failbuild
echo === GATE build_exit PASS ===>>"%HL%"
rem 2) gate: signed image version in DFU manifest
findstr /c:"%VER%+0" "%BD%\dfu_application.zip_manifest.json" >nul
if errorlevel 1 goto :failmanifest
echo === GATE manifest PASS ===>>"%HL%"
rem 3) gate: version string inside signed hex (decoded, never findstr)
"%PY%" C:\sgc\tools\gate_version.py "%BD%\03_pull_proto\zephyr\zephyr.signed.hex" %VER%>>"%HL%" 2>&1
if errorlevel 1 goto :failhex
echo === GATE hexcontent PASS ===>>"%HL%"
rem 4) hexsafe copy
if not exist C:\sgc\hexsafe mkdir C:\sgc\hexsafe
copy /y "%BD%\03_pull_proto\zephyr\zephyr.signed.hex" "%HS%">nul
if errorlevel 1 goto :failhexsafe
echo === GATE hexsafe PASS %HS% ===>>"%HL%"
rem 5) flash FROM THE SAFE COPY
call C:\sgc\tools\zflash.bat %HS%>"%FL%" 2>&1
findstr /c:"Verified OK" "%FL%" >nul
if errorlevel 1 goto :failflash
echo === GATE flash PASS ===>>"%HL%"
rem 6) boot banner gate (reset + 10s capture)
call C:\sgc\bootcap.bat>nul 2>&1
findstr /c:"v%VER%" C:\sgc\boot2.log >nul
if errorlevel 1 goto :failbanner
echo === HARNESS %date% %time% PASS ver=%VER% bd=%BD% ===>>"%HL%"
exit /b 0
:failbuild
echo === HARNESS %date% %time% FAIL build (see %BD%.log) ===>>"%HL%"
exit /b 1
:failmanifest
echo === HARNESS %date% %time% FAIL manifest (want %VER%+0) ===>>"%HL%"
exit /b 1
:failhex
echo === HARNESS %date% %time% FAIL hexcontent ===>>"%HL%"
exit /b 1
:failhexsafe
echo === HARNESS %date% %time% FAIL hexsafe copy ===>>"%HL%"
exit /b 1
:failflash
echo === HARNESS %date% %time% FAIL flash (see %FL%) ===>>"%HL%"
exit /b 1
:failbanner
echo === HARNESS %date% %time% FAIL banner (v%VER% not in boot2.log) ===>>"%HL%"
exit /b 1
