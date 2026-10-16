#pragma once

// Drives a GAIA v3 conversation over any QIODevice (an RFCOMM socket in the
// app, a simulated device in tests). Mirrors the MOONDROP Link start-up order:
//
//   1. legacy GET_API_VERSION (vendor 0x000A, command 0x0300)
//   2. Basic GET_SUPPORTED_FEATURES
//   3. Basic SET_TRANSPORT_PARAMETER(protocol version 4) if Basic >= v2
//   4. Basic REGISTER_NOTIFICATION for each feature we use
//   5. device info reads, then the session is ready

#include "gaia/gaiaframe.h"
#include "gaia/gaiaprotocol.h"
#include "gaia/touchmapping.h"

#include <QElapsedTimer>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <optional>

class QIODevice;

namespace gaia {

class Session : public QObject
{
    Q_OBJECT

public:
    enum class State { Idle, Handshaking, Discovering, Ready, Failed };
    Q_ENUM(State)

    enum class EarbudPosition { Unknown = -1, Left = 0, Right = 1 };

    struct DeviceInfo
    {
        int protocolVersion = 0;
        int apiMajor = 0;
        int apiMinor = 0;
        int gaiaVersion = -1;
        QString serialNumber;
        QString variantName;
        QString firmwareVersion;
        QVector<FeatureInfo> features;
    };

    explicit Session(QObject *parent = nullptr);
    ~Session() override;

    // The session does not take ownership of `io`.
    void start(QIODevice *io);
    void stop();

    State state() const { return m_state; }
    const DeviceInfo &deviceInfo() const { return m_info; }
    quint8 frameVersion() const { return m_frameVersion; }
    bool hasFeature(Feature f) const { return featureVersion(f) >= 0; }
    int featureVersion(Feature f) const;
    EarbudPosition primaryEarbud() const { return m_primary; }

    void setRequestTimeout(int ms) { m_requestTimeoutMs = ms; }
    void setTouchConfirmTimeout(int ms) { m_touchConfirmTimeoutMs = ms; }

    // Device information
    void requestDeviceInfo();
    void requestEarbudPosition();

    // Touch controls
    void requestTouchMapping();
    bool hasCurrentTouchMapping() const { return m_currentTouch.has_value(); }
    std::optional<touch::CurrentMapping> currentTouchMapping() const { return m_currentTouch; }
    std::optional<touch::Mapping> defaultTouchMapping() const { return m_defaultTouch; }
    void applyTouchMapping(const touch::Mapping &edited);
    void restoreDefaultTouchMapping();

    // Equaliser
    void requestEqInfo();
    void selectPreset(int presetId);
    void requestUserBands();
    void setUserBands(const QVector<EqBand> &bands);
    bool eqPresent() const { return m_eqPresent; }
    QVector<int> presets() const { return m_presets; }
    int selectedPreset() const { return m_selectedPreset; }
    int userBandCount() const { return m_userBandCount; }
    QVector<EqBand> userBands() const { return m_userBands; }

signals:
    void stateChanged(gaia::Session::State state);
    void ready();
    void failed(const QString &reason);
    void deviceInfoChanged();
    void earbudPositionChanged(gaia::Session::EarbudPosition primary);
    void handoverAnnounced(bool isStatic, int delaySeconds);

    void touchDefaultsChanged(const touch::Mapping &mapping);
    void touchCurrentChanged(const touch::CurrentMapping &current);
    void touchApplyFinished(bool ok, const QString &message);

    void eqStateChanged(bool present);
    void presetsChanged(const QVector<int> &presetIds);
    void selectedPresetChanged(int presetId);
    void presetSelectFinished(bool ok, const QString &message);
    void userBandCountChanged(int count);
    void userBandsChanged(const QVector<gaia::EqBand> &bands);
    void userBandsApplyFinished(bool ok, const QString &message);

    // A user-visible request failed (timeouts, error replies).
    void commandFailed(const QString &what, const QString &reason);
    // Every frame in and out, for the traffic log.
    void trafficLogged(bool outgoing, const QByteArray &raw, const QString &summary);

private:
    struct Pending
    {
        quint16 vendor = 0;
        quint16 key = 0;  // v3: feature << 8 | command, legacy: command
        qint64 deadline = 0;
    };

    void setState(State state);
    void fail(const QString &reason);
    void onReadyRead();
    void handleFrame(const Frame &frame);
    void handleLegacy(const Frame &frame);
    void handleV3(const Frame &frame);
    void handleBasic(PacketType type, quint8 command, const QByteArray &payload);
    void handleEarbud(PacketType type, quint8 command, const QByteArray &payload);
    void handleMusic(PacketType type, quint8 command, const QByteArray &payload);
    void handleError(quint8 feature, quint8 command, int status);
    void handleTimeout(const Pending &pending);
    void onFeaturesComplete();

    void sendLegacy(quint16 command, const QByteArray &payload = {});
    void sendV3(Feature feature, quint8 command, const QByteArray &payload = {});
    void writeFrame(const QByteArray &frame, const QString &summary);
    bool takePending(quint16 vendor, quint16 key);
    void checkTimeouts();
    void finishTouchApply(bool ok, const QString &message);
    void finishBandSet(bool ok, const QString &message);
    QString describe(const Frame &frame) const;

    QPointer<QIODevice> m_io;
    QMetaObject::Connection m_readConnection;
    FrameDecoder m_decoder;
    State m_state = State::Idle;
    quint8 m_frameVersion = 1;
    DeviceInfo m_info;
    QVector<FeatureInfo> m_pendingFeatures;
    EarbudPosition m_primary = EarbudPosition::Unknown;

    QList<Pending> m_pending;
    QTimer m_timeoutTimer;
    QElapsedTimer m_clock;
    int m_requestTimeoutMs = 10000;

    std::optional<touch::Mapping> m_defaultTouch;
    std::optional<touch::CurrentMapping> m_currentTouch;
    bool m_touchApplyInFlight = false;
    bool m_touchApplyAcked = false;
    QTimer m_touchConfirmTimer;
    int m_touchConfirmTimeoutMs = 5000;

    bool m_eqPresent = false;
    QVector<int> m_presets;
    int m_selectedPreset = -1;
    int m_userBandCount = 0;
    QVector<EqBand> m_userBands;
    int m_bandSetsOutstanding = 0;
    bool m_bandSetFailed = false;
};

} // namespace gaia
