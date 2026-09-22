@echo off
rem SGC Zephyr flash: zflash.bat <build-dir OR exact-hex-path>
rem Accepts either a build dir (finds zephyr.hex inside, first match) or the
rem exact hex path. NCS 3.4 sysbuild nests the app domain one level down:
rem   <build>\<app>\zephyr\zephyr.hex
rem e.g. C:/sgc/build/02_ble_hello/02_ble_hello/zephyr/zephyr.hex
rem so "zflash C:/sgc/build/02_ble_hello" is enough.
set "ARG=%~1"
if "%ARG%"=="" (
echo Usage: zflash.bat ^<build-dir^>  or  zflash.bat path-to-image.hex
goto end
)
set "HEX="
if exist "%ARG%\*" (
rem ARG is a directory: search for zephyr.hex below it
for /f "delims=" %%H in ('dir /s /b "%ARG%\zephyr.hex" 2^>nul') do if not defined HEX set "HEX=%%H"
) else (
set "HEX=%ARG%"
)
if not defined HEX (
echo [zflash] ERROR: no zephyr.hex found under %ARG%
goto end
)
call "%~dp0_oocd_common.bat"
if errorlevel 1 goto end
echo [zflash] flashing %HEX%
set HEX=%HEX:\=/%
%OOCD% %OOSCR% -f interface/cmsis-dap.cfg -c "transport select swd" -f target/nordic/nrf52.cfg -c "program %HEX% verify" -c "reset run" -c "shutdown"
:end
