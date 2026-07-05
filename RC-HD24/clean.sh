#!/bin/bash

echo "=== clean ==="
echo "current directory: $(pwd)"
echo ""

# Función para limpiar directorio
clean_directory() {
    local dir="$1"
    if [ -d "$dir" ]; then
        echo "Limpiando directorio: $dir"
        rm -rf "$dir"/*
        echo "  ✅ $dir limpiado"
    else
        echo "  ⚠️  Directorio $dir no existe"
    fi
}

# Limpiar directorios de build del proyecto principal
echo "Limpiando directorios de build..."
clean_directory "build"


# Limpiar archivos generados por CMake
echo ""
echo "Limpiando archivos generados por CMake..."
if [ -f "CMakeCache.txt" ]; then
    rm -f CMakeCache.txt
    echo "  ✅ CMakeCache.txt eliminado"
fi

if [ -f "CMakeFiles" ]; then
    rm -rf CMakeFiles
    echo "  ✅ CMakeFiles eliminado"
fi

if [ -f "cmake_install.cmake" ]; then
    rm -f cmake_install.cmake
    echo "  ✅ cmake_install.cmake eliminado"
fi

if [ -f "Makefile" ]; then
    rm -f Makefile
    echo "  ✅ Makefile eliminado"
fi

# Limpiar archivos de Qt (si existen)
echo ""
echo "Limpiando archivos generados por Qt..."
find . -name "*.moc" -delete 2>/dev/null && echo "  ✅ Archivos .moc eliminados"
find . -name "moc_*.cpp" -delete 2>/dev/null && echo "  ✅ Archivos moc_*.cpp eliminados"
find . -name "ui_*.h" -delete 2>/dev/null && echo "  ✅ Archivos ui_*.h eliminados"
find . -name "qrc_*.cpp" -delete 2>/dev/null && echo "  ✅ Archivos qrc_*.cpp eliminados"

# Limpiar archivos de compilación
echo ""
echo "Limpiando archivos de compilación..."
find . -name "*.o" -delete 2>/dev/null && echo "  ✅ Archivos .o eliminados"
find . -name "*.obj" -delete 2>/dev/null && echo "  ✅ Archivos .obj eliminados"

# Limpiar ejecutables
echo ""
echo "Limpiando ejecutables..."
find . -name "RC-HD24" -delete 2>/dev/null && echo "  ✅ Ejecutable RC-HD24 eliminado"


echo ""
echo "=== LIMPIEZA COMPLETADA ==="
echo ""
echo "Para recompilar:"
echo "  ./build.sh          # Compilación local (Linux / macOS)"
echo ""
echo "O manualmente:"
echo "  mkdir build && cd build"
echo "  cmake .."
echo "  make"
