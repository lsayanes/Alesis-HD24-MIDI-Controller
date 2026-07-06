#include "Bluetooth.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothServiceDiscoveryAgent>
#include <QBluetoothServiceInfo>
#include <QBluetoothUuid>
#include <QTimer>
#include <QCoreApplication>

#if QT_CONFIG(permissions)
#include <QPermissions>
#endif

// ---------------------------------------------------------------------------
// Construccion / destruccion
// ---------------------------------------------------------------------------
Bluetooth::Bluetooth(QObject *parent)
    : QObject(parent)
    , m_socket(new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this))
    , m_agent(new QBluetoothDeviceDiscoveryAgent(this))
    , m_serviceAgent(new QBluetoothServiceDiscoveryAgent(this))
    , m_connectTimer(new QTimer(this))
{
    m_connectTimer->setSingleShot(true);

    connect(m_socket, &QBluetoothSocket::connected,    this, &Bluetooth::onConnected);
    connect(m_socket, &QBluetoothSocket::disconnected, this, &Bluetooth::onDisconnected);
    connect(m_socket, &QBluetoothSocket::readyRead,    this, &Bluetooth::onReadyRead);
    connect(m_socket, &QBluetoothSocket::errorOccurred, this, &Bluetooth::onSocketError);

    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &Bluetooth::onDeviceDiscovered);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &Bluetooth::onDiscoveryFinished);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::canceled,
            this, &Bluetooth::onDiscoveryFinished);

    connect(m_serviceAgent, &QBluetoothServiceDiscoveryAgent::serviceDiscovered,
            this, &Bluetooth::onServiceDiscovered);
    connect(m_serviceAgent, &QBluetoothServiceDiscoveryAgent::finished,
            this, &Bluetooth::onServiceDiscoveryFinished);
    connect(m_serviceAgent, &QBluetoothServiceDiscoveryAgent::canceled,
            this, &Bluetooth::onServiceDiscoveryFinished);
    connect(m_serviceAgent, &QBluetoothServiceDiscoveryAgent::errorOccurred,
            this, &Bluetooth::onServiceDiscoveryError);

    connect(m_connectTimer, &QTimer::timeout, this, &Bluetooth::onConnectTimeout);
}

Bluetooth::~Bluetooth()
{
    cancelConnectAttempt();
    stopDiscovery();
    disconnectFromDevice();
}

// ---------------------------------------------------------------------------
// Descubrimiento de dispositivos
// ---------------------------------------------------------------------------
// Pide el permiso Bluetooth en runtime (Android 12+ / iOS / macOS moderno).
// Devuelve true si ya esta concedido; si esta pendiente, ejecuta 'retry'
// cuando el usuario lo conceda.
bool Bluetooth::ensurePermission(std::function<void()> retry)
{
#if QT_CONFIG(permissions)
    QBluetoothPermission perm;
    perm.setCommunicationModes(QBluetoothPermission::Access);

    switch (qApp->checkPermission(perm))
    {
    case Qt::PermissionStatus::Granted:
        return true;
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(perm, this,
            [this, retry](const QPermission &p)
            {
                if (p.status() == Qt::PermissionStatus::Granted)
                    retry();
                else
                    emit errorOccurred(tr("Permiso Bluetooth denegado por el usuario"));
            });
        return false;
    case Qt::PermissionStatus::Denied:
        emit errorOccurred(tr(
            "Permiso Bluetooth denegado. Habilitalo en Ajustes > Apps > RC-HD24 > Permisos."));
        return false;
    }
#else
    Q_UNUSED(retry);
#endif
    return true;
}

void Bluetooth::startDiscovery()
{
    if (!ensurePermission([this]{ startDiscovery(); }))
        return;

    if (m_agent->isActive())
        m_agent->stop();

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

    if (!m_autoConnectName.isEmpty() &&
        info.name().compare(m_autoConnectName, Qt::CaseInsensitive) == 0)
    {
        m_autoConnectName.clear();
        stopDiscovery();
        connectToDevice(info.address());
    }
}

void Bluetooth::onDiscoveryFinished()
{
    m_autoConnectName.clear();
    emit discoveryFinished();
}

// ---------------------------------------------------------------------------
// Conexion
// ---------------------------------------------------------------------------
void Bluetooth::cancelConnectAttempt()
{
    if (m_connectTimer)
        m_connectTimer->stop();

    if (m_serviceAgent && m_serviceAgent->isActive())
        m_serviceAgent->stop();
}

void Bluetooth::startConnectTimeout()
{
    m_connectTimer->start(CONNECT_TIMEOUT_MS);
}

void Bluetooth::connectViaSdp(const QBluetoothAddress &address)
{
    m_pendingAddress = address;
    m_serviceFound = false;
    m_triedDirectChannel = false;

    if (m_serviceAgent->isActive())
        m_serviceAgent->stop();

    m_serviceAgent->setRemoteAddress(address);
    const QBluetoothUuid spp(QBluetoothUuid::ServiceClassUuid::SerialPort);
    m_serviceAgent->setUuidFilter(spp);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // MinimalDiscovery hace una consulta SDP directa sobre la direccion ya
    // emparejada. FullDiscovery, en macOS, lanza un inquiry completo
    // (IOBluetoothDeviceInquiry) innecesario y lento: el ESP32 suele dejar de
    // ser "inquiry-discoverable" tras conectar/desconectar y el escaneo expira.
    m_serviceAgent->start(QBluetoothServiceDiscoveryAgent::MinimalDiscovery);
#else
    m_serviceAgent->start(spp);
#endif
}

