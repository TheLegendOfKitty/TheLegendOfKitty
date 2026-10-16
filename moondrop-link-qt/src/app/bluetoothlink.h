#pragma once

// Opens the RFCOMM link the official app uses: the Serial Port Profile UUID
// first, then Qualcomm's GAIA UUID, or a fixed RFCOMM channel if one is set.

#include <QBluetoothAddress>
#include <QBluetoothSocket>
#include <QBluetoothUuid>
#include <QList>
#include <QObject>
#include <QPointer>

class QIODevice;

class BluetoothLink : public QObject
{
    Q_OBJECT

public:
    explicit BluetoothLink(QObject *parent = nullptr);
    ~BluetoothLink() override;

    // channel 0 = find the service by UUID.
    void connectTo(const QBluetoothAddress &address, quint16 channel = 0);
    void disconnectFrom();

    bool isConnected() const { return m_connected; }
    bool isBusy() const { return m_socket != nullptr; }
    QIODevice *device() const;
    QString serviceDescription() const { return m_service; }

    static QBluetoothUuid gaiaServiceUuid();

signals:
    void connected();
    void disconnected();
    void failed(const QString &reason);
    void progress(const QString &message);

private:
    struct Attempt
    {
        QBluetoothUuid uuid;
        quint16 channel = 0;
        QString label;
    };

    void tryNext();
    void dropSocket();

    QPointer<QBluetoothSocket> m_socket;
    QList<Attempt> m_attempts;
    QBluetoothAddress m_address;
    QString m_service;
    QString m_lastError;
    bool m_connected = false;
};
