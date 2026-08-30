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
 * serializeHandshakePayload()
 * Excodes a handshake payload into its binary wire representation
 */
QByteArray Protocol::serializeHandshakePayload(const HandshakePayload& handshake) {
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
 * deserializeHandshakePayload()
 * Decodes a binary handshake payload into structured peer information
 */
bool Protocol::deserializeHandshakePayload(const QByteArray& data, HandshakePayload& handshake) {
    
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
 * serializeFileOfferPayload()
 * Serializes file-offer metadata into a protocol payload
 */
QByteArray Protocol::serializeFileOfferPayload(const FileOfferPayload& offer) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    // Use the same byte order as the rest of the FileBridge protocol
    stream.setByteOrder(QDataStream::BigEndian);
    // Pin Qt's serialization format so different Qt releases use the same wire format
    stream.setVersion(QDataStream::Qt_6_0);

    // Write fields in a fixed order so the receiver can reconstruct the offer
    stream << offer.transferId;
    stream << offer.fileName;
    stream << offer.fileSize;

    return data;
}


/**
 * deserializeFileOfferPayload()
 * Deserializes file-offer metadata from a protocol payload
 */
bool Protocol::deserializeFileOfferPayload(const QByteArray &data, FileOfferPayload &offer) {
    QDataStream stream(data);

    // Match the byte order used when serializing file offers
    stream.setByteOrder(QDataStream::BigEndian);

    // Use the same fixer serialization format used when the protocol data was written
    stream.setVersion(QDataStream::Qt_6_0);

    std::uint64_t transferId = 0;
    QString fileName;
    std::uint64_t fileSize = 0;

    // Read fields in exactly the same order used by serializeFileOffer()
    stream >> transferId;
    stream >> fileName;
    stream >> fileSize;

    // Reject truncated or otherwise malformed payloads
    if(stream.status() != QDataStream::Ok) {
        return false;
    }

    offer.transferId = transferId;
    offer.fileName = fileName;
    offer.fileSize = fileSize;

    return true;
}


/**
 * serializeFileAcceptPayload()
 * Serializes an accepted transfer identifier into a protocol payload
 */
QByteArray Protocol::serializeFileAcceptPayload(const FileAcceptPayload& accept) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    // Use the same byte order as the rest of the FileBridge protocol
    stream.setByteOrder(QDataStream::BigEndian);
    // Pin Qt's serialization format so different Qt 6 releases use the same wire representation
    stream.setVersion(QDataStream::Qt_6_0);

    // The message type already indicates acceptance, so only the transfer identifier is required
    stream << accept.transferId;

    return data;
}


/**
 * deserializeFileAcceptPayload()
 * Deserializes an accepted transfer identifier from a protocol payload
 */
bool Protocol::deserializeFileAcceptPayload(const QByteArray& data, FileAcceptPayload& accept) {
    QDataStream stream(data);

    // Match the byte order used when serializing FileAccept payloads
    stream.setByteOrder(QDataStream::BigEndian);
    // Use the same fixed serialization format used when the protocol data was written
    stream.setVersion(QDataStream::Qt_6_0);

    std::uint64_t transferId = 0;

    // Recover the identifier of the transfer that the receiver accepted
    stream >> transferId;

    // Reject malformed or incomplete payload data
    if(stream.status() != QDataStream::Ok) {
        return false;
    }

    // Update the output object only after decoding succeeds
    accept.transferId = transferId;

    return true;
}


/**
 * serializeFileRejectPayload()
 * Serializes a rejected transfer identifier into a protocol payload
 */
QByteArray Protocol::serializeFileRejectPayload(const FileRejectPayload& reject) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    // Use the same byte order as the rest of the FileBridge protocol
    stream.setByteOrder(QDataStream::BigEndian);
    //Pin Qt's serialization format so different Qt 6 releases use the same wire representation
    stream.setVersion(QDataStream::Qt_6_0);

    // The message type already indicates rejection, so only the transfer identifier is required
    stream << reject.transferId;

    return data;
}


