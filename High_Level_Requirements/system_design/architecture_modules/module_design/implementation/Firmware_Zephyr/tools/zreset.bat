@echo off
rem One-shot SWD reset (for boot-capture with concurrent serial listener).
rem Usage: zreset.bat [halt|run]
set "MODE=%~1"
if "%MODE%"=="" set "MODE=run"
call C:\sgc\tools\_oocd_common.bat
if errorlevel 1 exit /b 1
%OOCD% %OOSCR% -f interface/cmsis-dap.cfg -c "transport select swd" -f target/nordic/nrf52.cfg -c "init" -c "reset %MODE%" -c "shutdown"
