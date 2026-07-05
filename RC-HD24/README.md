# RC-HD24 — Control remoto Qt para Alesis ADAT HD24

Cliente gráfico multiplataforma (escritorio y Android) que controla una **Alesis ADAT HD24** a través del firmware **ESP32** del repositorio padre (`hd24_midi_controller.ino`). La app envía comandos de texto por **WiFi/TCP** o **Bluetooth Classic (SPP)**; el ESP32 los traduce a **MIDI Machine Control (MMC)** y los manda por MIDI DIN al conector **MIDI IN** de la grabadora.

```
   RC-HD24 (Qt)  --- WiFi/TCP ----\
   tablet / PC                      >-- ESP32 --- MIDI DIN --> HD24
   RC-HD24 (Qt)  --- Bluetooth ---/
```

## Funcionalidades

| Sección | Descripción |
|---------|-------------|
| **Pistas (ARM)** | 24 botones conmutables (rejilla 6×4). Al conectar, la app consulta `ARMSTATE` y sincroniza el estado visual. |
| **Transporte** | REW, PLAY, PAUSE, STOP, FF y REC. |
| **Localizar (LOCATE)** | Minutos y segundos (`LOCATE 00:mm:ss:00`) más botón **INICIO** (`LOCATE0`). |
| **Conexión** | Menú ☰ Conexión: WiFi/TCP, Bluetooth o desconectar. Solo un transporte activo a la vez. |

Los controles operativos quedan deshabilitados hasta que hay una conexión activa; el menú de conexión permanece siempre accesible.

## Requisitos

### Software

- **CMake** ≥ 3.16
- **C++17**
- **Qt 5 o Qt 6** con los módulos:
  - `Core`
  - `Widgets`
  - `Network`
  - `Bluetooth`

En macOS con Homebrew suele bastar con `brew install qt` (Qt 6) o `brew install qt@5`.

### Hardware (lado HD24)

- ESP32 clásico con el sketch `hd24_midi_controller.ino` cargado.
- El ESP32 debe estar en el modo de transporte que vayas a usar:
  - **WiFi/TCP** — pin `MODE_SELECT_PIN` (GPIO4) libre / pull-up.
  - **Bluetooth** — GPIO4 puenteado a GND al arrancar.
- La Mac/tablet y el ESP32 en la misma red WiFi (modo TCP) o emparejados por Bluetooth (modo SPP).

## Compilación

Desde el directorio `RC-HD24/`:

```bash
./build.sh          # compila en build/ (Debug)
./run.sh            # ejecuta build/RC-HD24
./brun.sh           # compila y ejecuta en un solo paso
./clean.sh          # limpia artefactos de build
```

