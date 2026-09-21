@echo off
call "C:\Program Files\Microsoft Visual Studio‚2\Community\VC\Auxiliary\Buildcvars64.bat" >nul
cl /nologo /EHsc /W1 /DUNICODE /D_UNICODE wvhello.cpp /Fe:wvhello.exe /link /SUBSYSTEM:WINDOWS nupkguild
atived\WebView2Loader.dll.lib
