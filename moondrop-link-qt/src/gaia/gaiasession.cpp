#include "gaia/gaiasession.h"

#include <QIODevice>

#include <algorithm>

namespace gaia {

namespace {

constexpr quint16 v3Key(quint8 feature, quint8 command)
{
    return static_cast<quint16>((feature << 8) | command);
}

constexpr quint16 v3Key(Feature feature, quint8 command)
{
    return v3Key(static_cast<quint8>(feature), command);
}

QString decodeString(const QByteArray &payload)
{
    QByteArray bytes = payload;
    while (bytes.endsWith('\0'))
        bytes.chop(1);
    return QString::fromUtf8(bytes).trimmed();
}

int firstByte(const QByteArray &payload, int fallback = -1)
{
    return payload.isEmpty() ? fallback : static_cast<quint8>(payload.at(0));
}

} // namespace

Session::Session(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    m_timeoutTimer.setInterval(200);
    connect(&m_timeoutTimer, &QTimer::timeout, this, &Session::checkTimeouts);
    m_touchConfirmTimer.setSingleShot(true);
    connect(&m_touchConfirmTimer, &QTimer::timeout, this, [this] {
        if (m_touchApplyAcked)
            finishTouchApply(true, tr("Sent. The earbuds did not confirm, so the mapping was re-read."));
        else
            finishTouchApply(false, tr("The earbuds did not answer within %1 s.")
                                        .arg(m_touchConfirmTimeoutMs / 1000.0, 0, 'g', 2));
    });
}

Session::~Session()
{
    stop();
}

void Session::start(QIODevice *io)
{
    stop();
    m_io = io;
    m_decoder.reset();
    m_frameVersion = 1;
    m_info = DeviceInfo();
    m_pendingFeatures.clear();
    m_primary = EarbudPosition::Unknown;
    m_defaultTouch.reset();
    m_currentTouch.reset();
    m_eqPresent = false;
    m_presets.clear();
    m_selectedPreset = -1;
    m_userBandCount = 0;
    m_userBands.clear();
    if (!m_io) {
        fail(tr("No connection."));
        return;
    }
    m_readConnection = connect(m_io, &QIODevice::readyRead, this, &Session::onReadyRead);
    setState(State::Handshaking);
    sendLegacy(kLegacyGetApiVersion);
    if (m_io->bytesAvailable() > 0)
        onReadyRead();
}

void Session::stop()
{
    disconnect(m_readConnection);
    m_io = nullptr;
    m_pending.clear();
    m_timeoutTimer.stop();
    m_touchConfirmTimer.stop();
    m_touchApplyInFlight = false;
    m_bandSetsOutstanding = 0;
    if (m_state != State::Idle)
        setState(State::Idle);
}

int Session::featureVersion(Feature f) const
{
    for (const FeatureInfo &info : m_info.features) {
        if (info.id == static_cast<int>(f))
            return info.version;
    }
    return -1;
}

void Session::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void Session::fail(const QString &reason)
{
    disconnect(m_readConnection);
    m_pending.clear();
    m_timeoutTimer.stop();
    m_touchConfirmTimer.stop();
    m_touchApplyInFlight = false;
    setState(State::Failed);
    emit failed(reason);
}

void Session::onReadyRead()
{
    if (!m_io)
        return;
    const QByteArray bytes = m_io->readAll();
    for (const Frame &frame : m_decoder.feed(bytes)) {
        handleFrame(frame);
        if (m_state == State::Failed || m_state == State::Idle)
            break;
    }
}

void Session::handleFrame(const Frame &frame)
{
    emit trafficLogged(false, frame.raw, describe(frame));
    if (!frame.isValid())
        return;
    if (frame.vendor() == kVendorLegacy)
        handleLegacy(frame);
    else if (frame.vendor() == kVendorV3)
        handleV3(frame);
}

void Session::handleLegacy(const Frame &frame)
{
    const quint16 command = frame.command();
    if (!(command & kLegacyAckBit) || (command & ~kLegacyAckBit) != kLegacyGetApiVersion)
        return;
    if (!takePending(kVendorLegacy, kLegacyGetApiVersion) || m_state != State::Handshaking)
        return;

    const QByteArray payload = frame.payload();
    const int status = firstByte(payload);
    if (status != 0) {
        fail(tr("The earbuds rejected the GAIA version request (%1).").arg(legacyStatusText(status)));
        return;
    }
    const auto version = ApiVersion::parse(payload.mid(1));
    if (!version) {
        fail(tr("The earbuds sent a malformed GAIA version reply."));
        return;
    }
    m_info.protocolVersion = version->protocolVersion;
    m_info.apiMajor = version->apiMajor;
    m_info.apiMinor = version->apiMinor;
    if (version->apiMajor != 3) {
        fail(tr("The earbuds speak GAIA v%1.%2; this app only supports GAIA v3.")
                 .arg(version->apiMajor)
                 .arg(version->apiMinor));
        return;
    }
    if (version->protocolVersion >= 1 && version->protocolVersion <= 4)
        m_frameVersion = static_cast<quint8>(version->protocolVersion);

    setState(State::Discovering);
    sendV3(Feature::Basic, basic::GetSupportedFeatures);
}

void Session::handleV3(const Frame &frame)
{
    const CommandWord word = CommandWord::fromRaw(frame.command());
    const QByteArray payload = frame.payload();

    if (word.type == PacketType::Response || word.type == PacketType::Error) {
        if (!takePending(kVendorV3, v3Key(word.feature, word.command)))
            return;  // unsolicited or already timed out
    }
    if (word.type == PacketType::Command)
        return;

    if (word.type == PacketType::Error) {
        handleError(word.feature, word.command, firstByte(payload));
        return;
    }

    switch (static_cast<Feature>(word.feature)) {
    case Feature::Basic:
        handleBasic(word.type, word.command, payload);
        break;
    case Feature::Earbud:
        handleEarbud(word.type, word.command, payload);
        break;
    case Feature::MusicProcessing:
        handleMusic(word.type, word.command, payload);
        break;
    default:
        break;
    }
}

void Session::handleBasic(PacketType type, quint8 command, const QByteArray &payload)
{
    if (type != PacketType::Response)
        return;

    switch (command) {
    case basic::GetSupportedFeatures:
    case basic::GetSupportedFeaturesNext: {
        if (m_state != State::Discovering)
            return;
        const FeatureList list = FeatureList::parse(payload);
        m_pendingFeatures += list.features;
        if (list.moreToCome)
            sendV3(Feature::Basic, basic::GetSupportedFeaturesNext);
        else
            onFeaturesComplete();
        break;
    }
    case basic::GetGaiaVersion:
        m_info.gaiaVersion = firstByte(payload);
        emit deviceInfoChanged();
        break;
    case basic::GetSerialNumber:
        m_info.serialNumber = decodeString(payload);
        emit deviceInfoChanged();
        break;
    case basic::GetVariantName:
        m_info.variantName = decodeString(payload);
        emit deviceInfoChanged();
        break;
    case basic::GetApplicationVersion:
        m_info.firmwareVersion = decodeString(payload);
        emit deviceInfoChanged();
        break;
    case basic::GetTransportInfo:
    case basic::SetTransportParameter:
        if (const auto param = TransportParameter::parse(payload)) {
            if (param->key == static_cast<int>(TransportKey::ProtocolVersion) && param->value >= 1
                && param->value <= 4) {
                m_frameVersion = static_cast<quint8>(param->value);
                emit deviceInfoChanged();
            }
        }
        break;
    default:
        break;
    }
}

void Session::onFeaturesComplete()
{
    m_info.features = m_pendingFeatures;
    m_pendingFeatures.clear();
    if (!hasFeature(Feature::Basic)) {
        fail(tr("The earbuds did not report the GAIA Basic feature."));
        return;
    }
    // The official app asks for protocol v4 (2-byte lengths) when Basic >= v2.
    if (featureVersion(Feature::Basic) >= 2)
        sendV3(Feature::Basic, basic::SetTransportParameter,
               encodeTransportParameter(TransportKey::ProtocolVersion, 4));
    for (Feature f : {Feature::Earbud, Feature::MusicProcessing}) {
        if (hasFeature(f))
            sendV3(Feature::Basic, basic::RegisterNotification,
                   QByteArray(1, static_cast<char>(f)));
    }
    requestDeviceInfo();
    setState(State::Ready);
    emit deviceInfoChanged();
    emit ready();
}

void Session::handleEarbud(PacketType type, quint8 command, const QByteArray &payload)
{
    if (type == PacketType::Notification) {
        switch (command) {
        case earbud::Handover:
            if (const auto info = HandoverInfo::parse(payload))
                emit handoverAnnounced(info->isStatic, info->delaySeconds);
            break;
        case earbud::PositionChanged: {
            const int pos = firstByte(payload);
            m_primary = pos == 0 ? EarbudPosition::Left : pos == 1 ? EarbudPosition::Right : EarbudPosition::Unknown;
            emit earbudPositionChanged(m_primary);
            break;
        }
        case earbud::ActionsChanged: {
            const int status = firstByte(payload, 0);
            if (m_touchApplyInFlight) {
                finishTouchApply(status == 0, status == 0
                                                  ? tr("Touch controls applied.")
                                                  : tr("The earbuds could not apply the mapping (status %1). "
                                                       "Make sure both earbuds are paired and try again.")
                                                        .arg(status));
            } else {
                sendV3(Feature::Earbud, earbud::GetCurrentActions);
            }
            break;
        }
        default:
            break;
        }
        return;
    }

    switch (command) {
    case earbud::GetPosition: {
        const int pos = firstByte(payload);
        m_primary = pos == 0 ? EarbudPosition::Left : pos == 1 ? EarbudPosition::Right : EarbudPosition::Unknown;
        emit earbudPositionChanged(m_primary);
        break;
    }
    case earbud::GetDefaultActions:
        if (const auto mapping = touch::Mapping::fromDefaultsPayload(payload)) {
            m_defaultTouch = *mapping;
            emit touchDefaultsChanged(*mapping);
        } else {
            emit commandFailed(tr("Read default touch controls"), tr("malformed reply"));
        }
        break;
    case earbud::GetCurrentActions:
        if (const auto current = touch::CurrentMapping::fromPayload(payload)) {
            m_currentTouch = *current;
            emit touchCurrentChanged(*current);
        } else {
            emit commandFailed(tr("Read touch controls"), tr("malformed reply"));
        }
        break;
    case earbud::SetActions:
        // Success is reported by the ActionsChanged notification that follows.
        m_touchApplyAcked = true;
        break;
    default:
        break;
    }
}

void Session::handleMusic(PacketType type, quint8 command, const QByteArray &payload)
{
    if (type == PacketType::Notification) {
        switch (command) {
        case music::EqStateChanged:
            m_eqPresent = firstByte(payload) == 1;
            emit eqStateChanged(m_eqPresent);
            break;
        case music::SelectedSetChanged:
            m_selectedPreset = firstByte(payload);
            emit selectedPresetChanged(m_selectedPreset);
            break;
        case music::UserSetChanged:
            requestUserBands();
            break;
        default:
            break;
        }
        return;
    }

    switch (command) {
    case music::GetEqState:
        m_eqPresent = firstByte(payload) == 1;
        emit eqStateChanged(m_eqPresent);
        break;
    case music::GetAvailablePresets:
        if (const auto ids = parsePresetList(payload)) {
            m_presets = *ids;
            emit presetsChanged(m_presets);
        } else {
            emit commandFailed(tr("Read EQ presets"), tr("malformed reply"));
        }
        break;
    case music::GetSelectedSet:
        m_selectedPreset = firstByte(payload);
        emit selectedPresetChanged(m_selectedPreset);
        break;
    case music::SetSelectedSet:
        emit presetSelectFinished(true, tr("Preset applied."));
        sendV3(Feature::MusicProcessing, music::GetSelectedSet);
        break;
    case music::GetUserSetBandCount:
        m_userBandCount = std::max(0, firstByte(payload, 0));
        emit userBandCountChanged(m_userBandCount);
        // Same condition as the official app before it reads the user bands.
        if (m_eqPresent && m_userBandCount > 0 && m_presets.contains(music::kUserPreset))
            requestUserBands();
        break;
    case music::GetUserSetConfiguration:
        if (const auto bands = parseUserBands(payload)) {
            for (const EqBand &band : *bands) {
                auto it = std::find_if(m_userBands.begin(), m_userBands.end(),
                                       [&](const EqBand &b) { return b.index == band.index; });
                if (it != m_userBands.end())
                    *it = band;
                else
                    m_userBands.append(band);
            }
            std::sort(m_userBands.begin(), m_userBands.end(),
                      [](const EqBand &a, const EqBand &b) { return a.index < b.index; });
            emit userBandsChanged(m_userBands);
        }
        break;
    case music::SetUserSetConfiguration:
        if (m_bandSetsOutstanding > 0 && --m_bandSetsOutstanding == 0)
            finishBandSet(!m_bandSetFailed, m_bandSetFailed ? tr("Some bands were rejected.")
                                                            : tr("Custom EQ applied."));
        break;
    default:
        break;
    }
}

void Session::handleError(quint8 feature, quint8 command, int status)
{
    const QString reason = v3StatusText(status);
    switch (static_cast<Feature>(feature)) {
    case Feature::Basic:
        if (command == basic::GetSupportedFeatures || command == basic::GetSupportedFeaturesNext) {
            if (m_state == State::Discovering)
                fail(tr("The earbuds refused feature discovery (%1).").arg(reason));
        }
        // Other Basic errors (protocol upgrade, info strings) are not fatal.
        break;
    case Feature::Earbud:
        if (command == earbud::SetActions)
            finishTouchApply(false, tr("The earbuds rejected the mapping (%1).").arg(reason));
        else if (command == earbud::GetDefaultActions || command == earbud::GetCurrentActions)
            emit commandFailed(tr("Read touch controls"), reason);
        break;
    case Feature::MusicProcessing:
        if (command == music::SetSelectedSet) {
            emit presetSelectFinished(false, tr("The earbuds rejected the preset (%1).").arg(reason));
        } else if (command == music::SetUserSetConfiguration) {
            m_bandSetFailed = true;
            if (m_bandSetsOutstanding > 0 && --m_bandSetsOutstanding == 0)
                finishBandSet(false, tr("The earbuds rejected the custom EQ (%1).").arg(reason));
        } else if (command == music::GetUserSetBandCount || command == music::GetUserSetConfiguration) {
            // Expected on firmware without a user EQ.
            m_userBandCount = 0;
            emit userBandCountChanged(0);
        } else {
            emit commandFailed(tr("Read equaliser"), reason);
        }
        break;
    default:
        break;
    }
}

void Session::handleTimeout(const Pending &pending)
{
    const QString waited = tr("no answer within %1 s").arg(m_requestTimeoutMs / 1000.0, 0, 'g', 3);
    if (pending.vendor == kVendorLegacy) {
        if (m_state == State::Handshaking)
            fail(tr("The earbuds did not answer the GAIA handshake. Check that they are connected to "
                    "this computer and not to the phone app."));
        return;
    }
    const quint8 feature = static_cast<quint8>(pending.key >> 8);
    const quint8 command = static_cast<quint8>(pending.key & 0xFF);
    if (feature == static_cast<quint8>(Feature::Basic)
        && (command == basic::GetSupportedFeatures || command == basic::GetSupportedFeaturesNext)) {
        if (m_state == State::Discovering)
            fail(tr("The earbuds did not answer feature discovery."));
        return;
    }
    if (feature == static_cast<quint8>(Feature::Earbud) && command == earbud::SetActions)
        return;  // handled by the confirmation timer
    if (feature == static_cast<quint8>(Feature::MusicProcessing) && command == music::SetSelectedSet) {
        emit presetSelectFinished(false, tr("The earbuds did not answer (%1).").arg(waited));
        return;
    }
    if (feature == static_cast<quint8>(Feature::MusicProcessing) && command == music::SetUserSetConfiguration) {
        m_bandSetFailed = true;
        if (m_bandSetsOutstanding > 0 && --m_bandSetsOutstanding == 0)
            finishBandSet(false, tr("The earbuds did not answer (%1).").arg(waited));
        return;
    }
    if (feature == static_cast<quint8>(Feature::Basic))
        return;  // info reads are best effort
    emit commandFailed(commandName(feature, PacketType::Command, command), waited);
}

void Session::requestDeviceInfo()
{
    sendV3(Feature::Basic, basic::GetGaiaVersion);
    sendV3(Feature::Basic, basic::GetSerialNumber);
    sendV3(Feature::Basic, basic::GetVariantName);
    sendV3(Feature::Basic, basic::GetApplicationVersion);
}

void Session::requestEarbudPosition()
{
    if (hasFeature(Feature::Earbud))
        sendV3(Feature::Earbud, earbud::GetPosition);
}

void Session::requestTouchMapping()
{
    if (!hasFeature(Feature::Earbud)) {
        emit commandFailed(tr("Read touch controls"), tr("the earbuds do not report the Earbud feature"));
        return;
    }
    sendV3(Feature::Earbud, earbud::GetDefaultActions);
    sendV3(Feature::Earbud, earbud::GetCurrentActions);
}

void Session::applyTouchMapping(const touch::Mapping &edited)
{
    if (m_touchApplyInFlight) {
        emit touchApplyFinished(false, tr("Another change is still being applied."));
        return;
    }
    if (!m_currentTouch) {
        emit touchApplyFinished(false, tr("Read the current touch controls first."));
        return;
    }
    // The official app always sends enabled = 1.
    const QByteArray payload = touch::encodeSetActions(true, edited, m_currentTouch->mapping);
    m_touchApplyInFlight = true;
    m_touchApplyAcked = false;
    m_touchConfirmTimer.start(m_touchConfirmTimeoutMs);
    sendV3(Feature::Earbud, earbud::SetActions, payload);
}

void Session::restoreDefaultTouchMapping()
{
    if (!m_defaultTouch) {
        emit touchApplyFinished(false, tr("The default mapping has not been read yet."));
        return;
    }
    applyTouchMapping(*m_defaultTouch);
}

void Session::finishTouchApply(bool ok, const QString &message)
{
    if (!m_touchApplyInFlight)
        return;
    m_touchApplyInFlight = false;
    m_touchConfirmTimer.stop();
    emit touchApplyFinished(ok, message);
    if (m_state == State::Ready)
        sendV3(Feature::Earbud, earbud::GetCurrentActions);
}

void Session::requestEqInfo()
{
    if (!hasFeature(Feature::MusicProcessing)) {
        emit commandFailed(tr("Read equaliser"), tr("the earbuds do not report the Music Processing feature"));
        return;
    }
    sendV3(Feature::MusicProcessing, music::GetEqState);
    sendV3(Feature::MusicProcessing, music::GetAvailablePresets);
    sendV3(Feature::MusicProcessing, music::GetSelectedSet);
    sendV3(Feature::MusicProcessing, music::GetUserSetBandCount);
}

void Session::selectPreset(int presetId)
{
    if (presetId < 0 || presetId > 0xFF) {
        emit presetSelectFinished(false, tr("Invalid preset."));
        return;
    }
    sendV3(Feature::MusicProcessing, music::SetSelectedSet, QByteArray(1, static_cast<char>(presetId)));
}

void Session::requestUserBands()
{
    if (m_userBandCount <= 0)
        return;
    for (int start = 0; start < m_userBandCount; start += music::kMaxBandsPerPacket) {
        const int end = std::min(start + music::kMaxBandsPerPacket, m_userBandCount) - 1;
        sendV3(Feature::MusicProcessing, music::GetUserSetConfiguration, encodeBandRange(start, end));
    }
}

void Session::setUserBands(const QVector<EqBand> &bands)
{
    if (m_bandSetsOutstanding > 0) {
        emit userBandsApplyFinished(false, tr("Another change is still being applied."));
        return;
    }
    QVector<EqBand> sorted = bands;
    std::sort(sorted.begin(), sorted.end(), [](const EqBand &a, const EqBand &b) { return a.index < b.index; });

    // Send contiguous runs of at most kMaxBandsPerPacket bands each.
    QVector<QVector<EqBand>> chunks;
    for (const EqBand &band : sorted) {
        if (chunks.isEmpty() || chunks.last().size() >= music::kMaxBandsPerPacket
            || chunks.last().last().index + 1 != band.index)
            chunks.append(QVector<EqBand>());
        chunks.last().append(band);
    }
    if (chunks.isEmpty()) {
        emit userBandsApplyFinished(false, tr("Nothing to apply."));
        return;
    }
    m_bandSetFailed = false;
    m_bandSetsOutstanding = chunks.size();
    for (const auto &chunk : chunks)
        sendV3(Feature::MusicProcessing, music::SetUserSetConfiguration, encodeUserBands(chunk));
}

void Session::finishBandSet(bool ok, const QString &message)
{
    emit userBandsApplyFinished(ok, message);
    // On success the earbuds send UserSetChanged, which triggers a re-read.
    if (!ok)
        requestUserBands();
}

void Session::sendLegacy(quint16 command, const QByteArray &payload)
{
    if (!m_io)
        return;
    const QByteArray frame = encodeFrame(m_frameVersion, kVendorLegacy, command, payload);
    m_pending.append({kVendorLegacy, command, m_clock.elapsed() + m_requestTimeoutMs});
    m_timeoutTimer.start();
    writeFrame(frame, QStringLiteral("Legacy GetApiVersion"));
}

void Session::sendV3(Feature feature, quint8 command, const QByteArray &payload)
{
    if (!m_io || m_state == State::Idle || m_state == State::Failed)
        return;
    const quint16 word = makeCommandWord(feature, PacketType::Command, command);
    const QByteArray frame = encodeFrame(m_frameVersion, kVendorV3, word, payload);
    if (frame.isEmpty()) {
        emit commandFailed(commandName(static_cast<int>(feature), PacketType::Command, command),
                           tr("payload too large"));
        return;
    }
    m_pending.append({kVendorV3, v3Key(feature, command), m_clock.elapsed() + m_requestTimeoutMs});
    m_timeoutTimer.start();
    writeFrame(frame, QStringLiteral("%1 %2 %3")
                          .arg(featureName(static_cast<int>(feature)),
                               commandName(static_cast<int>(feature), PacketType::Command, command),
                               payload.isEmpty() ? QString() : toHex(payload))
                          .trimmed());
}

void Session::writeFrame(const QByteArray &frame, const QString &summary)
{
    if (!m_io)
        return;
    m_io->write(frame);
    emit trafficLogged(true, frame, summary);
}

bool Session::takePending(quint16 vendor, quint16 key)
{
    for (int i = 0; i < m_pending.size(); ++i) {
        if (m_pending.at(i).vendor == vendor && m_pending.at(i).key == key) {
            m_pending.removeAt(i);
            if (m_pending.isEmpty())
                m_timeoutTimer.stop();
            return true;
        }
    }
    return false;
}

void Session::checkTimeouts()
{
    const qint64 now = m_clock.elapsed();
    QList<Pending> expired;
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->deadline <= now) {
            expired.append(*it);
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }
    if (m_pending.isEmpty())
        m_timeoutTimer.stop();
    for (const Pending &p : expired) {
        handleTimeout(p);
        if (m_state == State::Failed || m_state == State::Idle)
            break;
    }
}

