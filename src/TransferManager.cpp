#include "TransferManager.hpp"
#include "ConnectionManager.hpp"
#include "Protocol.hpp"

#include <QFileInfo>
#include <cstdint>


/**
 * TransferManager()
 * Constructs a transfer manager that uses the provided connection manager.
 */
TransferManager::TransferManager(ConnectionManager *connectionManager, QObject *parent)
    : QObject(parent),
      connectionManager_(connectionManager),
      nextTransferId_(1) {
    
    // Receive validated FileOffer messages from the connection layer.
    if(connectionManager_ != nullptr) {
        QObject::connect(
            connectionManager_,                              // Sender: Manager that receives protocol messages.
            &ConnectionManager::fileOfferReceived,           // Signal: emitted for a valid FileOffer payload.
            this,                                            // Receiver: Transfer manager that owns transfer state.
            &TransferManager::handleFileOfferReceived        // Slot: records the incoming transfer.
        );
    }

    // Receive acceptance responses for outgoing file offers
    QObject::connect(
        connectionManager_,                                 // Sender: ConnectionManager that received the protocol message
        &ConnectionManager::fileAcceptReceived,             // Signal: emmitted when a FileAccept message is received
        this,                                               // Receiver: This TransferManager instance
        &TransferManager::handleFileAcceptReceived          // Slot: (function pointer) -> when signal emits: 
                                                            // handleFileAcceptReceived(connection, accept)
    );

    // Receive rejection resonses for outgoing file offers
    QObject::connect(
        connectionManager_,                                 // Sender: ConnectionManager that received the protocol message
        &ConnectionManager::fileRejectReceived,             // Signal: Emitted when a FileReject message is received
        this,                                               // Receiver: This TransferManager instance
        &TransferManager::handleFileRejectReceived          // Slot: Handles the rejected outgoing transfer
    );
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

    // Report the successfully offered file so the UI can display its metadata.
    emit outgoingTransferOffered(
        transferId,
        fileInfo.fileName(),
        static_cast<std::uint64_t>(fileInfo.size())
    );

    return true;
}


/**
 * acceptIncomingTransfer()
 * Accepts one pending incoming transfer and notifies the sending peer.
 */
bool TransferManager::acceptIncomingTransfer(std::uint64_t transferId) {
    // Locate the incoming transfer that the user wants to accept.
    const auto transfer = incomingTransfers_.find(transferId);

    // Reject unknown transfer identifiers rather than sending an invalid response.
    if(transfer == incomingTransfers_.end()) {
        return false;
    }

    // Build the protocol payload identifying the accepted transfer.
    const Protocol::FileAcceptPayload accept {transferId};

    // Notify the peer that this transfer has been accepted.
    if(!connectionManager_->sendFileAccept(transfer->second.connection, accept)) {
        return false;
    }

    // The transfer is no longer waiting for an accept/reject decision.
    incomingTransfers_.erase(transfer);

    return true;
}


/**
 * rejectIncomingTransfer()
 * Rejects one pending incoming transfer and notifies the sending peer.
 */
bool TransferManager::rejectIncomingTransfer(std::uint64_t transferId) {
    // Locate the incoming transfer that the user wants to reject.
    const auto transfer = incomingTransfers_.find(transferId);

    // Reject unknown transfer identifiers rather than sending an invalid response.
    if(transfer == incomingTransfers_.end()) {
        return false;
    }

    // Build the protocol payload identifying the rejected transfer.
    const Protocol::FileRejectPayload reject {transferId};

    // Notify the peer that this transfer has been rejected.
    if(!connectionManager_->sendFileReject(transfer->second.connection, reject)) {

        return false;
    }

    // A rejected transfer no longer needs to remain pending locally.
    incomingTransfers_.erase(transfer);

    return true;
}


/**
 * handleFileOfferReceived()
 * Converts protocol-level file offers into tracked incoming transfers.
 */
void TransferManager::handleFileOfferReceived(PeerConnection *connection, const Protocol::FileOfferPayload& offer) {
    // Ignore offers that cannot be associated with a valid connected peer.
    if(connection == nullptr) {
        return;
    }

    // Reject duplicate transfer identifiers from the same transfer namespace for now.
    if(incomingTransfers_.contains(offer.transferId)) {
        return;
    }

    // Store the incoming offer so later Accept or Reject actions can reference it by ID.
    const IncomingTransfer transfer {
        offer.transferId,
        offer.fileName,
        offer.fileSize,
        connection
    };

    incomingTransfers_.emplace(
        offer.transferId,
        transfer
    );

    // Notify the GUI that a new pending incoming transfer is available.
    emit incomingTransferOffered(transfer);
}


/**
 * handleFileAcceptReceived()
 * Upddates outgoing transfer state after the remote peer accepts an offer
 */
void TransferManager::handleFileAcceptReceived(PeerConnection *connection, const Protocol::FileAcceptPayload& accept) {
    const auto transfer = outgoingTransfers_.find(accept.transferId);

    // Ignore responses that do not match a known outgoing transfer
    if(transfer == outgoingTransfers_.end()) {
        return;
    }

    // TEMPORARY: Notify compiler that connection is currently unused in this function
    Q_UNUSED(connection);
    // Emit a signal so the GUI can report that the outgoing transfer was accepted
    emit outgoingTransferAccepted(accept.transferId);
}


/**
 * handleFileRejectReceived()
 * Updates outgoing transfer state after the remote peer rejects an offer
 */
void TransferManager::handleFileRejectReceived(PeerConnection *connection, const Protocol::FileRejectPayload& reject) {
    const auto transfer = outgoingTransfers_.find(reject.transferId);

    // Ignore responses that do not match a known outgoing transfer
    if(transfer == outgoingTransfers_.end()) {
        return;
    }

    // TEMPORARY: Notify compiler that connection is currently unused in this function
    Q_UNUSED(connection);
    // Remove the rejected transfer because no file data will be sent for it
    outgoingTransfers_.erase(transfer);
    // Emit a signal to the GUI can report that the outgoing transfer was rejected
    emit outgoingTransferRejected(reject.transferId);
}


/**
 * generateTransferId()
 * Returns a new identifier for an outgoing transfer
 */
std::uint64_t TransferManager::generateTransferId() {
    return nextTransferId_++;
}
