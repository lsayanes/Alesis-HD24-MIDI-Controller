#include "Bluetooth.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>

// ---------------------------------------------------------------------------
// Construccion / destruccion
// ---------------------------------------------------------------------------
Bluetooth::Bluetooth(QObject *parent)
    : QObject(parent)
    , m_socket(new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this))
    , m_agent(new QBluetoothDeviceDiscoveryAgent(this))
{
    // Senales del socket SPP -> manejadores privados.
    connect(m_socket, &QBluetoothSocket::connected,    this, &Bluetooth::onConnected);
    connect(m_socket, &QBluetoothSocket::disconnected, this, &Bluetooth::onDisconnected);
    connect(m_socket, &QBluetoothSocket::readyRead,    this, &Bluetooth::onReadyRead);
    connect(m_socket, &QBluetoothSocket::errorOccurred, this, &Bluetooth::onSocketError);

    // Senales del agente de descubrimiento.
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &Bluetooth::onDeviceDiscovered);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &Bluetooth::onDiscoveryFinished);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::canceled,
            this, &Bluetooth::onDiscoveryFinished);
}

Bluetooth::~Bluetooth()
{
    stopDiscovery();
    disconnectFromDevice();
}

// ---------------------------------------------------------------------------
// Descubrimiento
// ---------------------------------------------------------------------------
void Bluetooth::startDiscovery()
{
    if (m_agent->isActive())
        m_agent->stop();

    // SPP es Bluetooth Classic, asi que buscamos dispositivos clasicos.
    m_agent->start(QBluetoothDeviceDiscoveryAgent::ClassicMethod);
}

void Bluetooth::stopDiscovery()
{
    if (m_agent && m_agent->isActive())
        m_agent->stop();
}

bool Bluetooth::isDiscovering() const
{
    return m_agent && m_agent->isActive();
}

void Bluetooth::onDeviceDiscovered(const QBluetoothDeviceInfo &info)
{
    emit deviceDiscovered(info.name(), info.address().toString());

    // Modo "conectar por nombre": si este equipo coincide, nos conectamos y
    // dejamos de buscar.
    if (!m_autoConnectName.isEmpty() &&
        info.name().compare(m_autoConnectName, Qt::CaseInsensitive) == 0)
    {
        const QString name = m_autoConnectName;
        m_autoConnectName.clear();
        stopDiscovery();
        connectToDevice(info.address());
    }
}

void Bluetooth::onDiscoveryFinished()
{
    m_autoConnectName.clear();   // si no lo encontramos, cancelamos el auto-connect
    emit discoveryFinished();
}

// ---------------------------------------------------------------------------
// Conexion
// ---------------------------------------------------------------------------
void Bluetooth::connectToDevice(const QBluetoothAddress &address)
{
    m_address = address.toString();
    m_buffer.clear();

    if (m_socket->state() != QBluetoothSocket::SocketState::UnconnectedState)
        m_socket->abort();

    // Perfil SPP (Serial Port Profile) sobre RFCOMM: es el que expone el ESP32.
    const QBluetoothUuid spp(QBluetoothUuid::ServiceClassUuid::SerialPort);
    m_socket->connectToService(address, spp);
}

void Bluetooth::connectToDevice(const QString &address)
{
    connectToDevice(QBluetoothAddress(address));
}

void Bluetooth::connectToName(const QString &name)
{
    m_autoConnectName = name;
    startDiscovery();
}

void Bluetooth::disconnectFromDevice()
{
    if (!m_socket)
        return;

    if (m_socket->state() == QBluetoothSocket::SocketState::UnconnectedState)
        return;

    m_socket->disconnectFromService();
}

bool Bluetooth::isConnected() const
{
    return m_socket &&
           m_socket->state() == QBluetoothSocket::SocketState::ConnectedState;
}

// ---------------------------------------------------------------------------
// Envio de comandos
// ---------------------------------------------------------------------------
bool Bluetooth::sendCommand(const QString &command)
{
    if (!isConnected())
    {
        emit errorOccurred(QStringLiteral("No hay conexion Bluetooth para enviar: %1").arg(command));
        return false;
    }

    QString line = command;
    if (!line.endsWith(QLatin1Char('\n')))
        line.append(QLatin1Char('\n'));

    const QByteArray data = line.toUtf8();
    const qint64 written = m_socket->write(data);
    return written == data.size();
}

// ---------------------------------------------------------------------------
// Manejadores de las senales del socket
// ---------------------------------------------------------------------------
void Bluetooth::onConnected()
{
    m_buffer.clear();
    emit connected();
}

void Bluetooth::onDisconnected()
{
    emit disconnected();
}

void Bluetooth::onReadyRead()
{
    // Igual que en TCP: SPP es un flujo de bytes, reensamblamos por lineas '\n'
    // y quitamos el '\r' final si viene.
    m_buffer.append(m_socket->readAll());

    int nl;
    while ((nl = m_buffer.indexOf('\n')) != -1)
    {
        QByteArray raw = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);

        if (raw.endsWith('\r'))
            raw.chop(1);

        emit lineReceived(QString::fromUtf8(raw));
    }
}

void Bluetooth::onSocketError(QBluetoothSocket::SocketError /*error*/)
{
    emit errorOccurred(m_socket->errorString());
}
