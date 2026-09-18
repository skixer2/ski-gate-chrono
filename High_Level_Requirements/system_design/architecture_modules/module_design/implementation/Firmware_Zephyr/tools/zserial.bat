@echo off
rem SGC Zephyr serial console wrapper: zserial [port] [secs]
rem (py does not search PATH for script args - this makes it uniform with the bats)
py "%~dp0zserial.py" %*
