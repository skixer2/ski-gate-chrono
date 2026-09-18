@echo off
rem SGC Zephyr build: zbuild.bat <sample-or-app-dir> <board> <build-dir> [overlay]
rem Requires: nRF Connect SDK v3.4.0 (C:\ncs) installed via VS Code Toolchain Manager
set "TC=C:\ncs\toolchains\dcbdc366a1"
set "PATH=%TC%\mingw64\bin;%TC%\bin;%TC%\opt\bin;%TC%\opt\bin\Scripts;%TC%\nrfutil\bin;%TC%\opt\zephyr-sdk\gnu\arm-zephyr-eabi\bin;C:\Windows\System32;C:\Windows"
set "ZEPHYR_TOOLCHAIN_VARIANT=zephyr/gnu"
set "ZEPHYR_SDK_INSTALL_DIR=%TC%\opt\zephyr-sdk"
set "PYTHONPATH=%TC%\opt\bin;%TC%\opt\bin\Lib;%TC%\opt\bin\Lib\site-packages"
set "NRFUTIL_HOME=%TC%\nrfutil\home"
set "SRC=%1"
set "BRD=%2"
set "BLD=%3"
cd /d %SRC%
if exist %BLD% rmdir /s /q %BLD%
if "%4"=="" goto plain
west build -b %BRD% -d %BLD% -- -DEXTRA_DTC_OVERLAY=%4
goto end
:plain
west build -b %BRD% -d %BLD%
:end
