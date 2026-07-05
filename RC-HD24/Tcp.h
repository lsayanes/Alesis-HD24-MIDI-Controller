#pragma once

// ---------------------------------------------------------------------------
// Tcp — transporte TCP/IP hacia el controlador HD24 (ESP32 en modo WiFi).
// ---------------------------------------------------------------------------
//
// El firmware del ESP32 (hd24_midi_controller.ino) levanta un servidor TCP en
// el puerto 5005 y habla un protocolo de TEXTO por lineas: cada comando es una
// linea ASCII terminada en '\n' (el '\r' se ignora) y cada respuesta llega
// tambien como una linea ("OK ...", "ERR ..." o "PONG"). Ademas, al conectar,
// el equipo saluda con "HD24 MIDI controller ready".
//
// Esta clase encapsula TODO lo relacionado con TCP: abrir/cerrar el socket,
// reensamblar las lineas que llegan (TCP no respeta limites de mensaje) y
// enviar comandos. No conoce el protocolo de alto nivel (PLAY, ARM, LOCATE...);
// eso vive en la capa de aplicacion, que solo llama a sendCommand() y escucha
// la senal lineReceived(). La clase Bluetooth expone la misma interfaz, asi que
// RCHD24 puede intercambiar un transporte por el otro sin cambios.

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QAbstractSocket>

class QTcpSocket;

class Tcp : public QObject
{
    Q_OBJECT

public:
    // Puerto TCP por defecto del firmware (const TCP_PORT del sketch ESP32).
    static constexpr quint16 DEFAULT_PORT = 5005;

    explicit Tcp(QObject *parent = nullptr);
    ~Tcp() override;

    // Conecta al servidor TCP del ESP32. 'host' puede ser una IP ("192.168.1.50")
    // o el nombre mDNS que anuncia el firmware ("hd24.local"). No bloquea: el
    // resultado llega por las senales connected() / errorOccurred().
    void connectToHost(const QString &host, quint16 port = DEFAULT_PORT);

    // Cierra la conexion de forma ordenada (si habia una abierta).
    void disconnectFromHost();

    // true si el socket esta conectado y listo para enviar/recibir.
    bool isConnected() const;

    // Ultimo host/puerto usados (utiles para reintentar la conexion).
    QString host() const { return m_host; }
    quint16 port() const { return m_port; }

public slots:
    // Envia un comando de texto. Se le agrega el '\n' que espera el firmware
    // (si ya termina en '\n' no se duplica). Devuelve false si no hay conexion.
    bool sendCommand(const QString &command);

signals:
    // Socket conectado y operativo.
    void connected();
    // Socket cerrado (por nosotros o por el equipo).
    void disconnected();
    // Una respuesta COMPLETA del equipo, ya sin el '\r'/'\n' del final.
    void lineReceived(const QString &line);
    // Error de conexion/transporte, con un mensaje legible.
    void errorOccurred(const QString &message);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    QTcpSocket *m_socket = nullptr;
    QByteArray  m_buffer;          // acumula bytes hasta completar lineas
    QString     m_host;
    quint16     m_port = 0;
};
