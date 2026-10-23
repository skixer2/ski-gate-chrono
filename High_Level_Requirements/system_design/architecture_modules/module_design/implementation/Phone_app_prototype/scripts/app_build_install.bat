@echo off
rem SGC app: pull VPS relay + build debug APK + install to connected phone.
rem Usage: app_build_install.bat [logname]
rem Logs to zio_claw\sgc_bench\logs\appbuild_<logname>.log; echoes APPBUILD_DONE or APPBUILD_FAILED at end.
setlocal
set "REPO=C:\Users\v17ni\Documents\projects\ski-gate-chrono"
set "APP=%REPO%\High_Level_Requirements\system_design\architecture_modules\module_design\implementation\Phone_app_prototype"
set "LOGDIR=C:\Users\v17ni\zio_claw\sgc_bench\logs"
if not exist "%LOGDIR%" mkdir "%LOGDIR%"
set "LOG=%LOGDIR%\appbuild_%1.log"
if "%1"=="" set "LOG=%LOGDIR%\appbuild_latest.log"
echo === %date% %time% === > "%LOG%"

cd /d "%REPO%" || goto fail
git pull --ff-only ZioClaw master >> "%LOG%" 2>&1 || goto fail
echo PULL_OK >> "%LOG%"
cd /d "%APP%" || goto fail
set "JAVA_HOME=C:\Program Files\Android\Android Studio\jbr"
call C:\Users\v17ni\develop\flutter\bin\flutter.bat build apk --debug >> "%LOG%" 2>&1 || goto fail
echo BUILD_OK >> "%LOG%"
set "ADB=C:\Users\v17ni\AppData\Local\Android\Sdk\platform-tools\adb.exe"
"%ADB%" install -r "%APP%\build\app\outputs\flutter-apk\app-debug.apk" >> "%LOG%" 2>&1
echo INSTALL_EXIT_%errorlevel% >> "%LOG%"
echo APPBUILD_DONE >> "%LOG%"
exit /b 0
:fail
echo APPBUILD_FAILED >> "%LOG%"
exit /b 1
