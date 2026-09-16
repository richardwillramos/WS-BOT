@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "F:\WS\BOT-CE\warspear-botv1.4"
cl /O2 /EHsc /MT /LD dllmain.cpp /Fewarspear-bot22.dll /link user32.lib
echo EXIT CODE: %ERRORLEVEL%
