#ifndef FILEBRIDGE_PEER_CONNECTION_HPP
#define FILEBRIDGE_PEER_CONNECTION_HPP

#include "Protocol.hpp"

#include <QObject>
#include <QTcpSocket>


/**
 * PeerConnection
 * Represents one active TCP connection to another FileBridge peer
 */
class PeerConnection : public QObject {
    Q_OBJECT

    public:

        /**
         * Constructor: PeerConnection()
         * Constructs a peer connection around an already-connected TCP socket
         */
        explicit PeerConnection(QTcpSocket *socket, QObject *parent = nullptr);

        /**
         * Destructor: ~PeerConnection()
         * Destroys the peer connection and its owned TCP socket
         */
        ~PeerConnection() override = default;

        /**
         * socket()
         * Returns the underlying TCP socket for this peer connection
         */
        QTcpSocket* socket() const;

        /**
         * sendMessage()
         * Serializes and sends one complete FileBridge protocol message to the peer
         */
        bool sendMessage(const Protocol::Message& message);

        /**
         * disconnectFromPeer()
         * Gracefully closes the TCP connection to the peer
         */
        void disconnectFromPeer();

    signals:

        /**
         * messageReceived()
         * Reports one complete framed FileBridge protocol message
         */
        void messageReceived(const Protocol::Message& message);

        /**
         * disconnected()
         * Reports that the remote peer has disconnected
         */
        void disconnected();

    private slots:

        /**
         * handleReadyRead()
         * Reads all currently available bytes from the peer socket
         */
        void handleReadyRead();

        /**
         * handleDisconnected()
         * Reports that the peer socket has disconnected
         */
        void handleDisconnected();

    private:

        // Connected TCP socket used to communicate with this peer
        QTcpSocket *socket_;

        // Stores incomplete TCP data until one or more complete protocol messages are available
        QByteArray receiveBuffer_;
};


#endif
