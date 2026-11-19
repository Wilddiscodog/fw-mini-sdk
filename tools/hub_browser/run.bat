@echo off
cd /d "%~dp0"
if exist "HubBrowser.exe" (
  start "" "HubBrowser.exe"
  exit /b 0
)
python hub_browser.py
if errorlevel 1 pause
