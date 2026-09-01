#include "ConnectionManager.hpp"
#include "Protocol.hpp"
#include "Version.hpp"

#include <QDebug>
#include <QSysInfo>

#include <algorithm>
#include <cstdint>


/**
 * Constructor: ConnectionManager()
 * Connects listener and connector events to the shared peer-connection setup path
 */
ConnectionManager::ConnectionManager(QObject *parent) : QObject(parent) {
    // Configure incoming sockets accepted by the local TCP listener
    QObject::connect(
        &listener_,                             // Object that emits the signal
        &TcpListener::connectionAccepted,       // Signal emitted after an incoming socket is accepted
        this,                                   // Object that receives the slot call
        [this](QTcpSocket *socket) {            // Slot that configures the accepted socket
            handleEstablishedSocket(
                socket,                         // socket: Connected TCP socket accepted by the local listener
                ConnectionDirection::Incoming   // direction: Remote FileBridge initiated this connection
            );
        }
    );

    // Configure outgoing sockets after a connection attempt succeeds
    QObject::connect(
        &connector_,                                // Object that emits the signal
        &PeerConnector::connectionEstablished,      // Signal emitted after and outgoing connection succeeds
        this,                                       // Object that receives the slot call
        [this](QTcpSocket *socket) {                // Shared slot for established sockets
            handleEstablishedSocket(
                socket,                             // socket: Connected TCP socket created by the outgoing connector
                ConnectionDirection::Outgoing       // direction: This FileBridge instance initiated the connection
            );
        }
    );

    // Forward outgoing connection failures to higher-level FileBridge code
    QObject::connect(
        &connector_,                            // Object that emits the signal
        &PeerConnector::connectionFailed,       // Signal emitted when an outgoing attempt fails
        this,                                   // Object that re-emits the failure
        &ConnectionManager::connectionFailed    // Signal exposed by ConnectionManager
    );
}


/**
 * start()
 * Starts the local TCP listener on an available port
 */
bool ConnectionManager::start() {
    return listener_.start();
}


/**
 * connectToPeer()
 * Starts an outgoing connection attempt to another FileBridge peer
 */
void ConnectionManager::connectToPeer(const QHostAddress& address, std::uint16_t port) {
    connector_.connectToPeer(address, port);
}


/**
 * approveConnection()
 * Approves a pending incoming FileBridge connection.
 */
bool ConnectionManager::approveConnection(PeerConnection *connection) {
    // std::find_if() returns the matching managed peer, or connections_.end()
    // when this connection is not currently tracked by ConnectionManager.
    auto peer = std::find_if(
        connections_.begin(),
        connections_.end(),
        [connection](const ManagedPeer& managedPeer) {
            return managedPeer.connection == connection;
        }
    );

    // Approval is valid only for an incoming peer that completed its handshake
    // and has not already been approved.
    if(peer == connections_.end() ||
        peer->direction != ConnectionDirection::Incoming ||
        !peer->handshakeReceived ||
        peer->approved) {
        
            return false;
    }

    const Protocol::Message message = Protocol::makeConnectionAcceptMessage();

    // Do not mark the peer approved unless the acceptance message was successfully queued.
    if(!connection->sendMessage(message)) {
        return false;
    }

    peer->approved = true;

    // Both handshake and local user approval are now complete.
    emit peerReady(connection);

    return true;
}


/**
 * rejectConnection()
 * Rejects a pending incoming FileBridge connection.
 */
bool ConnectionManager::rejectConnection(PeerConnection *connection) {
    // Search for the pending incoming peer represented by this connection pointer.
    const auto peer = std::find_if(
        connections_.begin(),
        connections_.end(),
        [connection](const ManagedPeer& managedPeer) {

            return managedPeer.connection == connection;
        }
    );

    if(peer == connections_.end() ||
        peer->direction != ConnectionDirection::Incoming ||
        !peer->handshakeReceived ||
        peer->approved) {
        
            return false;
    }

    const Protocol::Message message = Protocol::makeConnectionRejectMessage();

    // Queue the explicit rejection before closing the TCP connection.
    if(!connection->sendMessage(message)) {
        return false;
    }

    connection->disconnectFromPeer();

    return true;
}


/**
 * disconnectPeer()
 * Disconnects an established peer currently managed by FileBridge
 */
bool ConnectionManager::disconnectPeer(PeerConnection *connection) {
    // Reject null pointers before searching the managed connection collection
    if(connection == nullptr) {
        return false;
    }

    const auto peer = std::find_if(
        connections_.begin(),
        connections_.end(),
        [connection](const ManagedPeer& managedPeer) {

            return managedPeer.connection == connection;
        }
    );

    // Only connections currently owned by ConnectionManager may be disconnected here
    if(peer == connections_.end()) {
        return false;
    }

    // Ask PeerConnection to close its underlying TCP connection
    // The existing disconnected signal will perform normal cleanup afterward
    connection->disconnectFromPeer();

    return true;
}


