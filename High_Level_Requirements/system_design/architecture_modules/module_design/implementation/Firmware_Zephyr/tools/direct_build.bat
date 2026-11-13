@echo off
rem direct_build: cmake --build with the NCS toolchain PATH (ccache needs it). Toolchain: dcbdc366a1.
set "TC=C:\ncs\toolchains\dcbdc366a1"
set "PATH=%TC%\mingw64\bin;%TC%\bin;%TC%\opt\bin;%TC%\opt\bin\Scripts;%TC%\nrfutil\bin;%TC%\opt\zephyr-sdk\gnu\arm-zephyr-eabi\bin;C:\Windows\System32;C:\Windows"
C:\ncs\toolchains\dcbdc366a1\opt\bin\cmake.EXE --build C:/sgc/build/b0259 >C:\sgc\build\b0259_n2.log 2>&1 <NUL
echo BUILD_EXIT_%errorlevel% >>C:\sgc\build\b0259_n2.log
