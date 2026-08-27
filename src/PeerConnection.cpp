#include "PeerConnection.hpp"
#include "Protocol.hpp"

#include <QDebug>
#include <cstdint>


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
 * Buffers incoming TCP bytes and processes complete framed protocol messages
 */
void PeerConnection::handleReadyRead() {
    // TCP may deliver only part of a protocol message, so keep all unread bytes
    receiveBuffer_.append(socket_->readAll());

    // Process every complete message currently available in the received buffer
    while(receiveBuffer_.size() >= Protocol::headerSize()) {
    
        // Copy only the fixed-sized header bytes, leaving any payload bytes in the buffer
        const QByteArray headerData = receiveBuffer_.left(Protocol::headerSize());

        Protocol::MessageHeader header;

        // Reject malformed or undefined protocol headers
        if(!Protocol::deserializeHeader(headerData, header)) {
            qDebug() << "Invalid protocol header";
            receiveBuffer_.clear();
            return;
        }

        // Reject unreasonable payload sizes before converting or buffering further
        if(header.payloadSize > Protocol::MAX_PAYLOAD_SIZE) {
            qDebug() << "Protocol payload exceeds maximum allowed size";
            receiveBuffer_.clear();
            return;
        }

        // Calculate the total number of bytes required for this complete message
        const qsizetype messageSize = Protocol::headerSize() + static_cast<qsizetype>(header.payloadSize);

        // Wait for more TCP data if the complete payload has not arrived yet
        if(receiveBuffer_.size() < messageSize) {
            return;
        }

        // Extract only the payload bytes that belong to this protocol message
        const QByteArray payload = 
            receiveBuffer_.mid(
                Protocol::headerSize(),
                static_cast<qsizetype>(header.payloadSize)
        );

        // Package the decoded header and payload into one complete protocol message
        const Protocol::Message message {
            header,
            payload
        };

        // Notify higher-level code that a complete FileBridge message is ready
        emit messageReceived(message);

        // Remove the complete message so any following message can be processed
        receiveBuffer_.remove(0, messageSize);
    }
}


/**
 * handleDisconnected()
 * Reposts that the remote peer has disconnected
 */
void PeerConnection::handleDisconnected() {
    emit disconnected();
}
