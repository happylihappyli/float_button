# build_run.ps1 - PowerShell 版 build.bat
$ErrorActionPreference = "Continue"

# 1) 设置 MSVC 环境
$vcvars = "D:\Code\VS2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
Write-Host "=== Loading MSVC env ===" -ForegroundColor Cyan
$envLines = & cmd.exe /c "`"$vcvars`" x64 > nul 2>&1 && set" 2>&1
foreach ($line in $envLines) {
    if ($line -match "^([^=]+)=(.*)$") {
        Set-Item -Path "Env:\$($matches[1])" -Value $matches[2]
    }
}

# 2) 切到项目根
Set-Location "E:\GitHub3\cpp\float_button"

# 3) 找 scons
$scons = (Get-Command scons.exe -ErrorAction SilentlyContinue).Source
if (-not $scons) {
    # 兜底用 Python 调
    $scons = (Get-Command python.exe -ErrorAction SilentlyContinue).Source
    if ($scons) {
        $scons = "python -m SCons.Script"
    }
}
Write-Host "=== Using scons: $scons ===" -ForegroundColor Cyan

# 4) 跑 scons
Write-Host "=== scons start ===" -ForegroundColor Green
& scons 2>&1 | ForEach-Object { Write-Host $_ }
Write-Host "=== scons end, exit $LASTEXITCODE ===" -ForegroundColor Green
