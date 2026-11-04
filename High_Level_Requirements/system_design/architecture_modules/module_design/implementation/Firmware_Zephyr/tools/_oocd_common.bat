@echo off
rem Common OpenOCD locator: sets OOCD (binary) + OOSCR (-s scripts dir or empty).
rem Order: PlatformIO xPack openocd (user profile), then openocd on PATH.
set "OOSCR="
set "PIOOC=%USERPROFILE%\.platformio\packages\tool-openocd"
if exist "%PIOOC%\bin\openocd.exe" (
set "OOCD=%PIOOC%\bin\openocd.exe"
set "OOSCR=-s %PIOOC%\share\openocd\scripts"
goto :eof
)
where openocd >nul 2>nul
if %errorlevel%==0 (
set "OOCD=openocd"
goto :eof
)
echo [ERROR] openocd not found: install PlatformIO ^(user profile^) or put openocd on PATH
exit /b 1
