#pragma once

// Moondrop touch-control mapping carried in the GAIA Earbud feature.
//
// The mapping is 11 entries, one per function, in a fixed order. Each slot byte
// is side (high nibble) | gesture (low nibble). 0xFF in a SET request means
// "keep the current value". SET and GET_CURRENT prepend an "enabled" byte.

#include <QByteArray>
#include <QString>

#include <array>
#include <optional>

namespace touch {

enum class Function : int {
    PlayPause = 0,
    PreviousTrack = 1,
    NextTrack = 2,
    VolumeUp = 3,
    VolumeDown = 4,
    AnswerCall = 5,
    HangUpCall = 6,
    StartVoiceAssistant = 7,
    CancelVoiceAssistant = 8,
    AncOn = 9,
    AncOff = 10,
};
constexpr int kFunctionCount = 11;

enum class Side : quint8 {
    None = 0x00,
    Left = 0x10,
    Right = 0x20,
    Both = 0x30,
};

enum class Gesture : quint8 {
    None = 0,
    SingleTap = 1,
    DoubleTap = 2,
    TripleTap = 3,
    LongPress1s = 4,
    LongPress2s = 5,
};
constexpr int kGestureCount = 6;

constexpr quint8 kUnsetSlot = 0xFF;

struct Slot
{
    quint8 raw = kUnsetSlot;

    static Slot make(Side side, Gesture gesture);
    bool isUnset() const { return raw == kUnsetSlot; }
    Side side() const { return static_cast<Side>(raw & 0xF0); }
    Gesture gesture() const { return static_cast<Gesture>(raw & 0x0F); }
    bool isKnown() const;
    bool operator==(const Slot &o) const { return raw == o.raw; }
    bool operator!=(const Slot &o) const { return raw != o.raw; }
};

struct Mapping
{
    std::array<Slot, kFunctionCount> entries{};

    Slot &operator[](Function f) { return entries[static_cast<int>(f)]; }
    const Slot &operator[](Function f) const { return entries[static_cast<int>(f)]; }
    bool operator==(const Mapping &o) const { return entries == o.entries; }

    // GET_DEFAULT_ACTIONS reply: 11 slot bytes.
    static std::optional<Mapping> fromDefaultsPayload(const QByteArray &payload);
    QByteArray toSlotBytes() const;
};

struct CurrentMapping
{
    bool enabled = false;
    Mapping mapping;

    // GET_CURRENT_ACTIONS reply: [enabled] + 11 slot bytes.
    static std::optional<CurrentMapping> fromPayload(const QByteArray &payload);
};

// SET_ACTIONS payload: [enabled] + 11 entries. Unset entries in `edited` take the
// value from `current`, as the official app does before sending.
QByteArray encodeSetActions(bool enabled, const Mapping &edited, const Mapping &current);

QString functionName(Function f);
QString gestureName(Gesture g);
QString sideName(Side s);
QString slotDescription(const Slot &slot);

// Editing rules from the official app's touch screen for Bluetrum products.
//
// - Editable: previous/next track (left or right), play/pause, voice assistant
//   start/cancel, ANC on/off (either earbud), answer/hang up call (either earbud).
//   Volume up/down are not editable and are always sent as "keep".
// - Previous/next share a gesture and sit on opposite earbuds.
// - Assistant start/cancel and ANC on/off are toggles that share one gesture.
// - "Media" gestures (previous, play/pause, assistant, ANC) must be distinct,
//   and at most one of them may be a long press. The same applies to the
//   "call" gestures (answer, hang up).
class Editor
{
public:
    enum class Group { None, Media, Calls };

    static bool isEditable(Function f);
    static bool hasSideChoice(Function f);  // previous/next only
    static Group group(Function f);

    void load(const Mapping &current);
    bool isLoaded() const { return m_loaded; }

    Gesture gesture(Function f) const { return m_gestures[static_cast<int>(f)]; }
    Side side(Function f) const { return m_sides[static_cast<int>(f)]; }

    void setGesture(Function f, Gesture g);
    void setSide(Function f, Side s);

    // Empty string when the mapping can be applied.
    QString validate() const;

    // Mapping to send: editable functions filled in, others unset (0xFF).
    Mapping toMapping() const;

private:
    std::array<Gesture, kFunctionCount> m_gestures{};
    std::array<Side, kFunctionCount> m_sides{};
    bool m_loaded = false;
};

} // namespace touch
