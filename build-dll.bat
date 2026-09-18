@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "C:\Users\Admin\Documents\GitHub\WS-BOT"
cl /Od /Zi /EHsc /MT /LD dllmain.cpp /Fewarspear-bot23.dll /link user32.lib
echo EXIT CODE: %ERRORLEVEL%
