@echo off
rem SGC Zephyr build: zbuild.bat <app-dir> <board> <build-dir> [overlay]
rem Requires: nRF Connect SDK (default C:\ncs) installed via VS Code Toolchain Manager.
rem Override SDK root with NCS_DIR env var if installed elsewhere.
set "NCSROOT=%NCS_DIR%"
if "%NCSROOT%"=="" set "NCSROOT=C:\ncs"
rem Locate the west workspace (the SDK version dir containing .west)
set "WS="
for /d %%D in ("%NCSROOT%\*") do if exist "%%D\.west" set "WS=%%D"
if "%WS%"=="" (
echo [zbuild] ERROR: no west workspace ^(.west^) found under %NCSROOT%
goto end
)
rem Locate the toolchain env (any toolchains\* dir with environment.json)
set "TC="
for /d %%D in ("%NCSROOT%\toolchains\*") do if exist "%%D\environment.json" set "TC=%%D"
if "%TC%"=="" (
echo [zbuild] ERROR: no NCS toolchain found under %NCSROOT%\toolchains
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
rem Run west FROM the west workspace (apps may live anywhere, e.g. the repo)
cd /d %WS%
if exist "%BLD%" rmdir /s /q "%BLD%"
if "%4"=="" goto plain
west build -b %BRD% -d "%BLD%" "%SRC%" -- -DEXTRA_DTC_OVERLAY=%4
goto end
:plain
west build -b %BRD% -d "%BLD%" "%SRC%"
:end
