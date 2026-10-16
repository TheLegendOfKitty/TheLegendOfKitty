#include "gaia/gaiaframe.h"

namespace gaia {

namespace {

quint8 xorChecksum(const QByteArray &bytes)
{
    quint8 sum = 0;
    for (char c : bytes)
        sum ^= static_cast<quint8>(c);
    return sum;
}

quint16 readU16(const QByteArray &bytes, int offset)
{
    return static_cast<quint16>((static_cast<quint8>(bytes.at(offset)) << 8)
                                | static_cast<quint8>(bytes.at(offset + 1)));
}

bool usesLengthExtension(quint8 version, quint8 flags)
{
    return version >= 4 && (flags & kFlagLengthExtension);
}

} // namespace

quint16 Frame::vendor() const
{
    return isValid() ? readU16(content, 0) : 0;
}

quint16 Frame::command() const
{
    return isValid() ? readU16(content, 2) : 0;
}

QByteArray Frame::payload() const
{
    return isValid() ? content.mid(4) : QByteArray();
}

int maxPayloadForVersion(quint8 version)
{
    return version >= 4 ? kMaxExtendedPayload : kMaxShortPayload;
}

QByteArray encodeFrame(quint8 version, quint16 vendor, quint16 command,
                       const QByteArray &payload, bool withChecksum)
{
    const int length = payload.size();
    if (length > maxPayloadForVersion(version))
        return {};

    // Same rule as the official encoder: up to 255 fits the single length byte;
    // anything bigger needs the extension (version >= 4 only, checked above).
    const bool extended = length > 255;

    quint8 flags = 0;
    if (withChecksum)
        flags |= kFlagChecksum;
    if (extended)
        flags |= kFlagLengthExtension;

    QByteArray frame;
    frame.reserve(8 + length + (extended ? 1 : 0) + (withChecksum ? 1 : 0));
    frame.append(static_cast<char>(kStartOfFrame));
    frame.append(static_cast<char>(version));
    frame.append(static_cast<char>(flags));
    if (extended)
        frame.append(static_cast<char>((length >> 8) & 0xFF));
    frame.append(static_cast<char>(length & 0xFF));
    frame.append(static_cast<char>((vendor >> 8) & 0xFF));
    frame.append(static_cast<char>(vendor & 0xFF));
    frame.append(static_cast<char>((command >> 8) & 0xFF));
    frame.append(static_cast<char>(command & 0xFF));
    frame.append(payload);
    if (withChecksum)
        frame.append(static_cast<char>(xorChecksum(frame)));
    return frame;
}

void FrameDecoder::reset()
{
    m_buffer.clear();
}

int FrameDecoder::expectedLength() const
{
    if (m_buffer.size() < 4)
        return -1;
    const auto version = static_cast<quint8>(m_buffer.at(1));
    const auto flags = static_cast<quint8>(m_buffer.at(2));
    const bool extended = usesLengthExtension(version, flags);
    const int headerSize = extended ? 5 : 4;
    if (m_buffer.size() < headerSize)
        return -1;
    const int length = extended ? readU16(m_buffer, 3) : static_cast<quint8>(m_buffer.at(3));
    return headerSize + 4 + length + ((flags & kFlagChecksum) ? 1 : 0);
}

QVector<Frame> FrameDecoder::feed(const QByteArray &bytes)
{
    QVector<Frame> frames;
    for (char c : bytes) {
        if (m_buffer.isEmpty() && static_cast<quint8>(c) != kStartOfFrame) {
            ++m_skipped;
            continue;
        }
        m_buffer.append(c);

        const int expected = expectedLength();
        if (expected < 0 || m_buffer.size() < expected)
            continue;

        Frame frame;
        frame.raw = m_buffer;
        frame.version = static_cast<quint8>(m_buffer.at(1));
        frame.flags = static_cast<quint8>(m_buffer.at(2));
        const int headerSize = usesLengthExtension(frame.version, frame.flags) ? 5 : 4;
        const bool hasChecksum = frame.flags & kFlagChecksum;
        frame.content = m_buffer.mid(headerSize, expected - headerSize - (hasChecksum ? 1 : 0));
        if (hasChecksum) {
            const quint8 received = static_cast<quint8>(m_buffer.at(expected - 1));
            frame.checksumValid = received == xorChecksum(m_buffer.left(expected - 1));
        }
        frames.append(frame);
        m_buffer.clear();
    }
    return frames;
}

} // namespace gaia
