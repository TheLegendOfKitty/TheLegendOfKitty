#include "gaia/touchmapping.h"

#include <QtTest>

using namespace touch;

namespace {

// Same layout as the simulated earbuds.
const QByteArray kDefaults = QByteArray::fromHex("3213232111323430303535");

Mapping defaults()
{
    return *Mapping::fromDefaultsPayload(kDefaults);
}

} // namespace

class TestTouchMapping : public QObject
{
    Q_OBJECT

private slots:
    void decodesSlots()
    {
        const Mapping m = defaults();
        QCOMPARE(m[Function::PlayPause].side(), Side::Both);
        QCOMPARE(m[Function::PlayPause].gesture(), Gesture::DoubleTap);
        QCOMPARE(m[Function::PreviousTrack].side(), Side::Left);
        QCOMPARE(m[Function::PreviousTrack].gesture(), Gesture::TripleTap);
        QCOMPARE(m[Function::AncOn].gesture(), Gesture::LongPress2s);
        QVERIFY(!Mapping::fromDefaultsPayload(QByteArray(10, 0)));
    }

    void decodesCurrentWithEnabledByte()
    {
        const auto current = CurrentMapping::fromPayload(QByteArray(1, 1) + kDefaults);
        QVERIFY(current);
        QVERIFY(current->enabled);
        QCOMPARE(current->mapping, defaults());
        QVERIFY(!CurrentMapping::fromPayload(kDefaults));
    }

    void setPayloadKeepsUnsetSlotsFromCurrent()
    {
        Mapping edited;  // all unset
        edited[Function::PlayPause] = Slot::make(Side::Both, Gesture::SingleTap);
        const QByteArray payload = encodeSetActions(true, edited, defaults());
        QCOMPARE(payload.size(), 12);
        QCOMPARE(static_cast<quint8>(payload.at(0)), quint8(1));
        QCOMPARE(static_cast<quint8>(payload.at(1)), quint8(0x31));
        QCOMPARE(payload.mid(2), kDefaults.mid(1));
    }

    void defaultsAreValid()
    {
        Editor editor;
        editor.load(defaults());
        QVERIFY2(editor.validate().isEmpty(), qPrintable(editor.validate()));
    }

    void previousAndNextAreLinked()
    {
        Editor editor;
        editor.load(defaults());
        editor.setGesture(Function::PreviousTrack, Gesture::SingleTap);
        QCOMPARE(editor.gesture(Function::NextTrack), Gesture::SingleTap);
        QCOMPARE(editor.side(Function::NextTrack), Side::Right);

        editor.setSide(Function::NextTrack, Side::Left);
        QCOMPARE(editor.side(Function::PreviousTrack), Side::Right);
        QCOMPARE(editor.gesture(Function::PreviousTrack), Gesture::SingleTap);
    }

    void togglesShareAGesture()
    {
        Editor editor;
        editor.load(defaults());
        editor.setGesture(Function::CancelVoiceAssistant, Gesture::TripleTap);
        QCOMPARE(editor.gesture(Function::StartVoiceAssistant), Gesture::TripleTap);
        editor.setGesture(Function::AncOn, Gesture::LongPress1s);
        QCOMPARE(editor.gesture(Function::AncOff), Gesture::LongPress1s);
    }

    void rejectsDuplicateMediaGesture()
    {
        Editor editor;
        editor.load(defaults());
        editor.setGesture(Function::PreviousTrack, Gesture::DoubleTap);  // play/pause is double tap
        QCOMPARE(editor.validate(), QStringLiteral("Media controls: two functions use the same gesture."));
    }

    void rejectsTwoMediaLongPresses()
    {
        Editor editor;
        editor.load(defaults());
        editor.setGesture(Function::PlayPause, Gesture::LongPress1s);  // ANC already uses 2 s
        QCOMPARE(editor.validate(),
                 QStringLiteral("Media controls: only one function can use a long press (1 s or 2 s)."));
    }

    void rejectsCallConflicts()
    {
        Editor editor;
        editor.load(defaults());
        editor.setGesture(Function::AnswerCall, Gesture::LongPress1s);  // same as hang up
        QCOMPARE(editor.validate(), QStringLiteral("Call controls: two functions use the same gesture."));
        editor.setGesture(Function::AnswerCall, Gesture::LongPress2s);
        QCOMPARE(editor.validate(),
                 QStringLiteral("Call controls: only one function can use a long press (1 s or 2 s)."));
    }

    void noGestureNeverConflicts()
    {
        Editor editor;
        editor.load(defaults());
        editor.setGesture(Function::PreviousTrack, Gesture::None);
        editor.setGesture(Function::StartVoiceAssistant, Gesture::None);
        QVERIFY(editor.validate().isEmpty());
    }

    void toMappingFillsEditableSlotsOnly()
    {
        Editor editor;
        editor.load(defaults());
        const Mapping m = editor.toMapping();
        QVERIFY(m[Function::VolumeUp].isUnset());
        QVERIFY(m[Function::VolumeDown].isUnset());
        QCOMPARE(m[Function::PreviousTrack].raw, quint8(0x13));
        QCOMPARE(m[Function::NextTrack].raw, quint8(0x23));
        QCOMPARE(m[Function::HangUpCall].raw, quint8(0x34));
        QCOMPARE(m[Function::StartVoiceAssistant].raw, quint8(0x30));
    }

    void bothSidesLoadAsRightForTrackSkips()
    {
        Mapping m = defaults();
        m[Function::PreviousTrack] = Slot::make(Side::Both, Gesture::TripleTap);
        Editor editor;
        editor.load(m);
        QCOMPARE(editor.side(Function::PreviousTrack), Side::Right);
    }
};

QTEST_APPLESS_MAIN(TestTouchMapping)
#include "tst_touchmapping.moc"