/**
 * deserializeFilePayload()
 * Deserializes a rejected transfer identifier from a protocol payload
 */
bool Protocol::deserializeFileRejectPayload(const QByteArray& data, FileRejectPayload& reject) {
    QDataStream stream(data);

    // Match the byte order used when serializing FileReject payloads
    stream.setByteOrder(QDataStream::BigEndian);
    // Use the same fixed serialization format used when the protocol data was written
    stream.setVersion(QDataStream::Qt_6_0);

    std::uint64_t transferId = 0;

    // Recover the identifier of the transfer that the receiver rejected
    stream >> transferId;

    // Reject malformed or incomplete payload data
    if(stream.status() != QDataStream::Ok) {
        return false;
    }

    // Update the output object only after decoding succeeds
    reject.transferId = transferId;

    return true;
}


/**
 * serializeFileDataPayload()
 * Encodes one file-data chunk into the payload bytes of a FileData message
 */
QByteArray Protocol::serializeFileDataPayload(const FileDataPayload& fileData) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    // Use the same byte order as the rest of the FileBridge protocol
    stream.setByteOrder(QDataStream::BigEndian);
    // Pin Qt's serialization format so different Qt 6 releases use the same wire representation
    stream. setVersion(QDataStream::Qt_6_0);

    // Write the transfer identifier first so the receiver knows which file own this chunk
    stream << fileData.transferId;

    // Write the byte offset so the receiver knows where this chunk belongs in the file
    stream << fileData.offset;

    // Write the raw file bytes for this chunk
    stream << fileData.data;

    return data;
}


/**
 * deserializeFileDataPayload()
 * Decodes the payload bytes of a FileData message into one file-data chunk
 */
bool Protocol::deserializeFileDataPayload(const QByteArray& data, FileDataPayload& fileData) {
    QDataStream stream(data);

    // Match the byte order used when serializing FileData payloads
    stream.setByteOrder(QDataStream::BigEndian);
    // Pin Qt's serialization format so different Qt 6 releases use the same wire representation
    stream.setVersion(QDataStream::Qt_6_0);

    // Read into temp values so the output object is unchanged if decoding fails
    std::uint64_t transferId = 0;
    std::uint64_t offset = 0;
    QByteArray chunkData;

    // Fields must be read in exactly the same order in which they were serialized
    stream >> transferId;
    stream >> offset;
    stream >> chunkData;

    // Reject truncated or otherwise malformed payload data
    if(stream.status() != QDataStream::Ok) {
        return false;
    }

    // Store the decoded values only after the complete payload was read successfully
    fileData.transferId = transferId;
    fileData.offset = offset;
    fileData.data = chunkData;

    return true;
}


/**
 * serializeFileCompletePayload()
 * Encodes a completed transfer identifier into the payload bytes of a FileComplete
 * message
 */
QByteArray Protocol::serializeFileCompletePayload(const FileCompletePayload &complete) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    // Use the same byte order as the rest of the FileBridge protocol
    stream.setByteOrder(QDataStream::BigEndian);
    // Pin Qt's serialization format so different Qt 6 releases use the same wire representation
    stream.setVersion(QDataStream::Qt_6_0);

    // FileComplete only needs the identifier of the transfer that finished sending
    stream << complete.transferId;

    return data;
}


/**
 * deserializeFileCompletePayload()
 * Decodes the payload bytes of a FileComplete message into its transfer identifier
 */
bool Protocol::deserializeFileCompletePayload(const QByteArray &data, FileCompletePayload &complete) {
    QDataStream stream(data);

    // Match the byte order used when serializing FileComplete payloads
    stream.setByteOrder(QDataStream::BigEndian);
    // Use the same fixed serialization format used when the protocol data was written
    stream.setVersion(QDataStream::Qt_6_0);

    std::uint64_t transferId = 0;

    // Recover the identifier of the transfer that finished sending
    stream >> transferId;

    // Reject malformed or incomplete payload data
    if(stream.status() != QDataStream::Ok) {
        return false;
    }

    // Update the output object only after decoding succeeds
    complete.transferId = transferId;

    return true;
}


