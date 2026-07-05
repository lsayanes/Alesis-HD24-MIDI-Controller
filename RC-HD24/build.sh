#!/bin/bash

echo "=== Local Compilation ==="

# Create build directory
mkdir -p build
cd build

# Clean previous build
rm -rf *

# Detect Qt5 path (Homebrew) and configure CMake hints
CMAKE_EXTRA_ARGS=""
if command -v brew >/dev/null 2>&1; then
    # Prefer Qt6 if available unless QT_MAJOR=5
    if [ "${QT_MAJOR:-6}" = "6" ]; then
        QT6_PREFIX=$(brew --prefix qt 2>/dev/null || true)
        if [ -n "$QT6_PREFIX" ] && [ -d "$QT6_PREFIX/lib/cmake/Qt6" ]; then
            echo "Found Qt6 in: $QT6_PREFIX"
            CMAKE_EXTRA_ARGS="-DCMAKE_PREFIX_PATH=$QT6_PREFIX"
        fi
    fi
    if [ -z "$CMAKE_EXTRA_ARGS" ]; then
        QT5_PREFIX=$(brew --prefix qt@5 2>/dev/null || true)
        if [ -n "$QT5_PREFIX" ] && [ -d "$QT5_PREFIX/lib/cmake/Qt5" ]; then
            echo "Found Qt5 in: $QT5_PREFIX"
            CMAKE_EXTRA_ARGS="-DCMAKE_PREFIX_PATH=$QT5_PREFIX"
        fi
    fi
fi

# Respect Qt5_DIR if user exported it
if [ -n "$Qt5_DIR" ] && [ -d "$Qt5_DIR" ]; then
    echo "Using Qt5_DIR: $Qt5_DIR"
    CMAKE_EXTRA_ARGS="-DQt5_DIR=$Qt5_DIR $CMAKE_EXTRA_ARGS"
fi
# Respect Qt6_DIR if user exported it
if [ -n "$Qt6_DIR" ] && [ -d "$Qt6_DIR" ]; then
    echo "Using Qt6_DIR: $Qt6_DIR"
    CMAKE_EXTRA_ARGS="-DQt6_DIR=$Qt6_DIR $CMAKE_EXTRA_ARGS"
fi

echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Debug $CMAKE_EXTRA_ARGS

if [ $? -eq 0 ]; then
    echo "Compiling..."
    # Detect number of cores (Linux and macOS)
    CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    make -j$CORES

    if [ $? -eq 0 ]; then
        echo "=== Compilation successful ==="
        echo "Executable created: build/RCHD24"
        echo ""
        echo "To execute:"
        echo "./RC-HD24"
        echo ""
    else
        echo "Compilation error"
        exit 1
    fi
else
    echo "CMake configuration error"
    exit 1
fi
