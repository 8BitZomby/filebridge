#include "TransferManager.hpp"

#include "ConnectionManager.hpp"
#include "Protocol.hpp"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <cstdint>


/**
 * TransferManager()
 * Constructs a transfer manager that uses the provided connection manager.
 */
TransferManager::TransferManager(ConnectionManager *connectionManager, QObject *parent)
    : QObject(parent),
      connectionManager_(connectionManager),
      downloadDirectory_(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)),
      nextTransferId_(1) {
    
    // Receive validated FileOffer messages from the connection layer.
    if(connectionManager_ != nullptr) {
        QObject::connect(
            connectionManager_,                              // Sender: Manager that receives protocol messages.
            &ConnectionManager::fileOfferReceived,           // Signal: emitted for a valid FileOffer payload.
            this,                                            // Receiver: Transfer manager that owns transfer state.
            &TransferManager::handleFileOfferReceived        // Slot: records the incoming transfer.
        );

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

        // Receive file-data chunks for accepted incoming transfers
        QObject::connect(
            connectionManager_,                                 // Sender: ConnectionManager that received the protocol message
            &ConnectionManager::fileDataReceived,               // Signal: Emitted when a valid FileData message is received
            this,                                               // Receiver: This TransferManager instance
            &TransferManager::handleFileDataReceived            // Slot: Validates and records the incoming chunk
        );

        // Received completion messages for incoming file transfers
        QObject::connect(
            connectionManager_,                                 // Sender: ConnectionManager that received the protocol message
            &ConnectionManager::fileCompleteReceived,           // Signal: Emitted when a valid FileComplete message is received
            this,                                               // Receiver: This TransferManager instance
            &TransferManager::handleFileCompleteReceived        // Slot: Verifies and finalizes the incoming transfer
        );

        // Receive confirmation that the remote peer fully finalized one outgoing transfer.
        QObject::connect(
            connectionManager_,                                  // Sender: ConnectionManager that decoded the acknowledgement.
            &ConnectionManager::fileCompleteAckReceived,          // Signal: Emitted for a valid FileCompleteAck message.
            this,                                                 // Receiver: This TransferManager instance.
            &TransferManager::handleFileCompleteAckReceived       // Slot: Finalizes the matching outgoing transfer.
        );

        // Receive transfer-specific failures reported by the remote peer.
        QObject::connect(
            connectionManager_,                      // Sender: ConnectionManager that decoded the Error message.
            &ConnectionManager::errorReceived,       // Signal: Emitted for a valid transfer-specific Error payload.
            this,                                    // Receiver: This TransferManager instance.
            &TransferManager::handleErrorReceived    // Slot: Stops and reports the matching outgoing transfer.
        );

        // Continue asynchronous outgoing transfers as the peer socket drains queued bytes.
        QObject::connect(
            connectionManager_,                             // Sender: ConnectionManager forwarding peer socket write progress.
            &ConnectionManager::peerBytesWritten,           // Signal: Emitted when queued bytes leave one peer's socket buffer.
            this,                                           // Receiver: This TransferManager instance.
            &TransferManager::handlePeerBytesWritten        // Slot: Sends the next chunk for that peer when appropriate.
        );

        // Remove transfer state before ConnectionManager destroys a disconnected peer.
        QObject::connect(
            connectionManager_,                                 // Sender: ConnectionManager that owns peer connections.
            &ConnectionManager::peerDisconnected,               // Signal: Emitted when a managed peer connection ends.
            this,                                               // Receiver: This TransferManager instance.
            &TransferManager::handlePeerDisconnected            // Slot: Removes transfers associated with that peer.
        );
    }
}


/**
 * downloadDirectory()
 * Returns the directory where accepted incoming files are saved.
 */
QString TransferManager::downloadDirectory() const {
    return downloadDirectory_;
}


/**
 * setDownloadDirectory()
 * Changes the directory where accepted incoming files are saved.
 */
void TransferManager::setDownloadDirectory(const QString& directory) {
    downloadDirectory_ = directory;
}


/**
 * offerFile()
 * Creates and sends a new file-transfer offer and returns its transfer ID on success
 */
