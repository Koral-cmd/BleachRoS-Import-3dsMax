@echo off
chcp 65001 >nul
REM ================================================================
REM  BUILD.bat - собрать BleachRoSImport.dlu для каждого установленного
REM  3ds Max SDK (или только для одного года: BUILD.bat 2027) и сразу
REM  установить (INSTALL.bat). Лог: BUILD.log
REM  Нужно: Visual Studio 2022/2026 с C++ и CMake,
REM         C:\Program Files\Autodesk\3ds Max <год> SDK\maxsdk
REM ================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "LOG=%~dp0BUILD.log"
set "OUTPUT=%~dp0output"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "CMAKE="
set "GEN=Visual Studio 17 2022"
set "BUILT=0"
set "ONLY=%~1"
echo BUILD.bat %DATE% %TIME%> "%LOG%"
echo.
echo ================================================================
echo   BLEACH RoS Import - сборка и установка
echo ================================================================

where cmake >nul 2>&1
if not errorlevel 1 (set "CMAKE=cmake" & goto have_cmake)
if not exist "%VSWHERE%" goto no_cmake
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath 2^>nul`) do (
  if exist "%%i\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=%%i\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
if "%CMAKE%"=="" goto no_cmake
:have_cmake
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version [18.0^,19.0^) -property installationPath 2^>nul`) do set "GEN=Visual Studio 18 2026"

echo [1/2] Сборка (%GEN%) ...
if not "%ONLY%"=="" (call :build_one %ONLY%) else (
  for %%V in (2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026 2027) do call :build_one %%V
)
if "%BUILT%"=="0" goto no_sdk

echo.
echo [2/2] Установка ...
set BLEACH_NO_PAUSE=1
call "%~dp0INSTALL.bat"
set BLEACH_NO_PAUSE=
echo.
echo Готово: собрано версий Max: %BUILT%. Лог: %LOG%
goto end

:build_one
set "VER=%~1"
set "SDK=C:\Program Files\Autodesk\3ds Max %VER% SDK\maxsdk"
if not exist "%SDK%\include\max.h" goto :eof
echo       Max %VER% ...
if not exist "build_%VER%\CMakeCache.txt" (
  "%CMAKE%" -S . -B "build_%VER%" -G "%GEN%" -A x64 -DBLEACH_BUILD_PLUGIN=ON -DBLEACH_BUILD_TOOL=OFF -D3DSMAX_SDK_DIR="%SDK%" -DMAX_VERSION=%VER% >> "%LOG%" 2>&1
  if errorlevel 1 goto build_failed
)
"%CMAKE%" --build "build_%VER%" --config Release >> "%LOG%" 2>&1
if errorlevel 1 goto build_failed
mkdir "%OUTPUT%\%VER%" >nul 2>&1
set "DLU="
if exist "build_%VER%\bin\BleachRoSImport.dlu" set "DLU=build_%VER%\bin\BleachRoSImport.dlu"
if not defined DLU if exist "build_%VER%\bin\Release\BleachRoSImport.dlu" set "DLU=build_%VER%\bin\Release\BleachRoSImport.dlu"
if not defined DLU goto build_failed
copy /Y "%DLU%" "%OUTPUT%\%VER%\BleachRoSImport.dlu" >nul
echo             ok -^> output\%VER%\BleachRoSImport.dlu
set /a BUILT+=1
goto :eof

:build_failed
if exist "%OUTPUT%\%VER%\BleachRoSImport.dlu" del /q "%OUTPUT%\%VER%\BleachRoSImport.dlu"
echo             ОШИБКА СБОРКИ. Строки с ошибками:
findstr /R /C:"error C" /C:"error LNK" /C:"error MSB" /C:"error RC" "%LOG%" 2>nul
goto :eof

:no_cmake
echo CMake не найден - установи Visual Studio с компонентами C++ и CMake.
goto end
:no_sdk
echo Не найден ни один 3ds Max SDK в C:\Program Files\Autodesk\3ds Max ^<год^> SDK\maxsdk
goto end
:end
echo.
if not "%BLEACH_NO_PAUSE%"=="1" pause
