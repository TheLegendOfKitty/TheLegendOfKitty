#pragma once

// A pretend pair of Space Travel earbuds speaking GAIA v3, for tests and the
// --demo mode. It behaves like a sequential socket: bytes written to it are
// parsed as GAIA frames and replies become readable asynchronously.

#include "gaia/gaiaframe.h"
#include "gaia/gaiaprotocol.h"

#include <QHash>
#include <QIODevice>
#include <QVector>

namespace sim {

class SimulatedDevice : public QIODevice
{
    Q_OBJECT

public:
    explicit SimulatedDevice(QObject *parent = nullptr);

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;

    // Behaviour knobs for tests.
    void setSilent(bool silent) { m_silent = silent; }
    void setReplyDelayMs(int ms) { m_replyDelayMs = ms; }
    void setSendActionNotifications(bool send) { m_sendActionNotifications = send; }
    void setApiVersion(int protocolVersion, int major, int minor);
    void setFeatures(const QVector<gaia::FeatureInfo> &features) { m_features = features; }
    void failCommand(gaia::Feature feature, quint8 command, int status);

    // Pushes a notification as if the earbuds sent it on their own.
    void sendNotification(gaia::Feature feature, quint8 command, const QByteArray &payload);

    // State, for assertions.
    QByteArray currentActions() const { return m_currentActions; }
    QByteArray defaultActions() const { return m_defaultActions; }
    int selectedPreset() const { return m_selectedPreset; }
    QVector<gaia::EqBand> userBands() const { return m_userBands; }
    QVector<gaia::Frame> receivedFrames() const { return m_received; }
    quint8 lastFrameVersion() const { return m_lastFrameVersion; }

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 size) override;

private:
    void handleFrame(const gaia::Frame &frame);
    void handleBasic(quint8 command, const QByteArray &payload);
    void handleEarbud(quint8 command, const QByteArray &payload);
    void handleMusic(quint8 command, const QByteArray &payload);
    void respond(gaia::Feature feature, quint8 command, const QByteArray &payload = {});
    void respondError(gaia::Feature feature, quint8 command, int status);
    void queue(const QByteArray &frame);
    void flush();

    gaia::FrameDecoder m_decoder;
    QByteArray m_outgoing;
    QByteArray m_staged;
    QVector<gaia::Frame> m_received;
    bool m_silent = false;
    bool m_flushScheduled = false;
    int m_replyDelayMs = 5;
    bool m_sendActionNotifications = true;
    quint8 m_lastFrameVersion = 1;
    quint8 m_protocolVersion = 3;
    int m_apiMajor = 3;
    int m_apiMinor = 0;
    QVector<gaia::FeatureInfo> m_features;
    QHash<quint16, int> m_failures;  // feature << 8 | command -> status

    QByteArray m_defaultActions;
    QByteArray m_currentActions;
    bool m_eqPresent = true;
    QVector<int> m_presets;
    int m_selectedPreset = 1;
    QVector<gaia::EqBand> m_userBands;
};

} // namespace sim
