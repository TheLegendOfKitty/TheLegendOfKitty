#include "app/bluetoothlink.h"

#include <QUuid>

BluetoothLink::BluetoothLink(QObject *parent)
    : QObject(parent)
{
}

BluetoothLink::~BluetoothLink()
{
    dropSocket();
}

QBluetoothUuid BluetoothLink::gaiaServiceUuid()
{
    // 00001107-D102-11E1-9B23-00025B00A5A5
    return QBluetoothUuid(QUuid(0x00001107, 0xD102, 0x11E1, 0x9B, 0x23, 0x00, 0x02, 0x5B, 0x00, 0xA5, 0xA5));
}

QIODevice *BluetoothLink::device() const
{
    return m_connected ? m_socket.data() : nullptr;
}

void BluetoothLink::connectTo(const QBluetoothAddress &address, quint16 channel)
{
    disconnectFrom();
    m_address = address;
    m_lastError.clear();
    m_attempts.clear();
    if (channel > 0) {
        m_attempts.append({QBluetoothUuid(), channel, tr("RFCOMM channel %1").arg(channel)});
    } else {
        m_attempts.append({QBluetoothUuid(QBluetoothUuid::ServiceClassUuid::SerialPort), 0,
                           tr("Serial Port Profile")});
        m_attempts.append({gaiaServiceUuid(), 0, tr("Qualcomm GAIA service")});
    }
    tryNext();
}

void BluetoothLink::disconnectFrom()
{
    m_attempts.clear();
    const bool wasConnected = m_connected;
    m_connected = false;
    dropSocket();
    if (wasConnected)
        emit disconnected();
}

void BluetoothLink::dropSocket()
{
    if (!m_socket)
        return;
    QBluetoothSocket *socket = m_socket;
    m_socket = nullptr;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
}

void BluetoothLink::tryNext()
{
    dropSocket();
    if (m_attempts.isEmpty()) {
        emit failed(tr("Could not open a GAIA connection: %1. Make sure the earbuds are paired and "
                       "connected to this computer, and that Qt's Bluetooth helper (sdpscanner, "
                       "package libqt6bluetooth6-bin) is installed.")
                        .arg(m_lastError.isEmpty() ? tr("no matching service") : m_lastError));
        return;
    }
    const Attempt attempt = m_attempts.takeFirst();
    m_service = attempt.label;
    emit progress(tr("Connecting via %1…").arg(attempt.label));

    auto *socket = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this);
    m_socket = socket;

    connect(socket, &QBluetoothSocket::connected, this, [this, socket] {
        if (socket != m_socket)
            return;
        m_attempts.clear();
        m_connected = true;
        emit connected();
    });
    connect(socket, &QBluetoothSocket::disconnected, this, [this, socket] {
        if (socket != m_socket || !m_connected)
            return;
        m_connected = false;
        dropSocket();
        emit disconnected();
    });
    connect(socket, &QBluetoothSocket::errorOccurred, this, [this, socket](QBluetoothSocket::SocketError) {
        if (socket != m_socket)
            return;
        m_lastError = socket->errorString();
        if (m_connected) {
            m_connected = false;
            dropSocket();
            emit disconnected();
            return;
        }
        tryNext();
    });

    if (attempt.channel > 0)
        socket->connectToService(m_address, attempt.channel);
    else
        socket->connectToService(m_address, attempt.uuid);
}
