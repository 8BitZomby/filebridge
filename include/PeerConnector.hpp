#ifndef FILEBRIDGE_PEER_CONNECTOR_HPP
#define FILEBRIDGE_PEER_CONNECTOR_HPP

#include <QHostAddress>
#include <QObject>
#include <QTcpSocket>

#include <cstdint>


// Creates outgoing TCP connections to other FileBridge peers
class PeerConnector : public QObject {
    Q_OBJECT

    public:

        /**
         * Constructor: PeerConnector()
         * Constructs an outgoing peer connector with no active connection attempt
         */
        explicit PeerConnector(QObject *parent = nullptr);

        /**
         * connectToPeer()
         * Starts an asyncronous TCP connection attempt to the given address and port
         */
        void connectToPeer(const QHostAddress& address, std::uint16_t port);

        /**
         * cancelConnection()
         * Cancels the current outgoing connection attempt if one is in progress.
         */
        bool cancelConnection();

        /**
         * isConnecting()
         * Returns whether an outgoing connection attempt is currently in progress
         */
        bool isConnecting() const;

    signals:

        /**
         * connectionEstablished()
         * Reports a successfully established outgoing TCP connection
         */
        void connectionEstablished(QTcpSocket *socket);

        /**
         * connectionFailed()
         * Reports that the current outgoing connection attempt failed
         */
        void connectionFailed(const QString& errorMessage);

    private slots:

        /**
         * handleConnected()
         * Handles successful completion of the current TCP connection attempt
         */
        void handleConnected();

        /**
         * handleError()
         * Handles failure of the current TCP connection attempt
         */
        void handleError(QAbstractSocket::SocketError socketError);

    private:

        // Socket used only while establishing the current outgoing connection
        QTcpSocket *socket_;
};


#endif
