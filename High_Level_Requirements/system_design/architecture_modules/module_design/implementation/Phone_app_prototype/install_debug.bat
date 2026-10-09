@echo off
rem One-shot: install the freshly built debug APK (repo-relative, PC-side).
set "HERE=%~dp0"
set "ADB=C:\Users\v17ni\AppData\Local\Android\Sdk\platform-tools\adb.exe"
"%ADB%" install -r "%HERE%build\app\outputs\flutter-apk\app-debug.apk"
echo INSTALL_EXIT_%errorlevel%
