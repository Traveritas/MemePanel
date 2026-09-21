@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /W1 /DUNICODE /D_UNICODE wvhello.cpp /Fe:wvhello.exe /link /SUBSYSTEM:WINDOWS nupkg\build\native\x64\WebView2Loader.dll.lib user32.lib ole32.lib
