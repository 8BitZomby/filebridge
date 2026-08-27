#include "NetworkInfo.hpp"
#include "PeerConnection.hpp"
#include "PeerConnector.hpp"
#include "Protocol.hpp"
#include "TcpListener.hpp"

#include <QApplication>
#include <QDebug>
#include <QHostAddress>
#include <QSysInfo>
#include <QTcpSocket>
#include <QWidget>
#include <cstdint>

/**
 * main()
 * Starts FileBridge and prints the usable local IPv4 addresses detected on the system.
 */
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Retrieve all usable local IPv4 addresses for this device
    const auto addresses = NetworkInfo::localIPv4Addresses();

    // Print each usable inferface and address so local network detection can be verified
    for(const LocalNetworkAddress& address : addresses) {
        qDebug() << address.interfaceName << address.address.toString();
    }

    // Own the TCP server that accepts incoming FileBridge connections
    TcpListener listener;

    // Stop startup if FileBridge cannot bind an available listening port
    if(!listener.start()) {
        qDebug() << "Failed to start TCP listener";
        return 1;
    }

    // Report the port selected by the OS for incoming connections
    qDebug() << "Listening on port: " << listener.port();

    // Configure an already-established TCP socket as a complete FileBridge peer connection.
    auto configurePeerConnection = [&app](QTcpSocket *socket) {
        qDebug() << "Connected peer:"
                 << socket->peerAddress().toString()
                 << socket->peerPort();

        // Wrap the connected socket so PeerConnection owns communication and socket lifetime.
        PeerConnection *connection = new PeerConnection(socket, &app);

        // Build the initial handshake used to identify this FileBridge peer.
        Protocol::HandshakePayload handshake {
            Protocol::VERSION,
            "0.1.0",
            QSysInfo::machineHostName()
        };

        // Convert the structured handshake fields into their binary payload representation.
        const QByteArray handshakePayload = Protocol::serializeHandshake(handshake);

        // Wrap the handshake payload in a complete framed FileBridge protocol message.
        Protocol::Message handshakeMessage {
            {
                Protocol::MessageType::Handshake,
                static_cast<std::uint64_t>(handshakePayload.size())
            },
            handshakePayload
        };

        // Send our handshake immediately after the peer connection is established.
        if(!connection->sendMessage(handshakeMessage)) {
            qDebug() << "Failed to send handshake";
        }

        // Handle complete protocol messages received from the connected peer.
        QObject::connect(
            connection,                              // Object that emits the signal.
            &PeerConnection::messageReceived,        // Signal emitted after a full message is decoded.
            [](const Protocol::Message& message) {   // Handler receiving the decoded protocol message.

                // Process the message according to its protocol type.
                switch(message.header.type) {
                    case Protocol::MessageType::Handshake: {
                        Protocol::HandshakePayload handshake;

                        // Decode the structured peer information carried by the handshake.
                        if(!Protocol::deserializeHandshake(message.payload, handshake)) {
                            qDebug() << "Invalid handshake payload";
                            return;
                        }

                        qDebug() << "Handshake received";
                        qDebug() << "Protocol version:" << handshake.protocolVersion;
                        qDebug() << "Application version:" << handshake.applicationVersion;
                        qDebug() << "Device name:" << handshake.deviceName;

                        break;
                    }

                    case Protocol::MessageType::FileOffer:
                    case Protocol::MessageType::FileAccept:
                    case Protocol::MessageType::FileReject:
                    case Protocol::MessageType::FileData:
                    case Protocol::MessageType::FileComplete:
                    case Protocol::MessageType::Error:
                        // These message types will be handled as their protocol layers are added.
                        qDebug() << "Received unhandled message type:"
                                 << static_cast<std::uint8_t>(message.header.type);
                        break;

                    case Protocol::MessageType::Invalid:
                        // Invalid types should already have been rejected while parsing the header.
                        qDebug() << "Received invalid protocol message";
                        break;
                }
            }
        );

        // Destroy the peer wrapper after the remote peer disconnects.
        QObject::connect(
            connection,                         // Object that emits the signal.
            &PeerConnection::disconnected,      // Signal emitted when the peer disconnects.
            connection,                         // Object that receives the slot call.
            &QObject::deleteLater               // Slot that safely schedules the wrapper for deletion.
        );
    };

    // Configure every socket accepted by the local TCP listener.
    QObject::connect(
        &listener,                              // Object that emits the signal.
        &TcpListener::connectionAccepted,       // Signal emitted when an incoming connection is accepted.
        configurePeerConnection                 // Handler shared by all established peer connections.
    );

    // Creates outgoing TCP connections to other FileBridge peers.
    PeerConnector connector;

    // Configure successfully established outgoing sockets exactly like accepted sockets.
    QObject::connect(
        &connector,                             // Object that emits the signal.
        &PeerConnector::connectionEstablished, // Signal emitted after the outgoing TCP connection succeeds.
        configurePeerConnection                 // Shared handler for established peer sockets.
    );

    // Report outgoing connection failures while manual connection handling is being developed.
    QObject::connect(
        &connector,                             // Object that emits the signal.
        &PeerConnector::connectionFailed,      // Signal emitted when the outgoing connection attempt fails.
        [](const QString& errorMessage) {       // Handler receiving Qt's human-readable socket error.
            qDebug() << "Connection failed:" << errorMessage;
        }
    );

    // Temporarily connect to this FileBridge instance to test both connection directions.
    connector.connectToPeer(QHostAddress::LocalHost, listener.port());

    // Create the temp FileBridge window while networking is developed
    QWidget window;
    window.setWindowTitle("FileBridge");
    window.resize(800, 500);
    window.show();

    return app.exec();
}
