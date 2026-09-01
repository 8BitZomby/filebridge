#ifndef FILEBRIDGE_CONNECTION_MANAGER_HPP
#define FILEBRIDGE_CONNECTION_MANAGER_HPP

#include "PeerConnection.hpp"
#include "PeerConnector.hpp"
#include "Protocol.hpp"
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
         * disconnectPeer()
         * Disconnects an established peer currently managed by FileBridge
         */
        bool disconnectPeer(PeerConnection *connection);

        /**
         * sendFileOffer()
         * Sends matadata for one proposed file transfer to a ready peer
         */
        bool sendFileOffer(PeerConnection *connection, const Protocol::FileOfferPayload& offer);

        /**
         * sendFileAccept()
         * Sends acceptance for one pending incoming file transfer
         */
        bool sendFileAccept(PeerConnection *connection, const Protocol::FileAcceptPayload& accept);

        /**
         * sendFileReject()
         * Sends rejection for one pending incoming file transfer
         */
        bool sendFileReject(PeerConnection *connection, const Protocol::FileRejectPayload& reject);

        /**
         * sendFileData()
         * Sends one chunk of file data to a validated peer
         */
        bool sendFileData(PeerConnection *connection, const Protocol::FileDataPayload& fileData);

        /** sendFileComplete()
         * Sends completion for one finished outgoing file transfer
         */
        bool sendFileComplete(PeerConnection *connection, const Protocol::FileCompletePayload& complete);

        /**
         * listeningPort()
         * Returns the TCP port currently used for incoming peer connections
         */
        std::uint16_t listeningPort() const;

    signals:

        /**
         * peerReady()
         * Reports that a peer has completed a valid FileBridge handshake and is ready for use
         */
        void peerReady(PeerConnection *connection);

        /**
         * fileOfferReceived()
         * Reports file matadata offered by a connectied and validated peer
         */
        void fileOfferReceived(PeerConnection *connection, const Protocol::FileOfferPayload& offer);

        /**
         * fileAcceptReceived()
         * Reports that a remote peer accepted one offered file transfer
         */
        void fileAcceptReceived(PeerConnection *connection, const Protocol::FileAcceptPayload& accept);

        /**
         * fileRejectReceived()
         * Reports that a remote peer rejected one offered file transfer
         */
        void fileRejectReceived(PeerConnection *connection, const Protocol::FileRejectPayload& reject);

        /**
         * fileDataReceived()
         * Reports one decoded chunk of file data received from a peer
         */
        void fileDataReceived(PeerConnection *connection, const Protocol::FileDataPayload& fileData);

        /**
         * fileCompleteReceived()
         * Reports that a peer finished sending all file data for one transfer
         */
        void fileCompleteReceived(PeerConnection *connection, const Protocol::FileCompletePayload& complete);

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
         * ManagedPeer
         * Tracks an established TCP peer while FileBridge completes its protocol handshake
         */
        struct ManagedPeer {
            // Connection used to exchange framed FileBridge protocol messages
            PeerConnection *connection;

            // Becomes true after a valid handshake has been received from this peer
            bool handshakeReceived;
        };

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

        // Tracks all established peers, including those still completing their handshake
        std::vector<ManagedPeer> connections_;
};


#endif
