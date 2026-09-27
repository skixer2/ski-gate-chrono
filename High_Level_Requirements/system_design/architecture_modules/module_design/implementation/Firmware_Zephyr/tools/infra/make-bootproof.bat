@echo off
rem make-bootproof.bat — ONE-TIME setup of the boot-proof bench harness.
rem RUN AS ADMINISTRATOR (right-click -> Run as administrator).
rem Registers the watchdog as a BOOT task (SYSTEM, no login needed),
rem cleans up session-bound copies of the daemons, and starts it now.
rem Undo:  schtasks /delete /tn SGC-WatchdogBoot /f

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: please run this as Administrator.
    pause
    exit /b 1
)

echo [1/4] Stopping session-bound copies (they would fight the task)...
taskkill /f /im ssh.exe 2>nul
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='powershell.exe'\" | Where-Object {$_.CommandLine -like '*node-watchdog*'} | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }" 2>nul
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='python.exe' or Name='py.exe'\" | Where-Object {$_.CommandLine -like '*http.server*'} | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }" 2>nul

echo [2/4] Removing the old logon task if present...
schtasks /delete /tn SGC-NodeWatchdog /f 2>nul

echo [3/4] Registering the boot task (SYSTEM, onstart)...
schtasks /create /tn SGC-WatchdogBoot /tr "powershell -NoProfile -ExecutionPolicy Bypass -File C:\sgc\tools\infra\node-watchdog.ps1" /sc onstart /ru SYSTEM /rl highest /f
if %errorlevel% neq 0 (
    echo ERROR: task creation failed.
    pause
    exit /b 1
)

echo [4/4] (Re)starting it now (no reboot needed)...
schtasks /end /tn SGC-WatchdogBoot 2>nul
schtasks /run /tn SGC-WatchdogBoot
timeout /t 5 >nul
schtasks /query /tn SGC-WatchdogBoot

echo.
echo DONE. From the next boot on, the bench harness self-restores
echo with nobody logged in. Log: C:\Users\v17ni\node-watchdog.log
pause
