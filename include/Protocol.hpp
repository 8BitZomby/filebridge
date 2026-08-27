#ifndef FILEBRIDGE_PROTOCOL_HPP
#define FILEBRIDGE_PROTOCOL_HPP

#include <QByteArray>
#include <QString>

#include <cstdint>


// Contains FileBridge's wire-protocol types and serialization helpers
namespace Protocol {

    // Current FileBridge protocol version used to detect incompatible peers
    constexpr std::uint16_t VERSION = 1;

    // Maximum payload size allowed for any single protocol message
    constexpr std::uint64_t MAX_PAYLOAD_SIZE = 64ULL * 1024ULL * 1024ULL;

    // Identifies each FileBridge protocol message with a fixed one-byte value so
    // peers on different platforms interpret the same wire representation
    enum class MessageType : std::uint8_t {
        Invalid = 0,
        Handshake = 1,
        FileOffer = 2,
        FileAccept = 3,
        FileReject = 4,
        FileData = 5,
        FileComplete = 6,
        Error = 7
    };

    // Fixed metadata that precedes every FileBridge protocol payload
    struct MessageHeader {
        // Identifies how the payload could be interpreted
        MessageType type;

        // Number of payload bytes that follow this header
        std::uint64_t payloadSize;
    };

    // Represents one complete FileBridge protocol message after framing is decoded
    struct Message {
        // Fixed metadata describing the message type and payload size
        MessageHeader header;

        // Raw payload bytes belonging to this message
        QByteArray payload;
    };

    // Contains the information exchanged when two FileBridge peers first connect
    struct HandshakePayload {
        // FileBridge wire-protocol version used to verify peero compatibility
        std::uint16_t protocolVersion;

        // FileBridge application version reported by the remote peer
        QString applicationVersion;

        // Human-readable device name shown when identifying the connected peer
        QString deviceName;
    };

    /**
     * serializeHeader()
     * Encodes a message header into its platform-independent wire representation
     */
    QByteArray serializeHeader(const MessageHeader& header);

    /**
     * deserializeHeader()
     * Decodes a complete wire-format header into a MessageHeader
     */
    bool deserializeHeader(const QByteArray& data, MessageHeader& header);

    /**
     * headerSize()
     * Returns the fixed number of bytes used by every serialized message header
     */
    qsizetype headerSize();

    /**
     * isValidHeaderType()
     * Returns whether a raw message type value is defined by the FileBridge protocol
     */
    bool isValidMessageType(std::uint8_t type);

    /**
     * serializeHandshake()
     * Encodes a handshake payload into its binary wire representation
     */
    QByteArray serializeHandshake(const HandshakePayload& handshake);

    /**
     * deserializeHandshake()
     * Decodes a binary handshake payload into structured peer information
     */
    bool deserializeHandshake(const QByteArray& data, HandshakePayload& handshake);

    /**
     * serializeMessage()
     * Encodes a complete protocol message at its header followed by its payload
     */
    QByteArray serializeMessage(const Message& message);
}


#endif
