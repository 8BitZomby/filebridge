#include "NetworkInfo.hpp"
#include "PeerConnection.hpp"
#include "TcpListener.hpp"

#include <QApplication>
#include <QDebug>
#include <QTcpSocket>
#include <QWidget>

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

    // Log the remote address and port whenever a peer connections is accepted
    QObject::connect(
        &listener,                          // Sender: Object that emits a signal
        &TcpListener::connectionAccepted,   // Signal: emitted after a socket is accepted
        [&app](QTcpSocket *socket) {        // Lambda: handler captures app and receives the accepted socket
        
            qDebug() << "Accepted connection from:"
                     << socket->peerAddress().toString()
                     << socket->peerPort();

            // Parent the peer connection to the application so it remains alive while connected
            PeerConnection *connection =
                new PeerConnection(
                    socket,
                    &app
            );

            // Print raw incoming bytes until the FileBridge protocol layer is implemented
            QObject::connect(
                    connection,                     // Sender: Object that emits the signal
                    &PeerConnection::dataReceived,  // Signal: emitted when peer data arrives
                    [](const QByteArray& data) {    // Lambda: Handler that receives the emitted byte array

                        qDebug() << "Received:" << data;
            });

            // Destroy the peer wrapper once the remote side disconnects
            QObject::connect(
                connection,                     // Sender: Object that emits the signal
                &PeerConnection::disconnected,  // Signal: emitted when the peer disconnects
                connection,                     // Receiver: Object that should receive the slot call
                &QObject::deleteLater           // Lambda: Slot that safely schedules the object for deletion
            );
    });

    // Create the temp FileBridge window while networking is developed
    QWidget window;
    window.setWindowTitle("FileBridge");
    window.resize(800, 500);
    window.show();

    return app.exec();
}
