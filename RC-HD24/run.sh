
#!/bin/bash

echo "=== RUN ==="

cd build

# En macOS el ejecutable vive dentro del bundle .app. Lo lanzamos por su ruta
# interna (no con `open`) para conservar los logs por stdout; TCC igualmente
# usa el Info.plist del bundle para el permiso de Bluetooth.
if [ -d "RC-HD24.app" ]; then
    ./RC-HD24.app/Contents/MacOS/RC-HD24
else
    ./RC-HD24
fi

