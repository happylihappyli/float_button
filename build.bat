@echo off
REM build.bat - Windows floating button build script
REM Setup VS2022 environment then run scons

setlocal

call "D:\Code\VS2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo [build.bat] vcvarsall.bat failed
    exit /b 1
)

cd /d "%~dp0"

echo === Build start ===
scons %*
echo === Build end, exit code %errorlevel% ===
exit /b %errorlevel%
