@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "C:\Users\Admin\Documents\GitHub\WS-BOT\controller"
cl /Od /Zi /EHsc /MT /DUNICODE /D_UNICODE main.cpp /Fe:warspear-controller.exe /link user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib comdlg32.lib psapi.lib