Compilación manual:

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
./RC-HD24
```

### Elegir versión de Qt

Por defecto `build.sh` prefiere **Qt 6**. Para forzar Qt 5:

```bash
QT_MAJOR=5 ./build.sh
```

También podés indicar la ruta de Qt:

```bash
export Qt6_DIR=/ruta/a/qt6/lib/cmake/Qt6
# o
export Qt5_DIR=/ruta/a/qt5/lib/cmake/Qt5
```

## Uso

1. Encendé el ESP32 y confirmá en el Monitor Serie el modo activo (`WiFi / TCP` o `Bluetooth (SPP)`).
2. Abrí **RC-HD24**.
3. Tocá **☰ Conexión** y elegí el transporte:

### WiFi/TCP

- **Host:** `hd24.local` (mDNS del firmware) o la IP que muestre el Monitor Serie (p. ej. `192.168.1.50`).
- **Puerto:** `5005` (valor por defecto).

Si `hd24.local` no resuelve en tu Mac, usá la IP directamente.

### Bluetooth

- Tocá **Buscar dispositivos** y elegí el equipo **HD24** (nombre por defecto del firmware).
- Requiere que el ESP32 haya arrancado en modo Bluetooth (GPIO4 a GND).

Al conectar, el ESP32 responde con `HD24 MIDI controller ready` y la app envía `ARMSTATE` para alinear los botones de pista.

## Protocolo con el ESP32

Mismo protocolo de texto documentado en el [README del firmware](../README.md):

- Cada comando es **una línea ASCII** terminada en `\n`.
- Respuestas: `OK ...`, `ERR ...` o `PONG`.
- La barra de estado muestra la última línea recibida.

Comandos que usa la interfaz:

| Acción en la app | Comando enviado |
|------------------|-----------------|
| REW / PLAY / PAUSE / STOP / FF / REC | `REW`, `PLAY`, `PAUSE`, `STOP`, `FF`, `REC` |
| ARM pista *n* ON/OFF | `ARM n ON` / `ARM n OFF` |
| IR (locate) | `LOCATE 00:mm:ss:00` |
| INICIO | `LOCATE0` |
| Al conectar | `ARMSTATE` |

## Arquitectura del código

```
main.cpp       → QApplication, estilo Fusion, arranque de RCHD24
RCHD24         → Ventana principal, UI y despacho de comandos
Tcp            → Socket TCP (QTcpSocket), reensamblado de líneas
Bluetooth      → SPP/RFCOMM (QBluetoothSocket) + descubrimiento de dispositivos
```

`Tcp` y `Bluetooth` exponen la misma interfaz (`connect…`, `disconnect…`, `sendCommand`, señales `connected` / `disconnected` / `lineReceived` / `errorOccurred`). `RCHD24` no distingue cuál transporte está activo: solo llama a `send()`.

## Plataformas

| Plataforma | WiFi/TCP | Bluetooth | Notas |
|------------|----------|-----------|-------|
| **Linux** | ✅ | ✅ | Soporte completo de Qt Bluetooth. |
| **Windows** | ✅ | ✅ | Ejecutable GUI (sin consola). |
| **macOS** | ✅ | ⚠️ | TCP recomendado; Qt tiene soporte limitado de Bluetooth Classic en macOS. |
| **Android** | ✅ | ✅ | Objetivo principal del transporte BT. CMake preparado (`qt_add_executable`, SDK 28–35); requiere carpeta `android/` con manifest y permisos `BLUETOOTH_CONNECT` / `BLUETOOTH_SCAN`. |

En Android la ventana se abre maximizada; en escritorio, tamaño normal con estilo **Fusion**.

## Solución de problemas

| Síntoma | Qué revisar |
|---------|-------------|
| No conecta por TCP | Misma WiFi que el ESP32; probá IP en lugar de `hd24.local`; puerto 5005; ESP32 en modo WiFi (GPIO4 libre). |
| No aparece **HD24** en Bluetooth | ESP32 arrancado con GPIO4 a GND; permisos BT en Android; en macOS preferí TCP. |
| Comandos `OK` pero la HD24 no reacciona | Problema del lado MIDI/hardware del ESP32 (ver README del firmware). |
| Botones ARM desincronizados | Reconectá; al conectar se envía `ARMSTATE` automáticamente. |

## Estructura del proyecto

```
RC-HD24/
├── main.cpp           # Punto de entrada
├── RCHD24.h / .cpp    # UI principal
├── Tcp.h / .cpp       # Transporte WiFi/TCP
├── Bluetooth.h / .cpp # Transporte Bluetooth SPP
├── CMakeLists.txt     # Build Qt 5/6, desktop y Android
├── build.sh           # Compilar (Debug)
├── brun.sh            # Compilar y ejecutar
├── run.sh             # Ejecutar binario
└── clean.sh           # Limpiar build
```

## Relación con el firmware

Este cliente es la interfaz gráfica prevista en el README del ESP32. El firmware acepta **un cliente a la vez** (TCP o Bluetooth según el modo de arranque). Para detalles de MMC, Device ID, circuito MIDI y configuración de la HD24, consultá [../README.md](../README.md).

## Licencia y autor

Desarrollado por **iDev - JalaGamaes**. Versión de aplicación: **1.0** (Android empaquetado: 1.2 según `CMakeLists.txt`).