/**
 * sendFileOffer()
 * Sends metadata for one proposed file transfer to a ready peer
 */
bool ConnectionManager::sendFileOffer(PeerConnection *connection, const Protocol::FileOfferPayload& offer) {
    // Locate the managed state for the peer that should receive the offer
    const auto peer = std::find_if(
        connections_.begin(),
        connections_.end(),
        [connection](const ManagedPeer& managedPeer) {

            return managedPeer.connection == connection;
        }
    );

    // File-transfer messages may only be sent to peers that complete the handshake
    if(peer == connections_.end() || !peer->handshakeReceived || !peer->approved) {
        return false;
    }

    // Build the framed FileOffer message from the provided transfer metadata
    const Protocol::Message message = Protocol::makeFileOfferMessage(offer);

    return connection->sendMessage(message);
}


/**
 * sendFileAccept()
 * Sends acceptance for one pending incoming file transfer
 */
bool ConnectionManager::sendFileAccept(PeerConnection *connection, const Protocol::FileAcceptPayload& accept) {
    // Locate the managed state for the peer that should receive the response
    const auto peer = std::find_if(
        connections_.begin(),
        connections_.end(),
        [connection](const ManagedPeer& managedPeer) {

            return managedPeer.connection == connection;
        }
    );

    // Transfer responses may only be sent to validated FileBridge peers
    if(peer == connections_.end() || !peer->handshakeReceived || !peer->approved) {
        return false;
    }

    // Build the complete FileAccept protocol message from the transfer identifier
    const Protocol::Message message = Protocol::makeFileAcceptMessage(accept);

    return connection->sendMessage(message);
}

/**
 * sendFileReject()
 * Sends rejection for one pending incoming file transfer
 */
bool ConnectionManager::sendFileReject(PeerConnection *connection, const Protocol::FileRejectPayload& reject) {
    // Locate the managed state for the peer that should receive the response
    const auto peer = std::find_if(
        connections_.begin(),
        connections_.end(),
        [connection](const ManagedPeer& managedPeer) {

            return managedPeer.connection == connection;
        }
    );

    // Transfer responses may only be sent to validated FileBridge peers
    if(peer == connections_.end() || !peer->handshakeReceived || !peer->approved) {
        return false;
    }

    // Build the complete FileAccept protocol message from the transfer identifier
    const Protocol::Message message = Protocol::makeFileRejectMessage(reject);

    return connection->sendMessage(message);
}


/**
 * sendFileData()
 * Sends one chunk of file data to a validated peer
 */
bool ConnectionManager::sendFileData(PeerConnection *connection, const Protocol::FileDataPayload& fileData) {
    // Locate the managed state for the peer that should receive the file chunk
    const auto peer = std::find_if(
        connections_.begin(),
        connections_.end(),
        [connection](const ManagedPeer& managedPeer) {

            return managedPeer.connection == connection;
        }
    );

    // File data may only be sent to peers that completed the handshake
    if(peer == connections_.end() || !peer->handshakeReceived || !peer->approved) {
        return false;
    }

    // Build the complete FileData protocol message from the provided chunk
    const Protocol::Message message = Protocol::makeFileDataMessage(fileData);

    return connection->sendMessage(message);
}


/**
 * sendFileComplete()
 * Sends completion for one finished outgoing file transfer
 */
bool ConnectionManager::sendFileComplete(PeerConnection *connection, const Protocol::FileCompletePayload& complete) {
    // Locate the managed state for the peer that should receive the completeion message
    const auto peer = std::find_if(
        connections_.begin(),
        connections_.end(),
        [connection](const ManagedPeer& managedPeer) {

            return managedPeer.connection == connection;
        }
    );

    // File-transfer messages may only be sent to validated FileBridge peers
    if(peer == connections_.end() || !peer->handshakeReceived || !peer->approved) {
        return false;
    }

    // Build the complete FileComplete protocol message for the finished transfer
    const Protocol::Message message = Protocol::makeFileCompleteMessage(complete);

    return connection->sendMessage(message);
}


/**
 * listeningPort()
 * Returns the TCP port currently used for incoming peer connections
 */
std::uint16_t ConnectionManager::listeningPort() const {
    return listener_.port();
}


/**
 * handleEstablishedSocket()
 * Configures an established TCP socket and records whether it is incoming or outgoing
 */
