@echo off
setlocal

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"

if errorlevel 1 (
    echo ERRO: Falha ao inicializar o ambiente do Visual Studio.
    pause
    exit /b 1
)

cd /d "C:\Users\Admin\Documents\GitHub\WS-BOT"

if errorlevel 1 (
    echo ERRO: Nao foi possivel acessar a pasta do projeto.
    pause
    exit /b 1
)

echo.
echo === Compilando warspear-controller.exe ===

cl /nologo /O2 /EHsc /MT ^
    controller\main.cpp ^
    /Fe:warspear-controller.exe ^
    /link ^
    user32.lib ^
    gdi32.lib ^
    kernel32.lib ^
    comctl32.lib ^
    comdlg32.lib ^
    psapi.lib

set "BUILD_EXIT_CODE=%ERRORLEVEL%"

echo.
if "%BUILD_EXIT_CODE%"=="0" (
    echo BUILD CONCLUIDO COM SUCESSO.
    echo Arquivo: %CD%\warspear-controller.exe
) else (
    echo BUILD FALHOU.
)

echo BUILD_EXIT_CODE=%BUILD_EXIT_CODE%
pause
exit /b %BUILD_EXIT_CODE%