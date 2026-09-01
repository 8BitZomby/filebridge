#ifndef FILEBRIDGE_MAIN_WINDOW_HPP
#define FILEBRIDGE_MAIN_WINDOW_HPP

#include "ConnectionManager.hpp"
#include "PeerDiscovery.hpp"
#include "Protocol.hpp"
#include "TransferManager.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>


/**
 * MainWindow
 * Provides the primary FileBridge user interface
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

    public:

        /**
         * Constructor: MainWindow()
         * Constructs the FileBridge window and initializes its connection controls
         */
        explicit MainWindow(QWidget *parent = nullptr);

    private slots:

        /**
         * handleConnectClicked()
         * Validates the entered peer address and starts an outgoing connection
         */
        void handleConnectClicked();

        /** handleDisconnectClicked()
         * Disconnects the currently active FileBridge peer
         */
        void handleDisconnectClicked();

        /**
         * handlePeerReady()
         * Updates the interface after a peer completes the FileBridge handshake
         */
        void handlePeerReady(PeerConnection *connection);

        /** handlePeerDisconnected()
         * Updates the interface after the active FileBridge peer disconnects
         */
        void handlePeerDisconnected(PeerConnection *connection);

        /**
         * handleOutgoingTransferOffered()
         * Displays metadata for a file successfully offered to the connected peer.
         */
        void handleOutgoingTransferOffered(std::uint64_t transferId, const QString& fileName, std::uint64_t fileSize);

        /**
         * handleOutgoingTransferAccepted()
         */
        void handleOutgoingTransferAccepted(std::uint64_t transferId);

        /**
         * handleOutgoingTranferRejected()
         * Displays that the remote peer rejected an outgoing transfer
         */
        void handleOutgoingTransferRejected(std::uint64_t transferId);

        /**
         * handleIncomingTransferOffered()
         * Displays a pending incoming transfer reported by the transfer manager.
         */
        void handleIncomingTransferOffered(const TransferManager::IncomingTransfer& transfer);

        /**
         * handleAcceptTransferClicked()
         * Accepts the currently displayed incoming transfer
         */
        void handleAcceptTransferClicked();

        /**
         * handleRejectTransferClicked()
         * Rejects the currently displayed incoming transfer
         */
        void handleRejectTransferClicked();

        /**
         * handleChooseFileClicked()
         * Opens a file picker and prepares metadata for the selected file
         */
        void handleChooseFileClicked();

        /**
         * handleOutgoingTransferCompleted()
         * Displays that an outgoing file transfer finished successfully
         */
        void handleOutgoingTransferCompleted(std::uint64_t transferId);

        /**
         * handleIncomingTransferCompleted()
         * Displays that an incoming file transfer finished successfully
         */
        void handleIncomingTransferCompleted(std::uint64_t transferId);

        /**
         * handleConnectionFailed()
         * Displays an outgoing connection failure in the interface
         */
        void handleConnectionFailed(const QString& errorMessage);

        /**
         * handlePeerDiscovered()
         * Adds a newly discovered FileBridge instance to the nearby-devices list
         */
        void handlePeerDiscovered(const DiscoveredPeer& peer);

        /**
         * handlePeerLost()
         * Removes a FileBridge instance that is no longer advertising itself
         */
        void handlePeerLost(const DiscoveredPeer& peer);

        /**
         * handleNearbyDeviceDoubleClicked()
         * Starts a connection request to the discovered device selected by the user
         */
        void handleNearbyDeviceDoubleClicked(QListWidgetItem *item);

    private:

        /**
         * connectToNearbyDevice()
         * Starts an outgoing connection using connection data stored in a nearby-device item
         */
        void connectToNearbyDevice(QListWidgetItem *item);

        // Handle QListWidgetItem data role containing the peer's unique running-instance ID
        static constexpr int NEARBY_DEVICE_INSTANCE_ID_ROLE = Qt::UserRole;

        // Hidden QListWidgetItem data role containing the peer's IPv4 address
        static constexpr int NEARBY_DEVICE_ADDRESS_ROLE = Qt::UserRole + 1;

        // Hidden QListWidgetItem data role containing the peer's TCP listening port
        static constexpr int NEARBY_DEVICE_PORT_ROLE = Qt::UserRole + 2;

        // Discovers nearby FileBridge instances on the local network
        PeerDiscovery *peerDiscovery_;

        // Coordinates all incoming and outgoing FileBridge peer connections
        ConnectionManager connectionManager_;

        // Coordinates outgoing and incoming file-transfer state
        TransferManager transferManager_;

        // Identifies the peer currently available for file-transfer operations
        PeerConnection *activePeer_;

        // Identifies the incoming transfer currently awaiting a used decision
        std::uint64_t pendingIncomingTransferId_;

        // Stores the original filename for the incoming transfer acaiting a user decision
        QString pendingIncomingFileName_;

        // Displays the preferred local IPv4 address
        QLabel *localAddressLabel_;

        // Displays the automatically selected listening port
        QLabel *localPortLabel_;

        // Accepts the remote peer IPv4 address entered by the user
        QLineEdit *remoteAddressEdit_;

        // Accepts the remote peer listening port entered by the user
        QLineEdit *remotePortEdit_;

        // Displays FileBridge instances currently discovered on the local network
        QListWidget *nearbyDevicesList_;

        // Starts an outgoing peer connection using the entered address and port
        QPushButton *connectButton_;

        // Disconnects the currently active FileBridge peer
        QPushButton *disconnectButton_;

        // Opens a file picker so the user can choose a file to offer to the connected peer
        QPushButton *chooseFileButton_;

        // Accepts the currently displayed incoming file offer
        QPushButton *acceptTransferButton_;

        // Rejects the currently displayed incoming file offer
        QPushButton *rejectTransferButton_;

        // Displays the current FileBridge connection status
        QLabel *statusLabel_;
};


#endif
