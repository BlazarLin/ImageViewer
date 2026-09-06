@echo off
setlocal
set "QT=C:\Qt\Qt5.14.2\5.14.2\msvc2017_64"
set "CL=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30037\bin\Hostx64\x64\cl.exe"
set "ROOT=%~dp0.."
pushd "%ROOT%"
"%CL%" /nologo /EHsc /MD /std:c++17 /Zc:__cplusplus ^
    /I "%QT%\include" /I "%QT%\include\QtCore" ^
    /c test_qobj.cpp 2>&1
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%