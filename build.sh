#!/bin/bash

# Build script for Wing Connector Reaper Extension
# Supports macOS, Linux, and Windows (via WSL/MinGW)

set -e

echo "========================================="
echo "Wing Connector - Build Script"
echo "========================================="
echo ""

# Detect OS
OS="$(uname -s)"
case "${OS}" in
    Linux*)     PLATFORM=Linux;;
    Darwin*)    PLATFORM=macOS;;
    MINGW*|MSYS*|CYGWIN*) PLATFORM=Windows;;
    *)          PLATFORM="Unknown";;
esac

echo "Platform: ${PLATFORM}"
echo ""

# Configuration
BUILD_TYPE=${1:-Release}
BUILD_DIR="build"
INSTALL_DIR="install"

echo "Build type: ${BUILD_TYPE}"
echo "Build directory: ${BUILD_DIR}"
echo ""

# Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Check for dependencies
echo "Checking dependencies..."

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo "Error: CMake not found"
    echo "Please install CMake: https://cmake.org/download/"
    exit 1
fi

CMAKE_VERSION=$(cmake --version | head -n1)
echo "Found ${CMAKE_VERSION}"

# Run CMake
echo ""
echo "Running CMake configuration..."
cmake .. -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
         -DCMAKE_INSTALL_PREFIX=../${INSTALL_DIR}

if [ $? -ne 0 ]; then
    echo "CMake configuration failed"
    exit 1
fi

# Build
echo ""
echo "Building..."
cmake --build . --config ${BUILD_TYPE} -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

# Install
echo ""
echo "Installing to ${INSTALL_DIR}..."
cmake --install . --config ${BUILD_TYPE}

cd ..

# Show results
echo ""
echo "========================================="
echo "Build Complete!"
echo "========================================="
echo ""
echo "Extension built: ${INSTALL_DIR}/reaper_wingconnector${EXTENSION_SUFFIX}"
echo ""
echo "Installation instructions:"
echo ""

if [ "${PLATFORM}" == "macOS" ]; then
    REAPER_PATH="$HOME/Library/Application Support/REAPER/UserPlugins"
    echo "Copy to: ${REAPER_PATH}/"
    echo ""
    echo "Quick install:"
    echo "  mkdir -p \"${REAPER_PATH}\""
    echo "  cp ${INSTALL_DIR}/reaper_wingconnector.dylib \"${REAPER_PATH}/\""
    echo "  cp config.json \"${REAPER_PATH}/\""
elif [ "${PLATFORM}" == "Linux" ]; then
    REAPER_PATH="$HOME/.config/REAPER/UserPlugins"
    echo "Copy to: ${REAPER_PATH}/"
    echo ""
    echo "Quick install:"
    echo "  mkdir -p \"${REAPER_PATH}\""
    echo "  cp ${INSTALL_DIR}/reaper_wingconnector.so \"${REAPER_PATH}/\""
    echo "  cp config.json \"${REAPER_PATH}/\""
elif [ "${PLATFORM}" == "Windows" ]; then
    REAPER_PATH="%APPDATA%\\REAPER\\UserPlugins"
    echo "Copy to: ${REAPER_PATH}\\"
    echo ""
    echo "Quick install:"
    echo "  copy ${INSTALL_DIR}\\reaper_wingconnector.dll \"%APPDATA%\\REAPER\\UserPlugins\\\""
    echo "  copy config.json \"%APPDATA%\\REAPER\\UserPlugins\\\""
fi

echo ""
echo "Then restart Reaper to load the extension."
echo ""
