#!/bin/bash

echo "=== Compilacion + ejecucion ==="

# Crear directorio de build
mkdir -p build
cd build

# Limpiar build anterior
rm -rf *

# Detectar ruta de Qt5 (Homebrew) y configurar CMake
CMAKE_EXTRA_ARGS=""
if command -v brew >/dev/null 2>&1; then
    # Preferir Qt6 si está disponible salvo QT_MAJOR=5
    if [ "${QT_MAJOR:-6}" = "6" ]; then
        QT6_PREFIX=$(brew --prefix qt 2>/dev/null || true)
        if [ -n "$QT6_PREFIX" ] && [ -d "$QT6_PREFIX/lib/cmake/Qt6" ]; then
            echo "Qt6 encontrado en: $QT6_PREFIX"
            CMAKE_EXTRA_ARGS="-DCMAKE_PREFIX_PATH=$QT6_PREFIX"
        fi
    fi
    if [ -z "$CMAKE_EXTRA_ARGS" ]; then
        QT5_PREFIX=$(brew --prefix qt@5 2>/dev/null || true)
        if [ -n "$QT5_PREFIX" ] && [ -d "$QT5_PREFIX/lib/cmake/Qt5" ]; then
            echo "Qt5 encontrado en: $QT5_PREFIX"
            CMAKE_EXTRA_ARGS="-DCMAKE_PREFIX_PATH=$QT5_PREFIX"
        fi
    fi
fi

# Respetar Qt5_DIR si el usuario lo exportó
if [ -n "$Qt5_DIR" ] && [ -d "$Qt5_DIR" ]; then
    echo "Usando Qt5_DIR: $Qt5_DIR"
    CMAKE_EXTRA_ARGS="-DQt5_DIR=$Qt5_DIR $CMAKE_EXTRA_ARGS"
fi
# Respetar Qt6_DIR si el usuario lo exportó
if [ -n "$Qt6_DIR" ] && [ -d "$Qt6_DIR" ]; then
    echo "Usando Qt6_DIR: $Qt6_DIR"
    CMAKE_EXTRA_ARGS="-DQt6_DIR=$Qt6_DIR $CMAKE_EXTRA_ARGS"
fi

echo "Configurando con CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Debug $CMAKE_EXTRA_ARGS

if [ $? -eq 0 ]; then
    echo "Compilando..."
    # Detectar número de cores (Linux y macOS)
    CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    make -j$CORES

    if [ $? -eq 0 ]; then
        if [ -d "RC-HD24.app" ]; then
            ./RC-HD24.app/Contents/MacOS/RC-HD24
        else
            ./RC-HD24
        fi
    else
        echo "Error en la compilación"
        exit 1
    fi
else
    echo "Error en la configuración de CMake"
    exit 1
fi
