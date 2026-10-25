@echo off
chcp 65001 >nul
REM Удалить плагин из обоих мест установки (3ds Max должен быть закрыт).
set "A=%ALLUSERSPROFILE%\Autodesk\ApplicationPlugins\BleachRoSImport"
set "B=%APPDATA%\Autodesk\ApplicationPlugins\BleachRoSImport"
if exist "%B%" rmdir /s /q "%B%"
if exist "%A%" rmdir /s /q "%A%" 2>nul
if exist "%A%" powershell -NoProfile -Command "Start-Process cmd -ArgumentList '/c rmdir /s /q \"%A%\"' -Verb RunAs -Wait"
echo Удалено.
pause
