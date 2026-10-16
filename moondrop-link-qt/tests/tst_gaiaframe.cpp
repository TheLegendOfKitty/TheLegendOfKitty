#include "gaia/gaiaframe.h"

#include <QtTest>

using namespace gaia;

class TestGaiaFrame : public QObject
{
    Q_OBJECT

private slots:
    void encodesHandshake()
    {
        QCOMPARE(encodeFrame(1, 0x000A, 0x0300), QByteArray::fromHex("FF010000000A0300"));
    }

    void encodesPresetSelect()
    {
        QCOMPARE(encodeFrame(1, 0x001D, 0x0A03, QByteArray::fromHex("01")),
                 QByteArray::fromHex("FF010001001D0A0301"));
    }

    void encodesChecksumAsXorOfPrecedingBytes()
    {
        const QByteArray frame = encodeFrame(3, 0x001D, 0x0001, {}, true);
        QCOMPARE(frame.left(8), QByteArray::fromHex("FF030100001D0001"));
        quint8 x = 0;
        for (int i = 0; i < 8; ++i)
            x ^= static_cast<quint8>(frame.at(i));
        QCOMPARE(frame.size(), 9);
        QCOMPARE(static_cast<quint8>(frame.at(8)), x);
    }

    void lengthLimitsFollowVersion()
    {
        QVERIFY(!encodeFrame(3, 0x001D, 0x0000, QByteArray(254, 'a')).isEmpty());
        QVERIFY(encodeFrame(3, 0x001D, 0x0000, QByteArray(255, 'a')).isEmpty());

        // Version 4: 255 still fits the single length byte, 256 needs the extension.
        const QByteArray f255 = encodeFrame(4, 0x001D, 0x0000, QByteArray(255, 'a'));
        QCOMPARE(static_cast<quint8>(f255.at(2)), quint8(0));
        QCOMPARE(static_cast<quint8>(f255.at(3)), quint8(0xFF));
        QCOMPARE(f255.size(), 8 + 255);

        const QByteArray f256 = encodeFrame(4, 0x001D, 0x0000, QByteArray(256, 'a'));
        QCOMPARE(static_cast<quint8>(f256.at(2)), kFlagLengthExtension);
        QCOMPARE(static_cast<quint8>(f256.at(3)), quint8(0x01));
        QCOMPARE(static_cast<quint8>(f256.at(4)), quint8(0x00));
        QCOMPARE(f256.size(), 9 + 256);
    }

    void decodesByteByByteWithGarbage()
    {
        QByteArray stream = QByteArray::fromHex("0011");
        stream += encodeFrame(1, 0x001D, 0x0B03);
        stream += encodeFrame(1, 0x001D, 0x0A81, QByteArray::fromHex("02"));

        FrameDecoder decoder;
        QVector<Frame> frames;
        for (char c : stream)
            frames += decoder.feed(QByteArray(1, c));

        QCOMPARE(frames.size(), 2);
        QCOMPARE(decoder.skippedBytes(), qint64(2));
        QCOMPARE(frames[0].vendor(), quint16(0x001D));
        QCOMPARE(frames[0].command(), quint16(0x0B03));
        QVERIFY(frames[0].payload().isEmpty());
        QCOMPARE(frames[1].command(), quint16(0x0A81));
        QCOMPARE(frames[1].payload(), QByteArray::fromHex("02"));
    }

    void decodesExtendedLength()
    {
        const QByteArray payload(300, 'x');
        FrameDecoder decoder;
        const auto frames = decoder.feed(encodeFrame(4, 0x001D, 0x0000, payload));
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].payload(), payload);
    }

    void ignoresExtensionFlagBelowVersion4()
    {
        // Version 3 with the extension flag set still uses a 1-byte length.
        const QByteArray raw = QByteArray::fromHex("FF030201001D000005");
        FrameDecoder decoder;
        const auto frames = decoder.feed(raw);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].payload(), QByteArray::fromHex("05"));
    }

    void reportsChecksumValidity()
    {
        QByteArray good = encodeFrame(1, 0x001D, 0x0100, QByteArray::fromHex("AB"), true);
        QByteArray bad = good;
        bad[bad.size() - 1] = static_cast<char>(bad.at(bad.size() - 1) ^ 0x01);

        FrameDecoder decoder;
        auto frames = decoder.feed(good + bad);
        QCOMPARE(frames.size(), 2);
        QVERIFY(frames[0].checksumValid);
        QVERIFY(!frames[1].checksumValid);
        QCOMPARE(frames[1].payload(), QByteArray::fromHex("AB"));
    }
};

QTEST_APPLESS_MAIN(TestGaiaFrame)
#include "tst_gaiaframe.moc"
