@echo off
setlocal

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"

if errorlevel 1 (
    echo ERRO: Falha ao inicializar o ambiente do Visual Studio.
    pause
    exit /b 1
)

cd /d "C:\Users\Admin\Documents\GitHub\WS-BOT"

echo.
echo === Compilando recursos (icone) ===

rc /nologo controller\warspear.rc
if errorlevel 1 (
    echo AVISO: falha ao compilar o .rc - exe fica sem icone embutido.
)

echo.
echo === Compilando warspear-controller.exe ===

cl /nologo /O2 /EHsc /MTd /GS- ^
    controller\main.cpp ^
    controller\warspear.res ^
    /Fe:warspear-controller.exe ^
    /link ^
    user32.lib ^
    gdi32.lib ^
    kernel32.lib ^
    comctl32.lib ^
    comdlg32.lib ^
    psapi.lib ^
    /SUBSYSTEM:WINDOWS

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
