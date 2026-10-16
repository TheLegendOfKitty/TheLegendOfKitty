#pragma once

// GAIA framing used on the RFCOMM link, as implemented by the MOONDROP Link
// Android app (Qualcomm GAIA client library):
//
//   FF | version | flags | length (1 or 2 bytes) | vendor (2) | command (2) | payload | [checksum]
//
// - length counts the payload only (not vendor/command).
// - flags bit 0: a checksum byte follows (XOR of every preceding byte).
// - flags bit 1: 2-byte length; only honoured when version >= 4.
// - all multi-byte fields are big-endian.

#include <QByteArray>
#include <QVector>

namespace gaia {

constexpr quint8 kStartOfFrame = 0xFF;
constexpr quint8 kFlagChecksum = 0x01;
constexpr quint8 kFlagLengthExtension = 0x02;
constexpr int kMaxShortPayload = 254;
constexpr int kMaxExtendedPayload = 65535;

struct Frame
{
    quint8 version = 1;
    quint8 flags = 0;
    QByteArray content;  // vendor (2) + command (2) + payload
    QByteArray raw;      // the whole frame as received
    bool checksumValid = true;

    bool isValid() const { return content.size() >= 4; }
    quint16 vendor() const;
    quint16 command() const;
    QByteArray payload() const;
};

// Largest payload a frame of this version can carry.
int maxPayloadForVersion(quint8 version);

// Builds a complete frame. Returns an empty array if the payload is too large
// for the given protocol version.
QByteArray encodeFrame(quint8 version, quint16 vendor, quint16 command,
                       const QByteArray &payload = {}, bool withChecksum = false);

// Incremental decoder for a byte stream. Bytes before a start-of-frame marker
// are skipped, like the official app does.
class FrameDecoder
{
public:
    QVector<Frame> feed(const QByteArray &bytes);
    void reset();
    qint64 skippedBytes() const { return m_skipped; }

private:
    int expectedLength() const;

    QByteArray m_buffer;
    qint64 m_skipped = 0;
};

} // namespace gaia
