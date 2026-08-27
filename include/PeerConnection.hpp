#ifndef FILEBRIDGE_PEER_CONNECTION_HPP
#define FILEBRIDGE_PEER_CONNECTION_HPP

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

    signals:

        /**
         * dataReceived()
         * Reports raw bytes received from the connected peer
         */
        void dataReceived(const QByteArray& data);

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
};


#endif
