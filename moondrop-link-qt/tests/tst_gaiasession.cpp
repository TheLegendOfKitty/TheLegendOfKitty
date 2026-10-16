#include "gaia/gaiasession.h"
#include "sim/simulateddevice.h"

#include <QtTest>

#include <memory>

using namespace gaia;

class TestGaiaSession : public QObject
{
    Q_OBJECT

private:
    struct Rig
    {
        std::unique_ptr<sim::SimulatedDevice> device = std::make_unique<sim::SimulatedDevice>();
        std::unique_ptr<Session> session = std::make_unique<Session>();

        Rig() { device->open(QIODevice::ReadWrite | QIODevice::Unbuffered); }

        bool startAndWaitReady()
        {
            QSignalSpy ready(session.get(), &Session::ready);
            session->start(device.get());
            return ready.wait(2000);
        }
    };

private slots:
    void handshakeBytesMatchOfficialApp()
    {
        Rig rig;
        rig.device->setSilent(true);
        rig.session->start(rig.device.get());
        QCOMPARE(rig.device->receivedFrames().size(), 1);
        QCOMPARE(rig.device->receivedFrames().first().raw, QByteArray::fromHex("FF010000000A0300"));
    }

    void reachesReadyAndReadsInfo()
    {
        Rig rig;
        QVERIFY(rig.startAndWaitReady());
        QCOMPARE(rig.session->state(), Session::State::Ready);
        QCOMPARE(rig.session->deviceInfo().apiMajor, 3);
        QVERIFY(rig.session->hasFeature(Feature::Earbud));
        QVERIFY(rig.session->hasFeature(Feature::MusicProcessing));
        QVERIFY(!rig.session->hasFeature(Feature::Anc));
        QTRY_COMPARE(rig.session->deviceInfo().firmwareVersion, QStringLiteral("1.0.0-sim"));
        QCOMPARE(rig.session->deviceInfo().serialNumber, QStringLiteral("SIM0000001"));
        QCOMPARE(rig.session->deviceInfo().variantName, QStringLiteral("Space Travel (simulated)"));
        QCOMPARE(rig.session->deviceInfo().gaiaVersion, 3);
        // Basic v2 => the session upgrades to frame version 4, as the official app does.
        QTRY_COMPARE(int(rig.session->frameVersion()), 4);
    }

    void registersNotificationsForUsedFeatures()
    {
        Rig rig;
        QVERIFY(rig.startAndWaitReady());
        QTRY_VERIFY(rig.session->deviceInfo().firmwareVersion.size() > 0);
        QSet<int> registered;
        for (const Frame &f : rig.device->receivedFrames()) {
            const CommandWord w = CommandWord::fromRaw(f.command());
            if (f.vendor() == kVendorV3 && w.feature == 0 && w.command == basic::RegisterNotification)
                registered.insert(static_cast<quint8>(f.payload().at(0)));
        }
        QCOMPARE(registered, (QSet<int>{1, 5}));
    }

