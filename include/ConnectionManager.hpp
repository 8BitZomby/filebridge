#ifndef FILEBRIDGE_CONNECTION_MANAGER_HPP
#define FILEBRIDGE_CONNECTION_MANAGER_HPP

#include "PeerConnection.hpp"
#include "PeerConnector.hpp"
#include "TcpListener.hpp"

#include <QHostAddress>
#include <QObject>

#include <cstdint>
#include <vector>


/**
 * ConnectionManager
 * Coordinates incoming and outgoing peer connections for FileBridge
 */
class ConnectionManager : public QObject {
    Q_OBJECT

    public:

        /**
         * Constructor: ConnectionManager()
         * Constructs the connection manager with no active peer connections
         */
        explicit ConnectionManager(QObject *parent = nullptr);

        /**
         * start()
         * Starts the local TCP listener on an available port
         */
        bool start();

        /**
         * connectToPeer()
         * Starts an outgoing connection attempt to another FileBridge peer
         */
        void connectToPeer(const QHostAddress& address, std::uint16_t port);

        /**
         * listeningPort()
         * Returns the TCP port currently used for incoming peer connections
         */
        std::uint16_t listeningPort() const;

    signals:

        /**
         * peerConnected()
         * Reports that a complete FileBridge peer connection has been established
         */
        void peerConnected(PeerConnection *connection);

        /**
         * peerDisconnected()
         * Reports that an established FileBridge peer connection has ended
         */
        void peerDisconnected(PeerConnection *connection);

        /**
         * connectionFailed()
         * Reports that an outgoing connection attempt failed
         */
        void connectionFailed(const QString& errorMessage);

    private slots:

        /**
         * handleEstablishedSocket()
         * Configures an incoming or outgoing TCP socket as a FileBridge peer connection
         */
        void handleEstablishedSocket(QTcpSocket *socket);

        /**
         * handlePeerDisconnected()
         * Removes and destroys a peer connection after its socket disconnects
         */
        void handlePeerDisconnected();

    private:

        /**
         * sendHandshake()
         * Sends FileBridge identification and protocol information to a connected peer
         */
        bool sendHandshake(PeerConnection *connection);

        /**
         * handleMessage()
         * Processes a complete protocol message received from a peer
         */
        void handleMessage(PeerConnection *connection, const Protocol::Message& message);

        // Accepts incoming TCP connections from other FileBridge peers
        TcpListener listener_;

        // Creates outgoing TCP connections to other FileBridge peers
        PeerConnector connector_;

        // Owns references to all currently active peer connections
        std::vector<PeerConnection *> connections_;
};


#endif
