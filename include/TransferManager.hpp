#ifndef FILEBRIDGE_TRANSFER_MANAGER_HPP
#define FILEBRIDGE_TRANSFER_MANAGER_HPP

#include "ConnectionManager.hpp"

#include <QObject>
#include <QString>

#include <cstdint>
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
         * Creates and sends a new file-transfer offer to a ready peer
         */
        bool offerFile(PeerConnection *connection, const QString& filePath);

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
        };

        /**
         * sendFileContents()
         * Reads one accepted local file in chunks and sends each chunk to the peer
         */
        bool sendFileContents(const OutgoingTransfer& transfer);

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

        // Tracks pending incoming transfers by their sender-assigned transfer identifier.
        std::unordered_map<std::uint64_t, IncomingTransfer> incomingTransfers_;
};


#endif
