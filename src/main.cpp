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

    // Print detected addresses so the network-interface filtering can be verified.
    const auto addresses = NetworkInfo::localIPv4Addresses();

    for(const LocalNetworkAddress& address : addresses) {
        qDebug() << address.interfaceName << address.address.toString();
    }

    TcpListener listener;

    if(!listener.start()) {
        qDebug() << "Failed to start TCP listener";
        return 1;
    }

    qDebug() << "Listening on port: " << listener.port();

    QWidget window;
    window.setWindowTitle("FileBridge");
    window.resize(800, 500);
    window.show();

    return app.exec();
}
