#pragma once

// ---------------------------------------------------------------------------
// Bluetooth — transporte Bluetooth Classic (SPP) hacia el controlador HD24.
// ---------------------------------------------------------------------------
//
// El firmware del ESP32 (hd24_midi_controller.ino), en modo Bluetooth, expone
// un puerto serie por Bluetooth Classic (SPP / RFCOMM) con el nombre "HD24".
// El protocolo es EXACTAMENTE el mismo que por TCP: lineas ASCII terminadas en
// '\n', respuestas "OK ..." / "ERR ..." / "PONG" y el saludo inicial
// "HD24 MIDI controller ready". Por eso esta clase expone la MISMA interfaz que
// Tcp (connectXxx / disconnect / isConnected / sendCommand + las senales
// connected/disconnected/lineReceived/errorOccurred): RCHD24 puede usar uno u
// otro transporte sin distinguirlos.
//
// Extra respecto de Tcp: como el usuario no sabe la direccion MAC del equipo,
// esta clase incluye descubrimiento de dispositivos (QBluetoothDeviceDiscovery
// Agent) para listar los HD24 cercanos y conectarse por nombre o direccion.
//
// NOTA (plataforma): SPP/RFCOMM funciona en Android, Linux y Windows. En macOS
// el soporte de Qt para Bluetooth Classic es limitado; el objetivo real de este
// transporte es Android. Requiere el modulo Qt Bluetooth (Qt6::Bluetooth) y, en
// Android, permisos BLUETOOTH_CONNECT / BLUETOOTH_SCAN en el manifest.

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QList>
#include <QBluetoothAddress>
#include <QBluetoothSocket>

class QBluetoothDeviceDiscoveryAgent;
class QBluetoothDeviceInfo;

class Bluetooth : public QObject
{
    Q_OBJECT

public:
    // Nombre con el que el ESP32 se anuncia por Bluetooth (const BT_NAME del sketch).
    static constexpr const char *DEFAULT_DEVICE_NAME = "HD24";

    explicit Bluetooth(QObject *parent = nullptr);
    ~Bluetooth() override;

    // --- Descubrimiento -----------------------------------------------------
    // Arranca la busqueda de dispositivos Bluetooth cercanos. Cada equipo hallado
    // se informa por deviceDiscovered(); al terminar, discoveryFinished().
    void startDiscovery();
    void stopDiscovery();
    bool isDiscovering() const;

    // --- Conexion -----------------------------------------------------------
    // Conecta al servicio SPP del equipo con esa direccion MAC ("AA:BB:CC:DD:EE:FF").
    void connectToDevice(const QBluetoothAddress &address);
    void connectToDevice(const QString &address);

    // Comodidad: lanza un descubrimiento y se conecta automaticamente al primer
    // equipo cuyo nombre coincida con 'name' (por defecto "HD24").
    void connectToName(const QString &name = QString::fromLatin1(DEFAULT_DEVICE_NAME));

    // Cierra la conexion (si habia una abierta).
    void disconnectFromDevice();

    // true si el socket SPP esta conectado y listo.
    bool isConnected() const;

    // Direccion del equipo actualmente conectado / al que nos conectamos.
    QString peerAddress() const { return m_address; }

public slots:
    // Envia un comando de texto. Se le agrega el '\n' que espera el firmware.
    // Devuelve false si no hay conexion.
    bool sendCommand(const QString &command);

signals:
    void connected();
    void disconnected();
    void lineReceived(const QString &line);
    void errorOccurred(const QString &message);

    // Un equipo Bluetooth encontrado durante el descubrimiento.
    void deviceDiscovered(const QString &name, const QString &address);
    // Termino la busqueda de dispositivos.
    void discoveryFinished();

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QBluetoothSocket::SocketError error);
    void onDeviceDiscovered(const QBluetoothDeviceInfo &info);
    void onDiscoveryFinished();

private:
    QBluetoothSocket               *m_socket = nullptr;
    QBluetoothDeviceDiscoveryAgent *m_agent  = nullptr;
    QByteArray                      m_buffer;   // reensamblado de lineas
    QString                         m_address;  // MAC del equipo actual

    // Cuando connectToName() esta activo, guarda el nombre buscado para
    // conectarse en cuanto el descubrimiento lo encuentre.
    QString m_autoConnectName;
};
