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
         * cancelConnection()
         * Cancels the current outgoing connection attempt if one is in progress.
         */
        bool cancelConnection();

        /**
         * approveConnection()
         * Approves a pending incoming FileBridge connection.
         */
        bool approveConnection(PeerConnection *connection);

        /**
         * rejectConnection()
         * Rejects a pending incoming FileBridge connection.
         */
        bool rejectConnection(PeerConnection *connection);

        /**
         * disconnectPeer()
         * Disconnects an established peer currently managed by FileBridge
         */
        bool disconnectPeer(PeerConnection *connection);

        /**
         * sendFileOffer()
         * Sends metadata for one proposed file transfer to a ready peer
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
         * sendFileCompleteAck()
         * Sends acknowledgement that an incoming file transfer completed successfully
         */
        bool sendFileCompleteAck(PeerConnection *connection, const Protocol::FileCompleteAckPayload& completeAck);

        /**
         * sendError()
         * Sends a transfer-specific failure to a validated peer
         */
        bool sendError(PeerConnection *connection, const Protocol::ErrorPayload& error);

        /**
         * listeningPort()
         * Returns the TCP port currently used for incoming peer connections
         */
        std::uint16_t listeningPort() const;

        /**
         * deviceName()
         * Returns the remote device name associated with a managed peer connection.
         */
        QString deviceName(PeerConnection *connection) const;

    signals:

        /**
         * connectionApprovalRequested()
         * Reports an incoming peer whose valid handshake is awaiting local user approval.
         */
        void connectionApprovalRequested(
            PeerConnection *connection,
            const QString& deviceName
        );

        /**
         * connectionRejected()
         * Reports that the remote user rejected an outgoing connection request.
         */
        void connectionRejected(PeerConnection *connection);

        /**
         * peerReady()
         * Reports that a peer has completed a valid FileBridge handshake and is ready for use
         */
        void peerReady(PeerConnection *connection);

        /**
         * fileOfferReceived()
         * Reports file metadata offered by a connected and validated peer
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
         * fileCompleteAckReceived()
         * Reports that the remote receiver finalized an outgoing file transfer
         */
        void fileCompleteAckReceived(PeerConnection *connection, const Protocol::FileCompleteAckPayload& completeAck);

        /**
         * errorReceived()
         * Reports a transfer-specific failure sent by the remote peer
         */
        void errorReceived(PeerConnection *connection, const Protocol::ErrorPayload& error);

        /**
         * peerDisconnected()
         * Reports that an established FileBridge peer connection has ended
         */
        void peerDisconnected(PeerConnection *connection);

        /**
         * peerBytesWritten()
         * Reports bytes removed from a peer's Qt socket write buffer
         */
        void peerBytesWritten(PeerConnection *connection, qint64 bytes);

        /**
         * connectionFailed()
         * Reports that an outgoing connection attempt failed
         */
        void connectionFailed(const QString& errorMessage);

    private slots:

        /**
         * handlePeerDisconnected()
         * Removes and destroys a peer connection after its socket disconnects
         */
        void handlePeerDisconnected();

    private:

        /**
         * ConnectionDirection
         * Identifies whether FileBridge accepted or initiated a TCP peer connection.
         */
        enum class ConnectionDirection : std::uint8_t {
            Incoming = 0,
            Outgoing = 1
        };

        /**
         * handleEstablishedSocket()
         * Configures an incoming or outgoing TCP socket as a FileBridge peer connection
         */
        void handleEstablishedSocket(QTcpSocket *socket, ConnectionDirection direction);

        /**
         * ManagedPeer
         * Tracks an established TCP peer while FileBridge completes its protocol handshake
         */
        struct ManagedPeer {
            // Connection used to exchange framed FileBridge protocol messages
            PeerConnection *connection;

            // Identifies whether this TCP connection was accepted locally or initiated locally
            ConnectionDirection direction;

            // Becomes true after a valid handshake has been received from this peer
            bool handshakeReceived;

            // Becomes true only after the incoming connection request has been approved
            bool approved;

            // Human-readable device name learned from the remote peer's handshake
            QString deviceName;
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
