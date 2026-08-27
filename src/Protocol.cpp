#include "Protocol.hpp"

#include <QDataStream>
#include <QIODevice>

#include <cstdint>


/**
 * serializeHeader()
 * Encodes a message header into its fixed-size network byte representation
 */
QByteArray Protocol::serializeHeader(const MessageHeader& header) {
    QByteArray data;

    // Write binary protocol fields into the byte array
    QDataStream stream(&data, QIODevice::WriteOnly);

    // Use a fixed byte order so Windows and macOS interpret the same bytes
    stream.setByteOrder(QDataStream::BigEndian);

    // Serialize the one-byte message type followed byt the eight-byte payload size
    stream << static_cast<std::uint8_t>(header.type);
    stream << header.payloadSize;

    return data;
}


/**
 * deserializeHeader()
 * Decodes a complete wire-format header into a MessageHeader
 */
bool Protocol::deserializeHeader(const QByteArray& data, MessageHeader& header) {
    // Reject incomplete or oversized input instead of partially parsing a header
    if(data.size() != headerSize()) {
        return false;
    }

    QDataStream stream(data);

    // Match the byte order used when serializing protocol headers
    stream.setByteOrder(QDataStream::BigEndian);

    std::uint8_t type = 0;
    std::uint64_t payloadSize = 0;

    // Read the fixed header fields in the same order they were written
    stream >> type;
    stream >> payloadSize;

    // Reject malformed stream data before exposing partially decoded values
    if(stream.status() != QDataStream::Ok) {
        return false;
    }

    // Reject undefined message identifiers before exposing the decoded header
    if(!isValidMessageType(type)) {
        return false;
    }

    header.type = static_cast<MessageType>(type);
    header.payloadSize = payloadSize;

    return true;
}


/**
 * headerSize()
 * Returns the fixed number of bytes used by every serialized message header
 */
qsizetype Protocol::headerSize() {
    return sizeof(std::uint8_t) + sizeof(std::uint64_t);
}


/**
 * isValidHeaderType()
 * Returns whether a raw message type value is defined by the FileBridge protocol
 */
bool Protocol::isValidMessageType(std::uint8_t type) {
    switch(static_cast<MessageType>(type)) {
        case MessageType::Handshake:
        case MessageType::FileOffer:
        case MessageType::FileAccept:
        case MessageType::FileReject:
        case MessageType::FileData:
        case MessageType::FileComplete:
        case MessageType::Error:
            return true;
        case MessageType::Invalid:
            return false;
    }
    return false;
}