/**
 * makeFileDataMessage()
 * Builds a complete FileData message from one file-data chunk
 */
Protocol::Message Protocol::makeFileDataMessage(const FileDataPayload &fileData) {
    // Encode the chunk metadata and raw file bytes before adding common FileBridge framing
    const QByteArray payload = serializeFileDataPayload(fileData);

    // Aggregate initialization equivalent to:
    // Protocol::Message {
    //     Protocol::MessageHeader {messageType, payloadSize},
    //     payload
    // }
    return {
        {
            MessageType::FileData,
            static_cast<std::uint64_t>(payload.size())
        },
        payload
    };
}


/** 
 * makeFileCompleteMessage()
 * BUilds a complete FileComplete message for a finished transfer
 */
Protocol::Message Protocol::makeFileCompleteMessage(const FileCompletePayload &complete) {
    // Econde the FileComplete-specific transfer identifier into payload bytes
    const QByteArray payload = serializeFileCompletePayload(complete);

    // Aggregate initialization equivalent to:
    // Protocol::Message {
    //      Protocol::MessageHeader {messageType, payloadSize},
    //      payload
    // }
    return {
        {
            MessageType::FileComplete,
            static_cast<std::uint64_t>(payload.size())
        },
        payload
    };
}


/**
 * makeFileOfferMessage()
 * Builds a complete FileOffer message from structured file metadata
 */
Protocol::Message Protocol::makeFileOfferMessage(const FileOfferPayload& offer) {
    // Encode the message-specific meafata before adding the common FileBridge framing
    const QByteArray payload = serializeFileOfferPayload(offer);
    
    // Aggregate initialization equivalent to:
    // Protocol::Message {
    //     Protocol::MessageHeader {messageType, payloadSize},
    //     payload
    // }
    return {
        {
            MessageType::FileOffer,
            static_cast<std::uint64_t>(payload.size())
        },
        payload
    };
}


/**
 * makeFileAcceptMessage()
 * Builds a complete FileAccept message for the specified transfer
 */
Protocol::Message Protocol::makeFileAcceptMessage(const FileAcceptPayload& accept) {
    // Encode the accepted transfer identifier before adding the common FileBridge framing
    const QByteArray payload = serializeFileAcceptPayload(accept);

    // Aggregate initialization equivalent to:
    // Protocol::Message {
    //     Protocol::MessageHeader {messageType, payloadSize},
    //     payload
    // }
    return {
        {
            MessageType::FileAccept,
            static_cast<std::uint64_t>(payload.size())
        },
        payload
    };
}


/**
 * makeFileRejectMessage()
 * Builds a complete FileReject message for the specified transfer
 */
Protocol::Message Protocol::makeFileRejectMessage(const FileRejectPayload& reject) {
    // Encode the rejected transfer identifier before adding the common FileBridge framing
    const QByteArray payload = serializeFileRejectPayload(reject);

    // Aggregate initialization equivalent to:
    // Protocol::Message {
    //     Protocol::MessageHeader {messageType, payloadSize},
    //     payload
    // }
    return {
        {
            MessageType::FileReject,
            static_cast<std::uint64_t>(payload.size())
        },
        payload
    };
}


/**
 * serializeCompleteMessage()
 * Encodes a complete protocol message as its header followed by its payload
 */
QByteArray Protocol::serializeCompleteMessage(const Message& message) {
    // Rebuild the header size from the actual payload so they cannot disagree
    MessageHeader header = message.header;
    header.payloadSize = static_cast<std::uint64_t>(message.payload.size());

    QByteArray data = serializeHeader(header);

    // Append the payload immediately after the fixed-size protocol header
    data.append(message.payload);

    return data;
}
