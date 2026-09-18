@echo off
rem SGC Zephyr flash: zflash.bat <path\to\image.hex>  (flash + verify + reset run)
if "%1"=="" (
echo Usage: zflash.bat path-to-image.hex
goto end
)
call "%~dp0_oocd_common.bat"
if errorlevel 1 goto end
set "HEX=%~1"
set HEX=%HEX:\=/%
%OOCD% %OOSCR% -f interface/cmsis-dap.cfg -c "transport select swd" -f target/nordic/nrf52.cfg -c "program %HEX% verify" -c "reset run" -c "shutdown"
:end
