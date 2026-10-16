#include "gaia/gaiaprotocol.h"

#include <QStringList>

#include <cmath>

namespace gaia {

namespace {

quint8 u8(const QByteArray &b, int offset)
{
    return static_cast<quint8>(b.at(offset));
}

quint16 u16(const QByteArray &b, int offset)
{
    return static_cast<quint16>((u8(b, offset) << 8) | u8(b, offset + 1));
}

void appendU16(QByteArray &b, quint16 value)
{
    b.append(static_cast<char>((value >> 8) & 0xFF));
    b.append(static_cast<char>(value & 0xFF));
}

int clampInt(long long value, long long lo, long long hi)
{
    return static_cast<int>(value < lo ? lo : (value > hi ? hi : value));
}

} // namespace

CommandWord CommandWord::fromRaw(quint16 raw)
{
    CommandWord word;
    word.feature = static_cast<quint8>((raw >> 9) & 0x7F);
    word.type = static_cast<PacketType>((raw >> 7) & 0x03);
    word.command = static_cast<quint8>(raw & 0x7F);
    return word;
}

quint16 CommandWord::raw() const
{
    return static_cast<quint16>(((feature & 0x7F) << 9) | ((static_cast<quint8>(type) & 0x03) << 7)
                                | (command & 0x7F));
}

QString featureName(int featureId)
{
    switch (featureId) {
    case 0: return QStringLiteral("Basic");
    case 1: return QStringLiteral("Earbud");
    case 2: return QStringLiteral("ANC");
    case 3: return QStringLiteral("Voice UI");
    case 4: return QStringLiteral("Debug");
    case 5: return QStringLiteral("Music processing");
    case 6: return QStringLiteral("Upgrade");
    default: return QStringLiteral("Feature %1").arg(featureId);
    }
}

QString packetTypeName(PacketType type)
{
    switch (type) {
    case PacketType::Command: return QStringLiteral("command");
    case PacketType::Notification: return QStringLiteral("notification");
    case PacketType::Response: return QStringLiteral("response");
    case PacketType::Error: return QStringLiteral("error");
    }
    return {};
}

QString commandName(int featureId, PacketType type, int command)
{
    const bool notification = type == PacketType::Notification;
    if (featureId == static_cast<int>(Feature::Basic) && !notification) {
        switch (command) {
        case basic::GetGaiaVersion: return QStringLiteral("GetGaiaVersion");
        case basic::GetSupportedFeatures: return QStringLiteral("GetSupportedFeatures");
        case basic::GetSupportedFeaturesNext: return QStringLiteral("GetSupportedFeaturesNext");
        case basic::GetSerialNumber: return QStringLiteral("GetSerialNumber");
        case basic::GetVariantName: return QStringLiteral("GetVariantName");
        case basic::GetApplicationVersion: return QStringLiteral("GetApplicationVersion");
        case basic::RegisterNotification: return QStringLiteral("RegisterNotification");
        case basic::GetTransportInfo: return QStringLiteral("GetTransportInfo");
        case basic::SetTransportParameter: return QStringLiteral("SetTransportParameter");
        default: break;
        }
    } else if (featureId == static_cast<int>(Feature::Earbud)) {
        if (notification) {
            switch (command) {
            case earbud::Handover: return QStringLiteral("Handover");
            case earbud::PositionChanged: return QStringLiteral("PositionChanged");
            case earbud::ActionsChanged: return QStringLiteral("ActionsChanged");
            default: break;
            }
        } else {
            switch (command) {
            case earbud::GetPosition: return QStringLiteral("GetPosition");
            case earbud::GetDefaultActions: return QStringLiteral("GetDefaultActions");
            case earbud::GetCurrentActions: return QStringLiteral("GetCurrentActions");
            case earbud::SetActions: return QStringLiteral("SetActions");
            default: break;
            }
        }
    } else if (featureId == static_cast<int>(Feature::MusicProcessing)) {
        if (notification) {
            switch (command) {
            case music::EqStateChanged: return QStringLiteral("EqStateChanged");
            case music::SelectedSetChanged: return QStringLiteral("SelectedSetChanged");
            case music::UserSetChanged: return QStringLiteral("UserSetChanged");
            default: break;
            }
        } else {
            switch (command) {
            case music::GetEqState: return QStringLiteral("GetEqState");
            case music::GetAvailablePresets: return QStringLiteral("GetAvailablePresets");
            case music::GetSelectedSet: return QStringLiteral("GetSelectedSet");
            case music::SetSelectedSet: return QStringLiteral("SetSelectedSet");
            case music::GetUserSetBandCount: return QStringLiteral("GetUserSetBandCount");
            case music::GetUserSetConfiguration: return QStringLiteral("GetUserSetConfiguration");
            case music::SetUserSetConfiguration: return QStringLiteral("SetUserSetConfiguration");
            default: break;
            }
        }
    }
    return QStringLiteral("cmd %1").arg(command);
}

QString v3StatusText(int status)
{
    switch (status) {
    case 0: return QStringLiteral("feature not supported");
    case 1: return QStringLiteral("command not supported");
    case 2: return QStringLiteral("not authenticated");
    case 3: return QStringLiteral("insufficient resources");
    case 4: return QStringLiteral("authenticating");
    case 5: return QStringLiteral("invalid parameter");
    case 6: return QStringLiteral("incorrect state");
    case 7: return QStringLiteral("in progress");
    default: return QStringLiteral("feature-specific error %1").arg(status);
    }
}

QString legacyStatusText(int status)
{
    if (status == 0)
        return QStringLiteral("success");
    return v3StatusText(status);
}

std::optional<ApiVersion> ApiVersion::parse(const QByteArray &payloadAfterStatus)
{
    if (payloadAfterStatus.size() < 3)
        return std::nullopt;
    ApiVersion version;
    version.protocolVersion = u8(payloadAfterStatus, 0);
    version.apiMajor = u8(payloadAfterStatus, 1);
    version.apiMinor = u8(payloadAfterStatus, 2);
    return version;
}

FeatureList FeatureList::parse(const QByteArray &payload)
{
    FeatureList list;
    if (payload.isEmpty())
        return list;
    list.moreToCome = u8(payload, 0) != 0;
    for (int i = 1; i + 1 < payload.size(); i += 2)
        list.features.append({u8(payload, i), u8(payload, i + 1)});
    return list;
}

QByteArray encodeTransportParameter(TransportKey key, quint32 value)
{
    QByteArray b;
    b.append(static_cast<char>(key));
    b.append(static_cast<char>((value >> 24) & 0xFF));
    b.append(static_cast<char>((value >> 16) & 0xFF));
    b.append(static_cast<char>((value >> 8) & 0xFF));
    b.append(static_cast<char>(value & 0xFF));
    return b;
}

std::optional<TransportParameter> TransportParameter::parse(const QByteArray &payload)
{
    if (payload.size() < 5)
        return std::nullopt;
    TransportParameter p;
    p.key = u8(payload, 0);
    p.value = (static_cast<quint32>(u8(payload, 1)) << 24) | (static_cast<quint32>(u8(payload, 2)) << 16)
              | (static_cast<quint32>(u8(payload, 3)) << 8) | u8(payload, 4);
    return p;
}

std::optional<HandoverInfo> HandoverInfo::parse(const QByteArray &payload)
{
    if (payload.isEmpty())
        return std::nullopt;
    HandoverInfo info;
    info.isStatic = u8(payload, 0) == 0;
    info.delaySeconds = payload.size() > 1 ? u8(payload, 1) : 5;
    return info;
}

QString defaultPresetName(int presetId)
{
    switch (presetId) {
    case music::kOffPreset: return QStringLiteral("Flat");
    case 1: return QStringLiteral("Rock");
    case 2: return QStringLiteral("Pop");
    case 3: return QStringLiteral("Classical");
    case 4: return QStringLiteral("Techno");
    case 5: return QStringLiteral("R&B");
    case 6: return QStringLiteral("Ambient");
    case 7: return QStringLiteral("Metal");
    case 8: return QStringLiteral("Funk");
    case 9: return QStringLiteral("Speech");
    case music::kUserPreset: return QStringLiteral("User");
    default: return QStringLiteral("Preset %1").arg(presetId);
    }
}

std::optional<QVector<int>> parsePresetList(const QByteArray &payload)
{
    if (payload.isEmpty())
        return std::nullopt;
    const int count = u8(payload, 0);
    if (payload.size() < count + 1)
        return std::nullopt;
    QVector<int> ids;
    for (int i = 1; i <= count; ++i)
        ids.append(u8(payload, i));
    return ids;
}

QString filterTypeName(int filterType)
{
    switch (filterType) {
    case 0: return QStringLiteral("Bypass");
    case 1: return QStringLiteral("Low-pass (1st order)");
    case 2: return QStringLiteral("High-pass (1st order)");
    case 3: return QStringLiteral("All-pass (1st order)");
    case 4: return QStringLiteral("Low shelf (1st order)");
    case 5: return QStringLiteral("High shelf (1st order)");
    case 6: return QStringLiteral("Tilt (1st order)");
    case 7: return QStringLiteral("Low-pass (2nd order)");
    case 8: return QStringLiteral("High-pass (2nd order)");
    case 9: return QStringLiteral("All-pass (2nd order)");
    case 10: return QStringLiteral("Low shelf (2nd order)");
    case 11: return QStringLiteral("High shelf (2nd order)");
    case 12: return QStringLiteral("Tilt (2nd order)");
    case 13: return QStringLiteral("Parametric (peaking)");
    default: return QStringLiteral("Filter %1").arg(filterType);
    }
}

bool EqBand::operator==(const EqBand &other) const
{
    return index == other.index && frequencyHz == other.frequencyHz && filterType == other.filterType
           && std::lround(q * 4096.0) == std::lround(other.q * 4096.0)
           && std::lround(gainDb * 60.0) == std::lround(other.gainDb * 60.0);
}

QByteArray encodeBandRange(int startBand, int endBand)
{
    QByteArray b;
    b.append(static_cast<char>(startBand & 0xFF));
    b.append(static_cast<char>(endBand & 0xFF));
    return b;
}

QByteArray encodeUserBands(const QVector<EqBand> &bands)
{
    if (bands.isEmpty())
        return {};
    const int start = bands.first().index;
    const int end = start + bands.size() - 1;
    QByteArray b = encodeBandRange(start, end);
    for (const EqBand &band : bands) {
        appendU16(b, static_cast<quint16>(clampInt(band.frequencyHz, 0, 0xFFFF)));
        appendU16(b, static_cast<quint16>(clampInt(std::lround(band.q * 4096.0), 0, 0xFFFF)));
        b.append(static_cast<char>(band.filterType & 0xFF));
        const int gain = clampInt(std::lround(band.gainDb * 60.0), -32768, 32767);
        appendU16(b, static_cast<quint16>(static_cast<qint16>(gain)));
    }
    return b;
}

std::optional<QVector<EqBand>> parseUserBands(const QByteArray &payload)
{
    if (payload.size() < 2)
        return std::nullopt;
    const int start = u8(payload, 0);
    const int end = u8(payload, 1);
    if (end < start)
        return std::nullopt;
    const int count = end - start + 1;
    if (payload.size() < 2 + count * 7)
        return std::nullopt;
    QVector<EqBand> bands;
    for (int i = 0; i < count; ++i) {
        const int offset = 2 + i * 7;
        EqBand band;
        band.index = start + i;
        band.frequencyHz = u16(payload, offset);
        band.q = u16(payload, offset + 2) / 4096.0;
        band.filterType = u8(payload, offset + 4);
        band.gainDb = static_cast<qint16>(u16(payload, offset + 5)) / 60.0;
        bands.append(band);
    }
    return bands;
}

std::optional<QVector<int>> parseChangedBands(const QByteArray &payload)
{
    return parsePresetList(payload);  // same [count][values...] layout
}

QString toHex(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toHex(' ')).toUpper();
}

} // namespace gaia