std::optional<std::uint64_t> TransferManager::offerFile(PeerConnection *connection, const QString& filePath) {
    // A transfer requires both a valid connection manager and a target peer.
    if(connectionManager_ == nullptr || connection == nullptr) {
        return std::nullopt;
    }

    // Read filesystem metadata without loading the file contents into memory.
    const QFileInfo fileInfo(filePath);

    // Reject paths that do not refer to a readable regular file.
    if(!fileInfo.exists() || !fileInfo.isFile() || !fileInfo.isReadable()) {
        return std::nullopt;
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
        return std::nullopt;
    }

    // Remember the local file and peer so later transfer responses can be matched correctly.
    // The source file itself is opened only after the remote peer accepts the offer.
    outgoingTransfers_.emplace(
        transferId,
        OutgoingTransfer {
            transferId,                                     // transferId: Protocol identifier for this transfer.
            filePath,                                       // filePath: Full local path of the source file.
            connection,                                     // connection: Peer that received the offer.
            nullptr,                                        // file: Opened later when transmission actually begins.
            static_cast<std::uint64_t>(fileInfo.size()),    // fileSize: Total source-file size in bytes.
            0                                               // bytesSent: No file data has been sent yet.
        }
    );

    // Report the successfully offered file so the UI can display its metadata.
    emit outgoingTransferOffered(
        transferId,
        fileInfo.fileName(),
        static_cast<std::uint64_t>(fileInfo.size())
    );

    // Return the exact identifier assigned to this successfully created transfer
    return std::optional<std::uint64_t>(transferId);
}


/**
 * acceptIncomingTransfer()
 * Accepts one pending incoming transfer, using an optional destination directory override.
 */
