#ifndef FILEBRIDGE_MAIN_WINDOW_HPP
#define FILEBRIDGE_MAIN_WINDOW_HPP

#include "ConnectionManager.hpp"
#include "PeerDiscovery.hpp"
#include "Protocol.hpp"
#include "TransferManager.hpp"

#include <QButtonGroup>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>


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
         * handleConnectionInfoClicked()
         * Displays the local IP address and listening port used for manual connections
         */
        void handleConnectionInfoClicked();

        /**
         * handleConnectClicked()
         * Validates the entered peer address and starts an outgoing connection
         */
        void handleConnectClicked();

        /**
         * handleManualConnectionClicked()
         * Displays a dialog for connecting directly to a peer by IP address and port
         */
        void handleManualConnectionClicked();

        /** 
         * handleDisconnectClicked()
         * Disconnects the currently active FileBridge peer
         */
        void handleDisconnectClicked();

        /**
         * handleSendModeClicked()
         * Displays the outgoing-file page in the transfer area
         */
        void handleSendModeClicked();

        /**
         * handleReceiveModeClicked()
         * Displays the incoming-file page in the transfer area
         */
        void handleReceiveModeClicked();

        /**
         * handleConnectionApprovalRequested()
         * Displays an approval dialog for an incoming FileBridge connection request
         */
        void handleConnectionApprovalRequested(PeerConnection *connection, const QString& deviceName);

        /**
         * handleConnectionRejected()
         * Updates the interface when a remote user rejects an outgoing connection request
         */
        void handleConnectionRejected(PeerConnection *connection);

        /**
         * 
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
         * handleAcceptAllIncomingClicked()
         * Accepts the currently displayed incoming transfer
         */
        void handleAcceptAllIncomingClicked();

        /**
         * handleRejectAllIncomingClicked()
         * Rejects the currently displayed incoming transfer
         */
        void handleRejectAllIncomingClicked();

        /**
         * handleRemoveSelectedIncomingClicked()
         * Removes completed or rejected transfers selected in the incoming list
         */
        void handleRemoveSelectedIncomingClicked();

        /**
         * handleAddFilesClicked()
         * Opens a multi-file picker and add the selected files to the outgoing queue
         */
        void handleAddFilesClicked();

        /**
         * handleRemoveSelectedFilesClicked()
         * Removes the selected files from the outgoing queue before they are sent
         */
        void handleRemoveSelectedFilesClicked();

        /**
         * handleSendFilesClicked()
         * Strarts sending the queued files to the active peer one at a time
         */
        void handleSendFilesClicked();

        /**
         * handleOutgoingTransferFailed()
         * Marks an outgoing transfer as failed and keeps its history row visible
         */
        void handleOutgoingTransferFailed(std::uint64_t transferId, const QString& errorMessage);

        /**
         * handleOutgoingTransferCompleted()
         * Displays that an outgoing file transfer finished successfully
         */
        void handleOutgoingTransferCompleted(std::uint64_t transferId);

        /**
         * handleIncomingTransferFailed()
         * Marks an incoming transfer as failed and allows its history row to be removed
         */
        void handleIncomingTransferFailed(std::uint64_t transferId, const QString& errorMessage);

        /**
         * handleIncomingTransferCompleted()
         * Displays that an incoming file transfer finished successfully
         */
        void handleIncomingTransferCompleted(std::uint64_t transferId);

        /**
         * handleOutgoingTransferSent()
         * Marks the matching outgoing transfer as sent while awaiting receiver confirmation
         */
        void handleOutgoingTransferSent(std::uint64_t transferId);

        /**
         * handleIncomingTransferProgress()
         * Updates the visible progress for an incoming file transfer
         */
        void handleIncomingTransferProgress(std::uint64_t transferId, std::uint64_t bytesReceived, std::uint64_t fileSize);

        /**
         * handleOutgoingTransferProgress()
         * Updates the matching outgoing transfer row as additional file data is sent
         */
        void handleOutgoingTransferProgress(std::uint64_t transferId, std::uint64_t bytesSent, std::uint64_t fileSize);

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

        /**
         * offerQueuedFiles()
         * Sends file offers for every queued file that has not already been offered
         */
        void offerQueuedFiles();

        /**
         * updateOutgoingQueueControls()
         * Updates queue-button availability from the current connection and sending state
         */
        void updateOutgoingQueueControls();

        /**
         * updateOutgoingTransferDisplay()
         * Updates one outgoing transfer row to match its current state and progress
         */
        void updateOutgoingTransferDisplay(QListWidgetItem *item);

        /**
         * updateReceiveModeAttention()
         * Updates the Receive selector text and attention styling from active incoming transfers
         */
        void updateReceiveModeAttention();

        /**
         * updateIncomingRemoveControl()
         * Enables Remove Selected when at least one selected incoming transfer may be removed
         */
        void updateIncomingRemoveControl();

        /**
         * updateIncomingDecisionControls()
         * Enables Accept Selected and Reject Selected when a pending incoming transfer is selected
         */
        void updateIncomingDecisionControls();

        /**
         * updateIncomingTransferDisplay()
         * Rebuilds one incoming-transfer row from its stored metadata and lifecycle state
         */
        void updateIncomingTransferDisplay(QListWidgetItem *item);

        // Hidden QListWidgetItem data role containing the peer's unique running-instance ID
        static constexpr int NEARBY_DEVICE_INSTANCE_ID_ROLE = Qt::UserRole;

        // Hidden QListWidgetItem data role containing the peer's IPv4 address
        static constexpr int NEARBY_DEVICE_ADDRESS_ROLE = Qt::UserRole + 1;

        // Hidden QListWidgetItem data role containing the peer's TCP listening port
        static constexpr int NEARBY_DEVICE_PORT_ROLE = Qt::UserRole + 2;

        // Hidden QListWidgetItem data role containing an outgoing file's full local path
        static constexpr int OUTGOING_FILE_PATH_ROLE = Qt::UserRole;

        // Hidden QListWidgetItem data role containing the protocol transfer ID assigned after an offer is sent
        static constexpr int OUTGOING_TRANSFER_ID_ROLE = Qt::UserRole + 1;

        /**
         * OutgoingTransferState
         * Represents the lifecycle state displayed for one outgoing transfer
         */
        enum class OutgoingTransferState : int {
            Pending = 0,
            Waiting = 1,
            Sending = 2,
            Sent = 3,
            Completed = 4,
            Rejected = 5,
            Failed = 6
        };

        // Hidden QListWidgetItem data role containing the outgoing transfer's current display state
        static constexpr int OUTGOING_TRANSFER_STATE_ROLE = Qt::UserRole + 2;

        static constexpr int OUTGOING_FILE_NAME_ROLE = Qt::UserRole + 3;

        // Hidden QListWidgetItem data role containing the outgoing file's total size in bytes
        static constexpr int OUTGOING_FILE_SIZE_ROLE = Qt::UserRole + 4;

        // Hidden QListWidgetItem data role containing the number of bytes sent so far
        static constexpr int OUTGOING_BYTES_SENT_ROLE = Qt::UserRole + 5;

        // Hidden QListWidgetItem data role containing an incoming file's transfer identifier
        static constexpr int INCOMING_TRANSFER_ID_ROLE = Qt::UserRole;

        // Hidden QListWidgetItem data role indicating whether an incoming offer still needs a decision
        static constexpr int INCOMING_TRANSFER_PENDING_ROLE = Qt::UserRole + 1;

        // Hidden QListWidgetItem data role indicating whether the row may be removed from transfer history
        static constexpr int INCOMING_TRANSFER_REMOVABLE_ROLE = Qt::UserRole + 2;

        /**
         * IncomingTransferState
         * Represents the lifecycle state displayed for one incoming transfer
         */
        enum class IncomingTransferState : int {
            Pending = 0,
            Waiting = 1,
            Receiving = 2,
            Completed = 3,
            Rejected = 4,
            Failed = 5
        };

        // Hidden QListWidgetItem data role containing the incoming transfer's current display state
        static constexpr int INCOMING_TRANSFER_STATE_ROLE = Qt::UserRole + 3;

        // Hidden QListWidgetItem data role containing the incoming transfer's original filename
        static constexpr int INCOMING_TRANSFER_FILE_NAME_ROLE = Qt::UserRole + 4;

        // Hidden QListWidgetItem data role containing the incoming transfer's total size in bytes
        static constexpr int INCOMING_TRANSFER_FILE_SIZE_ROLE = Qt::UserRole + 5;

        // Hidden QListWidgetItem data role containing the number of bytes received so far
        static constexpr int INCOMING_TRANSFER_BYTES_RECEIVED_ROLE = Qt::UserRole + 6;

        // Discovers nearby FileBridge instances on the local network
        PeerDiscovery *peerDiscovery_;

        // Coordinates all incoming and outgoing FileBridge peer connections
        ConnectionManager connectionManager_;

        // Coordinates outgoing and incoming file-transfer state
        TransferManager transferManager_;

        // Identifies the peer currently available for file-transfer operations
        PeerConnection *activePeer_;

        // Controls whether incoming file offers are accepted automatically for the current connection
        bool autoAcceptIncomingTransfers_;

        // Becomes true while FileBridge is processing the current outgoing file queue
        bool outgoingQueueSending_;

        // Opens a dialog showing local address and listening-port information
        QToolButton *connectionInfoButton_;

        // Displays FileBridge instances currently discovered on the local network
        QListWidget *nearbyDevicesList_;

        // Starts an outgoing peer connection using the entered address and port
        QPushButton *connectButton_;

        // Disconnects the currently active FileBridge peer
        QPushButton *disconnectButton_;

        // Opens the direct IP-address and port connection dialog
        QPushButton *manualConnectionButton_;

        // Groups the Send and Receive mode button so only one can be selected at a time
        QButtonGroup *transferModeButtonGroup_;

        // Selects the outgoing-file page of the transfer area
        QPushButton *sendModeButton_;

        // Selects the incoming-file page and dispays pending-offer attention information
        QPushButton *receiveModeButton_;

        // Displays either the Send page or Receive page without changing the window size
        QStackedWidget *transferStack_;

        // Displays files queued locally for the next outgoing transfer
        QListWidget *outgoingFilesList_;

        // Opens a multi-file picker and adds selected files to the outgoing queue
        QPushButton *addFilesButton_;

        // Removes selected files from the outgoing queue before sending
        QPushButton *removeSelectedFilesButton_;

        // Starts sending the files currently stored in the outgoing queue
        QPushButton *sendFilesButton_;

        // Starts sending the files currenly stored in the outgoing queue
        QListWidget *incomingFilesList_;

        // Accepts the pending incoming file selected in the Receive list
        QPushButton *acceptTransferButton_;

        // Rejects the pending incoming file selected in the Receive list
        QPushButton *rejectTransferButton_;

        // Removes selected completed or rejected entries from the incoming transfer list
        QPushButton *removeSelectedIncomingButton_;

        // Displays the current FileBridge connection status
        QLabel *statusLabel_;
};


#endif
