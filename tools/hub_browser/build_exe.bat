@echo off
setlocal
cd /d "%~dp0"

where python >nul 2>&1
if errorlevel 1 (
  echo [ERR] python not found
  exit /b 1
)

python -c "import PyInstaller" 2>nul
if errorlevel 1 (
  echo [INFO] installing pyinstaller...
  python -m pip install -U pyinstaller
)

echo [INFO] building HubBrowser.exe (onefile, windowed)...
python -m PyInstaller --noconfirm --clean --onefile --windowed ^
  --name HubBrowser ^
  --distpath dist ^
  --workpath build ^
  --specpath build ^
  hub_browser.py

if errorlevel 1 (
  echo [ERR] build failed
  exit /b 1
)

if not exist "HubBrowser.exe" (
  copy /Y "dist\HubBrowser.exe" "HubBrowser.exe" >nul
)

echo.
echo [OK] %cd%\HubBrowser.exe
echo      cache will be created next to the exe: .\cache\
endlocal