void ConnectionManager::handleEstablishedSocket(QTcpSocket *socket, ConnectionDirection direction) {
    // Ignore invalid socket pointers rather than creating an unusable peer connection
    if(socket == nullptr) {
        return;
    }

    // Create one managed wrapper for the established TCP connection
    PeerConnection *connection = new PeerConnection(socket, this);

    // Track the peer immediately, but do not consider it ready until its handshake is validated
    connections_.push_back(
        ManagedPeer {
            connection, // connection: PeerConnection that owns this established TCP socket
            direction,  // direction: Records which side initiated the TCP connection
            false,      // handshakeReceived: No remote handshake has been validated yet
            false,      // approved: Connection is not usable until approval completes
            QString()   // deviceName: Filled when the remote handshake is decoded
        }
    );

    // Process complete protocol messages received from this specific peer
    QObject::connect(
        connection,                         // Object that emits the signal
        &PeerConnection::messageReceived,   // Signal emitted after a complete protocol message is decoded
        this,                               // Object that owns the message-handling logic
        [this, connection](const Protocol::Message& message) {

            handleMessage(connection, message);
        }
    );

    // Remove and destroy the peer wrapper when its socket disconnects
    QObject::connect(
        connection,                             // Object that emits the signal
        &PeerConnection::disconnected,          // Signal emitted when the peer disconnects
        this,                                   // Object that handles connection bookkeeping
        &ConnectionManager::handlePeerDisconnected
    );

    // Send our protocol and identity information immediately after the connection setup
    if(!sendHandshake(connection)) {
        qDebug() << "Failed to send handshake";
    }
}


/**
 * handlePeerDisconnected()
 * Removes and destroys the peer connection whose socket has disconnected
 */
void ConnectionManager::handlePeerDisconnected() {
    // sender() identifies which PeerConnection emitted the disconnected signal
    PeerConnection *connection = qobject_cast<PeerConnection *>(sender());

    if(connection == nullptr) {
        return;
    }

    // Remove the manager's record for this peer before destroying the QObject
    connections_.erase(
        std::remove_if(
            connections_.begin(),
            connections_.end(),
            [connection](const ManagedPeer& peer) {

                return peer.connection == connection;
            }
        ),
        connections_.end()
    );

    emit peerDisconnected(connection);
    
    // Defer QObject destruction until Qt has finished processing the current signal
    connection->deleteLater();
}


/**
 * sendHandshake()
 * Sends FileBridge protocol, application, and device information to the peer
 */
bool ConnectionManager::sendHandshake(PeerConnection *connection) {
    Protocol::HandshakePayload handshake {
        Protocol::VERSION,
        FILEBRIDGE_VERSION,
        QSysInfo::machineHostName()
    };

    // Encode the structured handshake fields as protocol payload
    const QByteArray payload = Protocol::serializeHandshakePayload(handshake);

    // Wrap the payload in a framed protocol message
    const Protocol::Message message {
        {
            Protocol::MessageType::Handshake,
                static_cast<std::uint64_t>(payload.size())
        },
        payload
    };

    return connection->sendMessage(message);
}


/**
 * handleMessage()
 * Processes a complete protocol message received from a tracked peer
 */
