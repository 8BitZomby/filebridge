#include "NetworkInfo.hpp"
#include "TcpListener.hpp"

#include <QApplication>
#include <QDebug>
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
    QObject::connect(&listener, &TcpListener::connectionAccepted,
            [](QTcpSocket *socket) {
        qDebug() << "Accepted connection from:"
                 << socket->peerAddress().toString()
                 << socket->peerPort();
    });

    // Create the temp FileBridge window while networking is developed
    QWidget window;
    window.setWindowTitle("FileBridge");
    window.resize(800, 500);
    window.show();

    return app.exec();
}
