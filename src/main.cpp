#include "ConnectionManager.hpp"
#include "NetworkInfo.hpp"

#include <QApplication>
#include <QDebug>
#include <QHostAddress>
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

    // Own all incoming and outgoing FileBridge peer connections
    ConnectionManager connectionManager;

    // Stop startup if FileBridge cannot open an available listening port
    if(!connectionManager.start()) {
        qDebug() << "Failed to start connection manager";
        return 1;
    }

    // Report the listening endpoint while the GUI controls are not yet implemented
    qDebug() << "Listening on port:" << connectionManager.listeningPort();

    // Temporarilty connect FileBridge to itself to verify both connection directions
    connectionManager.connectToPeer(
            QHostAddress::LocalHost,
            connectionManager.listeningPort()
    );

    // Create the temp FileBridge window while networking is developed
    QWidget window;
    window.setWindowTitle("FileBridge");
    window.resize(800, 500);
    window.show();

    return app.exec();
}
