@echo off
rem resume_build: build leg for invoke-resume protocol. Args: <builddir>
set "BD=%~1"
if "%BD%"=="" set "BD=C:\sgc\build\b0258"
cmd /c "C:\sgc\tools\zbuild_resume.bat C:\sgc\apps\03_pull_proto arduino_nicla_sense_me %BD% >%BD%.log 2>&1 <NUL"
echo BUILD_EXIT_%errorlevel% >>%BD%.log
