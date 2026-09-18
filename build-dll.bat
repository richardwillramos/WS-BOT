@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "C:\Users\Admin\Documents\warspear-botv1.5"
cl /O2 /EHsc /MT /LD dllmain.cpp /Fewarspear-bot22.dll /link user32.lib
echo EXIT CODE: %ERRORLEVEL%
