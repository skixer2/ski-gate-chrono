@echo off
rem SGC Zephyr hard reset via SWD (no serial port needed)
call "%~dp0_oocd_common.bat"
if errorlevel 1 goto end
echo Hard reset via SWD...
%OOCD% %OOSCR% -f interface/cmsis-dap.cfg -c "transport select swd" -f target/nordic/nrf52.cfg -c "init" -c "reset run" -c "shutdown"
