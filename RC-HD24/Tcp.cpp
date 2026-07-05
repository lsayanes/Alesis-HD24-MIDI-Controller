#include "Tcp.h"

#include <QTcpSocket>

// ---------------------------------------------------------------------------
// Construccion / destruccion
// ---------------------------------------------------------------------------
Tcp::Tcp(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
{
    // Conectamos las senales del socket a nuestros manejadores privados. Desde
    // afuera solo se ven las senales de esta clase (connected, lineReceived...).
    connect(m_socket, &QTcpSocket::connected,    this, &Tcp::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &Tcp::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,    this, &Tcp::onReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &Tcp::onSocketError);
}

Tcp::~Tcp()
{
    // El socket es hijo de este QObject, asi que Qt lo destruye solo. Cerramos
    // por prolijidad para no dejar una conexion a medio cerrar.
    disconnectFromHost();
}

// ---------------------------------------------------------------------------
// Conexion
// ---------------------------------------------------------------------------
void Tcp::connectToHost(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
    m_buffer.clear();

    // Si veniamos de otra conexion, la cerramos antes de abrir la nueva.
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();

    m_socket->connectToHost(host, port);
}

void Tcp::disconnectFromHost()
{
    if (!m_socket)
        return;

    if (m_socket->state() == QAbstractSocket::UnconnectedState)
        return;

    m_socket->disconnectFromHost();
    // Si aun no llego a conectar, disconnectFromHost() no dispara nada: forzamos.
    if (m_socket->state() != QAbstractSocket::UnconnectedState &&
        !m_socket->waitForDisconnected(1000))
        m_socket->abort();
}

bool Tcp::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

// ---------------------------------------------------------------------------
// Envio de comandos
// ---------------------------------------------------------------------------
bool Tcp::sendCommand(const QString &command)
{
    if (!isConnected())
    {
        emit errorOccurred(QStringLiteral("No hay conexion TCP para enviar: %1").arg(command));
        return false;
    }

    // El firmware espera una linea terminada en '\n'. No la duplicamos si ya vino.
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
void Tcp::onConnected()
{
    m_buffer.clear();
    emit connected();
}

void Tcp::onDisconnected()
{
    emit disconnected();
}

void Tcp::onReadyRead()
{
    // TCP es un flujo: un readyRead puede traer media linea, varias lineas o
    // una linea partida entre dos lecturas. Acumulamos en m_buffer y emitimos
    // una senal por cada '\n' completo, quitando el '\r' del final si esta.
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

void Tcp::onSocketError(QAbstractSocket::SocketError /*error*/)
{
    emit errorOccurred(m_socket->errorString());
}
