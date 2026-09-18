@echo off
rem SGC Zephyr memory peek via SWD. Edit the mdw line for the address you need.
call "%~dp0_oocd_common.bat"
if errorlevel 1 goto end
%OOCD% %OOSCR% -f interface/cmsis-dap.cfg -c "transport select swd" -f target/nordic/nrf52.cfg -c "init" -c "reset halt" -c "mdw 0x10001200 4" -c "reset run" -c "shutdown"
