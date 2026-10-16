#pragma once

// GAIA v3 constants and payload codecs for the commands MOONDROP Link uses
// with the Space Travel earbuds. Values come from the decompiled Android app
// (version 1.3.12).

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>

namespace gaia {

// Legacy (v1/v2) vendor, used only for the opening "get API version" handshake.
constexpr quint16 kVendorLegacy = 0x000A;
constexpr quint16 kLegacyGetApiVersion = 0x0300;
constexpr quint16 kLegacyAckBit = 0x8000;

// GAIA v3 vendor (Qualcomm).
constexpr quint16 kVendorV3 = 0x001D;

enum class Feature : quint8 {
    Basic = 0,
    Earbud = 1,
    Anc = 2,
    VoiceUi = 3,
    Debug = 4,
    MusicProcessing = 5,
    Upgrade = 6,
};

enum class PacketType : quint8 {
    Command = 0,
    Notification = 1,
    Response = 2,
    Error = 3,
};

// v3 command word: feature (bits 15-9) | type (bits 8-7) | command (bits 6-0).
struct CommandWord
{
    quint8 feature = 0;
    PacketType type = PacketType::Command;
    quint8 command = 0;

    static CommandWord fromRaw(quint16 raw);
    quint16 raw() const;
};

constexpr quint16 makeCommandWord(Feature feature, PacketType type, quint8 command)
{
    return static_cast<quint16>((static_cast<quint8>(feature) << 9)
                                | (static_cast<quint8>(type) << 7)
                                | (command & 0x7F));
}

namespace basic {
enum Command : quint8 {
    GetGaiaVersion = 0,
    GetSupportedFeatures = 1,
    GetSupportedFeaturesNext = 2,
    GetSerialNumber = 3,
    GetVariantName = 4,
    GetApplicationVersion = 5,
    RegisterNotification = 7,
    GetTransportInfo = 12,
    SetTransportParameter = 13,
};
} // namespace basic

// The Earbud feature carries Moondrop's touch-remapping extension in commands 1-3.
namespace earbud {
enum Command : quint8 {
    GetPosition = 0,
    GetDefaultActions = 1,
    GetCurrentActions = 2,
    SetActions = 3,
};
enum Notification : quint8 {
    Handover = 0,
    PositionChanged = 1,
    ActionsChanged = 2,  // payload: [status], 0 = success
};
} // namespace earbud

namespace music {
enum Command : quint8 {
    GetEqState = 0,
    GetAvailablePresets = 1,
    GetSelectedSet = 2,
    SetSelectedSet = 3,
    GetUserSetBandCount = 4,
    GetUserSetConfiguration = 5,
    SetUserSetConfiguration = 6,
};
enum Notification : quint8 {
    EqStateChanged = 0,
    SelectedSetChanged = 1,
    UserSetChanged = 2,
};
constexpr int kOffPreset = 0;
constexpr int kUserPreset = 63;
constexpr int kMaxBandsPerPacket = 36;
} // namespace music

enum class TransportKey : quint8 {
    MaxTxPacketSize = 1,
    OptimumTxPacketSize = 2,
    MaxRxPacketSize = 3,
    OptimumRxPacketSize = 4,
    TxFlowControl = 5,
    RxFlowControl = 6,
    ProtocolVersion = 7,
};

QString featureName(int featureId);
QString commandName(int featureId, PacketType type, int command);
QString packetTypeName(PacketType type);
QString v3StatusText(int status);
QString legacyStatusText(int status);

// Reply to the legacy GET_API_VERSION, after the status byte.
struct ApiVersion
{
    int protocolVersion = 0;
    int apiMajor = 0;
    int apiMinor = 0;

    static std::optional<ApiVersion> parse(const QByteArray &payloadAfterStatus);
};

struct FeatureInfo
{
    int id = 0;
    int version = 0;
};

// Reply to GET_SUPPORTED_FEATURES(_NEXT): [more flag] then (id, version) pairs.
struct FeatureList
{
    bool moreToCome = false;
    QVector<FeatureInfo> features;

    static FeatureList parse(const QByteArray &payload);
};

QByteArray encodeTransportParameter(TransportKey key, quint32 value);
struct TransportParameter
{
    int key = 0;
    quint32 value = 0;

    static std::optional<TransportParameter> parse(const QByteArray &payload);
};

// Earbud HANDOVER notification: [type (0 static, 1 dynamic)][delay seconds].
struct HandoverInfo
{
    bool isStatic = true;
    int delaySeconds = 5;

    static std::optional<HandoverInfo> parse(const QByteArray &payload);
};

// --- Music processing ---

QString defaultPresetName(int presetId);

// [count][id, id, ...]
std::optional<QVector<int>> parsePresetList(const QByteArray &payload);

enum class FilterType : quint8 {
    Bypass = 0,
    LowPass1 = 1,
    HighPass1 = 2,
    AllPass1 = 3,
    LowShelf1 = 4,
    HighShelf1 = 5,
    Tilt1 = 6,
    LowPass2 = 7,
    HighPass2 = 8,
    AllPass2 = 9,
    LowShelf2 = 10,
    HighShelf2 = 11,
    Tilt2 = 12,
    Parametric = 13,
};
constexpr int kFilterTypeCount = 14;
QString filterTypeName(int filterType);

struct EqBand
{
    int index = 0;
    int frequencyHz = 1000;
    double q = 1.0;
    int filterType = static_cast<int>(FilterType::Parametric);
    double gainDb = 0.0;

    bool operator==(const EqBand &other) const;
};

// GET_USER_SET_CONFIGURATION request: [start band][end band].
QByteArray encodeBandRange(int startBand, int endBand);

// User set configuration: [start][end] then 7 bytes per band:
// frequency Hz (u16), Q x4096 (u16), filter type (u8), gain dB x60 (s16).
QByteArray encodeUserBands(const QVector<EqBand> &bands);
std::optional<QVector<EqBand>> parseUserBands(const QByteArray &payload);

// USER_SET_CHANGED notification: [count][band index, ...]
std::optional<QVector<int>> parseChangedBands(const QByteArray &payload);

QString toHex(const QByteArray &bytes);

} // namespace gaia
