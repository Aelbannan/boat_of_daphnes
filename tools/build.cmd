@echo off
rem Builds a target (default: tww) in Visual Studio's environment with its own CMake (clang-cl is on
rem its PATH; devkitPro's MSYS2 cmake, also on PATH here, must not touch this build), configuring
rem first if needed - a new source file makes CMake reconfigure, which needs the compiler found.
rem   tools\build.cmd [target]
setlocal
set TARGET=%1
if "%TARGET%"=="" set TARGET=tww
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
cd /d "%~dp0.."
if not exist build\windows-clang\CMakeFiles\rules.ninja "%CMAKE%" --preset windows-clang || exit /b 1
"%CMAKE%" --build build\windows-clang --target %TARGET%
