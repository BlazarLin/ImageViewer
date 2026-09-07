param(
    [ValidateSet("Debug", "Release")]
    [string]$Cfg = "Debug"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
if (!(Test-Path -LiteralPath $vswhere)) {
    throw "未找到 vswhere，请安装 VS2019 C++ 桌面开发工具。"
}
$msbuild = & $vswhere -version '[16.0,17.0)' -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (!$msbuild) {
    throw "未找到 VS2019 MSBuild，请安装 v142 工具集与 Windows SDK。"
}
# MSBuild 根据已安装的工具集和 SDK 配置头文件、库路径，不覆盖环境变量。
Push-Location $root
try {
    Write-Host "构建 $Cfg | x64" -ForegroundColor Cyan
    & $msbuild ImageViewer.sln -t:Build -p:Configuration=$Cfg -p:Platform=x64 -m -v:minimal
    $buildExitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $buildExitCode
