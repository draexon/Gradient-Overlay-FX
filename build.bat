@echo off
rem Configure and build Inner Glow Effect on Windows (x64).
rem
rem Requires: CMake 3.20+, Visual Studio 2022 with the Desktop C++ workload,
rem and AE_SDK_PATH pointing at the After Effects SDK root.
rem
rem   build.bat            builds Release
rem   build.bat Debug      builds Debug
rem   build.bat Release install    builds and copies the .aex into the AE plug-ins folder
rem                                (needs an elevated prompt: it writes to Program Files)

setlocal
cd /d "%~dp0"

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

if "%AE_SDK_PATH%"=="" (
	echo.
	echo AE_SDK_PATH is not set.
	echo.
	echo Download the After Effects SDK from https://developer.adobe.com/after-effects/
	echo unpack it, then point AE_SDK_PATH at the folder that directly contains Examples\
	echo.
	echo   setx AE_SDK_PATH "C:\path\to\AfterEffectsSDK"
	echo.
	echo Open a new terminal after running setx, then run build.bat again.
	echo.
	exit /b 1
)

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1

cmake --build build --config %CONFIG%
if errorlevel 1 exit /b 1

if /i "%2"=="install" (
	cmake --install build --config %CONFIG%
	if errorlevel 1 exit /b 1
)

echo.
echo Built: build\%CONFIG%\InnerGlowEffect.aex
echo.
