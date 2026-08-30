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

    // Remember the local file and peer so later transfer reponses can be matched correctly
    outgoingTransfers_.emplace(
        transferId,
        OutgoingTransfer {
            transferId,
            filePath,
            connection
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
 * Starts sending file contents after the remote peer accepts an outgoing transfer.
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

    // Begin sending the accepted file as a sequence of FileData messages.
    if(!sendFileContents(transfer->second)) {
        qDebug() << "Failed to send accepted file contents";
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
        qDebug() << "Failed to open destination file:"
                 << destinationFile.errorString();
        return;
    }

    // Position the file cursor at the exact byte offset represented by this chunk.
    if(!destinationFile.seek(static_cast<qint64>(fileData.offset))) {
        qDebug() << "Failed to seek destination file:"
                 << destinationFile.errorString();
        return;
    }

    // Write the complete chunk to disk.
    const qint64 bytesWritten = destinationFile.write(fileData.data);

    if(bytesWritten != fileData.data.size()) {
        qDebug() << "Failed to write complete chunk:"
                 << "expected =" << fileData.data.size()
                 << "written =" << bytesWritten
                 << "error =" << destinationFile.errorString();
        return;
    }

    // Record the exact number of file bytes successfully written so far.
    transfer->second.bytesReceived += chunkSize;
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

    // Notify the GUI that the incoming file was completely received and written
    emit incomingTransferCompleted(complete.transferId);

    // The completed incoming transfer no longer needs to remain tracked
    incomingTransfers_.erase(transfer);
}


/**
 * sendFileContents()
 * Reads one accepted local file in chunks and sends each chunk to the peer.
 */
bool TransferManager::sendFileContents(const OutgoingTransfer& transfer) {
    // Open the original local file for read-only binary access.
    QFile file(transfer.filePath);

    if(!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open source file:" << file.errorString();
        return false;
    }

    // Keep individual protocol messages reasonably small instead of loading the entire file at once.
    constexpr qint64 CHUNK_SIZE = 64 * 1024;

    std::uint64_t offset = 0;

    // Read and send the file sequentially until no bytes remain.
    while(!file.atEnd()) {
        const QByteArray chunk = file.read(CHUNK_SIZE);

        // An unexpected empty chunk indicates that reading failed before EOF.
        if(chunk.isEmpty()) {
            if(file.atEnd()) {
                break;
            }

            qDebug() << "Failed while reading source file:" << file.errorString();
            return false;
        }

        // Identify both the transfer and exact byte position represented by this chunk.
        const Protocol::FileDataPayload fileData {
            transfer.transferId,
            offset,
            chunk
        };

        // Stop immediately if the network layer cannot queue this chunk for transmission.
        if(!connectionManager_->sendFileData(transfer.connection, fileData)) {
            qDebug() << "Failed to send FileData chunk";
            return false;
        }

        // Advance the offset by the exact number of raw file bytes just sent.
        offset += static_cast<std::uint64_t>(chunk.size());
    }
    // Tell the receiver that no more FileData messages will be sent for this transfer
    const Protocol::FileCompletePayload complete { transfer.transferId };

    if(!connectionManager_->sendFileComplete(transfer.connection, complete)) {
        return false;
    }

    // Report that all file data and the completion message were successfully queued
    emit outgoingTransferCompleted(transfer.transferId);

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
