#include "TransferManager.hpp"
#include "ConnectionManager.hpp"
#include "Protocol.hpp"

#include <QFileInfo>
#include <cstdint>


/**
 * TransferManager()
 * Constructs a transfer manager that uses the provided connection manager
 */
TransferManager::TransferManager(ConnectionManager *connectionManager, QObject *parent) :
    QObject(parent),
    connectionManager_(connectionManager),
    nextTransferId_(1) {        
}


/**
 * offerFile()
 * Creates and sends a new file-transfer offer to a ready peer
 */
bool TransferManager::offerFile(PeerConnection *connection, const QString& filePath) {
    // A transfer requires both a valid connection manager and a target peer
    if(connectionManager_ == nullptr || connection  == nullptr) {
        return false;
    }

    // Read filesystem metadata without leading the file contents into memory
    const QFileInfo fileInfo(filePath);

    // Reject paths that do not refer to a readable regualr file
    if(!fileInfo.exists() || !fileInfo.isFile() || !fileInfo.isReadable()) {
        return false;
    }

    // Allocate an identifier that wwill associate later protocol messages with this file
    const std::uint64_t transferId = generateTransferId();

    // Build the metadata sent to the receiving peer before any file contents are transmitted
    const Protocol::FileOfferPayload offer {
        transferId,
        fileInfo.fileName(),
        static_cast<std::uint64_t>(fileInfo.size())
    };

    // Send the offer before recording local transfer state so failed sends leave no stale entry
    if(!connectionManager_->sendFileOffer(connection, offer)) {
        return false;
    }

    // Remember the local path so an eventual FileAccept can identify which file to transmit
    outgoingTransfers_.emplace(
        transferId,
        OutgoingTransfer {
            transferId,
            filePath
        }
    );

    return true;
}


/**
 * generateTransferId()
 * Returns a new identifier for an outgoing transfer
 */
std::uint64_t TransferManager::generateTransferId() {
    return nextTransferId_++;
}
