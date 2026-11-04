@echo off
rem zflash with full log capture (openocd output vanishes through node transport).
rem Usage: zflash_log.bat <builddir> [logname]
set "BD=%~1"
set "LOG=C:\Users\v17ni\zio_claw\sgc_bench\logs\zflash_%2.log"
if "%2"=="" set "LOG=C:\Users\v17ni\zio_claw\sgc_bench\logs\zflash_latest.log"
set "USERPROFILE=C:\Users\v17ni"
call C:\sgc\tools\zflash.bat "%BD%" > "%LOG%" 2>&1
echo ZFLASH_EXIT_%errorlevel% >> "%LOG%"
