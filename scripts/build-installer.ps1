# 2026-09-12
# 功能：基于 package-release.ps1 的独立运行目录生成 Inno Setup 安装版 EXE 与 SHA-256。
# 目的：让 GitHub Release 同时提供便携 ZIP 和带版本号的 EXE 安装包。
param(
    [string]$PackageDir = "",
    [string]$OutputDir = "dist",
    [string]$InnoSetupDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

if (!$PackageDir) { throw "请通过 -PackageDir 指定 package-release.ps1 输出的独立运行目录。" }
$PackageDir = (Resolve-Path -LiteralPath $PackageDir).Path
if (!(Test-Path -LiteralPath (Join-Path $PackageDir "ImageViewer.exe"))) {
    throw "PackageDir 必须包含 ImageViewer.exe。"
}

$versionText = Get-Content -LiteralPath (Join-Path $root "src/app/AppVersion.h") -Raw
if ($versionText -notmatch 'QString\("(\d+\.\d+\.\d+)"\)') { throw "无法读取版本号。" }
$version = $Matches[1]

# 定位 Inno Setup 编译器：参数 > 常见安装路径 > PATH。
$isccCandidates = @(
    $InnoSetupDir,
    "${env:ProgramFiles(x86)}\Inno Setup 6",
    "$env:ProgramFiles\Inno Setup 6"
) | Where-Object { $_ }
$iscc = $null
foreach ($candidate in $isccCandidates) {
    $path = Join-Path $candidate "ISCC.exe"
    if (Test-Path -LiteralPath $path) { $iscc = $path; break }
}
if (!$iscc) {
    $found = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($found) { $iscc = $found.Source }
}
if (!$iscc) { throw "未找到 Inno Setup 6（ISCC.exe）。请安装后重试，或通过 -InnoSetupDir 指定。" }

$outputRoot = if ([IO.Path]::IsPathRooted($OutputDir)) { $OutputDir } else { Join-Path $root $OutputDir }
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

$setupName = "ImageViewer-$version-windows-x64-setup.exe"
& $iscc "/DAppVersion=$version" "/DSourceDir=$PackageDir" "/DOutputDir=$outputRoot" "/DRoot=$root" `
    (Join-Path $PSScriptRoot "installer.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup 编译失败。" }

$setup = Join-Path $outputRoot $setupName
if (!(Test-Path -LiteralPath $setup)) { throw "未找到安装包输出：$setup" }
$hash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $setupName" | Set-Content -LiteralPath "$setup.sha256" -Encoding ASCII

Write-Host "安装包：$setup"
Write-Host "校验值：$setup.sha256"
