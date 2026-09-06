@echo off
setlocal
set "CFG=%~1"
if "%CFG%"=="" set "CFG=Debug"
set "MSBUILD=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
set "ROOT=%~dp0.."
pushd "%ROOT%"
echo === Build %CFG% | x64 ===
"%MSBUILD%" ImageViewer.sln -t:Build -p:Configuration=%CFG% -p:Platform=x64 -m -v:minimal
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%