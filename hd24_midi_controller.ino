/*
 * Controlador MIDI para Alesis ADAT HD24  (version ESP32 / WiFi)
 * -------------------------------------------------------------
 * Plataforma: ESP32 (WiFi + varios UART de hardware)
 * Entrada   : comandos de texto por TCP/IP sobre WiFi (desde una app movil)
 * Salida    : MIDI DIN de 5 pines hacia el MIDI IN de la HD24 (via Serial2)
 *
 * A diferencia de la version Arduino Uno (botones fisicos + MIDI por el UART
 * USB), aca el ESP32 se conecta a tu WiFi, levanta un servidor TCP y traduce
 * los comandos de texto que le manda el cliente grafico a MIDI Machine Control
 * (MMC), que viaja dentro de SysEx universales en tiempo real:
 *
 *     F0 7F <DeviceID> 06 <comando...> F7
 *
 * Referencia: ADAT HD24 Reference Manual, Cap. 8 ("Synchronization and MIDI").
 * La HD24 recibe MMC por su MIDI IN (REW, PLAY, STOP, REC, LOCATE, etc.) y
 * comparte el Device ID configurado en MIDI05:Dev ID (por defecto 000).
 *
 * VENTAJA del ESP32: el MIDI sale por Serial2 (GPIO17 por defecto), asi que el
 * USB / Monitor Serie queda LIBRE para depurar sin desconectar el cable MIDI.
 *
 * NOTA DE HARDWARE (3.3V): el ESP32 es de 3.3V, no 5V. La salida MIDI funciona
 * igual (MIDI es un lazo de corriente), pero para respetar el estandar conviene
 * usar el esquema de resistencias para 3.3V (ver README).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <BluetoothSerial.h>   // Bluetooth Classic (SPP) — solo ESP32 clasico

#include "secret.h"

// El Bluetooth Classic solo existe en el ESP32 original. Los ESP32-S2/S3/C3
// no tienen BT Classic (solo BLE), asi que este sketch requiere un ESP32 clasico.
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error "Este sketch necesita un ESP32 clasico con Bluetooth Classic habilitado."
#endif

// ---------------------------------------------------------------------------
// Configuracion de red
// ---------------------------------------------------------------------------



// Puerto TCP donde escucha el controlador. La app se conecta aca.
const uint16_t TCP_PORT = 5005;

// Nombre mDNS: la app puede conectarse a "hd24.local" en vez de una IP fija.
const char* MDNS_NAME = "hd24";

// ---------------------------------------------------------------------------
// Configuracion Bluetooth (Classic / SPP) y seleccion de modo
// ---------------------------------------------------------------------------

// Nombre con el que el ESP32 aparece al emparejar por Bluetooth desde la tablet.
const char* BT_NAME = "HD24";

// Pin que elige el transporte AL ARRANCAR (se lee una sola vez en setup()):
//   - libre / sin conectar  -> WiFi/TCP   (comportamiento original, pull-up interno)
//   - puenteado a GND        -> Bluetooth (SPP)
// GPIO4 es un pin libre y seguro (no es strapping). Cambia el numero si lo usas
// para otra cosa. Nunca se encienden los dos radios a la vez: se elige uno u otro.
const int MODE_SELECT_PIN = 4;

// Transporte activo, decidido en setup() segun MODE_SELECT_PIN.
enum Transport : uint8_t { MODE_WIFI, MODE_BT };
Transport transport = MODE_WIFI;

// Objeto Bluetooth Classic (solo se usa si transport == MODE_BT).
BluetoothSerial SerialBT;

// ---------------------------------------------------------------------------
// Configuracion MIDI
// ---------------------------------------------------------------------------

// Device ID de la HD24. Debe coincidir con MIDI05:Dev ID del equipo.
// 0x00 = dispositivo 000 (valor de fabrica). 0x7F = "broadcast" (todos).
const uint8_t DEVICE_ID = 0x7F;

// Velocidad MIDI estandar.
const long MIDI_BAUD = 31250;

// Pin de salida MIDI (TX de Serial2). GPIO17 es el TX2 por defecto del ESP32.
// El RX no se usa (el controlador solo transmite), pero Serial2 pide un pin.
const int MIDI_TX_PIN = 17;
const int MIDI_RX_PIN = 16;

// Formato de time code para LOCATE: 0=24fps, 1=25fps, 2=30 drop, 3=30 nondrop.
// La HD24 genera MTC a 30 fps, asi que usamos 30 non-drop.
const uint8_t TC_TYPE = 3;

// Cantidad de pistas de la HD24 (24). Se usa para el arm de pistas.
const uint8_t NUM_TRACKS = 24;

// Alias para el puerto MIDI: asi el resto del codigo no sabe que es Serial2.
HardwareSerial& Midi = Serial2;

// ---------------------------------------------------------------------------
// Comandos MMC (segundo byte de comando dentro del SysEx)
// ---------------------------------------------------------------------------
enum MmcCmd : uint8_t {
  MMC_STOP          = 0x01,
  MMC_PLAY          = 0x02,
  MMC_DEFERRED_PLAY = 0x03,  // PLAY que espera a estar listo (recomendado)
  MMC_FAST_FORWARD  = 0x04,
  MMC_REWIND        = 0x05,
  MMC_RECORD_STROBE = 0x06,  // "punch in" / iniciar grabacion
  MMC_RECORD_EXIT   = 0x07,  // "punch out"
  MMC_RECORD_PAUSE  = 0x08,
  MMC_PAUSE         = 0x09,
  MMC_EJECT         = 0x0A,
  MMC_CHASE         = 0x0B,
  MMC_RESET         = 0x0D
};

// Mascara de pistas armadas (bit 0 = pista 1 ... bit 23 = pista 24).
uint32_t armedTracks = 0;

// Bobinado continuo (REW/FF). MMC FAST FORWARD/REWIND son "momentaneos": la
// HD24 solo bobina mientras le siga llegando el comando, asi que hay que
// repetirlo periodicamente. 0 = detenido; si no, guarda MMC_REWIND o
// MMC_FAST_FORWARD y loop() reenvia ese byte hasta que otro comando lo cancele.
uint8_t windCmd = 0;
unsigned long lastWindMs = 0;
const unsigned long WIND_REPEAT_MS = 100;   // cada cuanto reenviar FF/REW

// ---------------------------------------------------------------------------
// Envio de MMC (identico a la version Uno, pero por 'Midi' = Serial2)
// ---------------------------------------------------------------------------

// Comando MMC simple de un solo byte (transporte).
void sendMmc(uint8_t cmd) {
  Midi.write(0xF0);       // inicio SysEx
  Midi.write(0x7F);       // Universal Real Time
  Midi.write(DEVICE_ID);  // Device ID destino
  Midi.write(0x06);       // sub-id: MMC Command
  Midi.write(cmd);        // comando
  Midi.write(0xF7);       // fin SysEx
  Midi.flush();
}

// LOCATE a una posicion absoluta (comando 0x44, sub 0x01 = TARGET).
// El byte de horas incluye el tipo de time code en los bits 5-6.
void sendLocate(uint8_t hr, uint8_t mn, uint8_t sc, uint8_t fr, uint8_t sf) 
{
    uint8_t hourByte = (uint8_t)((TC_TYPE & 0x03) << 5) | (hr & 0x1F);
    Midi.write(0xF0);
    Midi.write(0x7F);
    Midi.write(DEVICE_ID);
    Midi.write(0x06);
    Midi.write(0x44);   // LOCATE
    Midi.write(0x06);   // longitud del campo que sigue
    Midi.write(0x01);   // sub-comando: TARGET (posicion absoluta)
    Midi.write(hourByte);
    Midi.write(mn & 0x7F);
    Midi.write(sc & 0x7F);
    Midi.write(fr & 0x7F);
    Midi.write(sf & 0x7F);
    Midi.write(0xF7);
    Midi.flush();
}

// Arma/desarma pistas via MMC WRITE (0x40) al campo TRACK RECORD READY (0x4F).
// El campo usa el "Standard Track Bitmap": primer byte = cantidad de bytes
// del bitmap; luego cada byte lleva 7 pistas (bit a bit, bit7 siempre 0).
//
// OFFSET DE LA HD24 (medido en la unidad real): la HD24 no ubica la pista 1 en
// el bit global 1 como sugiere el estandar, sino 4 bits mas adelante (los
// primeros bits los usa para flags internos). Empiricamente:
//     ARM 5  -> arma track 1   (bit global 9  = 5 + 4)... ver abajo
//     ARM 24 -> arma track 20  antes de corregir
// Corregimos sumando TRACK_BIT_OFFSET = 4 a la posicion global de cada pista,
// de modo que "ARM n" arme exactamente la pista n. La pista 24 pasa al bit
// global 28, por eso ahora hacen falta 5 bytes de bitmap (no 4).
//
// NOTA: el arm por MMC depende del firmware. Este offset se calibro contra una
// HD24 concreta; si tu unidad respondiera corrida, ajusta TRACK_BIT_OFFSET.
const uint8_t TRACK_BIT_OFFSET = 4;

void sendTrackRecordReady()
{
    // Pista 24 -> bit global 24 + offset = 28 -> byte 4 (indice) -> 5 bytes.
    const uint8_t bitmapBytes = 5;
    uint8_t bitmap[bitmapBytes];

    for (uint8_t i = 0; i < bitmapBytes; i++)
      bitmap[i] = 0;

    // Pista N -> bit global (N + offset); cada byte aporta 7 bits.
    for (uint8_t t = 1; t <= NUM_TRACKS; t++)
    {
      if (armedTracks & (1UL << (t - 1)))
      {
        uint8_t globalBit = t + TRACK_BIT_OFFSET;   // offset medido en la HD24 real
        uint8_t byteIndex = globalBit / 7;
        uint8_t bitIndex  = globalBit % 7;

        if (byteIndex < bitmapBytes)
          bitmap[byteIndex] |= (uint8_t)(1 << bitIndex);
      }
    }

    Midi.write(0xF0);
    Midi.write(0x7F);
    Midi.write(DEVICE_ID);
    Midi.write(0x06);
    Midi.write(0x40);                 // WRITE
    Midi.write((uint8_t)(1 + 1 + bitmapBytes)); // long: campo + count + bitmap
    Midi.write(0x4F);                 // TRACK RECORD READY
    Midi.write(bitmapBytes);          // cantidad de bytes del bitmap
    for (uint8_t i = 0; i < bitmapBytes; i++) Midi.write(bitmap[i]);
    Midi.write(0xF7);
    Midi.flush();
}

// ---------------------------------------------------------------------------
// Servidor TCP y parser de comandos de texto
// ---------------------------------------------------------------------------
//
// Protocolo (una linea de texto ASCII por comando, terminada en \n; el \r se
// ignora). No distingue mayus/minus. La respuesta empieza con "OK" o "ERR".
//
//   PLAY | STOP | REW | FF | REC | REC_EXIT | PAUSE | RESET
//   LOCATE0                     -> vuelve a 00:00:00:00
//   LOCATE hh:mm:ss:ff[:sf]     -> posicion absoluta (subframes opcional)
//   ARM n                       -> alterna el arm de la pista n (1..24)
//   ARM n ON | OFF              -> fija el arm de la pista n
//   ARMCLEAR                    -> desarma todas las pistas
//   ARMSTATE                    -> devuelve la mascara actual de pistas armadas
//   PING                        -> responde PONG (keep-alive / descubrimiento)
//
// Ejemplos de respuesta:  "OK PLAY"  /  "OK ARM 3 ON"  /  "ERR bad command"

WiFiServer server(TCP_PORT);
WiFiClient client;

// Devuelve la mascara de pistas armadas como "1,3,5" (o "-" si ninguna).
String armedTracksToString() 
{
    String out;
    for (uint8_t t = 1; t <= NUM_TRACKS; t++) 
    {
      if (armedTracks & (1UL << (t - 1))) 
      {
        if (out.length()) out += ",";
        out += String(t);
      }
    }
    
    return out.length() ? out : String("-");
}

// Parsea "hh:mm:ss:ff" o "hh:mm:ss:ff:sf" y hace LOCATE. Devuelve true si ok.
bool parseAndLocate(const String& arg) 
{
    int v[5] = {0, 0, 0, 0, 0};   // hr, mn, sc, fr, sf
    int count = 0;
    int start = 0;

    while (count < 5) 
    {
      int sep = arg.indexOf(':', start);
      String tok = (sep < 0) ? arg.substring(start) : arg.substring(start, sep);
      tok.trim();
      if (tok.length() == 0) return false;
      v[count++] = tok.toInt();
      if (sep < 0) break;
      start = sep + 1;
    }
    
    if (count < 4) 
      return false;   // hace falta al menos hh:mm:ss:ff
    
    sendLocate((uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2],
              (uint8_t)v[3], (uint8_t)v[4]);
    
    return true;
}

// Ejecuta una linea de comando y devuelve la respuesta (sin el \n final).
String handleCommand(String line) 
{

    line.trim();

    if (line.length() == 0) 
      return String("ERR empty");

    // Separo el verbo (primera palabra) del resto de argumentos.
    int sp = line.indexOf(' ');
    String verb = (sp < 0) ? line : line.substring(0, sp);
    String args = (sp < 0) ? String("") : line.substring(sp + 1);
    args.trim();
    verb.toUpperCase();

    // REW/FF arrancan el bobinado continuo (loop() reenvia el byte).
    // El resto de los comandos de transporte lo cancelan (windCmd = 0).
    if (verb == "PLAY")     { windCmd = 0; sendMmc(MMC_DEFERRED_PLAY); return "OK PLAY"; }
    if (verb == "STOP")     { windCmd = 0; sendMmc(MMC_STOP);          return "OK STOP"; }
    if (verb == "REW")      { windCmd = MMC_REWIND;       sendMmc(MMC_REWIND);       return "OK REW"; }
    if (verb == "FF")       { windCmd = MMC_FAST_FORWARD; sendMmc(MMC_FAST_FORWARD); return "OK FF"; }
    if (verb == "REC")      { sendMmc(MMC_RECORD_STROBE); return "OK REC"; }
    if (verb == "REC_EXIT") { sendMmc(MMC_RECORD_EXIT);   return "OK REC_EXIT"; }
    if (verb == "PAUSE")    { windCmd = 0; sendMmc(MMC_PAUSE);         return "OK PAUSE"; }
    if (verb == "RESET")    { windCmd = 0; sendMmc(MMC_RESET);         return "OK RESET"; }
    if (verb == "LOCATE0")  { windCmd = 0; sendLocate(0,0,0,0,0);      return "OK LOCATE0"; }
    if (verb == "PING")     { return "PONG"; }

    if (verb == "LOCATE")
    {
      windCmd = 0;   // un LOCATE detiene cualquier bobinado en curso
      if (parseAndLocate(args))
        return "OK LOCATE " + args;
      
      return "ERR locate needs hh:mm:ss:ff[:sf]";
    }

    if (verb == "ARMCLEAR") 
    {
      armedTracks = 0;
      sendTrackRecordReady();
    
      return "OK ARMCLEAR";
    }

    if (verb == "ARMSTATE") 
      return "OK ARMSTATE " + armedTracksToString();

    if (verb == "ARM") 
    {
        // "ARM n" (toggle) o "ARM n ON|OFF".
        int sp2 = args.indexOf(' ');
        String nStr = (sp2 < 0) ? args : args.substring(0, sp2);
        String mode = (sp2 < 0) ? String("") : args.substring(sp2 + 1);
        mode.trim();  mode.toUpperCase();

        int n = nStr.toInt();
        if (n < 1 || n > NUM_TRACKS) 
          return "ERR track out of range 1.." + String(NUM_TRACKS);

        uint32_t bit = (1UL << (n - 1));
        if (mode == "" )        
          armedTracks ^= bit;          // toggle
        else if (mode == "ON")  
          armedTracks |= bit;
        else if (mode == "OFF") 
          armedTracks &= ~bit;
        else 
          return "ERR arm mode must be ON or OFF";

        sendTrackRecordReady();
        
        bool on = (armedTracks & bit) != 0;        
        return "OK ARM " + String(n) + (on ? " ON" : " OFF");
    }

    return "ERR unknown command: " + verb;
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
void connectWifi() 
{

    Serial.print("Conectando a WiFi \"");
    Serial.print(WIFI_SSID);
    Serial.print("\" ");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED) 
    {
      delay(400);
      Serial.print(".");
    }
    
    Serial.println(" conectado.");
    Serial.print("IP del controlador: ");
    Serial.println(WiFi.localIP());

    if (MDNS.begin(MDNS_NAME)) 
    {
      MDNS.addService("hd24", "tcp", TCP_PORT);   // servicio anunciado
      Serial.print("mDNS activo: ");
      Serial.print(MDNS_NAME);
      Serial.print(".local:");
      Serial.println(TCP_PORT);
    }
}

// ---------------------------------------------------------------------------
// Procesamiento de comandos comun a ambos transportes
// ---------------------------------------------------------------------------
//
// Tanto WiFiClient como BluetoothSerial heredan de Stream, asi que el mismo
// codigo lee una linea, la despacha a handleCommand() y responde por donde llego.
void processLine(Stream& io)
{
    String line = io.readStringUntil('\n');
    line.replace("\r", "");
    String resp = handleCommand(line);
    io.println(resp);
    Serial.print("< ");  Serial.print(line);
    Serial.print("  > "); Serial.println(resp);
}

// ---------------------------------------------------------------------------
// Bucle por transporte
// ---------------------------------------------------------------------------
void loopWifi()
{
    // Reconexion automatica si se cae el WiFi.
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("WiFi caido, reconectando...");
      connectWifi();
    }

    // Aceptar un cliente nuevo si no hay ninguno conectado.
    if (!client || !client.connected())
    {
        // Seguridad: sin cliente, cortar cualquier bobinado en curso.
        windCmd = 0;

        WiFiClient incoming = server.available();

        if (incoming)
        {
          client = incoming;
          Serial.print("Cliente conectado: ");
          Serial.println(client.remoteIP());
          client.println("HD24 MIDI controller ready");
        }
    }

    // Leer comandos del cliente, linea por linea.
    if (client && client.connected() && client.available())
      processLine(client);
}

void loopBt()
{
    // Detectar (des)conexion del cliente Bluetooth para saludar / frenar bobinado.
    static bool wasConnected = false;
    bool now = SerialBT.hasClient();

    if (now && !wasConnected)
    {
      Serial.println("Cliente Bluetooth conectado");
      SerialBT.println("HD24 MIDI controller ready");
    }

    // Seguridad: sin cliente, cortar cualquier bobinado en curso.
    if (!now)
      windCmd = 0;

    wasConnected = now;

    // Leer comandos del cliente, linea por linea.
    if (now && SerialBT.available())
      processLine(SerialBT);
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);                                  // USB, solo depuracion
    Midi.begin(MIDI_BAUD, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

    // Elegir transporte una sola vez, segun el pin de seleccion.
    // Libre (pull-up, HIGH) = WiFi ; puenteado a GND (LOW) = Bluetooth.
    pinMode(MODE_SELECT_PIN, INPUT_PULLUP);
    delay(10);   // dejar estabilizar el pull-up antes de leer
    transport = (digitalRead(MODE_SELECT_PIN) == LOW) ? MODE_BT : MODE_WIFI;
    
//    transport = MODE_BT;

    if (transport == MODE_WIFI)
    {
      Serial.println("Modo: WiFi / TCP");
      connectWifi();
      server.begin();
      Serial.print("Servidor TCP escuchando en el puerto ");
      Serial.println(TCP_PORT);
    }
    else
    {
      Serial.println("Modo: Bluetooth (SPP)");
      SerialBT.begin(BT_NAME);
      Serial.print("Bluetooth activo, emparejar con: ");
      Serial.println(BT_NAME);
    }
}

void loop()
{
    // Bobinado continuo (comun a ambos transportes): mientras REW/FF esten
    // activos, reenviar el comando cada WIND_REPEAT_MS para que la HD24 siga.
    if (windCmd != 0 && (millis() - lastWindMs) >= WIND_REPEAT_MS)
    {
      sendMmc(windCmd);
      lastWindMs = millis();
    }

    if (transport == MODE_WIFI)
      loopWifi();
    else
      loopBt();
}
