# Controlador MIDI WiFi para Alesis ADAT HD24 (ESP32)

Controlador de transporte para la **Alesis HD24** basado en **ESP32**. Se conecta
a tu **WiFi**, levanta un **servidor TCP** y traduce los comandos de texto que le
manda un cliente (una app de Android/iOS) a **MIDI Machine Control (MMC)**, que
envía por una salida **MIDI DIN de 5 pines** hacia el conector **MIDI IN** de la
HD24.

```
   App movil  --- WiFi/TCP --->  ESP32  --- MIDI DIN --->  HD24 (MIDI IN)
   (botones)                   (traduce a MMC)
```

> Según el *ADAT HD24 Reference Manual*, Cap. 8, la HD24 recibe comandos MMC
> (REW, PLAY, STOP, REC, LOCATE, etc.) por su MIDI IN. El controlador solo
> **transmite** MIDI; no necesita recibir para funcionar.

Esta es la evolución de la versión original con **Arduino Uno + botones físicos**:
el ESP32 reemplaza los botones, la electrónica y el cableado por una conexión WiFi,
así toda la interfaz vive en la app.

## Ventajas frente a la versión Uno

- **Sin botones ni cables**: la interfaz es la app.
- **USB libre para depurar**: el MIDI sale por `Serial2` (GPIO17), no por el UART
  del USB. Podés dejar el Monitor Serie abierto con el cable MIDI conectado.
- **Descubrimiento por mDNS**: la app puede conectarse a `hd24.local` sin IP fija.

## Funcionalidades (comandos MMC)

| Comando de la app | Comando MMC enviado                            |
|-------------------|------------------------------------------------|
| `PLAY`            | Deferred Play (`0x03`)                          |
| `STOP`            | Stop (`0x01`)                                   |
| `REW`             | Rewind (`0x05`)                                 |
| `FF`              | Fast Forward (`0x04`)                           |
| `REC`             | Record Strobe (`0x06`) sobre las pistas armadas |
| `REC_EXIT`        | Record Exit / punch out (`0x07`)                |
| `PAUSE`           | Pause (`0x09`)                                  |
| `RESET`           | Reset (`0x0D`)                                  |
| `LOCATE0`         | Locate a `00:00:00:00` (`0x44`)                 |
| `LOCATE hh:mm:ss:ff` | Locate a posición absoluta (`0x44`)          |
| `ARM n`           | Track Record Ready (`0x40`/`0x4F`)              |

Todos los mensajes tienen la forma `F0 7F <DeviceID> 06 <comando...> F7`.

## Protocolo TCP (para el cliente gráfico)

- El ESP32 escucha en **TCP puerto `5005`** (configurable en el sketch).
- Cada comando es **una línea de texto ASCII terminada en `\n`** (el `\r` se ignora).
- **No** distingue mayúsculas/minúsculas.
- La respuesta empieza con **`OK`** o **`ERR`** (o **`PONG`** para el ping).

| Enviar                     | Efecto                                       | Respuesta típica        |
|----------------------------|----------------------------------------------|-------------------------|
| `PLAY` `STOP` `REW` `FF`   | Transporte                                   | `OK PLAY`               |
| `REC` `REC_EXIT` `PAUSE` `RESET` | Transporte / grabación                 | `OK REC`                |
| `LOCATE0`                  | Vuelve a `00:00:00:00`                        | `OK LOCATE0`            |
| `LOCATE 00:01:23:00`       | Va a esa posición (`hh:mm:ss:ff[:sf]`)        | `OK LOCATE 00:01:23:00` |
| `ARM 3`                    | Alterna (toggle) el arm de la pista 3         | `OK ARM 3 ON`           |
| `ARM 3 ON` / `ARM 3 OFF`   | Fija el arm de la pista 3                      | `OK ARM 3 OFF`          |
| `ARMCLEAR`                 | Desarma todas las pistas                       | `OK ARMCLEAR`           |
| `ARMSTATE`                 | Consulta qué pistas están armadas              | `OK ARMSTATE 1,3,5`     |
| `PING`                     | Keep-alive / test de conexión                  | `PONG`                  |

Al conectarse, el ESP32 saluda con `HD24 MIDI controller ready`.

> **REW y FF son "shuttle con enganche".** El MMC de Fast Forward/Rewind es
> momentáneo (la HD24 solo bobina mientras le sigue llegando el comando), así
> que el ESP32 **reenvía el comando cada 100 ms** hasta que mandás `STOP`
> (o cualquier otro comando de transporte, o se desconecta el cliente). En la
> app: `REW` para empezar a rebobinar, `STOP` para frenar.

Ejemplo de prueba rápida desde una PC en la misma red (sin app):

```sh
# con netcat
nc hd24.local 5005
PLAY
ARM 1 ON
LOCATE 00:00:10:00
```

## Materiales

