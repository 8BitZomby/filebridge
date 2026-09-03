#ifndef FILEBRIDGE_TRANSFER_MANAGER_HPP
#define FILEBRIDGE_TRANSFER_MANAGER_HPP

#include "ConnectionManager.hpp"
#include "Protocol.hpp"

#include <QFile>
#include <QObject>
#include <QString>

#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>


/**
 * TransferManager
 * Coordinates FileBridge file-transfer state independently of the user interface
 */
class TransferManager : public QObject {
    Q_OBJECT

    public:

        /**
         * IncomingTransfer
         * Stores metadata and receive state for a file offered by a remote peer.
         */
        struct IncomingTransfer {
            // Unique identifier assigned by the sending peer.
            std::uint64_t transferId;

            // Original file name reported by the sender.
            QString fileName;

            // Full local filesystem path where received file data will be written
            QString destinationPath;

            // Total file size in bytes.
            std::uint64_t fileSize;

            // Number of file bytes sucessfully received for this transfer so far
            std::uint64_t bytesReceived;

            // Peer that offered the file.
            PeerConnection *connection;
        };

        /**
         * Constructor: TransferManager()
         * Constructs a transfer manager that uses the provided connection manager
         */
        explicit TransferManager(ConnectionManager *ConnectionManager, QObject *parent = nullptr);

        /**
         * downloadDirectory()
         * Returns the directory where accepted incoming files are saved.
         */
        QString downloadDirectory() const;

        /**
         * setDownloadDirectory()
         * Changes the directory where accepted incoming files are saved.
         */
        void setDownloadDirectory(const QString& directory);
        
        /**
         * offerFile()
         * Creates and sends a new file-transfer offer and returns its transfer ID on success
         */
        std::optional<std::uint64_t> offerFile(PeerConnection *connection, const QString& filePath);

        /**
         * acceptIncomingTransfer()
         * Accepts one pending incoming transfer, using an optional destination directory
         * override
         */
        bool acceptIncomingTransfer(std::uint64_t transferId, const QString& destinationDirectory = QString());

        /**
         * rejectIncomingTransfer()
         * Rejects one pending incoming transfer and notifies the sending peer.
         */
        bool rejectIncomingTransfer(std::uint64_t transferId);
    
    signals:

        /**
         * incomingTransferOffered()
         * Reports a validated incoming file offer to higher-level application code.
         */
        void incomingTransferOffered(const IncomingTransfer& transfer);

        /**
         * outgoingTransferOffered()
         * Reports metadata for a file that was successfully offered to a remote peer.
         */
        void outgoingTransferOffered(std::uint64_t transferId, const QString& fileName, std::uint64_t fileSize);

        /**
         * outgoingTransferAccepted()
         * Reports that a remote peer accepted an outgoing transfer.
         */
        void outgoingTransferAccepted(std::uint64_t transferId);

        /**
         * outgoingTransferRejected()
         * Reports that a remote peer rejected an outgoing transfer.
         */
        void outgoingTransferRejected(std::uint64_t transferId);

        /**
         * outgoingTransferFailed()
         * Reports that an outgoing transfer failed after it had already begun.
         */
        void outgoingTransferFailed(std::uint64_t transferId, const QString& errorMessage);

        /**
         * outgoingTransferCompleted()
         * Reports that an outgoing transfer finished sending all file data
         */
        void outgoingTransferCompleted(std::uint64_t transferId);

        /**
         * incomingTransferCompleted()
         * Reports that an incoming transfer finished receiving all expected file data
         */
        void incomingTransferCompleted(std::uint64_t transferId);

        /**
         * incomingTransferFailed()
         * Reports that an incoming transfer could not continue because of a local failure
         */
        void incomingTransferFailed(std::uint64_t transferId, const QString& errorMessage);

        /**
         * outgoingTransferProgress()
         * Reports updated byte progress for an outgoing file transfer
         */
        void outgoingTransferProgress(std::uint64_t transferId, std::uint64_t bytesSent, std::uint64_t fileSize);

        /**
         * incomingTransferProgress()
         * Reports updated byte progress for an incoming file transfer
         */
        void incomingTransferProgress(std::uint64_t transferId, std::uint64_t bytesReceived, std::uint64_t fileSize);

        /**
         * outgoingTransferSent()
         * Reports that all outgoing file data and the completion message were queued successfully
         */
        void outgoingTransferSent(std::uint64_t transferId);

    private slots:

        /**
         * handleFileOfferReceived()
         * Converts protocol-level file offers into tracked incoming transfers.
         */
        void handleFileOfferReceived(PeerConnection *connection, const Protocol::FileOfferPayload& offer);

