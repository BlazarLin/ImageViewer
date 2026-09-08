param(
    [string]$BinaryDir = "bin/Release",
    [string]$QtDir = $env:QTDIR,
    [string]$DependencySourceDir = "",
    [string]$VCRedistDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (!$QtDir -or !(Test-Path -LiteralPath (Join-Path $QtDir "bin/windeployqt.exe"))) {
    throw "请通过 QTDIR 或 -QtDir 指定与 EXE 匹配的 Qt 安装目录。"
}
if (!$VCRedistDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsPath) {
            $redistVersion = Get-ChildItem -LiteralPath (Join-Path $vsPath "VC/Redist/MSVC") -Directory |
                Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
                Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
            if ($redistVersion) {
                $crt = Get-ChildItem -LiteralPath (Join-Path $redistVersion.FullName "x64") -Directory -Filter "Microsoft.VC*.CRT" | Select-Object -First 1
                if ($crt) { $VCRedistDir = $crt.FullName }
            }
        }
    }
}
if (!$VCRedistDir -or !(Test-Path -LiteralPath (Join-Path $VCRedistDir "vcruntime140.dll"))) {
    throw "未找到 MSVC x64 CRT，请安装 C++ 工具集或指定 -VCRedistDir。"
}
$binaryRoot = if ([IO.Path]::IsPathRooted($BinaryDir)) { $BinaryDir } else { Join-Path $root $BinaryDir }
$binaryRoot = (Resolve-Path -LiteralPath $binaryRoot).Path
$exe = Join-Path $binaryRoot "ImageViewer.exe"
$opencv = @(Get-ChildItem -LiteralPath $binaryRoot -Filter "opencv_world*.dll" |
    Where-Object { $_.Name -match '^opencv_world\d+\.dll$' })
if (!(Test-Path -LiteralPath $exe) -or $opencv.Count -ne 1) {
    throw "目录必须包含 Release EXE 和一份非 Debug opencv_world DLL。"
}
$versionText = Get-Content -LiteralPath (Join-Path $root "src/app/AppVersion.h") -Raw
if ($versionText -notmatch 'QString\("(\d+\.\d+\.\d+)"\)') { throw "无法读取版本号。" }
$version = $Matches[1]
$name = "ImageViewer-$version-windows-x64"
$dist = Join-Path $root "dist"
$stage = Join-Path $dist ("stage-" + [Guid]::NewGuid().ToString("N"))
$package = Join-Path $stage $name
New-Item -ItemType Directory -Path $package -Force | Out-Null
Copy-Item -LiteralPath $exe -Destination $package
Copy-Item -LiteralPath $opencv[0].FullName -Destination $package
Copy-Item -LiteralPath (Join-Path $binaryRoot "translations") -Destination $package -Recurse
& (Join-Path $QtDir "bin/windeployqt.exe") --release --no-compiler-runtime --no-translations --no-opengl-sw --dir $package (Join-Path $package "ImageViewer.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt 部署失败。" }
Get-ChildItem -LiteralPath $VCRedistDir -Filter "*.dll" | Copy-Item -Destination $package
foreach ($file in @("README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", "CHANGELOG.md", "CONTRIBUTING.md", "SECURITY.md")) {
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination $package
}
Copy-Item -LiteralPath (Join-Path $root "licenses") -Destination $package -Recurse
Copy-Item -LiteralPath (Join-Path $root "docs") -Destination $package -Recurse
if ($DependencySourceDir) {
    $sourceRoot = (Resolve-Path -LiteralPath $DependencySourceDir).Path
    foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File |
        Where-Object { $_.Name -match '^(LICENSE|COPYING|NOTICE)|^qt_attribution\.json$' }) {
        $relative = $file.FullName.Substring($sourceRoot.Length).TrimStart('\', '/')
        $target = Join-Path (Join-Path $package "licenses/dependency-sources") $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
}
$qtVersion = & (Join-Path $QtDir "bin/qmake.exe") -query QT_VERSION
if ($LASTEXITCODE -ne 0) { throw "无法读取 Qt 版本。" }
$revision = git -C $root rev-parse HEAD
$dirty = [bool](git -C $root status --porcelain)
[ordered]@{
    version = $version
    revision = $revision
    workingTreeModified = $dirty
    qtVersion = $qtVersion
    opencvRuntime = $opencv[0].Name
    builtAtUtc = [DateTime]::UtcNow.ToString("o")
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package "build-info.json") -Encoding UTF8
foreach ($required in @("Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll", "vcruntime140.dll", "msvcp140.dll", "platforms/qwindows.dll", "imageformats/qjpeg.dll", "translations/ImageViewer_en_US.qm")) {
    if (!(Test-Path -LiteralPath (Join-Path $package $required))) { throw "发布包缺失：$required" }
}
$archive = Join-Path $dist "$name.zip"
Compress-Archive -LiteralPath $package -DestinationPath $archive -Force
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $name.zip" | Set-Content -LiteralPath "$archive.sha256" -Encoding ASCII
Write-Host "便携包：$archive"
Write-Host "校验值：$archive.sha256"
Write-Host "独立运行目录：$package"
