#!/bin/bash

echo "========================================"
echo "MeetAnt vcpkg Setup Script for Linux/macOS"
echo "========================================"

# Check if vcpkg is already installed
if [ -n "$VCPKG_ROOT" ] && [ -f "$VCPKG_ROOT/vcpkg" ]; then
    echo "vcpkg is already installed at $VCPKG_ROOT"
else
    # Check common vcpkg locations
    if [ -f "$HOME/vcpkg/vcpkg" ]; then
        export VCPKG_ROOT="$HOME/vcpkg"
        echo "Found vcpkg at $HOME/vcpkg"
    elif [ -f "/opt/vcpkg/vcpkg" ]; then
        export VCPKG_ROOT="/opt/vcpkg"
        echo "Found vcpkg at /opt/vcpkg"
    else
        # vcpkg not found, clone and install
        echo "vcpkg not found. Installing vcpkg..."
        cd "$HOME"
        git clone https://github.com/Microsoft/vcpkg.git
        cd vcpkg
        ./bootstrap-vcpkg.sh
        export VCPKG_ROOT="$PWD"
        
        echo ""
        echo "Please add the following to your ~/.bashrc or ~/.zshrc:"
        echo "export VCPKG_ROOT=$VCPKG_ROOT"
        echo ""
    fi
fi

# Install dependencies
echo ""
echo "Installing dependencies..."
cd "$VCPKG_ROOT"

# Detect platform
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    TRIPLET="x64-linux"
    echo "Detected Linux platform"
    
    # Install required system packages for wxWidgets
    if command -v apt-get &> /dev/null; then
        echo "Installing system dependencies for wxWidgets..."
        sudo apt-get update
        sudo apt-get install -y libgtk-3-dev libwebkitgtk-4.0-dev libnotify-dev
    elif command -v dnf &> /dev/null; then
        echo "Installing system dependencies for wxWidgets..."
        sudo dnf install -y gtk3-devel webkit2gtk3-devel libnotify-devel
    fi
elif [[ "$OSTYPE" == "darwin"* ]]; then
    TRIPLET="x64-osx"
    echo "Detected macOS platform"
fi

echo "Installing wxWidgets..."
./vcpkg install wxwidgets:$TRIPLET

echo "Installing PortAudio..."
./vcpkg install portaudio:$TRIPLET

echo "Installing CURL..."
./vcpkg install curl:$TRIPLET

echo ""
echo "========================================"
echo "Setup complete!"
echo "========================================"
echo ""
echo "To build the project, run:"
echo "  mkdir build"
echo "  cd build"
echo "  cmake .. -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
echo "  cmake --build . --config Release"
echo "" 