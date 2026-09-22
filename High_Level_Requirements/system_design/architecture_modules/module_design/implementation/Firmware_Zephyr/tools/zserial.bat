@echo off
rem SGC Zephyr serial console wrapper: zserial [port] [secs]
rem (py does not search PATH for script args - this makes it uniform with the bats)
rem
rem IMPORTANT - DO NOT change "py" to "python":
rem   py      -> Python 3.13 (Launcher) - HAS pyserial (the only dependency)
rem   python  -> WindowsApps Store alias -> DIFFERENT interpreter, NO pyserial
rem              -> "ModuleNotFoundError: No module named 'serial'"
py "%~dp0zserial.py" %*
