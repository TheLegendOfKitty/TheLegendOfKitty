#include "sim/simulateddevice.h"

#include <QTimer>

#include <cstring>

namespace sim {

using namespace gaia;

namespace {

constexpr quint16 failureKey(Feature feature, quint8 command)
{
    return static_cast<quint16>((static_cast<quint8>(feature) << 8) | command);
}

quint8 slot(quint8 side, quint8 gesture)
{
    return static_cast<quint8>(side | gesture);
}

} // namespace

SimulatedDevice::SimulatedDevice(QObject *parent)
    : QIODevice(parent)
{
    m_features = {{0, 2}, {1, 1}, {5, 1}};

    // Plausible factory layout; order follows the protocol's 11 slots.
    m_defaultActions = QByteArray();
    for (quint8 b : {slot(0x30, 2),   // play/pause: double tap, either
                     slot(0x10, 3),   // previous: triple tap, left
                     slot(0x20, 3),   // next: triple tap, right
                     slot(0x20, 1),   // volume up: single tap, right
                     slot(0x10, 1),   // volume down: single tap, left
                     slot(0x30, 2),   // answer: double tap
                     slot(0x30, 4),   // hang up: long press 1 s
                     slot(0x30, 0),   // assistant start: none
                     slot(0x30, 0),   // assistant cancel: none
                     slot(0x30, 5),   // ANC on: long press 2 s
                     slot(0x30, 5)})  // ANC off: long press 2 s
        m_defaultActions.append(static_cast<char>(b));
    m_currentActions = QByteArray(1, 1) + m_defaultActions;

    m_presets = {1, 2, 3, music::kUserPreset};
    m_userBands = {
        {0, 60, 0.7, static_cast<int>(FilterType::LowShelf2), 0.0},
        {1, 250, 1.0, static_cast<int>(FilterType::Parametric), 0.0},
        {2, 1000, 1.0, static_cast<int>(FilterType::Parametric), 0.0},
        {3, 4000, 1.0, static_cast<int>(FilterType::Parametric), 0.0},
        {4, 10000, 0.7, static_cast<int>(FilterType::HighShelf2), 0.0},
    };
}

void SimulatedDevice::setApiVersion(int protocolVersion, int major, int minor)
{
    m_protocolVersion = static_cast<quint8>(protocolVersion);
    m_apiMajor = major;
    m_apiMinor = minor;
}

void SimulatedDevice::failCommand(Feature feature, quint8 command, int status)
{
    m_failures.insert(failureKey(feature, command), status);
}

qint64 SimulatedDevice::bytesAvailable() const
{
    return m_outgoing.size() + QIODevice::bytesAvailable();
}

qint64 SimulatedDevice::readData(char *data, qint64 maxSize)
{
    const qint64 n = qMin<qint64>(maxSize, m_outgoing.size());
    std::memcpy(data, m_outgoing.constData(), static_cast<size_t>(n));
    m_outgoing.remove(0, static_cast<int>(n));
    return n;
}

qint64 SimulatedDevice::writeData(const char *data, qint64 size)
{
    const QByteArray bytes(data, static_cast<int>(size));
    for (const Frame &frame : m_decoder.feed(bytes))
        handleFrame(frame);
    return size;
}

void SimulatedDevice::handleFrame(const Frame &frame)
{
    m_received.append(frame);
    m_lastFrameVersion = frame.version;
    if (m_silent || !frame.isValid())
        return;

    if (frame.vendor() == kVendorLegacy && frame.command() == kLegacyGetApiVersion) {
        QByteArray payload;
        payload.append('\0');  // success
        payload.append(static_cast<char>(m_protocolVersion));
        payload.append(static_cast<char>(m_apiMajor));
        payload.append(static_cast<char>(m_apiMinor));
        queue(encodeFrame(frame.version, kVendorLegacy, kLegacyGetApiVersion | kLegacyAckBit, payload));
        return;
    }
    if (frame.vendor() != kVendorV3)
        return;

    const CommandWord word = CommandWord::fromRaw(frame.command());
    if (word.type != PacketType::Command)
        return;
    const auto feature = static_cast<Feature>(word.feature);
    const auto failure = m_failures.constFind(failureKey(feature, word.command));
    if (failure != m_failures.constEnd()) {
        respondError(feature, word.command, failure.value());
        return;
    }
    bool supported = false;
    for (const FeatureInfo &f : m_features)
        supported = supported || f.id == word.feature;
    if (!supported) {
        respondError(feature, word.command, 0);  // feature not supported
        return;
    }
    switch (feature) {
    case Feature::Basic: handleBasic(word.command, frame.payload()); break;
    case Feature::Earbud: handleEarbud(word.command, frame.payload()); break;
    case Feature::MusicProcessing: handleMusic(word.command, frame.payload()); break;
    default: respondError(feature, word.command, 1); break;
    }
}

void SimulatedDevice::handleBasic(quint8 command, const QByteArray &payload)
{
    switch (command) {
    case basic::GetGaiaVersion:
        respond(Feature::Basic, command, QByteArray(1, 3));
        break;
    case basic::GetSupportedFeatures: {
        QByteArray reply(1, 0);  // no more features
        for (const FeatureInfo &f : m_features) {
            reply.append(static_cast<char>(f.id));
            reply.append(static_cast<char>(f.version));
        }
        respond(Feature::Basic, command, reply);
        break;
    }
    case basic::GetSerialNumber:
        respond(Feature::Basic, command, QByteArrayLiteral("SIM0000001"));
        break;
    case basic::GetVariantName:
        respond(Feature::Basic, command, QByteArrayLiteral("Space Travel (simulated)"));
        break;
    case basic::GetApplicationVersion:
        respond(Feature::Basic, command, QByteArrayLiteral("1.0.0-sim"));
        break;
    case basic::RegisterNotification:
        respond(Feature::Basic, command);
        break;
    case basic::SetTransportParameter:
        if (const auto p = TransportParameter::parse(payload)) {
            respond(Feature::Basic, command, encodeTransportParameter(static_cast<TransportKey>(p->key), p->value));
            if (p->key == static_cast<int>(TransportKey::ProtocolVersion))
                m_protocolVersion = static_cast<quint8>(p->value);
        } else {
            respondError(Feature::Basic, command, 5);
        }
        break;
    default:
        respondError(Feature::Basic, command, 1);
        break;
    }
}

void SimulatedDevice::handleEarbud(quint8 command, const QByteArray &payload)
{
    switch (command) {
    case earbud::GetPosition:
        respond(Feature::Earbud, command, QByteArray(1, 0));
        break;
    case earbud::GetDefaultActions:
        respond(Feature::Earbud, command, m_defaultActions);
        break;
    case earbud::GetCurrentActions:
        respond(Feature::Earbud, command, m_currentActions);
        break;
    case earbud::SetActions:
        if (payload.size() != m_currentActions.size()) {
            respondError(Feature::Earbud, command, 5);
            break;
        }
        for (int i = 0; i < payload.size(); ++i) {
            if (static_cast<quint8>(payload.at(i)) != 0xFF)
                m_currentActions[i] = payload.at(i);
        }
        respond(Feature::Earbud, command);
        if (m_sendActionNotifications)
            sendNotification(Feature::Earbud, earbud::ActionsChanged, QByteArray(1, 0));
        break;
    default:
        respondError(Feature::Earbud, command, 1);
        break;
    }
}

void SimulatedDevice::handleMusic(quint8 command, const QByteArray &payload)
{
    switch (command) {
    case music::GetEqState:
        respond(Feature::MusicProcessing, command, QByteArray(1, m_eqPresent ? 1 : 0));
        break;
    case music::GetAvailablePresets: {
        QByteArray reply(1, static_cast<char>(m_presets.size()));
        for (int id : m_presets)
            reply.append(static_cast<char>(id));
        respond(Feature::MusicProcessing, command, reply);
        break;
    }
    case music::GetSelectedSet:
        respond(Feature::MusicProcessing, command, QByteArray(1, static_cast<char>(m_selectedPreset)));
        break;
    case music::SetSelectedSet: {
        const int id = payload.isEmpty() ? -1 : static_cast<quint8>(payload.at(0));
        if (!m_presets.contains(id)) {
            respondError(Feature::MusicProcessing, command, 5);
            break;
        }
        m_selectedPreset = id;
        respond(Feature::MusicProcessing, command);
        sendNotification(Feature::MusicProcessing, music::SelectedSetChanged, QByteArray(1, static_cast<char>(id)));
        break;
    }
    case music::GetUserSetBandCount:
        respond(Feature::MusicProcessing, command, QByteArray(1, static_cast<char>(m_userBands.size())));
        break;
    case music::GetUserSetConfiguration: {
        if (payload.size() < 2) {
            respondError(Feature::MusicProcessing, command, 5);
            break;
        }
        const int start = static_cast<quint8>(payload.at(0));
        const int end = static_cast<quint8>(payload.at(1));
        if (end < start || end >= m_userBands.size()) {
            respondError(Feature::MusicProcessing, command, 5);
            break;
        }
        respond(Feature::MusicProcessing, command, encodeUserBands(m_userBands.mid(start, end - start + 1)));
        break;
    }
    case music::SetUserSetConfiguration: {
        const auto bands = parseUserBands(payload);
        if (!bands || bands->isEmpty() || bands->last().index >= m_userBands.size()) {
            respondError(Feature::MusicProcessing, command, 5);
            break;
        }
        QByteArray changed(1, static_cast<char>(bands->size()));
        for (const EqBand &band : *bands) {
            m_userBands[band.index] = band;
            changed.append(static_cast<char>(band.index));
        }
        respond(Feature::MusicProcessing, command);
        sendNotification(Feature::MusicProcessing, music::UserSetChanged, changed);
        break;
    }
    default:
        respondError(Feature::MusicProcessing, command, 1);
        break;
    }
}

void SimulatedDevice::respond(Feature feature, quint8 command, const QByteArray &payload)
{
    queue(encodeFrame(m_lastFrameVersion, kVendorV3, makeCommandWord(feature, PacketType::Response, command),
                      payload));
}

void SimulatedDevice::respondError(Feature feature, quint8 command, int status)
{
    queue(encodeFrame(m_lastFrameVersion, kVendorV3, makeCommandWord(feature, PacketType::Error, command),
                      QByteArray(1, static_cast<char>(status))));
}

void SimulatedDevice::sendNotification(Feature feature, quint8 command, const QByteArray &payload)
{
    queue(encodeFrame(m_lastFrameVersion, kVendorV3, makeCommandWord(feature, PacketType::Notification, command),
                      payload));
}

void SimulatedDevice::queue(const QByteArray &frame)
{
    m_staged.append(frame);
    if (m_flushScheduled)
        return;
    m_flushScheduled = true;
    QTimer::singleShot(m_replyDelayMs, this, &SimulatedDevice::flush);
}

void SimulatedDevice::flush()
{
    m_flushScheduled = false;
    if (m_staged.isEmpty())
        return;
    m_outgoing.append(m_staged);
    m_staged.clear();
    emit readyRead();
}

} // namespace sim
