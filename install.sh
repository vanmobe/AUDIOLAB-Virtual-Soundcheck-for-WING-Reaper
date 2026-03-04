#!/bin/bash

# Installation script for Reaper COLAB.wing.reaper.virtualsoundcheck
# This script installs dependencies and sets up the extension

echo "========================================="
echo "Reaper COLAB.wing.reaper.virtualsoundcheck - Installation"
echo "========================================="
echo ""

# Check if Python 3 is installed
if ! command -v python3 &> /dev/null; then
    echo "Error: Python 3 is not installed"
    echo "Please install Python 3.7 or later from python.org"
    exit 1
fi

PYTHON_VERSION=$(python3 --version)
echo "Found $PYTHON_VERSION"
echo ""

# Check if pip is available
if ! python3 -m pip --version &> /dev/null; then
    echo "Error: pip is not available"
    echo "Please install pip for Python 3"
    exit 1
fi

echo "Installing Python dependencies..."
python3 -m pip install -r requirements.txt

if [ $? -ne 0 ]; then
    echo ""
    echo "Error: Failed to install dependencies"
    exit 1
fi

echo ""
echo "========================================="
echo "Installation Complete!"
echo "========================================="
echo ""
echo "Next steps:"
echo "1. Edit config.json with your Wing console's IP address"
echo "2. Copy these files to your Reaper Scripts folder:"
echo ""

# Detect OS and show appropriate path
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "   macOS: ~/Library/Application Support/REAPER/Scripts/"
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
    echo "   Windows: %APPDATA%\\REAPER\\Scripts\\"
else
    echo "   Linux: ~/.config/REAPER/Scripts/"
fi

echo ""
echo "3. In Reaper:"
echo "   - Go to Actions → Show action list"
echo "   - Click 'ReaScript: Load ReaScript'"
echo "   - Select wing_connector.py"
echo ""
echo "4. Make sure your Wing console has OSC enabled"
echo "   (Setup → Network → OSC)"
echo ""
echo "Ready to connect to your Behringer Wing!"