void ConnectionManager::handleMessage(PeerConnection *connection, const Protocol::Message& message) {
    // Locate the connection state associated with the peer that sent this message
    const auto peer = std::find_if(
        connections_.begin(),
        connections_.end(),
        [connection](const ManagedPeer& managedPeer) {

            return managedPeer.connection == connection;
        }
    );

    // Ignore messages from connections that are no longer managed
    if(peer == connections_.end()) {
        return;
    }

    // No transfer-level messages are valid until the peer completes its handshake
    if(message.header.type != Protocol::MessageType::Handshake && !peer->handshakeReceived) {
        qDebug() << "Received protocol message before handshake completed";
        connection->disconnectFromPeer();
        return;
    }

    // File-transfer messages are forbidden until connection approval completes
    const bool isTransferMessage =
        message.header.type == Protocol::MessageType::FileOffer ||
        message.header.type == Protocol::MessageType::FileAccept ||
        message.header.type == Protocol::MessageType::FileReject ||
        message.header.type == Protocol::MessageType::FileData ||
        message.header.type == Protocol::MessageType::FileComplete;

    if(isTransferMessage && !peer->approved) {
        qDebug() << "Received transfer message before connection approval";
        connection->disconnectFromPeer();
        return;
    }

    switch(message.header.type) {
        case Protocol::MessageType::Handshake: {
            // A peer should perform the FileBridge handshake only once per connection
            if(peer->handshakeReceived) {
                qDebug() << "Received duplicate handshake";
                connection->disconnectFromPeer();
                return;
            }

            Protocol::HandshakePayload handshake;

            // Reject malformed handshake payloads before using peer-probided information
            if(!Protocol::deserializeHandshakePayload(message.payload, handshake)) {
                qDebug() << "Invalid handshake payload";
                connection->disconnectFromPeer();
                return;
            }

            // Protocol versions must match before either peer can safely exchange messages
            if(handshake.protocolVersion != Protocol::VERSION) {
                qDebug() << "Unsupported protocol version" << handshake.protocolVersion;
                connection->disconnectFromPeer();
                return;
            }

            // The connection is now a validated FileBridge peer and may exchange transfer messages
            peer->handshakeReceived = true;
            peer->deviceName = handshake.deviceName;

            if(peer->direction == ConnectionDirection::Incoming) {
                // Incoming peers require an explicit decision from the local user
                emit connectionApprovalRequested(
                        connection,
                        peer->deviceName
                );
            }

            // Outgoing peers remain pending until the remote user sends
            // ConnectionAccept or ConnectionReject
            break;
        }

        case Protocol::MessageType::ConnectionAccept: {
            // Approval messages deliberately contain no payload.
            if(!message.payload.isEmpty()) {
                qDebug() << "ConnectionAccept unexpectedly contained payload data";
                connection->disconnectFromPeer();
                return;
            }

            // Only the initiator of an outgoing connection may receive approval.
            if(peer->direction != ConnectionDirection::Outgoing || peer->approved) {
                qDebug() << "Unexpected ConnectionAccept message";
                connection->disconnectFromPeer();
                return;
            }
            peer->approved = true;
            emit peerReady(connection);
            break;
        }

        case Protocol::MessageType::ConnectionReject: {
            // Rejection messages deliberately contain no payload.
            if(!message.payload.isEmpty()) {
                qDebug() << "ConnectionReject unexpectedly contained payload data";
                connection->disconnectFromPeer();
                return;
            }

            // Only an outgoing connection request can be rejected by the remote user.
            if(peer->direction != ConnectionDirection::Outgoing || peer->approved) {
                qDebug() << "Unexpected ConnectionReject message";
                connection->disconnectFromPeer();
                return;
            }

            emit connectionRejected(connection);

            // Rejected requests are no longer useful, so close the TCP connection.
            connection->disconnectFromPeer();

            break;
        }

        case Protocol::MessageType::FileOffer: {
            Protocol::FileOfferPayload offer;
            // Reject malformed transfer metadata rather than exposing it to higher-level code
            if(!Protocol::deserializeFileOfferPayload(message.payload, offer)) {
                qDebug() << "Invalid FileOffer payload";
                connection->disconnectFromPeer();
                return;
            }
            emit fileOfferReceived(connection, offer);
            break;
        }

        case Protocol::MessageType::FileAccept: {
            Protocol::FileAcceptPayload accept;
            // Reject malformed acceptance messages before exposing them to transfer
            if(!Protocol::deserializeFileAcceptPayload(message.payload, accept)) {
                qDebug() << "Invalid FileAccept payload";
                connection->disconnectFromPeer();
                return;
            }
            emit fileAcceptReceived(connection, accept);
            break;
        }

        case Protocol::MessageType::FileReject: {
            Protocol::FileRejectPayload reject;
            // Reject malformed rejection messages before exposing them to transfer logic
            if(!Protocol::deserializeFileRejectPayload(message.payload, reject)) {
                qDebug() << "Invalid FileReject payload";
                connection->disconnectFromPeer();
                return;
            }
            emit fileRejectReceived(connection, reject);
            break;
        }

        case Protocol::MessageType::FileData: {
            Protocol::FileDataPayload fileData;
            // Reject malformed file-data payloads before exposing them to transfer logic
            if(!Protocol::deserializeFileDataPayload(message.payload, fileData)) {
                qDebug() << "Invalid FileData payload";
                connection->disconnectFromPeer();
                return;
            }
            emit fileDataReceived(connection, fileData);
            break;
        }

        case Protocol::MessageType::FileComplete: {
            Protocol::FileCompletePayload complete;
            // Reject malformed completion messages before exposing them to transfer logic
            if(!Protocol::deserializeFileCompletePayload(message.payload, complete)) {  
                qDebug() << "Invalid FileComplete payload";
                connection->disconnectFromPeer();
                return;
            }
            emit fileCompleteReceived(connection, complete);
            break;
        }

        case Protocol::MessageType::Error: {
            // Transfer-level handling will be added as each protocol message is implemented
            qDebug() << "Received unhandled message type:"
                     << static_cast<std::uint8_t>(message.header.type);
            break;
        }

        case Protocol::MessageType::Invalid:
            // Invalid message types should already be rejected by the framing parser
            qDebug() << "Received invalid protocol message";
            break;
    }
}