    void failsWhenDeviceIsSilent()
    {
        Rig rig;
        rig.device->setSilent(true);
        rig.session->setRequestTimeout(300);
        QSignalSpy failed(rig.session.get(), &Session::failed);
        rig.session->start(rig.device.get());
        QVERIFY(failed.wait(2000));
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("handshake")));
        QCOMPARE(rig.session->state(), Session::State::Failed);
    }

    void rejectsNonV3Devices()
    {
        Rig rig;
        rig.device->setApiVersion(1, 2, 1);
        QSignalSpy failed(rig.session.get(), &Session::failed);
        rig.session->start(rig.device.get());
        QVERIFY(failed.wait(2000));
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("GAIA v2.1")));
    }

    void readsAndAppliesTouchMapping()
    {
        Rig rig;
        QVERIFY(rig.startAndWaitReady());
        QSignalSpy current(rig.session.get(), &Session::touchCurrentChanged);
        rig.session->requestTouchMapping();
        QVERIFY(current.wait(2000));
        QVERIFY(rig.session->defaultTouchMapping().has_value());
        QVERIFY(rig.session->currentTouchMapping()->enabled);

        touch::Editor editor;
        editor.load(rig.session->currentTouchMapping()->mapping);
        editor.setGesture(touch::Function::PreviousTrack, touch::Gesture::SingleTap);
        editor.setSide(touch::Function::PreviousTrack, touch::Side::Right);
        QVERIFY(editor.validate().isEmpty());

        QSignalSpy finished(rig.session.get(), &Session::touchApplyFinished);
        rig.session->applyTouchMapping(editor.toMapping());
        QVERIFY(finished.wait(2000));
        QCOMPARE(finished.first().at(0).toBool(), true);

        const QByteArray stored = rig.device->currentActions();
        QCOMPARE(static_cast<quint8>(stored.at(0)), quint8(1));
        QCOMPARE(static_cast<quint8>(stored.at(1 + int(touch::Function::PreviousTrack))), quint8(0x21));
        QCOMPARE(static_cast<quint8>(stored.at(1 + int(touch::Function::NextTrack))), quint8(0x11));
        // Volume slots are not editable and keep their values.
        QCOMPARE(stored.mid(1 + int(touch::Function::VolumeUp), 2), rig.device->defaultActions().mid(3, 2));
        // The session re-reads the mapping after applying.
        QTRY_COMPARE(rig.session->currentTouchMapping()->mapping.toSlotBytes(), stored.mid(1));
    }

    void touchApplyWithoutConfirmationStillSucceeds()
    {
        Rig rig;
        rig.device->setSendActionNotifications(false);
        rig.session->setTouchConfirmTimeout(200);
        QVERIFY(rig.startAndWaitReady());
        QSignalSpy current(rig.session.get(), &Session::touchCurrentChanged);
        rig.session->requestTouchMapping();
        QVERIFY(current.wait(2000));

        QSignalSpy finished(rig.session.get(), &Session::touchApplyFinished);
        rig.session->restoreDefaultTouchMapping();
        QVERIFY(finished.wait(2000));
        QCOMPARE(finished.first().at(0).toBool(), true);
        QVERIFY(finished.first().at(1).toString().contains(QStringLiteral("did not confirm")));
    }

    void touchApplyErrorIsReported()
    {
        Rig rig;
        rig.device->failCommand(Feature::Earbud, earbud::SetActions, 6);
        QVERIFY(rig.startAndWaitReady());
        QSignalSpy current(rig.session.get(), &Session::touchCurrentChanged);
        rig.session->requestTouchMapping();
        QVERIFY(current.wait(2000));

        QSignalSpy finished(rig.session.get(), &Session::touchApplyFinished);
        rig.session->restoreDefaultTouchMapping();
        QVERIFY(finished.wait(2000));
        QCOMPARE(finished.first().at(0).toBool(), false);
        QVERIFY(finished.first().at(1).toString().contains(QStringLiteral("incorrect state")));
    }

    void readsEqAndSelectsPreset()
    {
        Rig rig;
        QVERIFY(rig.startAndWaitReady());
        QSignalSpy bands(rig.session.get(), &Session::userBandsChanged);
        rig.session->requestEqInfo();
        QVERIFY(bands.wait(2000));
        QVERIFY(rig.session->eqPresent());
        QCOMPARE(rig.session->presets(), (QVector<int>{1, 2, 3, 63}));
        QCOMPARE(rig.session->selectedPreset(), 1);
        QCOMPARE(rig.session->userBandCount(), 5);
        QCOMPARE(rig.session->userBands().size(), 5);
        QCOMPARE(rig.session->userBands().at(2).frequencyHz, 1000);

        QSignalSpy selected(rig.session.get(), &Session::presetSelectFinished);
        rig.session->selectPreset(2);
        QVERIFY(selected.wait(2000));
        QCOMPARE(selected.first().at(0).toBool(), true);
        QCOMPARE(rig.device->selectedPreset(), 2);
        QTRY_COMPARE(rig.session->selectedPreset(), 2);
    }

    void invalidPresetIsRejected()
    {
        Rig rig;
        QVERIFY(rig.startAndWaitReady());
        QSignalSpy selected(rig.session.get(), &Session::presetSelectFinished);
        rig.session->selectPreset(9);
        QVERIFY(selected.wait(2000));
        QCOMPARE(selected.first().at(0).toBool(), false);
        QCOMPARE(rig.device->selectedPreset(), 1);
    }

    void appliesCustomBands()
    {
        Rig rig;
        QVERIFY(rig.startAndWaitReady());
        QSignalSpy bands(rig.session.get(), &Session::userBandsChanged);
        rig.session->requestEqInfo();
        QVERIFY(bands.wait(2000));

        QVector<EqBand> edited = rig.session->userBands();
        edited[2].gainDb = 4.5;
        edited[3].q = 2.0;
        QSignalSpy finished(rig.session.get(), &Session::userBandsApplyFinished);
        rig.session->setUserBands(edited);
        QVERIFY(finished.wait(2000));
        QCOMPARE(finished.first().at(0).toBool(), true);
        QCOMPARE(rig.device->userBands(), edited);
        QTRY_COMPARE(rig.session->userBands(), edited);
    }

    void reportsHandover()
    {
        Rig rig;
        QVERIFY(rig.startAndWaitReady());
        QSignalSpy handover(rig.session.get(), &Session::handoverAnnounced);
        rig.device->sendNotification(Feature::Earbud, earbud::Handover, QByteArray::fromHex("0002"));
        QVERIFY(handover.wait(2000));
        QCOMPARE(handover.first().at(0).toBool(), true);
        QCOMPARE(handover.first().at(1).toInt(), 2);
    }
};

QTEST_GUILESS_MAIN(TestGaiaSession)
#include "tst_gaiasession.moc"
