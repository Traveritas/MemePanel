@echo off
rem Kill any running instance then start fresh build (ASCII only)
taskkill /IM MemePanel.exe /F >nul 2>&1
timeout /t 1 /nobreak >nul
start "" "%~dp0out\MemePanel.exe" -show