void Bluetooth::connectRfcomm(const QBluetoothAddress &address, quint16 channel)
{
    if (m_socket->state() != QBluetoothSocket::SocketState::UnconnectedState)
        m_socket->abort();

    m_socket->connectToService(address, channel);
}

void Bluetooth::connectToDevice(const QBluetoothAddress &address)
{
    if (!ensurePermission([this, address]{ connectToDevice(address); }))
        return;

    m_address = address.toString();
    m_buffer.clear();

    stopDiscovery();
    cancelConnectAttempt();

    if (m_socket->state() != QBluetoothSocket::SocketState::UnconnectedState)
        m_socket->abort();

    // connectToService(address, uuid) suele quedarse en ServiceLookupState
    // (consulta SDP) y en macOS a veces hace timeout sin mensaje util.
    // Primero descubrimos el servicio SPP; si falla, probamos canal RFCOMM 1.
    startConnectTimeout();
    connectViaSdp(address);
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
    cancelConnectAttempt();

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

void Bluetooth::onServiceDiscovered(const QBluetoothServiceInfo &info)
{
    if (info.device().address() != m_pendingAddress)
        return;

    const auto spp = QBluetoothUuid(QBluetoothUuid::ServiceClassUuid::SerialPort);
    const bool isSpp = info.serviceUuid() == spp
                       || info.serviceClassUuids().contains(spp)
                       || info.protocolServiceMultiplexer() > 0;

    if (!isSpp)
        return;

    m_serviceFound = true;
    m_serviceAgent->stop();

    if (m_socket->state() != QBluetoothSocket::SocketState::UnconnectedState)
        m_socket->abort();

    m_socket->connectToService(info);
}

void Bluetooth::onServiceDiscoveryFinished()
{
    if (m_serviceFound || isConnected())
        return;

    if (!m_triedDirectChannel)
    {
        m_triedDirectChannel = true;
        connectRfcomm(m_pendingAddress, DEFAULT_RFCOMM_CHANNEL);
        return;
    }

    cancelConnectAttempt();
    emit errorOccurred(tr(
        "No se encontro el servicio SPP del ESP32. "
        "En macOS, empareja \"HD24\" en Ajustes del Sistema > Bluetooth antes de conectar."));
}

void Bluetooth::onServiceDiscoveryError()
{
    if (m_serviceFound || isConnected())
        return;

    if (!m_triedDirectChannel)
    {
        m_triedDirectChannel = true;
        connectRfcomm(m_pendingAddress, DEFAULT_RFCOMM_CHANNEL);
        return;
    }

    cancelConnectAttempt();
    const QString detail = m_serviceAgent->errorString();
    emit errorOccurred(tr("Error al buscar servicio Bluetooth SPP: %1")
                           .arg(detail.isEmpty() ? tr("desconocido") : detail));
}

void Bluetooth::onConnectTimeout()
{
    if (isConnected())
        return;

    cancelConnectAttempt();
    m_socket->abort();

    emit errorOccurred(tr(
        "Timeout al conectar por Bluetooth (%1 s). "
        "Verifica que el ESP32 este en modo BT, que \"HD24\" este emparejado "
        "en Ajustes del Sistema > Bluetooth (macOS) y que no haya otra app conectada.")
                           .arg(CONNECT_TIMEOUT_MS / 1000));
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
// Manejadores del socket
// ---------------------------------------------------------------------------
void Bluetooth::onConnected()
{
    cancelConnectAttempt();
    m_buffer.clear();
    emit connected();
}

void Bluetooth::onDisconnected()
{
    cancelConnectAttempt();
    emit disconnected();
}

void Bluetooth::onReadyRead()
{
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

QString Bluetooth::describeSocketError(QBluetoothSocket::SocketError error,
                                         const QString &fallback)
{
    switch (error)
    {
    case QBluetoothSocket::SocketError::NoSocketError:
        return QString();
    case QBluetoothSocket::SocketError::UnknownSocketError:
        return QObject::tr("Error de socket Bluetooth desconocido");
    case QBluetoothSocket::SocketError::ServiceNotFoundError:
        return QObject::tr("Servicio SPP no encontrado (empareja \"HD24\" en Ajustes del Sistema)");
    case QBluetoothSocket::SocketError::NetworkError:
        return QObject::tr("Error de red Bluetooth");
    case QBluetoothSocket::SocketError::UnsupportedProtocolError:
        return QObject::tr("Protocolo RFCOMM no soportado en esta plataforma");
    case QBluetoothSocket::SocketError::OperationError:
        return QObject::tr("Operacion Bluetooth rechazada");
    default:
        break;
    }

    if (!fallback.isEmpty())
        return fallback;

    return QObject::tr("Error Bluetooth (codigo %1)").arg(static_cast<int>(error));
}

void Bluetooth::onSocketError(QBluetoothSocket::SocketError error)
{
    if (error == QBluetoothSocket::SocketError::NoSocketError)
        return;

    cancelConnectAttempt();

    QString msg = describeSocketError(error, m_socket->errorString());
    const auto state = m_socket->state();
    msg += tr(" [estado=%1, MAC=%2]")
               .arg(static_cast<int>(state))
               .arg(m_address);

#ifdef Q_OS_MACOS
    msg += tr(". En macOS, empareja el dispositivo antes de conectar desde la app.");
#endif

    emit errorOccurred(msg);
}
