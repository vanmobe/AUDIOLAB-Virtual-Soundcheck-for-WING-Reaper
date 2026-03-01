@echo off
REM Build script for Wing Connector (Windows)

echo =========================================
echo Wing Connector - Build Script (Windows)
echo =========================================
echo.

REM Configuration
set BUILD_TYPE=Release
if not "%1"=="" set BUILD_TYPE=%1

set BUILD_DIR=build
set INSTALL_DIR=install

echo Build type: %BUILD_TYPE%
echo Build directory: %BUILD_DIR%
echo.

REM Check for CMake
where cmake >nul 2>&1
if errorlevel 1 (
    echo Error: CMake not found
    echo Please install CMake from https://cmake.org/download/
    pause
    exit /b 1
)

for /f "tokens=*" %%i in ('cmake --version') do (
    echo Found %%i
    goto :cmake_found
)
:cmake_found

REM Create build directory
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"

REM Run CMake
echo.
echo Running CMake configuration...
cmake .. -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
         -DCMAKE_INSTALL_PREFIX=../%INSTALL_DIR%

if errorlevel 1 (
    echo CMake configuration failed
    cd ..
    pause
    exit /b 1
)

REM Build
echo.
echo Building...
cmake --build . --config %BUILD_TYPE% -j %NUMBER_OF_PROCESSORS%

if errorlevel 1 (
    echo Build failed
    cd ..
    pause
    exit /b 1
)

REM Install
echo.
echo Installing to %INSTALL_DIR%...
cmake --install . --config %BUILD_TYPE%

cd ..

REM Show results
echo.
echo =========================================
echo Build Complete!
echo =========================================
echo.
echo Extension built: %INSTALL_DIR%\reaper_wingconnector.dll
echo.
echo Installation instructions:
echo.
echo Copy to: %%APPDATA%%\REAPER\UserPlugins\
echo.
echo Quick install:
echo   copy %INSTALL_DIR%\reaper_wingconnector.dll "%%APPDATA%%\REAPER\UserPlugins\"
echo   copy config.json "%%APPDATA%%\REAPER\UserPlugins\"
echo.
echo Then restart Reaper to load the extension.
echo.
pause
