@echo off
rem Debug mode: kill old, start with -show -imedebug
taskkill /IM MemePanel.exe /F >nul 2>&1
timeout /t 1 /nobreak >nul
start "" "%~dp0out\MemePanel.exe" -show -imedebug
