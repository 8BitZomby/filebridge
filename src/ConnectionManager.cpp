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
        &ConnectionManager::handleEstablishedSocket // Slot that configures the accepted socket
    );

    // Configure outgoing sockets after a connection attempt succeeds
    QObject::connect(
        &connector_,                                // Object that emits the signal
        &PeerConnector::connectionEstablished,      // Signal emitted after and outgoing connection succeeds
        this,                                       // Object that receives the slot call
        &ConnectionManager::handleEstablishedSocket // Shared slot for established sockets
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
 * listeningPort()
 * Returns the TCP port currently used for incoming peer connections
 */
std::uint16_t ConnectionManager::listeningPort() const {
    return listener_.port();
}


/**
 * handleEstablishedSocket()
 * Wraps and established TCP socket in PeerConnection and begins the handshake
 */
void ConnectionManager::handleEstablishedSocket(QTcpSocket *socket) {
    // Ignore invalid socket pointers rather than creating an unusable peer connection
    if(socket == nullptr) {
        return;
    }

    // Create one managed wrapper for the established TCP connection
    PeerConnection *connection = new PeerConnection(socket, this);
    
    // Track the peer immediately, but do not consider it ready until its handshake is validated
    connections_.push_back({connection, false});

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

            qDebug() << "Handshake received";
            qDebug() << "Protocol version:" << handshake.protocolVersion;
            qDebug() << "Application version:" << handshake.applicationVersion;
            qDebug() << "Device name:" << handshake.deviceName;

            emit peerReady(connection);
            break;
        }

        case Protocol::MessageType::FileOffer:
        case Protocol::MessageType::FileAccept:
        case Protocol::MessageType::FileReject:
        case Protocol::MessageType::FileData:
        case Protocol::MessageType::FileComplete:
        case Protocol::MessageType::Error:
            // Transfer-level handling will be added as each protocol message is implemented
            qDebug() << "Received unhandled message type:"
                     << static_cast<std::uint8_t>(message.header.type);
            break;
        case Protocol::MessageType::Invalid:
            // Invalid message types should already be rejected by the framing parser
            qDebug() << "Received invalid protocol message";
            break;
    }
}
