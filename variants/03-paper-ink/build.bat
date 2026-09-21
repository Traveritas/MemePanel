@echo off
rem MemePanel build: MSVC or clang-cl, CRT-free, single exe (ASCII only!)
setlocal
cd /d "%~dp0"
if not exist out mkdir out

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set VCPATH=
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VCPATH=%%i
if not defined VCPATH (
    echo [X] MSVC C++ toolchain not found ^(VS Build Tools + C++ workload required^)
    exit /b 1
)
call "%VCPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

set CC=cl
if exist "%VCPATH%\VC\Tools\Llvm\x64\bin\clang-cl.exe" set "CC=%VCPATH%\VC\Tools\Llvm\x64\bin\clang-cl.exe"
echo [*] compiler: %CC%

set CFLAGS=/nologo /GS- /O1 /Os /W3 /wd4100 /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /DNDEBUG /c
set LIBS=kernel32.lib user32.lib gdi32.lib shell32.lib ole32.lib windowscodecs.lib shlwapi.lib imm32.lib

rc /nologo /fo out\app.res src\app.rc || exit /b 1
%CC% %CFLAGS% /Foout\util.obj    src\util.c    || exit /b 1
%CC% %CFLAGS% /Foout\wicthumb.obj src\wicthumb.c || exit /b 1
%CC% %CFLAGS% /Foout\store.obj   src\store.c   || exit /b 1
%CC% %CFLAGS% /Foout\gp.obj      src\gp.c      || exit /b 1
%CC% %CFLAGS% /Foout\main.obj    src\main.c    || exit /b 1

link /nologo /NODEFAULTLIB /SUBSYSTEM:WINDOWS /MACHINE:X64 /ENTRY:entry /OPT:REF /OPT:ICF /MERGE:.rdata=.text /OUT:out\MemePanel.exe out\app.res out\util.obj out\wicthumb.obj out\store.obj out\gp.obj out\main.obj %LIBS% || exit /b 1

for %%A in (out\MemePanel.exe) do echo [OK] out\MemePanel.exe  %%~zA bytes
