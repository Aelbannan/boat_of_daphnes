@echo off
rem tools\build_target.cmd <target> [ninja args]: builds one target in Visual Studio's environment.
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
cd /d "%~dp0.."
"%CMAKE%" --build build\windows-clang --target %* 
