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

    /**
     * MessageType
     * Identifies each FileBridge protocol message with a fixed one-byte value so
     * peers on different platforms interpret the same wire representation
     */
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

    /**
     * MessageHeader
     * Fixed metadata that precedes every FileBridge protocol payload
     */
    struct MessageHeader {
        // Identifies how the payload could be interpreted
        MessageType type;

        // Number of payload bytes that follow this header
        std::uint64_t payloadSize;
    };

    /**
     * Message
     * Represents one complete FileBridge protocol message after framing is decoded
     */
    struct Message {
        // Fixed metadata describing the message type and payload size
        MessageHeader header;

        // Raw payload bytes belonging to this message
        QByteArray payload;
    };

    /**
     * HandshakePayload
     * Contains the information exchanged when two FileBridge peers first connect
     */
    struct HandshakePayload {
        // FileBridge wire-protocol version used to verify peero compatibility
        std::uint16_t protocolVersion;

        // FileBridge application version reported by the remote peer
        QString applicationVersion;

        // Human-readable device name shown when identifying the connected peer
        QString deviceName;
    };

    /**
     * FileOfferPayload
     * Describes one file that a peer wants to transfer
     */
    struct FileOfferPayload {
        // Unique identifier used to associate later transfer messages with this file
        std::uint64_t transferId;

        // Original file name presented to the receiving used
        QString fileName;

        // Total file size in bytes
        std::uint64_t fileSize;
    };

    /**
     * FileAcceptPayload
     * Identifies the offered file that the receiver has accepted
     */
    struct FileAcceptPayload {
        // Identifies the transfer that may now begin sending file data
        std::uint64_t transferId;
    };

    /**
     * FileRejectPayload
     * Identifies the offered file that the receiver has declined
     */
    struct FileRejectPayload {
        // Identifies the transfer that will not proceed
        std::uint64_t transferId;
    };

    /**
     * FileDataPayload
     * Carries one chunk of file contents for an accepted transfer
     */
    struct FileDataPayload {
        // Identifies which accepted transfer this chunk belongs to
        std::uint64_t transferId;

        // Zero-based byte offset where this chunk belongs in the file
        std::uint64_t offset;

        // Raw file bytes contained in this chunk
        QByteArray data;
    };

    /**
     * FileCompletePayload
     * Identifies a file transfer whose data has been fully sent
     */
    struct FileCompletePayload {
        // Identifies the transfer that has finished sending file data
        std::uint64_t transferId;
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
     * serializeHandshakePayload()
     * Encodes a handshake payload into its binary wire representation
     */
    QByteArray serializeHandshakePayload(const HandshakePayload& handshake);

    /**
     * deserializeHandshakePayload()
     * Decodes a binary handshake payload into structured peer information
     */
    bool deserializeHandshakePayload(const QByteArray& data, HandshakePayload& handshake);

    /**
     * serializeFileOfferPayload()
     * Serializes file-offer metadata into a protocol payload
     */
    QByteArray serializeFileOfferPayload(const FileOfferPayload& offer);

    /**
     * deserializeFileOfferpayload()
     * Deserializes file-offer metadata from a protocol payload
     */
    bool deserializeFileOfferPayload(const QByteArray& data, FileOfferPayload& offer);

    /**
     * serializeFileAcceptPayload()
     * Serializes an accepted transfer identifier into a protocol payload
     */
    QByteArray serializeFileAcceptPayload(const FileAcceptPayload& accept);

    /**
     * deserializeFileAcceptPayload()
     * Deserializes an accepted transfer identifier from a protocol payload
     */
    bool deserializeFileAcceptPayload(const QByteArray& data, FileAcceptPayload& accept);

    /**
     * serializeFileRejectPayload()
     * Serializes a rejected transfer identifier into a protocol payload
     */
    QByteArray serializeFileRejectPayload(const FileRejectPayload& reject);

    /**
     * deserializeFileRejectPayload()
     * Deserializes a rejected transfer identifier from a protocol payload
     */
    bool deserializeFileRejectPayload(const QByteArray& data, FileRejectPayload& reject);

    /**
     * serializeFileDataPayload()
     * Encodes one file-data chunk into the payload bytes of a FileData message
     */
    QByteArray serializeFileDataPayload(const FileDataPayload& fileData);

    /**
     * deserializeFileDataPayload()
     * Decodes the payload bytes of a FileData message into one file-data chunk
     */
    bool deserializeFileDataPayload(const QByteArray& data, FileDataPayload& fileData);

    /**
     * serializeFileCompletePayload()
     * Encodes a completed transfer identifier into the payload bytes of a 
     * FileComplete message
     */
    QByteArray serializeFileCompletePayload(const FileCompletePayload& complete);

    /**
     * deserializeFileCompletePayload()
     * Decodes the payload bytes of a FileComplete message into its transfer
     * identifier
     */
    bool deserializeFileCompletePayload(const QByteArray& data, FileCompletePayload& complete);

    /**
     * makeFileOfferMessage()
     * Builds a complete FileOffer message from structured file metadata
     */
    Message makeFileOfferMessage(const FileOfferPayload& offer);

    /**
     * makeFileAcceptMessage()
     * Builds a complete FileAccept message for the specified transfer
     */
    Message makeFileAcceptMessage(const FileAcceptPayload& accept);

    /**
     * makeFileRejectMessage()
     * Builds a complete FileReject message for the specified transfer
     */
    Message makeFileRejectMessage(const FileRejectPayload& reject);

    /**
     * makeFileDataMessage()
     * Builds a complete FileData message from one file-data chunk
     */
    Message makeFileDataMessage(const FileDataPayload& fileData);

    /**
     * makeFileCompleteMessage()
     * Builds a complete FileComplete message for a finished transfer
     */
    Message makeFileCompleteMessage(const FileCompletePayload& complete);

    /**
     * serializeMessage()
     * Encodes a complete FileBridge message as its header followed by its payload bytes
     */
    QByteArray serializeCompleteMessage(const Message& message);
}


#endif
