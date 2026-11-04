@echo off
rem Build the SGC debug APK with the Android Studio JBR as JAVA_HOME.
set "JAVA_HOME=C:\Program Files\Android\Android Studio\jbr"
cd /d C:\Users\v17ni\Documents\projects\ski-gate-chrono\High_Level_Requirements\system_design\architecture_modules\module_design\implementation\Phone_app_prototype
call C:\Users\v17ni\develop\flutter\bin\flutter.bat build apk --debug
