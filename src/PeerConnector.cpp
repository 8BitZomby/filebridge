#include "PeerConnector.hpp"
#include <cstdint>


/**
 * PeerConnector()
 * Constructs an outgoing peer connector with no active connection attempt
 */
PeerConnector::PeerConnector(QObject *parent) : QObject(parent), socket_(nullptr) {}


/**
 * connectToPeer()
 * Starts an asyncronous TCP connection attempt to the given address and port
 */
void PeerConnector::connectToPeer(const QHostAddress& address, std::uint16_t port) {
    // Ignore a new request while another connection attempt is already active
    if(isConnecting()) {
        return;
    }

    // Create a temporary socket owned by the connector during connection setup
    socket_ = new QTcpSocket(this);

    // Handle successful completion of the asynchronous connection attempt
    QObject::connect(
        socket_,                            // Socket that emits the signal
        &QTcpSocket::connected,             // Signal emitted once the TCP connection succeeds
        this,                               // Object that receives the slot call
        &PeerConnector::handleConnected     // Slot that hands off the connected socket
    );

    // Handle connection failures reported by the socket
    QObject::connect(
        socket_,                            // Socket that emits the signal
        &QTcpSocket::errorOccurred,         // Signal emitted when a socket error occurred
        this,                               // Object that receives the slot call
        &PeerConnector::handleError         // Slot that reports and cleans up the failure
    );

    // Begin the non-blocking connection attempt
    socket_->connectToHost(address, port);
}


/**
 * isConnecting()
 * Returns whether an outgoing connection attempt is currently in progress
 */
bool PeerConnector::isConnecting() const {
    return socket_ != nullptr;
}


/**
 * handleConnected()
 * Hands ownership of the successfully connected socket to higher-level code
 */
void PeerConnector::handleConnected() {
    if(socket_ == nullptr) {
        return;
    }

    // Detach the socket from the connector so PeerConnection can assume ownership
    socket_->setParent(nullptr);

    QTcpSocket *connectedSocket = socket_;
    socket_ = nullptr;

    // Report the established socket after releasing connector ownership
    emit connectionEstablished(connectedSocket);
}


/**
 * handleError()
 * Reports the current socket error and cleans up the failed connection attempt
 */
void PeerConnector::handleError(QAbstractSocket::SocketError socketError) {
    Q_UNUSED(socketError);

    if(socket_ == nullptr) {
        return;
    }

    // Preserve the human-readable error before the temporary socket is destroyed
    const QString errorMessage = socket_->errorString();

    socket_->deleteLater();
    socket_ = nullptr;

    emit connectionFailed(errorMessage);
}
