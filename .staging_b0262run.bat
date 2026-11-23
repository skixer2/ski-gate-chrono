@echo off
call C:\sgc\tools\zbuild_resume.bat C:\sgc\apps\03_pull_proto arduino_nicla_sense_me C:\sgc\build\b0262 >C:\sgc\build\b0262.log 2>&1 <NUL
echo BUILD_EXIT_%errorlevel% >>C:\sgc\build\b0262.log
