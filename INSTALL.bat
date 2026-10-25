@echo off
chcp 65001 >nul
REM ================================================================
REM  INSTALL.bat - установить (или ОБНОВИТЬ) плагин.
REM  Берёт .dlu из output\<год>\ (после BUILD.bat) или из Contents\<год>\
REM  (готовый архив релиза). Ставит пакет в
REM    %ProgramData%\Autodesk\ApplicationPlugins\BleachRoSImport  (все пользователи, просит права)
REM    или %AppData%\Autodesk\ApplicationPlugins\BleachRoSImport  (без прав админа)
REM  Старая версия удаляется целиком и заменяется новой.
REM  3ds Max перед установкой нужно закрыть.
REM ================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "PKG=%~dp0package\BleachRoSImport"
set "DEST_ALL=%ALLUSERSPROFILE%\Autodesk\ApplicationPlugins\BleachRoSImport"
set "DEST_ME=%APPDATA%\Autodesk\ApplicationPlugins\BleachRoSImport"
set "STAGE=%TEMP%\BleachRoSImport_stage"
set "DEST="
if /i "%~1"=="/elevated" set BLEACH_ELEVATED=1

echo.
echo ================================================================
echo   BLEACH RoS Import - установка / обновление
echo ================================================================

REM ---- источник ----
set "SRC="
if exist "%~dp0output" set "SRC=%~dp0output"
if not defined SRC if exist "%~dp0Contents" set "SRC=%~dp0Contents"
if not defined SRC goto no_src
if not exist "%PKG%\PackageContents.xml" if exist "%~dp0PackageContents.xml" set "PKG=%~dp0"

REM ---- куда можно писать ----
mkdir "%ALLUSERSPROFILE%\Autodesk\ApplicationPlugins" >nul 2>&1
2>nul (>>"%ALLUSERSPROFILE%\Autodesk\ApplicationPlugins\.bleachprobe" (call )) && set "DEST=%DEST_ALL%"
del "%ALLUSERSPROFILE%\Autodesk\ApplicationPlugins\.bleachprobe" >nul 2>&1
if defined DEST goto have_dest
if defined BLEACH_ELEVATED goto fallback
echo Нужны права администратора для ProgramData - перезапуск с повышением ...
powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -ArgumentList '/elevated' -Verb RunAs -Wait" >nul 2>&1
if errorlevel 1 goto fallback
goto end
:fallback
echo Без прав администратора - ставлю только для текущего пользователя.
set "DEST=%DEST_ME%"
:have_dest
echo Куда: %DEST%

REM ---- 3ds Max держит старую .dlu? ----
set LOCKED=
if exist "%DEST%" for /R "%DEST%" %%f in (BleachRoSImport.dl*) do if /i "%%~xf"==".dlu" (2>nul (>>"%%f" (call )) || set "LOCKED=%%f")
if defined LOCKED goto locked

REM ---- собрать пакет ----
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\Contents" >nul 2>&1
copy /Y "%PKG%\PackageContents.xml" "%STAGE%\" >nul
set FOUND=0
for %%V in (2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026 2027) do (
  if exist "%SRC%\%%V\BleachRoSImport.dlu" (
    mkdir "%STAGE%\Contents\%%V" >nul 2>&1
    copy /Y "%SRC%\%%V\BleachRoSImport.dlu" "%STAGE%\Contents\%%V\" >nul
    set /a FOUND+=1
    echo       Max %%V
  )
)
if "%FOUND%"=="0" goto no_src
set "SCR=%~dp0scripts"
if not exist "%SCR%\BleachImport.mcr" set "SCR=%SRC%"
mkdir "%STAGE%\Contents\MacroScripts" "%STAGE%\Contents\Pre-Start-Up_Scripts" "%STAGE%\Contents\Post-Start-Up_Scripts" >nul 2>&1
copy /Y "%SCR%\BleachImport.mcr" "%STAGE%\Contents\MacroScripts\" >nul
copy /Y "%SCR%\BleachMenu_2025_2027.ms" "%STAGE%\Contents\Pre-Start-Up_Scripts\" >nul
copy /Y "%SCR%\BleachMenu_2016_2024.ms" "%STAGE%\Contents\Post-Start-Up_Scripts\" >nul

REM ---- заменить старую версию целиком ----
if exist "%DEST%" rmdir /s /q "%DEST%"
xcopy /E /I /Y /Q "%STAGE%" "%DEST%" >nul
if errorlevel 1 goto copy_failed
rmdir /s /q "%STAGE%" >nul 2>&1
if /i "%DEST%"=="%DEST_ALL%" if exist "%DEST_ME%\PackageContents.xml" rmdir /s /q "%DEST_ME%" >nul 2>&1
if not exist "%DEST%\PackageContents.xml" goto copy_failed

REM ---- Visual C++ Runtime (если лежит рядом - тихо ставим/обновляем) ----
if exist "%~dp0vc_redist.x64.exe" (
  echo Visual C++ Runtime ...
  "%~dp0vc_redist.x64.exe" /install /quiet /norestart
)

echo.
echo ГОТОВО (%FOUND% версий Max). Запусти 3ds Max: меню "Bleach Tool".
goto end

:no_src
echo ОШИБКА: нет собранных .dlu (output\^<год^>\ или Contents\^<год^>\). Сначала BUILD.bat.
goto end
:locked
echo ОШИБКА: файл занят - закрой 3ds Max и запусти ещё раз:
echo   %LOCKED%
goto end
:copy_failed
echo ОШИБКА копирования в %DEST%
goto end
:end
echo.
if defined BLEACH_NO_PAUSE goto out
pause
:out
endlocal
