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
    // Pin Qt's serialization format so different Qt 6 releases use the same wire representation.
    stream.setVersion(QDataStream::Qt_6_0);

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
    // Pin Qt's serialization format so different Qt 6 releases use the same wire representation.
    stream.setVersion(QDataStream::Qt_6_0);

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


/**
 * serializeHandshake()
 * Excodes a handshake payload into its binary wire representation
 */
QByteArray Protocol::serializeHandshake(const HandshakePayload& handshake) {
    QByteArray data;

    // Write the handshake fields into a binary payload
    QDataStream stream(&data, QIODevice::WriteOnly);
    // Use a fixed byte order so all supported platforms interpret the data identically
    stream.setByteOrder(QDataStream::BigEndian);
    // Pin Qt's serialization format so different Qt 6 releases use the same wire representation.
    stream.setVersion(QDataStream::Qt_6_0);

    // Serialize fields in a fixed order that deserializeHandshake() must mirror
    stream << handshake.protocolVersion;
    stream << handshake.applicationVersion;
    stream << handshake.deviceName;

    return data;
}


/**
 * deserializeHandshake()
 * Decodes a binary handshake payload into structured peer information
 */
bool Protocol::deserializeHandshake(const QByteArray& data, HandshakePayload& handshake) {
    
    QDataStream stream(data);
    // Match the byte order used when serializing handshake payloads
    stream.setByteOrder(QDataStream::BigEndian);
    // Pin Qt's serialization format so different Qt 6 releases use the same wire representation.
    stream.setVersion(QDataStream::Qt_6_0);

    std::uint16_t protocolVersion = 0;
    QString applicationVersion;
    QString deviceName;

    // Read fields in exactly the same order they were serialized
    stream >> protocolVersion;
    stream >> applicationVersion;
    stream >> deviceName;

    // Reject malformed or incomplete handshake payloads
    if(stream.status() != QDataStream::Ok) {
        return false;
    }

    handshake.protocolVersion = protocolVersion;
    handshake.applicationVersion = applicationVersion;
    handshake.deviceName = deviceName;

    return true;
}


/**
 * serializeMessage()
 * Encodes a complete protocol message as its header followed by its payload
 */
QByteArray Protocol::serializeMessage(const Message& message) {
    // Rebuild the header size from the actual payload so they cannot disagree
    MessageHeader header = message.header;
    header.payloadSize = static_cast<std::uint64_t>(message.payload.size());

    QByteArray data = serializeHeader(header);

    // Append the payload immediately after the fixed-size protocol header
    data.append(message.payload);

    return data;
}
