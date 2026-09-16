@echo off
REM Warspear Bot - Build Script
REM Requires Visual Studio Build Tools (cl.exe in PATH)

echo === Warspear Bot Builder ===
echo.

REM Check for compiler
where cl.exe >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: cl.exe not found!
    echo Open "Developer Command Prompt for VS" or run:
    echo   "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
    pause
    exit /b 1
)

echo Compiling warspear-bot.dll...
cl /LD /EHsc /O2 /std:c++17 ^
    dllmain.cpp ^
    /Fe:warspear-bot.dll ^
    /link user32.lib gdi32.lib kernel32.lib

if %ERRORLEVEL% equ 0 (
    echo.
    echo BUILD SUCCESSFUL: warspear-bot.dll
    echo.
    echo To inject:
    echo   1. Open Cheat Engine
    echo   2. Attach to warspear.exe (PID of game client)
    echo   3. Memory View ^> Tools ^> Inject DLL
    echo   4. Select warspear-bot.dll
    echo.
    echo Hotkeys:
    echo   F1 = Toggle Auto Attack
    echo   F2 = Toggle Auto Heal
    echo   F3 = Toggle Follow Player
    echo   F4 = Toggle Overlay
    echo   F12 = Disable all
) else (
    echo.
    echo BUILD FAILED!
)

pause
