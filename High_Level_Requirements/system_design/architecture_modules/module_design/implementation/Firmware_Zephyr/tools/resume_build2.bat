@echo off
rem resume_build2: identical to resume_build.bat but log file %BD%_r2.log (b0259.log is lock-poisoned by a stale handle).
set "BD=%~1"
if "%BD%"=="" set "BD=C:\sgc\build\b0259"
cmd /c "C:\sgc\tools\zbuild_resume.bat C:\sgc\apps\03_pull_proto arduino_nicla_sense_me %BD% >%BD%_r2.log 2>&1 <NUL"
echo BUILD_EXIT_%errorlevel% >>%BD%_r2.log
