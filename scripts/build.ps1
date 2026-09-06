param(
    [string]$Cfg = "Debug"
)

$ErrorActionPreference = "Stop"
$msbuild = "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
$root = Split-Path -Parent $PSScriptRoot

# 手动设置 MSVC / Windows SDK 头路径(避免 vswhere 缺失导致 vcvars64 失败)
$vcRoot = "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30037"
$sdkRoot = "C:\Program Files (x86)\Windows Kits\10"
$sdkVer = "10.0.19041.0"
$vcInclude = "$vcRoot\include"
$vcLib = "$vcRoot\lib\x64"
$sdkUcrtInclude = "$sdkRoot\Include\$sdkVer\ucrt"
$sdkUcrtLib = "$sdkRoot\Lib\$sdkVer\ucrt\x64"
$sdkUmInclude = "$sdkRoot\Include\$sdkVer\um"
$sdkUmLib = "$sdkRoot\Lib\$sdkVer\um\x64"
$sdkSharedInclude = "$sdkRoot\Include\$sdkVer\shared"

$env:INCLUDE = "$vcInclude;$sdkUcrtInclude;$sdkUmInclude;$sdkSharedInclude"
$env:LIB = "$vcLib;$sdkUcrtLib;$sdkUmLib"
$env:LIBPATH = "$vcLib"

Set-Location $root

Write-Host "=== Build $Cfg | x64 ===" -ForegroundColor Cyan
& $msbuild ImageViewer.sln -t:Build -p:Configuration=$Cfg -p:Platform=x64 -m -v:minimal
$rc = $LASTEXITCODE
Write-Host "=== ExitCode: $rc ===" -ForegroundColor Yellow
exit $rc