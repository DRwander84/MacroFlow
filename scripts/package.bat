@echo off
setlocal EnableExtensions

rem MacroFlow packaging entry point.
rem Usage: scripts\package.bat -QtDir "...\msvc2022_64" -InnoDir "...\Inno Setup 6"

set "QT_DIR="
set "INNO_DIR="

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="-QtDir" (
    set "QT_DIR=%~2"
    shift
    shift
    goto parse
)
if /I "%~1"=="-InnoDir" (
    set "INNO_DIR=%~2"
    shift
    shift
    goto parse
)
echo ERROR: Unknown argument: %~1
echo Usage: %~nx0 -QtDir "Qt MSVC x64 directory" -InnoDir "Inno Setup 6 directory"
exit /b 2

:parsed
if not defined QT_DIR (
    echo ERROR: -QtDir is required.
    exit /b 2
)
if not defined INNO_DIR (
    echo ERROR: -InnoDir is required.
    exit /b 2
)

if not exist "%QT_DIR%\bin\windeployqt.exe" (
    echo ERROR: windeployqt.exe was not found under "%QT_DIR%\bin".
    exit /b 2
)

set "ISCC="
if exist "%INNO_DIR%\ISCC.exe" set "ISCC=%INNO_DIR%\ISCC.exe"
if exist "%INNO_DIR%\Compiler\ISCC.exe" set "ISCC=%INNO_DIR%\Compiler\ISCC.exe"
if /I "%INNO_DIR:~-8%"=="ISCC.exe" if exist "%INNO_DIR%" set "ISCC=%INNO_DIR%"
if not defined ISCC (
    echo ERROR: ISCC.exe not found. -InnoDir must be the Inno Setup 6 directory or ISCC.exe path.
    exit /b 2
)


set "ROOT=%CD%"
set "CONFIG=Release"
set "BUILD_DIR=%ROOT%\build\%CONFIG%"
set "STAGE=%ROOT%\out\portable\MacroFlow"
set "ZIP=%ROOT%\out\MacroFlow-Portable.zip"
set "ISS=%ROOT%\scripts\MacroFlow.iss"


 echo [1/4] Building MacroFlow...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\build.ps1" -QtDir "%QT_DIR%" -Config %CONFIG%
if errorlevel 1 exit /b %errorlevel%

 echo [2/4] Deploying Qt runtime...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\deploy.ps1" -QtDir "%QT_DIR%" -Config %CONFIG%
if errorlevel 1 exit /b %errorlevel%
if not exist "%BUILD_DIR%\MacroFlow.exe" (
    echo ERROR: MacroFlow.exe was not produced at "%BUILD_DIR%\MacroFlow.exe".
    exit /b 1
)

 echo [3/4] Preparing Portable package...
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"
if errorlevel 1 exit /b 1
xcopy "%BUILD_DIR%\*" "%STAGE%\" /E /I /H /Y /Q
if errorlevel 1 exit /b 1
copy /Y "%ROOT%\LICENSE" "%STAGE%\LICENSE" >nul
if errorlevel 1 exit /b 1
copy /Y "%ROOT%\NOTICE" "%STAGE%\NOTICE" >nul
if errorlevel 1 exit /b 1
if exist "%ZIP%" del /q "%ZIP%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\create-portable-archive.ps1" -SourceDir "%STAGE%" -OutputFile "%ZIP%"
if errorlevel 1 exit /b 1

 echo [4/4] Compiling Setup installer...
if not exist "%ROOT%\out\setup" mkdir "%ROOT%\out\setup"
"%ISCC%" /DSourceDir="%STAGE%" /DOutputDir="%ROOT%\out\setup" "%ISS%"
if errorlevel 1 exit /b %errorlevel%

echo.
echo Packaging completed successfully.
echo Portable folder: %STAGE%
echo Portable ZIP:    %ZIP%
echo Setup output:    %ROOT%\out\setup
exit /b 0
