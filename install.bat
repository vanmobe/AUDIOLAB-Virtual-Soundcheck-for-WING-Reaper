@echo off
REM Installation script for Reaper Wing Connector (Windows)
REM This script installs dependencies and sets up the extension

echo =========================================
echo Reaper Wing Connector - Installation
echo =========================================
echo.

REM Check if Python 3 is installed
python --version >nul 2>&1
if errorlevel 1 (
    echo Error: Python 3 is not installed
    echo Please install Python 3.7 or later from python.org
    pause
    exit /b 1
)

python --version
echo.

REM Check if pip is available
python -m pip --version >nul 2>&1
if errorlevel 1 (
    echo Error: pip is not available
    echo Please install pip for Python 3
    pause
    exit /b 1
)

echo Installing Python dependencies...
python -m pip install -r requirements.txt

if errorlevel 1 (
    echo.
    echo Error: Failed to install dependencies
    pause
    exit /b 1
)

echo.
echo =========================================
echo Installation Complete!
echo =========================================
echo.
echo Next steps:
echo 1. Edit config.json with your Wing console's IP address
echo 2. Copy these files to your Reaper Scripts folder:
echo    %APPDATA%\REAPER\Scripts\
echo.
echo 3. In Reaper:
echo    - Go to Actions -^> Show action list
echo    - Click 'ReaScript: Load ReaScript'
echo    - Select wing_connector.py
echo.
echo 4. Make sure your Wing console has OSC enabled
echo    (Setup -^> Network -^> OSC)
echo.
echo Ready to connect to your Behringer Wing!
echo.
pause