bool TransferManager::acceptIncomingTransfer(
    std::uint64_t transferId,
    const QString& destinationDirectory
) {
    // Locate the incoming transfer that the user wants to accept.
    const auto transfer = incomingTransfers_.find(transferId);

    // Reject unknown transfer identifiers rather than sending an invalid response.
    if(transfer == incomingTransfers_.end()) {
        return false;
    }

    // Use the provided directory when one was specified; otherwise use FileBridge's configured default.
    const QString targetDirectory =
        destinationDirectory.isEmpty()
            ? downloadDirectory_
            : destinationDirectory;

    // A transfer cannot be accepted if no usable destination directory is available.
    if(targetDirectory.isEmpty()) {
        return false;
    }

    // Build the complete destination path from the chosen directory and original filename.
    transfer->second.destinationPath = createUniqueDestinationPath(targetDirectory, transfer->second.fileName);

    // Build the protocol payload identifying the accepted transfer.
    const Protocol::FileAcceptPayload accept {
        transferId
    };

    // Notify the sender only after the destination path has been established.
    if(!connectionManager_->sendFileAccept(
        transfer->second.connection,
        accept
    )) {
        // Clear the destination because the transfer was not successfully accepted.
        transfer->second.destinationPath.clear();
        return false;
    }

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
        QString(),
        offer.fileSize,
        0,
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
 * Starts or queues an outgoing transfer after the remote peer accepts it.
 */
void TransferManager::handleFileAcceptReceived(PeerConnection *connection, const Protocol::FileAcceptPayload& accept) {
    // Locate the outgoing transfer identified by the acceptance message.
    const auto transfer = outgoingTransfers_.find(accept.transferId);

    // Reject responses that do not correspond to a known outgoing transfer.
    if(transfer == outgoingTransfers_.end()) {
        qDebug() << "FileAccept rejected: outgoing transfer was not found";
        return;
    }

    // Reject an acceptance response that came from a different peer than the original offer.
    if(transfer->second.connection != connection) {
        qDebug() << "FileAccept rejected: response came from unexpected peer";
        return;
    }

    // Notify the GUI that the remote peer accepted the offered file.
    emit outgoingTransferAccepted(accept.transferId);

    // Only one file may actively transmit to a peer at a time. Accepted files
    // beyond the active one wait in arrival order until that transfer finishes.
    if(activeOutgoingTransfers_.contains(connection)) {
        queuedOutgoingTransfers_[connection].push_back(accept.transferId);
        return;
    }

    // Record this transfer as the peer's active sender before transmission starts.
    activeOutgoingTransfers_[connection] = accept.transferId;

    if(!sendFileContents(transfer->second)) {
        qDebug() << "Failed to send accepted file contents";
        activeOutgoingTransfers_.erase(connection);
        return;
    }
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
 * failIncomingTransfer()
 * Reports a local incoming-transfer failure to both the remote peer and the GUI.
 */
void TransferManager::failIncomingTransfer(
    std::uint64_t transferId,
    Protocol::TransferErrorCode errorCode,
    const QString& errorMessage
) {
    const auto transfer = incomingTransfers_.find(transferId);

    if(transfer == incomingTransfers_.end()) {
        return;
    }

    PeerConnection *connection = transfer->second.connection;

    const Protocol::ErrorPayload error {
        transferId,
        errorCode,
        "Transfer failed on remote device"
    };

    // Tell the sender to stop transmitting this transfer as soon as possible.
    if(!connectionManager_->sendError(connection, error)) {
        qDebug() << "Failed to send transfer Error message";
    }

    // Report the same failure locally before discarding TransferManager state.
    emit incomingTransferFailed(transferId, errorMessage);

    incomingTransfers_.erase(transfer);
}


/**
 * handleFileDataReceived()
 * Validates and writes one incoming file-data chunk to its destination file.
 */
void TransferManager::handleFileDataReceived(
    PeerConnection *connection,
    const Protocol::FileDataPayload& fileData
) {
    // Locate the incoming transfer identified by this FileData message.
    const auto transfer = incomingTransfers_.find(fileData.transferId);

    if(transfer == incomingTransfers_.end()) {
        qDebug() << "FileData rejected: unknown transfer ID" << fileData.transferId;
        return;
    }

    // Reject data that came from a different peer than the one that offered the file.
    if(transfer->second.connection != connection) {
        qDebug() << "FileData rejected: unexpected peer";
        return;
    }

    // Require chunks to arrive exactly where the previous chunk ended.
    if(fileData.offset != transfer->second.bytesReceived) {
        qDebug() << "FileData rejected: unexpected offset"
                 << "expected =" << transfer->second.bytesReceived
                 << "received =" << fileData.offset;
        return;
    }

    // Determine how many raw file bytes are contained in this chunk.
    const std::uint64_t chunkSize =
        static_cast<std::uint64_t>(fileData.data.size());

    // Reject a chunk that would exceed the file size declared in the original offer.
    if(chunkSize > transfer->second.fileSize - transfer->second.bytesReceived) {
        qDebug() << "FileData rejected: chunk exceeds offered file size";
        return;
    }

    // File data cannot be written until a destination path has been established.
    if(transfer->second.destinationPath.isEmpty()) {
        qDebug() << "FileData rejected: destination path is empty";
        return;
    }

    // Open the destination file without discarding chunks that were already written.
    QFile destinationFile(transfer->second.destinationPath);

    if(!destinationFile.open(QIODevice::ReadWrite)) {
        const QString errorMessage =
            "Failed to open destination file: " +
            destinationFile.errorString();

        qDebug() << errorMessage;

        failIncomingTransfer(
            fileData.transferId,
            Protocol::TransferErrorCode::FileOpenFailed,
            errorMessage
        );
        return;
    }

    if(!destinationFile.seek(static_cast<qint64>(fileData.offset))) {
        const QString errorMessage =
            "Failed to seek destination file: " +
            destinationFile.errorString();

        qDebug() << errorMessage;

        failIncomingTransfer(
            fileData.transferId,
            Protocol::TransferErrorCode::FileSeekFailed,
            errorMessage
        );
        return;
    }

    // Write the complete chunk to disk.
    const qint64 bytesWritten = destinationFile.write(fileData.data);

    if(bytesWritten != fileData.data.size()) {
        const QString errorMessage =
            "Failed to write destination file: " +
            destinationFile.errorString();

        qDebug() << "Failed to write complete chunk:"
                 << "expected =" << fileData.data.size()
                 << "written =" << bytesWritten
                 << "error =" << destinationFile.errorString();

        failIncomingTransfer(
            fileData.transferId,
            Protocol::TransferErrorCode::FileWriteFailed,
            errorMessage
        );
        return;
    }

    // Record the exact number of file bytes successfully written so far.
    transfer->second.bytesReceived += chunkSize;

    // Report progress only after the chunk has been written successfully
    emit incomingTransferProgress(
        transfer->second.transferId,
        transfer->second.bytesReceived,
        transfer->second.fileSize
    );
}


/**
 * handleFileCompleteReceived()
 * Finalizes an incoming transfer after the sender reports completion
 */
void TransferManager::handleFileCompleteReceived(PeerConnection *connection, const Protocol::FileCompletePayload& complete) {
    // Locate the incoming transfer identified by the completion message
    const auto transfer = incomingTransfers_.find(complete.transferId);

    // Ignore completion messages that do not match a known incoming transfer
    if(transfer == incomingTransfers_.end()) {
        return;
    }
    
    // Reject completion messages that came from a different peer than the original offer
    if(transfer->second.connection != connection) {
        return;
    }

    // A transfer is only complete when every byte declared in the original offer was received
    if(transfer->second.bytesReceived != transfer->second.fileSize) {
        return;
    }

    // Confirm successful receipt only after the full offered byte count has been written.
    const Protocol::FileCompleteAckPayload completeAck {
        complete.transferId
    };

    if(!connectionManager_->sendFileCompleteAck(connection, completeAck)) {
        qDebug() << "Failed to send FileCompleteAck";
        return;
    }

    // Notify the local GUI that the incoming file was completely received and written.
    emit incomingTransferCompleted(complete.transferId);

    // The completed incoming transfer no longer needs to remain tracked.
    incomingTransfers_.erase(transfer);
}


/**
 * handleFileCompleteAckReceived()
 * Finalizes an outgoing transfer and starts the next queued transfer for that peer.
 */
void TransferManager::handleFileCompleteAckReceived(
    PeerConnection *connection,
    const Protocol::FileCompleteAckPayload& completeAck
) {
    const auto transfer = outgoingTransfers_.find(completeAck.transferId);

    // Ignore acknowledgements that do not match a known outgoing transfer.
    if(transfer == outgoingTransfers_.end()) {
        return;
    }

    // Reject acknowledgements that came from a different peer than the original transfer.
    if(transfer->second.connection != connection) {
        return;
    }

    // Only the transfer currently assigned to this peer may be finalized.
    const auto activeTransfer = activeOutgoingTransfers_.find(connection);

    if(
        activeTransfer == activeOutgoingTransfers_.end() ||
        activeTransfer->second != completeAck.transferId
    ) {
        return;
    }

    // The receiver has now confirmed that the file was fully received and finalized.
    emit outgoingTransferCompleted(completeAck.transferId);

    outgoingTransfers_.erase(transfer);
    activeOutgoingTransfers_.erase(activeTransfer);

    startNextQueuedOutgoingTransfer(connection);
}


/**
 * handleErrorReceived()
 * Stops an outgoing transfer after the remote peer reports a transfer failure.
 */
void TransferManager::handleErrorReceived(
    PeerConnection *connection,
    const Protocol::ErrorPayload& error
) {
    const auto transfer = outgoingTransfers_.find(error.transferId);

    // Ignore errors that do not correspond to a transfer currently owned locally.
    if(transfer == outgoingTransfers_.end()) {
        return;
    }

    // A peer may only fail a transfer that was originally offered to that same peer.
    if(transfer->second.connection != connection) {
        return;
    }

    // Stop reading additional source data immediately if this transfer was active.
    if(transfer->second.file != nullptr) {
        transfer->second.file->close();
        delete transfer->second.file;
        transfer->second.file = nullptr;
    }

    // Release this peer's active slot when the failed transfer was the active sender.
    const auto activeTransfer = activeOutgoingTransfers_.find(connection);
    if(
        activeTransfer != activeOutgoingTransfers_.end() &&
        activeTransfer->second == error.transferId
    ) {
        activeOutgoingTransfers_.erase(activeTransfer);
    }

    // Preserve the remote diagnostic message for the GUI before discarding transfer state.
    emit outgoingTransferFailed(error.transferId, error.message);

    outgoingTransfers_.erase(transfer);

    // A failed active transfer must not block later accepted files for this peer.
    startNextQueuedOutgoingTransfer(connection);
}


/**
 * handlePeerBytesWritten()
 * Continues the active outgoing transfer after the peer socket finishes draining queued bytes.
 */
void TransferManager::handlePeerBytesWritten(PeerConnection *connection, qint64 bytes) {
    Q_UNUSED(bytes);

    if(connection == nullptr || connection->socket() == nullptr) {
        return;
    }

    // bytesWritten may be emitted several times for one queued message. Wait until
    // Qt's write buffer is completely empty before queueing the next file chunk.
    if(connection->socket()->bytesToWrite() > 0) {
        return;
    }

    // Only the transfer explicitly assigned as active for this peer may continue.
    const auto activeTransfer = activeOutgoingTransfers_.find(connection);
    if(activeTransfer == activeOutgoingTransfers_.end()) {
        return;
    }

    const auto transfer = outgoingTransfers_.find(activeTransfer->second);
    if(transfer == outgoingTransfers_.end()) {
        qDebug() << "Active outgoing transfer was not found";
        activeOutgoingTransfers_.erase(activeTransfer);
        return;
    }

    // A transfer with no open source file has already queued all of its data and
    // is waiting for the receiver's FileCompleteAck.
    if(transfer->second.file == nullptr) {
        return;
    }

    if(!sendNextOutgoingChunk(transfer->second)) {
        qDebug() << "Failed to continue outgoing file transfer";

        transfer->second.file->close();
        delete transfer->second.file;
        transfer->second.file = nullptr;
    }
}


/**
 * handlePeerDisconnected()
 * Fails and removes transfer state associated with a peer that is no longer connected.
 */
void TransferManager::handlePeerDisconnected(PeerConnection *connection) {
    // The disconnected peer can no longer have an active or queued sender schedule.
    activeOutgoingTransfers_.erase(connection);
    queuedOutgoingTransfers_.erase(connection);

    // Every unresolved outgoing transfer for this peer has been interrupted and
    // must become Failed rather than silently disappearing from TransferManager.
    for(auto transfer = outgoingTransfers_.begin(); transfer != outgoingTransfers_.end();) {
        if(transfer->second.connection != connection) {
            ++transfer;
            continue;
        }

        // Release any source file that was still open for asynchronous sending.
        if(transfer->second.file != nullptr) {
            transfer->second.file->close();
            delete transfer->second.file;
            transfer->second.file = nullptr;
        }

        const std::uint64_t transferId = transfer->second.transferId;

        // Notify the GUI before removing the transfer so its persistent history
        // row records that the connection interruption caused the failure.
        emit outgoingTransferFailed(
            transferId,
            "Peer disconnected"
        );

        transfer = outgoingTransfers_.erase(transfer);
    }

    // Incoming transfers are also unresolved once their sending peer disappears.
    for(auto transfer = incomingTransfers_.begin(); transfer != incomingTransfers_.end();) {
        if(transfer->second.connection != connection) {
            ++transfer;
            continue;
        }

        const std::uint64_t transferId = transfer->second.transferId;

        // Preserve the interrupted transfer as Failed in the receiver's history.
        emit incomingTransferFailed(
            transferId,
            "Peer disconnected"
        );

        transfer = incomingTransfers_.erase(transfer);
    }
}


/**
 * sendFileContents()
 * Opens an accepted source file and starts its asynchronous chunk transmission
 */
bool TransferManager::sendFileContents(OutgoingTransfer& transfer) {
    // A transfer should never have more than one source file open at the same time.
    if(transfer.file != nullptr) {
        return false;
    }

    transfer.file = new QFile(transfer.filePath);

    if(!transfer.file->open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open source file:"
                 << transfer.file->errorString();

        delete transfer.file;
        transfer.file = nullptr;

        return false;
    }

    // Start this transfer from the beginning of the source file.
    transfer.bytesSent = 0;

    // Queue only the first chunk here. Future chunks are triggered after Qt
    // reports that previously queued socket bytes have been written.
    if(!sendNextOutgoingChunk(transfer)) {
        transfer.file->close();
        delete transfer.file;
        transfer.file = nullptr;

        return false;
    }

    return true;
}


/**
 * startNextQueuedOutgoingTransfer()
 * Starts the next accepted outgoing transfer waiting for the specified peer.
 */
void TransferManager::startNextQueuedOutgoingTransfer(PeerConnection *connection) {
    if(connection == nullptr || activeOutgoingTransfers_.contains(connection)) {
        return;
    }

    const auto queue = queuedOutgoingTransfers_.find(connection);

    if(queue == queuedOutgoingTransfers_.end() || queue->second.empty()) {
        return;
    }

    // Accepted transfers begin in the same FIFO order in which they were queued.
    const std::uint64_t nextTransferId = queue->second.front();
    queue->second.pop_front();

    if(queue->second.empty()) {
        queuedOutgoingTransfers_.erase(queue);
    }

    const auto nextTransfer = outgoingTransfers_.find(nextTransferId);

    if(nextTransfer == outgoingTransfers_.end()) {
        qDebug() << "Queued outgoing transfer was not found";
        return;
    }

    // Claim the active slot before transmission begins so only one file
    // can send to this peer at a time.
    activeOutgoingTransfers_[connection] = nextTransferId;

    if(!sendFileContents(nextTransfer->second)) {
        qDebug() << "Failed to send next queued file";
        activeOutgoingTransfers_.erase(connection);
    }
}


/**
 * sendNextOutgoingChunk()
 * Sends the next chunk of an active outgoing transfer and returns control to Qt
 */
bool TransferManager::sendNextOutgoingChunk(OutgoingTransfer& transfer) {
    if(transfer.file == nullptr || !transfer.file->isOpen()) {
        return false;
    }

    // Keep individual protocol messages reasonably small so Qt can return to
    // the event loop between chunks and repaint sender-side transfer progress.
    constexpr qint64 CHUNK_SIZE = 64 * 1024;

    // Reaching EOF means all file data has been queued and only FileComplete remains.
    if(transfer.file->atEnd()) {
        const Protocol::FileCompletePayload complete {
            transfer.transferId
        };

        if(!connectionManager_->sendFileComplete(transfer.connection, complete)) {
            return false;
        }

        transfer.file->close();
        delete transfer.file;
        transfer.file = nullptr;

        // The receiver still needs to acknowledge successful finalization.
        emit outgoingTransferSent(transfer.transferId);
        return true;
    }

    const QByteArray chunk = transfer.file->read(CHUNK_SIZE);

    if(chunk.isEmpty()) {
        qDebug() << "Failed while reading source file:"
                 << transfer.file->errorString();
        return false;
    }

    const Protocol::FileDataPayload fileData {
        transfer.transferId,
        transfer.bytesSent,
        chunk
    };

    if(!connectionManager_->sendFileData(transfer.connection, fileData)) {
        qDebug() << "Failed to send FileData chunk";
        return false;
    }

    // Advance sender progress only after the complete chunk was queued successfully.
    transfer.bytesSent += static_cast<std::uint64_t>(chunk.size());

    emit outgoingTransferProgress(
        transfer.transferId,
        transfer.bytesSent,
        transfer.fileSize
    );

    return true;
}


/**
 * createUniqueDestinationPath()
 * Builds an unused destination path by adding " (n)" before the file extension
 * when needed
 */
QString TransferManager::createUniqueDestinationPath(const QString& directory, const QString& fileName) const {
    const QDir destinationDirectory(directory);

    // Use the original filename when no file with that name already exists
    const QString originalPath = destinationDirectory.filePath(fileName);

    if(!QFileInfo::exists(originalPath)) {
        return originalPath;
    }

    const QFileInfo fileInfo(fileName);

    // Preserve the complete extension while adding the numeric suffix to the base name
    const QString baseName = fileInfo.completeBaseName();
    const QString suffix = fileInfo.completeSuffix();
    std::uint64_t copyNumber = 1;

    // Increment the suffix until an unused filename is found
    while(true) {
        QString candidateName = baseName + " (" + QString::number(copyNumber) + ")";

        // Add the original extension only when the file actually has one
        if(!suffix.isEmpty()) {
            candidateName += "." + suffix;
        }

        const QString candidatePath = destinationDirectory.filePath(candidateName);

        if(!QFileInfo::exists(candidatePath)) {
            return candidatePath;
        }
        ++copyNumber;
    }
}


/**
 * generateTransferId()
 * Returns a new identifier for an outgoing transfer
 */
std::uint64_t TransferManager::generateTransferId() {
    return nextTransferId_++;
}
