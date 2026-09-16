@echo off
REM Warspear Bot - Build All
REM Requires: cl.exe in PATH (run vcvars32.bat first)

echo ============================================
echo    Warspear Bot - Build All Components
echo ============================================
echo.

set ROOT=%~dp0

REM --- Build DLL ---
echo [1/2] Building warspear-bot.dll ...
cd /d "%ROOT%"
cl /LD /EHsc /O2 /std:c++17 ^
    dllmain.cpp ^
    /Fe:warspear-bot.dll ^
    /link user32.lib gdi32.lib kernel32.lib

if %ERRORLEVEL% neq 0 (
    echo DLL BUILD FAILED!
    pause
    exit /b 1
)
echo DLL built successfully!
echo.

REM --- Build Controller ---
echo [2/2] Building warspear-controller.exe ...
cd /d "%ROOT%controller%"
cl /EHsc /O2 /std:c++17 ^
    main.cpp ^
    /Fe:"%ROOT%warspear-controller.exe" ^
    /link user32.lib gdi32.lib psapi.lib comctl32.lib

if %ERRORLEVEL% neq 0 (
    echo CONTROLLER BUILD FAILED!
    pause
    exit /b 1
)
echo Controller built successfully!
echo.

echo ============================================
echo    BUILD COMPLETE!
echo ============================================
echo.
echo   warspear-bot.dll          - Inject into game
echo   warspear-controller.exe   - GUI control panel
echo.
echo Usage:
echo   1. Start Warspear Online and login
echo   2. Run warspear-controller.exe
echo   3. Select warspear.exe in process list
echo   4. Click "Inject DLL"
echo   5. Use tabs to control bot features
echo.
echo Hotkeys (in-game):
echo   F1 = Toggle Auto Attack
echo   F2 = Toggle Auto Heal
echo   F3 = Toggle Follow Player
echo   F4 = Toggle Overlay
echo   F5 = Toggle Auto Collect
echo.

pause
