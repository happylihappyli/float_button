@echo off
call "D:\Code\VS2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>&1
dumpbin /headers "%~dp0bin\float_button.exe" | findstr /i "machine"
