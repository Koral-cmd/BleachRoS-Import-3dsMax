@echo off
chcp 65001 >nul
REM ================================================================
REM  BUILD_RELEASE.bat - из output\<год>\ собрать:
REM    dist\BleachRoSImport-<версия>.zip   (распаковал -> INSTALL.bat)
REM    dist\BleachRoSImport-<версия>-Setup.exe (если есть Inno Setup 6)
REM ================================================================
setlocal
cd /d "%~dp0.."
set "VER=1.3.0"
set "STAGE=dist\stage"
if not exist output goto no_output
if exist dist rmdir /s /q dist
mkdir "%STAGE%\Contents\MacroScripts" "%STAGE%\Contents\Pre-Start-Up_Scripts" "%STAGE%\Contents\Post-Start-Up_Scripts" >nul 2>&1
copy /Y package\BleachRoSImport\PackageContents.xml "%STAGE%\" >nul
for %%V in (2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026 2027) do (
  if exist "output\%%V\BleachRoSImport.dlu" (
    mkdir "%STAGE%\Contents\%%V" >nul 2>&1
    copy /Y "output\%%V\BleachRoSImport.dlu" "%STAGE%\Contents\%%V\" >nul
  )
)
copy /Y scripts\BleachImport.mcr "%STAGE%\Contents\MacroScripts\" >nul
copy /Y scripts\BleachMenu_2025_2027.ms "%STAGE%\Contents\Pre-Start-Up_Scripts\" >nul
copy /Y scripts\BleachMenu_2016_2024.ms "%STAGE%\Contents\Post-Start-Up_Scripts\" >nul

REM ZIP: структура пакета + INSTALL.bat/UNINSTALL.bat рядом (Contents\<год> + скрипты)
mkdir "dist\zip" >nul 2>&1
xcopy /E /I /Y /Q "%STAGE%\Contents" "dist\zip\Contents" >nul
copy /Y "%STAGE%\PackageContents.xml" "dist\zip\" >nul
copy /Y scripts\*.m* "dist\zip\Contents\" >nul
copy /Y INSTALL.bat "dist\zip\" >nul
copy /Y UNINSTALL.bat "dist\zip\" >nul
powershell -NoProfile -Command "Compress-Archive -Path 'dist\zip\*' -DestinationPath 'dist\BleachRoSImport-%VER%.zip' -Force"
echo ZIP: dist\BleachRoSImport-%VER%.zip

set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" (
  "%ISCC%" installer\BleachRoSImport.iss
  echo Setup: dist\BleachRoSImport-%VER%-Setup.exe
) else (
  echo Inno Setup 6 не найден - Setup.exe пропущен, ZIP готов.
)
powershell -NoProfile -Command "Get-ChildItem dist\*.zip, dist\*.exe | Get-FileHash -Algorithm SHA256 | ForEach-Object { $_.Hash + '  ' + (Split-Path $_.Path -Leaf) } | Out-File -Encoding ascii dist\SHA256SUMS.txt"
goto end
:no_output
echo Нет output\ - сначала BUILD.bat
:end
pause
