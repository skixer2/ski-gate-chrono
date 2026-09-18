@echo off
rem SGC Zephyr flash: zflash.bat <path\to\image.hex>  (flash + verify + reset run)
if "%1"=="" (
echo Usage: zflash.bat path-to-image.hex
goto end
)
set "HEX=%~1"
set HEX=%HEX:\=/%
C:\Users\v17ni\.platformio\packages\tool-openocd\bin\openocd.exe -s C:\Users\v17ni\.platformio\packages\tool-openocd\share\openocd\scripts -f interface/cmsis-dap.cfg -c "transport select swd" -f target/nordic/nrf52.cfg -c "program %HEX% verify" -c "reset run" -c "shutdown"
:end
