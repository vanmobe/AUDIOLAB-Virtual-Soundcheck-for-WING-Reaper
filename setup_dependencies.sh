#!/bin/bash

# Dependency download script for COLAB.wing.reaper.virtualsoundcheck
# Downloads required third-party libraries

set -e

echo "========================================="
echo "COLAB.wing.reaper.virtualsoundcheck - Dependency Setup"
echo "========================================="
echo ""

# Create lib directory
mkdir -p lib
cd lib

# Download Reaper SDK
echo "[1/2] Setting up Reaper SDK..."
if [ ! -d "reaper-sdk" ]; then
    mkdir -p reaper-sdk
    echo ""
    echo "MANUAL STEP REQUIRED:"
    echo "Download Reaper SDK files from: https://www.reaper.fm/sdk/plugin/"
    echo ""
    echo "Required files:"
    echo "  - reaper_plugin.h"
    echo "  - reaper_plugin_functions.h"
    echo ""
    echo "Save them to: $(pwd)/reaper-sdk/"
    echo ""
    echo "Press Enter when files are downloaded..."
    read
    
    if [ ! -f "reaper-sdk/reaper_plugin.h" ] || [ ! -f "reaper-sdk/reaper_plugin_functions.h" ]; then
        echo "ERROR: Required SDK files not found in lib/reaper-sdk/"
        echo "Please download them manually and re-run this script"
        exit 1
    fi
    
    echo "✓ Reaper SDK files found"
else
    echo "✓ Reaper SDK already exists"
fi

echo ""

# Download oscpack
echo "[2/2] Downloading oscpack library..."
if [ ! -d "oscpack" ]; then
    if command -v git &> /dev/null; then
        git clone https://github.com/RossBencina/oscpack.git
        echo "✓ oscpack cloned successfully"
    else
        echo ""
        echo "Git not found. Please download manually:"
        echo "https://github.com/RossBencina/oscpack"
        echo ""
        echo "Extract to: $(pwd)/oscpack/"
        exit 1
    fi
else
    echo "✓ oscpack already exists"
fi

cd ..

echo ""
echo "========================================="
echo "Dependency Setup Complete!"
echo "========================================="
echo ""
echo "Dependencies installed:"
echo "  ✓ Reaper SDK"
echo "  ✓ oscpack library"
echo ""
echo "Next steps:"
echo "  1. Edit config.json with your Wing's IP address"
echo "  2. Run ./build.sh to compile the extension"
echo "  3. Copy to Reaper UserPlugins folder"
echo ""
