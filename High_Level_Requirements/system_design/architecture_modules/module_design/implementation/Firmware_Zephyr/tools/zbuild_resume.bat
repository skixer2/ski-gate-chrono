@echo off
rem zbuild_resume: zbuild_ota.bat WITHOUT the rmdir wipe - incremental resume across invoke kills.
rem Args: <app-dir> <board> <build-dir>   (sysbuild + MCUboot, same env setup as zbuild_ota)
set "NCSROOT=%NCS_DIR%"
if "%NCSROOT%"=="" set "NCSROOT=C:\ncs"
set "WS="
for /d %%D in ("%NCSROOT%\*") do if not defined WS if exist "%%D\.west" set "WS=%%D"
if "%WS%"=="" (
echo [zbuild_resume] ERROR: no west workspace found under %NCSROOT%
goto end
)
set "TC="
for /d %%D in ("%NCSROOT%\toolchains\*") do if exist "%%D\environment.json" set "TC=%%D"
if "%TC%"=="" (
echo [zbuild_resume] ERROR: no NCS toolchain found under %NCSROOT%\toolchains
goto end
)
set "PATH=%TC%\mingw64\bin;%TC%\bin;%TC%\opt\bin;%TC%\opt\bin\Scripts;%TC%\nrfutil\bin;%TC%\opt\zephyr-sdk\gnu\arm-zephyr-eabi\bin;C:\Windows\System32;C:\Windows"
set "ZEPHYR_TOOLCHAIN_VARIANT=zephyr/gnu"
set "ZEPHYR_SDK_INSTALL_DIR=%TC%\opt\zephyr-sdk"
set "PYTHONPATH=%TC%\opt\bin;%TC%\opt\bin\Lib;%TC%\opt\bin\Lib\site-packages"
set "NRFUTIL_HOME=%TC%\nrfutil\home"
set "SRC=%~f1"
set "BRD=%2"
set "BLD=%~f3"
set GIT_CONFIG_COUNT=1
set GIT_CONFIG_KEY_0=safe.directory
set GIT_CONFIG_VALUE_0=*
cd /d %WS%
west -z "%WS%\zephyr" build --sysbuild -b %BRD% -d "%BLD%" "%SRC%" -- -DSB_CONFIG_BOOTLOADER_MCUBOOT=y
:end
exit /b %errorlevel%
