#include "gaia/gaiaprotocol.h"

#include <QtTest>

using namespace gaia;

class TestGaiaProtocol : public QObject
{
    Q_OBJECT

private slots:
    void commandWords()
    {
        QCOMPARE(makeCommandWord(Feature::MusicProcessing, PacketType::Command, music::SetSelectedSet), quint16(0x0A03));
        QCOMPARE(makeCommandWord(Feature::MusicProcessing, PacketType::Notification, 3), quint16(0x0A83));
        QCOMPARE(makeCommandWord(Feature::MusicProcessing, PacketType::Response, 3), quint16(0x0B03));
        QCOMPARE(makeCommandWord(Feature::MusicProcessing, PacketType::Error, 3), quint16(0x0B83));
        QCOMPARE(makeCommandWord(Feature::Earbud, PacketType::Command, earbud::SetActions), quint16(0x0203));
        QCOMPARE(makeCommandWord(Feature::Basic, PacketType::Command, basic::GetSupportedFeatures), quint16(0x0001));

        const CommandWord w = CommandWord::fromRaw(0x0282);
        QCOMPARE(w.feature, quint8(1));
        QCOMPARE(w.type, PacketType::Notification);
        QCOMPARE(w.command, quint8(2));
        QCOMPARE(w.raw(), quint16(0x0282));
    }

    void parsesApiVersion()
    {
        const auto v = ApiVersion::parse(QByteArray::fromHex("030300"));
        QVERIFY(v);
        QCOMPARE(v->protocolVersion, 3);
        QCOMPARE(v->apiMajor, 3);
        QCOMPARE(v->apiMinor, 0);
        QVERIFY(!ApiVersion::parse(QByteArray::fromHex("0303")));
    }

    void parsesFeatureList()
    {
        const FeatureList list = FeatureList::parse(QByteArray::fromHex("00000201010501"));
        QVERIFY(!list.moreToCome);
        QCOMPARE(list.features.size(), 3);
        QCOMPARE(list.features[0].id, 0);
        QCOMPARE(list.features[0].version, 2);
        QCOMPARE(list.features[2].id, 5);
        QCOMPARE(list.features[2].version, 1);
        QVERIFY(FeatureList::parse(QByteArray::fromHex("010002")).moreToCome);
    }

    void transportParameter()
    {
        QCOMPARE(encodeTransportParameter(TransportKey::ProtocolVersion, 4), QByteArray::fromHex("0700000004"));
        const auto p = TransportParameter::parse(QByteArray::fromHex("010000FFFF"));
        QVERIFY(p);
        QCOMPARE(p->key, 1);
        QCOMPARE(p->value, quint32(0xFFFF));
    }

    void presetList()
    {
        const auto ids = parsePresetList(QByteArray::fromHex("040102033F"));
        QVERIFY(ids);
        QCOMPARE(*ids, (QVector<int>{1, 2, 3, 63}));
        QVERIFY(!parsePresetList(QByteArray::fromHex("0301")));
        QCOMPARE(defaultPresetName(63), QStringLiteral("User"));
        QCOMPARE(defaultPresetName(0), QStringLiteral("Flat"));
        QCOMPARE(defaultPresetName(42), QStringLiteral("Preset 42"));
    }

    void userBandEncoding()
    {
        // 1000 Hz, Q 1.5 (0x1800), parametric (13), -3.5 dB (-210 = 0xFF2E).
        const QVector<EqBand> bands{{2, 1000, 1.5, 13, -3.5}};
        QCOMPARE(encodeUserBands(bands), QByteArray::fromHex("020203E818000DFF2E"));

        const auto decoded = parseUserBands(QByteArray::fromHex("020203E818000DFF2E"));
        QVERIFY(decoded);
        QCOMPARE(decoded->size(), 1);
        QCOMPARE(decoded->at(0), bands[0]);
    }

    void userBandRoundTrip()
    {
        QVector<EqBand> bands;
        for (int i = 0; i < 5; ++i)
            bands.append({i, 100 * (i + 1), 0.7 + i * 0.1, 13, i - 2.25});
        const auto decoded = parseUserBands(encodeUserBands(bands));
        QVERIFY(decoded);
        QCOMPARE(*decoded, bands);
        QVERIFY(!parseUserBands(QByteArray::fromHex("0001")));  // two bands promised, none sent
    }

    void handover()
    {
        const auto info = HandoverInfo::parse(QByteArray::fromHex("0003"));
        QVERIFY(info);
        QVERIFY(info->isStatic);
        QCOMPARE(info->delaySeconds, 3);
        const auto dyn = HandoverInfo::parse(QByteArray::fromHex("01"));
        QVERIFY(dyn);
        QVERIFY(!dyn->isStatic);
        QCOMPARE(dyn->delaySeconds, 5);
    }
};

QTEST_APPLESS_MAIN(TestGaiaProtocol)
#include "tst_gaiaprotocol.moc"
