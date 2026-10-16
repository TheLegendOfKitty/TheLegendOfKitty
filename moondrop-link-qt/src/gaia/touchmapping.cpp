#include "gaia/touchmapping.h"

#include <QSet>
#include <QVector>

namespace touch {

namespace {

bool isLongPress(Gesture g)
{
    return g == Gesture::LongPress1s || g == Gesture::LongPress2s;
}

Side opposite(Side s)
{
    return s == Side::Left ? Side::Right : Side::Left;
}

// Checks one validation group the way the official app does.
QString checkGroup(const QVector<Gesture> &gestures, const QString &groupName)
{
    QVector<Gesture> used;
    for (Gesture g : gestures) {
        if (g != Gesture::None)
            used.append(g);
    }
    QSet<int> unique;
    int longPresses = 0;
    for (Gesture g : used) {
        unique.insert(static_cast<int>(g));
        if (isLongPress(g))
            ++longPresses;
    }
    if (unique.size() != used.size())
        return QStringLiteral("%1: two functions use the same gesture.").arg(groupName);
    if (longPresses > 1)
        return QStringLiteral("%1: only one function can use a long press (1 s or 2 s).").arg(groupName);
    return {};
}

} // namespace

Slot Slot::make(Side side, Gesture gesture)
{
    Slot slot;
    slot.raw = static_cast<quint8>(static_cast<quint8>(side) | static_cast<quint8>(gesture));
    return slot;
}

bool Slot::isKnown() const
{
    if (isUnset())
        return false;
    const quint8 s = raw & 0xF0;
    const quint8 g = raw & 0x0F;
    return (s == 0x00 || s == 0x10 || s == 0x20 || s == 0x30) && g < kGestureCount;
}

std::optional<Mapping> Mapping::fromDefaultsPayload(const QByteArray &payload)
{
    if (payload.size() < kFunctionCount)
        return std::nullopt;
    Mapping m;
    for (int i = 0; i < kFunctionCount; ++i)
        m.entries[i].raw = static_cast<quint8>(payload.at(i));
    return m;
}

QByteArray Mapping::toSlotBytes() const
{
    QByteArray b;
    for (const Slot &s : entries)
        b.append(static_cast<char>(s.raw));
    return b;
}

std::optional<CurrentMapping> CurrentMapping::fromPayload(const QByteArray &payload)
{
    if (payload.size() < kFunctionCount + 1)
        return std::nullopt;
    CurrentMapping c;
    c.enabled = payload.at(0) == 1;
    auto m = Mapping::fromDefaultsPayload(payload.mid(1));
    if (!m)
        return std::nullopt;
    c.mapping = *m;
    return c;
}

QByteArray encodeSetActions(bool enabled, const Mapping &edited, const Mapping &current)
{
    QByteArray b;
    b.append(static_cast<char>(enabled ? 1 : 0));
    for (int i = 0; i < kFunctionCount; ++i) {
        const Slot &slot = edited.entries[i].isUnset() ? current.entries[i] : edited.entries[i];
        b.append(static_cast<char>(slot.raw));
    }
    return b;
}

QString functionName(Function f)
{
    switch (f) {
    case Function::PlayPause: return QStringLiteral("Play / pause");
    case Function::PreviousTrack: return QStringLiteral("Previous track");
    case Function::NextTrack: return QStringLiteral("Next track");
    case Function::VolumeUp: return QStringLiteral("Volume up");
    case Function::VolumeDown: return QStringLiteral("Volume down");
    case Function::AnswerCall: return QStringLiteral("Answer call");
    case Function::HangUpCall: return QStringLiteral("Hang up call");
    case Function::StartVoiceAssistant: return QStringLiteral("Start voice assistant");
    case Function::CancelVoiceAssistant: return QStringLiteral("Cancel voice assistant");
    case Function::AncOn: return QStringLiteral("Noise cancelling on");
    case Function::AncOff: return QStringLiteral("Noise cancelling off");
    }
    return {};
}

QString gestureName(Gesture g)
{
    switch (g) {
    case Gesture::None: return QStringLiteral("No gesture");
    case Gesture::SingleTap: return QStringLiteral("Single tap");
    case Gesture::DoubleTap: return QStringLiteral("Double tap");
    case Gesture::TripleTap: return QStringLiteral("Triple tap");
    case Gesture::LongPress1s: return QStringLiteral("Long press 1 s");
    case Gesture::LongPress2s: return QStringLiteral("Long press 2 s");
    }
    return QStringLiteral("Gesture %1").arg(static_cast<int>(g));
}

QString sideName(Side s)
{
    switch (s) {
    case Side::None: return QStringLiteral("no earbud");
    case Side::Left: return QStringLiteral("left");
    case Side::Right: return QStringLiteral("right");
    case Side::Both: return QStringLiteral("either earbud");
    }
    return QStringLiteral("side 0x%1").arg(static_cast<int>(s), 2, 16, QLatin1Char('0'));
}

QString slotDescription(const Slot &slot)
{
    if (slot.isUnset())
        return QStringLiteral("(not set)");
    if (!slot.isKnown())
        return QStringLiteral("unknown (0x%1)").arg(slot.raw, 2, 16, QLatin1Char('0'));
    if (slot.gesture() == Gesture::None)
        return gestureName(Gesture::None);
    return QStringLiteral("%1, %2").arg(gestureName(slot.gesture()), sideName(slot.side()));
}

bool Editor::isEditable(Function f)
{
    return f != Function::VolumeUp && f != Function::VolumeDown;
}

bool Editor::hasSideChoice(Function f)
{
    return f == Function::PreviousTrack || f == Function::NextTrack;
}

Editor::Group Editor::group(Function f)
{
    switch (f) {
    case Function::PreviousTrack:
    case Function::NextTrack:
    case Function::PlayPause:
    case Function::StartVoiceAssistant:
    case Function::CancelVoiceAssistant:
    case Function::AncOn:
    case Function::AncOff:
        return Group::Media;
    case Function::AnswerCall:
    case Function::HangUpCall:
        return Group::Calls;
    case Function::VolumeUp:
    case Function::VolumeDown:
        return Group::None;
    }
    return Group::None;
}

void Editor::load(const Mapping &current)
{
    for (int i = 0; i < kFunctionCount; ++i) {
        const Slot &slot = current.entries[i];
        const auto f = static_cast<Function>(i);
        const bool known = slot.isKnown();
        m_gestures[i] = known ? slot.gesture() : Gesture::None;
        if (hasSideChoice(f)) {
            // Official app: a slot on both earbuds is sent back as "right".
            m_sides[i] = known && slot.side() == Side::Left ? Side::Left : Side::Right;
        } else {
            m_sides[i] = Side::Both;
        }
    }
    m_loaded = true;
}

void Editor::setGesture(Function f, Gesture g)
{
    if (!isEditable(f))
        return;
    m_gestures[static_cast<int>(f)] = g;
    switch (f) {
    case Function::PreviousTrack:
        m_gestures[static_cast<int>(Function::NextTrack)] = g;
        m_sides[static_cast<int>(Function::NextTrack)] = opposite(side(Function::PreviousTrack));
        break;
    case Function::NextTrack:
        m_gestures[static_cast<int>(Function::PreviousTrack)] = g;
        m_sides[static_cast<int>(Function::PreviousTrack)] = opposite(side(Function::NextTrack));
        break;
    case Function::StartVoiceAssistant:
        m_gestures[static_cast<int>(Function::CancelVoiceAssistant)] = g;
        break;
    case Function::CancelVoiceAssistant:
        m_gestures[static_cast<int>(Function::StartVoiceAssistant)] = g;
        break;
    case Function::AncOn:
        m_gestures[static_cast<int>(Function::AncOff)] = g;
        break;
    case Function::AncOff:
        m_gestures[static_cast<int>(Function::AncOn)] = g;
        break;
    default:
        break;
    }
}

void Editor::setSide(Function f, Side s)
{
    if (!hasSideChoice(f) || (s != Side::Left && s != Side::Right))
        return;
    m_sides[static_cast<int>(f)] = s;
    const Function partner = f == Function::PreviousTrack ? Function::NextTrack : Function::PreviousTrack;
    m_sides[static_cast<int>(partner)] = opposite(s);
    m_gestures[static_cast<int>(partner)] = gesture(f);
}

QString Editor::validate() const
{
    const QString media = checkGroup({gesture(Function::PreviousTrack), gesture(Function::PlayPause),
                                      gesture(Function::StartVoiceAssistant), gesture(Function::AncOn)},
                                     QStringLiteral("Media controls"));
    if (!media.isEmpty())
        return media;
    return checkGroup({gesture(Function::HangUpCall), gesture(Function::AnswerCall)},
                      QStringLiteral("Call controls"));
}

Mapping Editor::toMapping() const
{
    Mapping m;
    for (int i = 0; i < kFunctionCount; ++i) {
        const auto f = static_cast<Function>(i);
        if (!isEditable(f))
            continue;  // stays 0xFF: keep current
        m.entries[i] = Slot::make(hasSideChoice(f) ? m_sides[i] : Side::Both, m_gestures[i]);
    }
    return m;
}

} // namespace touch
