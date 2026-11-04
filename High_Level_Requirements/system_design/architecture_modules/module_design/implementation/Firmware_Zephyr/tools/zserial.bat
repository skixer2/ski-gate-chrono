@echo off
rem SGC Zephyr serial console wrapper: zserial [port] [secs]
rem (py does not search PATH for script args - this makes it uniform with the bats)
rem
rem IMPORTANT - DO NOT change "py" to "python":
rem   py      -> Python 3.13 (Launcher) - HAS pyserial (the only dependency)
rem   python  -> WindowsApps Store alias -> DIFFERENT interpreter, NO pyserial
rem              -> "ModuleNotFoundError: No module named 'serial'"
rem
rem PATH NOTE: this machine's user PATH got clobbered (HKCU\Environment\Path
rem literally contains "%PATH%;C:\msys64\ucrt64\bin"), so fresh shells do NOT
rem have py on PATH. Resolve the launcher via LOCALAPPDATA (its standard
rem per-user location), fall back to PATH for other machines.
set "PYEXE=%LOCALAPPDATA%\Programs\Python\Launcher\py.exe"
if not exist "%PYEXE%" set "PYEXE=py"
"%PYEXE%" "%~dp0zserial.py" %*
