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
         * Constructor: TransferManager()
         * Constructs a transfer manager that uses the provided connection manager
         */
        explicit TransferManager(ConnectionManager *ConnectionManager, QObject *parent = nullptr);

        /**
         * offerFile()
         * Creates and sends a new file-transfer offer to a ready peer
         */
        bool offerFile(PeerConnection *connection, const QString& filePath);

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
        };

        /**
         * generateTransferId()
         * Returns a new identifier for an outgoing transfer
         */
        std::uint64_t generateTransferId();

        // Connection layer used to send transfer-related protocol messages
        ConnectionManager *connectionManager_;

        // Next monotonically increasing identifier assigned to a local outgoing transfer
        std::uint64_t nextTransferId_;

        // Tracks pending outgoing transfers by their unique transfer identifier
        std::unordered_map<std::uint64_t, OutgoingTransfer> outgoingTransfers_;
};


#endif
