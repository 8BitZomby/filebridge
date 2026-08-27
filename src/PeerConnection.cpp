#include "PeerConnection.hpp"


/**
 * PeerConnection()
 * Constructs a peer connection around an already-connected TCP socket
 */
PeerConnection::PeerConnection(QTcpSocket *socket, QObject *parent) : QObject(parent), socket_(socket) {
    // Make this object responsible for the accepted socket's lifetime
    socket_->setParent(this);

    // Read incoming data whenever Qt reports that bytes are available
    connect(socket_, &QTcpSocket::readyRead, this, &PeerConnection::handleReadyRead);

    // React when the remote peer closes the connection
    connect(socket_, &QTcpSocket::readyRead, this, &PeerConnection::handleDisconnected);
}


/**
 * socket()
 * Returns the underlying TCP socket for this peer connection
 */
QTcpSocket* PeerConnection::socket() const {
    return socket_;
}


/**
 * handleReadyRead()
 * Reads all bytes currently available from the connected peer
 */
void PeerConnection::handleReadyRead() {
    // TCP is a byte stream, so this may contain any amount of currently
    // available data
    const QByteArray data = socket_->readAll();

    if(data.isEmpty()) {
        return;
    }

    // Pass the received bytes upward without interpreting the protocol yet
    emit dataReceived(data);
}


/**
 * handleDisconnected()
 * Reposts that the remote peer has disconnected
 */
void PeerConnection::handleDisconnected() {
    emit disconnected();
}