        /**
         * handleFileAcceptReceived()
         * Updates outgoing transfer state after the remote peer accepts an offer.
         */
        void handleFileAcceptReceived(PeerConnection *connection, const Protocol::FileAcceptPayload& accept);

        /**
         * handleFileRejectReceived()
         * Updates outgoing transfer state after the remote peer rejects an offer.
         */
        void handleFileRejectReceived(PeerConnection *connection, const Protocol::FileRejectPayload& reject);

        /**
         * handleFileDataReceived()
         * Validates and records one incoming file-data chunk
         */
        void handleFileDataReceived(PeerConnection *connection, const Protocol::FileDataPayload& fileData);

        /**
         * handleFileompleteReceived()
         * Finalizes an incoming transfer after the sender reports completion
         */
        void handleFileCompleteReceived(PeerConnection *connection, const Protocol::FileCompletePayload& complete);

        /**
         * handleFileCompleteAckReceived()
         * Finalizes an outgoing transfer after the receiver confirms successful completion
         */
        void handleFileCompleteAckReceived(PeerConnection *connection, const Protocol::FileCompleteAckPayload& completeAck);

        /**
         * handleErrorReceived()
         * Stops an outgoing transfer after the remote peer reports a transfer failure.
         */
        void handleErrorReceived(PeerConnection *connection, const Protocol::ErrorPayload& error);

        /**
         * handlePeerBytesWritten()
         * Continues outgoing transfers when queued socket bytes are written
         */
        void handlePeerBytesWritten(PeerConnection *connection, qint64 bytes);

        /**
         * handlePeerDisconnected()
         * Removes transfer state associated with a peer that is no longer connected
         */
        void handlePeerDisconnected(PeerConnection *connection);

    private:

        /**
         * OutgoingTransfer
         * Stores local state for a file that has been offered to a remote peer
         */
        struct OutgoingTransfer {
            // Unique identifier used by all protocol messages belonging to this transfer
            std::uint64_t transferId;

            // Full local filesystem path used when the file contents are eventually transmitted
            QString filePath;

            // Peer that accepted and will receive this outgoing transfer
            PeerConnection *connection;

            // Open source file retained while asynchronous chunks are being transmitted
            QFile *file;

            // Total number of file bytes expected to be sent
            std::uint64_t fileSize;

            // Byte offset of the next file chunk that should be sent
            std::uint64_t bytesSent;
        };

        /**
         * sendFileContents()
         * Reads one accepted local file in chunks and sends each chunk to the peer
         */
        bool sendFileContents(OutgoingTransfer& transfer);

        /**
         * failIncomingTransfer()
         * Reports a local incoming-transfer failure to both the remote peer and the GUI
         */
        void failIncomingTransfer(std::uint64_t transferId, Protocol::TransferErrorCode errorCode, const QString& errorMessage);

        /**
         * startNextQueuedOutgoingTransfer()
         * Starts the next accepted outgoing transfer waiting for the specified peer
         */
        void startNextQueuedOutgoingTransfer(PeerConnection *connection);

        /**
         * sendNextOutgoingChunk()
         * Sends the next chunk of an active outgoing transfer and returns control to Qt
         */
        bool sendNextOutgoingChunk(OutgoingTransfer& transfer);

        /**
         * createUniqueDestinationPath()
         * Builds an unused destination path by adding " (n)" before the file extension
         * when needed
         */
        QString createUniqueDestinationPath(const QString& direectory, const QString& fileName) const;

        /**
         * generateTransferId()
         * Returns a new identifier for an outgoing transfer
         */
        std::uint64_t generateTransferId();

        // Connection layer used to send transfer-related protocol messages
        ConnectionManager *connectionManager_;

        // Directory where accepted incoming files are saved by default.
        QString downloadDirectory_;

        // Next monotonically increasing identifier assigned to a local outgoing transfer
        std::uint64_t nextTransferId_;

        // Tracks pending outgoing transfers by their unique transfer identifier
        std::unordered_map<std::uint64_t, OutgoingTransfer> outgoingTransfers_;

        // Tracks the one outgoing transfer currently transmitting for each peer
        std::unordered_map<PeerConnection *, std::uint64_t> activeOutgoingTransfers_;

        // Holds accepted outgoing transfers waiting for the current transfer to finish
        std::unordered_map<PeerConnection *, std::deque<std::uint64_t>> queuedOutgoingTransfers_;

        // Tracks pending incoming transfers by their sender-assigned transfer identifier.
        std::unordered_map<std::uint64_t, IncomingTransfer> incomingTransfers_;
};


#endif