QString Session::describe(const Frame &frame) const
{
    if (!frame.isValid())
        return QStringLiteral("Malformed frame");
    QString text;
    if (frame.vendor() == kVendorLegacy) {
        const quint16 cmd = frame.command();
        text = QStringLiteral("Legacy 0x%1%2").arg(cmd & ~kLegacyAckBit, 4, 16, QLatin1Char('0'))
                   .arg(cmd & kLegacyAckBit ? QStringLiteral(" ack") : QString());
    } else if (frame.vendor() == kVendorV3) {
        const CommandWord w = CommandWord::fromRaw(frame.command());
        text = QStringLiteral("%1 %2 %3").arg(featureName(w.feature), commandName(w.feature, w.type, w.command),
                                              packetTypeName(w.type));
        if (w.type == PacketType::Error)
            text += QStringLiteral(" (%1)").arg(v3StatusText(firstByte(frame.payload())));
    } else {
        text = QStringLiteral("Vendor 0x%1 command 0x%2")
                   .arg(frame.vendor(), 4, 16, QLatin1Char('0'))
                   .arg(frame.command(), 4, 16, QLatin1Char('0'));
    }
    if (!frame.payload().isEmpty())
        text += QStringLiteral(" | ") + toHex(frame.payload());
    if (!frame.checksumValid)
        text += QStringLiteral(" [bad checksum]");
    return text;
}

} // namespace gaia