- Placa **ESP32** (cualquier dev board con WiFi; p. ej. ESP32-WROOM DevKit).
- 1 conector **DIN hembra de 5 pines** (180°) para MIDI OUT.
- Resistencias para la salida MIDI de **3.3V** (ver circuito).
- Cable MIDI estándar entre el controlador y el **MIDI IN** de la HD24.
- **No** hacen falta botones ni resistencias de pull-up: la interfaz es la app.

## Circuito MIDI OUT (DIN de 5 pines) — ¡3.3V, no 5V!

El ESP32 trabaja a **3.3V**, no a 5V como el Arduino Uno. MIDI es un lazo de
corriente, así que funciona, pero los valores de resistencia cambian. El
estándar MIDI 1.0 para fuentes de **3.3V** usa **33 Ω** y **10 Ω**:

```
   ESP32                           Conector DIN-5 (MIDI OUT)
   -----                           -------------------------
   3V3   ----[ 33 Ω ]------------> pin 4   
   TX2 (GPIO17)--[ 10 Ω ]--------> pin 5
   GND   ------------------------> pin 2 (malla)

   Conectar este DIN al  MIDI IN  de la HD24 con un cable MIDI normal.

```

Notas:
- Salida MIDI en **GPIO17 (TX2)** por defecto; se cambia con `MIDI_TX_PIN`.
- Muchos módulos MIDI OUT prehechos asumen 5V; si usás uno, alimentalo con 5V
  (VIN/USB del ESP32) y respetá su esquema, no el de 3.3V.

## Configuración antes de subir el sketch

En `hd24_midi_controller.ino`, editá arriba de todo:

```cpp
const char* WIFI_SSID = "RED_WIFI";
const char* WIFI_PASS = "PASSWORD";
const uint16_t TCP_PORT = 5005;    // puerto TCP
const char* MDNS_NAME  = "hd24";   // -> hd24.local
```

Otros ajustes MIDI (heredados de la versión Uno):

- `DEVICE_ID`: Device ID de la HD24 (`0x00` por defecto, `0x7F` = broadcast).
- `TC_TYPE`: tipo de time code para LOCATE (`3` = 30 fps non-drop).
- `NUM_TRACKS`: cantidad de pistas (24).
- `MIDI_TX_PIN`: pin de salida MIDI (GPIO17 por defecto).

## Configuración de la HD24

1. Presioná **MIDI** en el panel hasta llegar a las páginas de MIDI.
2. En **`MIDI05:Dev ID`** verificá el **Device ID** (de fábrica `000`). Debe
   coincidir con `DEVICE_ID` en el sketch (por defecto `0x00`).
3. No hace falta activar "Send MMC" para recibir; esa página es para que la
   HD24 *emita* MMC al mover sus propias teclas.

## Cómo cargar el sketch

1. En el Arduino IDE, instalá el soporte para **ESP32** (Boards Manager →
   "esp32" de Espressif). Las librerías `WiFi` y `ESPmDNS` vienen incluidas.
2. Seleccioná tu placa ESP32 y el puerto.
3. Editá `WIFI_SSID` / `WIFI_PASS`.
4. Subí el sketch (a diferencia del Uno, **no** hace falta desconectar el MIDI).
5. Abrí el **Monitor Serie a 115200** para ver la IP asignada y los comandos que
   van llegando.

## Cómo funciona el ESP32 (resumen del firmware)

- `setup()`: arranca el USB (115200, solo debug) y `Serial2` (31250, MIDI),
  se conecta al WiFi y levanta el servidor TCP + mDNS.
- `loop()`: reconecta el WiFi si se cae, acepta un cliente, y lee sus comandos
  línea por línea. Cada línea pasa por `handleCommand()`, que llama a las mismas
  funciones `sendMmc()` / `sendLocate()` / `sendTrackRecordReady()` de la versión
  Uno. **La capa MIDI/MMC no cambió**; solo cambió de dónde vienen las órdenes.

## Ideas para el cliente gráfico (Android/iOS)

- Abrir un **socket TCP** a `hd24.local:5005` (o a la IP mostrada en el Monitor).
- Mandar cada botón como una línea (`"PLAY\n"`, `"ARM 3\n"`, ...).
- Leer las respuestas `OK/ERR` para dar feedback; usar `ARMSTATE` al conectar
  para reflejar qué pistas están armadas.
- Mandar `PING` cada pocos segundos como keep-alive / detección de caída.

## Limitaciones conocidas

- Atiende **un cliente TCP a la vez** (suficiente para una app). Si se conecta
  otro, el anterior se reemplaza al reconectar.
- El **arm de pistas** por MMC (`Track Record Ready`) depende del firmware de la
  HD24. El transporte y el LOCATE son sólidos; si el arm no respondiera en tu
  unidad, probá ajustar el bitmap en `sendTrackRecordReady()`.
- La HD24 **no** se puede esclavizar a MTC entrante por MIDI (solo es maestro de
  time code), por eso este controlador usa MMC y no MIDI Clock/MTC.
```
