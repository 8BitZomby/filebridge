#include "TcpListener.hpp"

#include <QHostAddress>
#include <cstdint>


/**
 * TcpListener()
 * Constructs the listener and connects Qt's new-connection signal to 
 * its handler
 */
TcpListener::TcpListener(QObject *parent) : QObject(parent) {
    // React whenever QTcpServer reports one or more pending connections
    connect(&server_, &QTcpServer::newConnection, this, &TcpListener::handleNewConnection);
}


/**
 * start()
 * Starts listening for incoming connections on an available IPv4 TCP port
 */
bool TcpListener::start() {
    if(server_.isListening()) {
        return true;
    }

    // Port 0 asks the OS to assign an available TCP port
    return server_.listen(QHostAddress::AnyIPv4, 0);
}


/**
 * stop()
 * Stops accepting new incoming TCP connections
 */
void TcpListener::stop() {
    server_.close();
}


/**
 * isListening()
 * Returns whether the listener is currently listening for connections
 */
bool TcpListener::isListening() const {
    return server_.isListening();
}


/**
 * port()
 * Returns the port assigned to the TCP server while it is listening
 */
std::uint16_t TcpListener::port() const {
    return static_cast<std::uint16_t>(server_.serverPort());
}


/**
 * handleNewConnection()
 * Accepts every pending connections and reports each accepted socket
 */
void TcpListener::handleNewConnection() {
    // Multiple peers may be queued before Qt processes the new-connection event
    while(server_.hasPendingConnections()) {
        QTcpSocket *socket = server_.nextPendingConnection();

        // Ignore an invalid result rather than emitting a null socket
        if(socket == nullptr) {
            continue;
        }

        // Notify the rest of FileBridge that a peer connection is ready to use
        emit connectionAccepted(socket);
    }
}
