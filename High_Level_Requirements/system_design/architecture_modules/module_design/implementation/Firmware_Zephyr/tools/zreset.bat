@echo off
rem SGC Zephyr hard reset via SWD (no serial port needed)
echo Hard reset via SWD...
C:\Users\v17ni\.platformio\packages\tool-openocd\bin\openocd.exe -s C:\Users\v17ni\.platformio\packages\tool-openocd\share\openocd\scripts -f interface/cmsis-dap.cfg -c "transport select swd" -f target/nordic/nrf52.cfg -c "init" -c "reset run" -c "shutdown"
