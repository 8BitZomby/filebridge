#include "ConnectionManager.hpp"
#include "NetworkInfo.hpp"

#include <QApplication>
#include <QDebug>
#include <QHostAddress>
#include <QString>
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

    // Own all incoming and outgoing FileBridge peer connections
    ConnectionManager connectionManager;

    // Stop startup if FileBridge cannot open an available listening port
    if(!connectionManager.start()) {
        qDebug() << "Failed to start connection manager";
        return 1;
    }

    // Report the listening endpoint while the GUI controls are not yet implemented
    qDebug() << "Listening on port:" << connectionManager.listeningPort();

    // Connect to a remote peer when an IP address and port are provided on the command line
    if(argc == 3) {
        const QHostAddress remoteAddress(QString::fromLocal8Bit(argv[1]));

        bool validPort = false;
        const unsigned int parsedPort = QString::fromLocal8Bit(argv[2]).toUInt(&validPort);

        // Reject invalid connection arguments before attempting a network connection
        if(remoteAddress.isNull() || !validPort || parsedPort > 65535) {
            qDebug() << "Usage: fb [ip port]";
            return 1;
        }

        connectionManager.connectToPeer(
                remoteAddress,
                static_cast<std::uint16_t>(parsedPort)
        );
    }
    else if(argc != 1) {
        qDebug() << "Usage: fb [ip port]";
        return 1;
    }

    // Create the temp FileBridge window while networking is developed
    QWidget window;
    window.setWindowTitle("FileBridge");
    window.resize(800, 500);
    window.show();

    return app.exec();
}
