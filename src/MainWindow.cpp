#include "MainWindow.hpp"
#include "ConnectionManager.hpp"
#include "NetworkInfo.hpp"
#include "PeerDiscovery.hpp"
#include "Protocol.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QIntValidator>
#include <QLabel>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>


/**
 * Constructor: MainWindow()
 * Constructs the FileBridge window and initializes its connection controls
 */
MainWindow::MainWindow(QWidget *parent) : 
    QMainWindow(parent),
    peerDiscovery_(nullptr),
    transferManager_(&connectionManager_, this),
    activePeer_(nullptr),
    autoAcceptIncomingTransfers_(false),
    pendingIncomingTransferId_(0),
    localAddressLabel_(new QLabel(this)),
    localPortLabel_(new QLabel(this)),
    remoteAddressEdit_(new QLineEdit(this)),
    remotePortEdit_(new QLineEdit(this)),
    nearbyDevicesList_(new QListWidget(this)),
    connectButton_(new QPushButton("Connect", this)),
    disconnectButton_(new QPushButton("Disconnect", this)),
    chooseFileButton_(new QPushButton("Choose File", this)),
    acceptTransferButton_(new QPushButton("Accept", this)),
    rejectTransferButton_(new QPushButton("Reject", this)),
    statusLabel_(new QLabel("Ready", this)) {

        // Start listening for incoming FileBridge connections before displaying connections
        if(!connectionManager_.start()) {
            statusLabel_->setText("Failed to start local listener");
            connectButton_->setEnabled(false);
        }
        else {
            // Create LAN discovery using the TCP port assigned by ConnectionManager
            peerDiscovery_ = new PeerDiscovery(
                connectionManager_.listeningPort(), // listeningPort: TCP port nearby peers should use to connect to this instance
                this                                // parent: MainWindow owns and destroys the PeerDiscovery object
            );

            // Add newly discovered FileBridge instances to the nearby-devices list.
            QObject::connect(
                peerDiscovery_,                         // Sender: Service tracking FileBridge instances on the local network.
                &PeerDiscovery::peerDiscovered,         // Signal: Emitted when a previously unknown peer is first discovered.
                this,                                   // Receiver: This MainWindow instance.
                &MainWindow::handlePeerDiscovered       // Slot: Adds the discovered peer to the visible device list.
            );

            // Remove devices that stop sending discovery heartbeats.
            QObject::connect(
                peerDiscovery_,                         // Sender: Service tracking FileBridge instances on the local network.
                &PeerDiscovery::peerLost,               // Signal: Emitted when a known peer exceeds the heartbeat timeout.
                this,                                   // Receiver: This MainWindow instance.
                &MainWindow::handlePeerLost             // Slot: Removes the expired peer from the visible device list.
            );

            // Start receiving and periodically broadcasting FileBridge discovery announcements
            if(!peerDiscovery_->start()) {
                qDebug() << "Failed to start peer discovery";
            }
        }

        // Display the preferred local IPv4 address that another device can use to connect
        const QHostAddress localAddress = NetworkInfo::preferredLocalIPv4Address();

        if(localAddress.isNull()) {
            localAddressLabel_->setText("Unavailable");
        }
        else {
            localAddressLabel_->setText(localAddress.toString());
        }

        // Display the automatically selected listening port
        localPortLabel_->setText(QString::number(connectionManager_.listeningPort()));

        // Restrict the port field to the valid TCP/UDP port-number range
        remotePortEdit_->setValidator(new QIntValidator(1, 65535, remotePortEdit_));

        remoteAddressEdit_->setPlaceholderText("192.168.0.10");
        remotePortEdit_->setPlaceholderText("Port");

        // Make nearby devices list small but scrollable if more devices show up
        nearbyDevicesList_->setMaximumHeight(50);

        // File offers are only valid after a peer completes the FileBridge handshake
        chooseFileButton_->setEnabled(false);

        // Disconnect is only available after a peer completes the FileBridge handshake
        disconnectButton_->setEnabled(false);

        // Accept and Reject are only available while an incoming transfer is awaiting a decision
        acceptTransferButton_->setEnabled(false);
        rejectTransferButton_->setEnabled(false);

        // Start an outgoing connection when the user presses the Connect button
        QObject::connect(
            connectButton_,                                 // Button that emits the signal
            &QPushButton::clicked,                          // Signal emitted when the button is pressed
            this,                                           // Window that handles the action
            &MainWindow::handleConnectClicked               // Slot that validates and connects to the peer
        );

        // End the active peer connection when the user presses Disconnect.
        QObject::connect(
            disconnectButton_,                              // Sender: Disconnect button in the FileBridge window.
            &QPushButton::clicked,                          // Signal: Emitted when the user clicks Disconnect.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleDisconnectClicked            // Slot: Requests disconnection of the active peer.
        );

        // Start a connection request when the user double-clicks a discovered device.
        QObject::connect(
            nearbyDevicesList_,                             // Sender: List displaying discovered FileBridge devices.
            &QListWidget::itemDoubleClicked,                // Signal: Emitted when the user double-clicks one device entry.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleNearbyDeviceDoubleClicked    // Slot: Connects using the selected peer's stored address and port.
        );

        // Open the file picker when the user chooses to prepare for a transfer
        QObject::connect(
            chooseFileButton_,                              // Button that emits the signal
            &QPushButton::clicked,                          // Signal emitted when the button is pressed
            this,                                           // Window that handles the action
            &MainWindow::handleChooseFileClicked            // Slot that opens the file picker
        );

        // Accept the currently displayed incoming transfer when the user clicks Accept
        QObject::connect(
            acceptTransferButton_,                          // Sender: Accept button in the FileBridge window
            &QPushButton::clicked,                          // Signal: Emitted when the user clicks the button
            this,                                           // Receiver: This MainWindow instance
            &MainWindow::handleAcceptTransferClicked        // Slot: Accepts the pending incoming transfer
        );

        // Reject the currently displayed incoming transfer when the user clicks Reject
        QObject::connect(
            rejectTransferButton_,                          // Sender: Reject button in the FileBridge window
            &QPushButton::clicked,                          // Signal: Emitted when the user clicks the button
            this,                                           // Receiver: This MainWindow instance
            &MainWindow::handleRejectTransferClicked        // Slot: Rejects the pending incoming transfer
        );

        // Ask the local user whether an incoming FileBridge connection should be trusted.
        QObject::connect(
            &connectionManager_,                                  // Sender: Manager validating incoming FileBridge peers.
            &ConnectionManager::connectionApprovalRequested,      // Signal: Emitted after an incoming peer completes its handshake
            this,                                                 // Receiver: This MainWindow instance.
            &MainWindow::handleConnectionApprovalRequested        // Slot: Displays the Accept/Reject connection dialog.
        );

        // Report when another user rejects a connection initiated by this FileBridge instance.
        QObject::connect(
            &connectionManager_,                                  // Sender: Manager tracking the outgoing connection request.
            &ConnectionManager::connectionRejected,               // Signal: Emitted after the remote user rejects the request.
            this,                                                 // Receiver: This MainWindow instance.
            &MainWindow::handleConnectionRejected                 // Slot: Restores the disconnected interface state.
        );

        // Update the interface only after a peer completes a valid FileBridge handshake
        QObject::connect(   
            &connectionManager_,                            // Manager that emits peer readiness
            &ConnectionManager::peerReady,                  // Signal emitted after handshake validation succeeds
            this,                                           // Window that displays the connection state
            &MainWindow::handlePeerReady                    // Slot that marks the peer as ready
        );

        // Reset the interface when an established FileBridge peer disconnects.
        QObject::connect(
            &connectionManager_,                            // Sender: Manager tracking established FileBridge peers.
            &ConnectionManager::peerDisconnected,           // Signal: Emitted when a managed peer connection ends.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handlePeerDisconnected             // Slot: Clears the active-peer state when appropriate.
        );

        // Display incoming transfers after TransferManager records their transfer state.
        QObject::connect(
            &transferManager_,                              // Manager that owns transfer state.
            &TransferManager::incomingTransferOffered,      // Signal emitted for a new pending transfer.
            this,                                           // Window that displays the transfer.
            &MainWindow::handleIncomingTransferOffered      // Slot that updates the interface.
        );

        // Display metadata after TransferManager successfully sends an outgoing file offer.
        QObject::connect(
            &transferManager_,                              // Manager that owns transfer state.
            &TransferManager::outgoingTransferOffered,      // Signal emitted after an offer is sent.
            this,                                           // Window that displays the transfer.
            &MainWindow::handleOutgoingTransferOffered      // Slot that updates the interface.
        );

        // Report when the remote peer accepts an outgoing file transfer
        QObject::connect(
            &transferManager_,                              // Sender: Manager that owns transfer state
            &TransferManager::outgoingTransferAccepted,     // Signal: Emitted when the remote peer accepts and offer
            this,                                           // Receiver: This MainWindow instance
            &MainWindow::handleOutgoingTransferAccepted     // Slot: Updates the sender's transfer status
        );

        // Report when the remote peer rejects an outgoing file transfer
        QObject::connect(
            &transferManager_,                              // Sender: Manager that owns transfer state
            &TransferManager::outgoingTransferRejected,     // Signal: Emitted when the remote peer rejects an offer
            this,                                           // Receiver: This MainWindow instance
            &MainWindow::handleOutgoingTransferRejected     // Slot: Updates the sender's transfer status
        );

        // Report when all data for an outgoing transfer has been sent
        QObject::connect(
            &transferManager_,                              // Sender: Manager that owns transfer state
            &TransferManager::outgoingTransferCompleted,    // Signal: Emitted after all file data and completion are sent
            this,                                           // Receiver: This MainWindow instance
            &MainWindow::handleOutgoingTransferCompleted    // Slot: Updates the receiver's completion status
        );

        // Report when an incoming transfer has been completely received and written.

        QObject::connect(
            &transferManager_,                              // Sender: Manager that owns transfer state.
            &TransferManager::incomingTransferCompleted,    // Signal: Emitted after all expected file data is received.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleIncomingTransferCompleted    // Slot: Updates the receiver's completion status.
        );

        // Use a central widget because QMainWindow reserves its outer structure for menus and toolbars
        QWidget *centralWidget = new QWidget(this);
        QVBoxLayout *layout = new QVBoxLayout(centralWidget);

        layout->addWidget(new QLabel("Your address:", centralWidget));
        layout->addWidget(localAddressLabel_);

        layout->addWidget(new QLabel("Port:", centralWidget));
        layout->addWidget(localPortLabel_);

        layout->addWidget(new QLabel("Nearby devices:", centralWidget));
        layout->addWidget(nearbyDevicesList_);

        layout->addWidget(new QLabel("Connect to peer:", centralWidget));
        layout->addWidget(remoteAddressEdit_);
        layout->addWidget(remotePortEdit_);
        layout->addWidget(connectButton_);
        layout->addWidget(disconnectButton_);

        layout->addWidget(new QLabel("File transfer:", centralWidget));
        layout->addWidget(chooseFileButton_);
        layout->addWidget(acceptTransferButton_);
        layout->addWidget(rejectTransferButton_);

        layout->addWidget(new QLabel("Status:", centralWidget));
        layout->addWidget(statusLabel_);

        setCentralWidget(centralWidget);

        setWindowTitle("FileBridge");
        resize(500, 350);
}


/**
 * handleConnectClicked()
 * Validates the entered peer address and starts an outgoing connection
 */
void MainWindow::handleConnectClicked() {
    // Prefer a discovered peer when the user has selected one in the nearby-devices list
    QListWidgetItem *selectedPeer = nearbyDevicesList_->currentItem();

    if(selectedPeer != nullptr) {
        connectToNearbyDevice(selectedPeer);
        return;
    }

    // No discovered peer is selected, so use the manually entered fallback address and port
    const QHostAddress remoteAddress(remoteAddressEdit_->text());

    bool validPort = false;
    const unsigned int remotePort = remotePortEdit_->text().toUInt(&validPort);

    // Reject incomplete or invalid connection information before reaching the network layer
    if(remoteAddress.isNull() || !validPort || remotePort == 0 || remotePort > 65535) {
        statusLabel_->setText("Invalid IP address or port");
        return;
    }

    statusLabel_->setText("Connecting...");
    connectButton_->setEnabled(false);

    connectionManager_.connectToPeer(
            remoteAddress,
            static_cast<std::uint16_t>(remotePort)
    );
}


/**
 * handleDisconnectClicked()
 * Disconnects the currently active FileBridge peer.
 */
void MainWindow::handleDisconnectClicked() {
    // A null activePeer_ means this window does not currently have a validated peer to disconnect.
    if(activePeer_ == nullptr) {
        return;
    }

    statusLabel_->setText("Disconnecting...");
    disconnectButton_->setEnabled(false);

    // ConnectionManager owns the peer lifecycle and will emit peerDisconnected
    // when the underlying TCP connection actually ends.
    if(!connectionManager_.disconnectPeer(activePeer_)) {
        statusLabel_->setText("Failed to disconnect");
        disconnectButton_->setEnabled(true);
    }
}


/**
 * handleConnectionApprovalRequested()
 * Displays an approval dialog for an incoming FileBridge connection request
 */
void MainWindow::handleConnectionApprovalRequested(
    PeerConnection *connection,
    const QString& deviceName
) {
    // A connection request without a valid managed peer cannot be approved.
    if(connection == nullptr) {
        return;
    }

    // Bring the receiving FileBridge window forward before displaying its child dialog.
    show();
    raise();
    activateWindow();

    // Request attention if macOS does not immediately grant this application focus.
    QApplication::alert(this);

    // Parent the dialog directly to this receiving MainWindow.
    // Unlike the native QMessageBox implementation on macOS, QDialog allows
    // FileBridge to control the dialog's placement relative to its parent.
    QDialog approvalDialog(this);

    approvalDialog.setWindowTitle("Connection request");
    approvalDialog.setModal(true);

    QLabel *messageLabel = new QLabel(
        deviceName + " wants to connect to FileBridge.",
        &approvalDialog
    );

    messageLabel->setWordWrap(true);

    // This preference lasts only until the current peer disconnects.
    QCheckBox *autoAcceptCheckBox = new QCheckBox(
        "Automatically accept incoming files for this connection",
        &approvalDialog
    );

    autoAcceptCheckBox->setChecked(true);

    QPushButton *rejectButton = new QPushButton(
        "Reject",
        &approvalDialog
    );

    QPushButton *acceptButton = new QPushButton(
        "Accept",
        &approvalDialog
    );

    // Accept is the primary action when the user presses Return.
    acceptButton->setDefault(true);

    // Rejecting the dialog produces QDialog::Rejected.
    QObject::connect(
        rejectButton,                 // Sender: Reject button in the connection approval dialog.
        &QPushButton::clicked,        // Signal: Emitted when the user clicks Reject.
        &approvalDialog,              // Receiver: The approval dialog itself.
        &QDialog::reject              // Slot: Closes the dialog with the Rejected result.
    );

    // Accepting the dialog produces QDialog::Accepted.
    QObject::connect(
        acceptButton,                 // Sender: Accept button in the connection approval dialog.
        &QPushButton::clicked,        // Signal: Emitted when the user clicks Accept.
        &approvalDialog,              // Receiver: The approval dialog itself.
        &QDialog::accept              // Slot: Closes the dialog with the Accepted result.
    );

    // Keep the two decision buttons together on one horizontal row.
    QHBoxLayout *buttonLayout = new QHBoxLayout();

    buttonLayout->addWidget(rejectButton);
    buttonLayout->addWidget(acceptButton);

    // Build the complete connection-request dialog vertically.
    QVBoxLayout *dialogLayout = new QVBoxLayout(&approvalDialog);

    dialogLayout->addWidget(messageLabel);
    dialogLayout->addWidget(autoAcceptCheckBox);
    dialogLayout->addLayout(buttonLayout);

    approvalDialog.adjustSize();

    // mapToGlobal() converts the center of this receiving MainWindow from
    // window-relative coordinates into screen coordinates.
    const QPoint parentCenter = mapToGlobal(rect().center());

    // Position the dialog so its own center exactly matches the center of
    // the receiving FileBridge window.
    approvalDialog.move(
        parentCenter.x() - approvalDialog.width() / 2,
        parentCenter.y() - approvalDialog.height() / 2
    );

    approvalDialog.raise();
    approvalDialog.activateWindow();

    const int result = approvalDialog.exec();

    if(result == QDialog::Accepted) {
        // Remember whether this peer's file offers should be accepted automatically
        // for the lifetime of this connection.
        autoAcceptIncomingTransfers_ = autoAcceptCheckBox->isChecked();

        if(!connectionManager_.approveConnection(connection)) {
            autoAcceptIncomingTransfers_ = false;
            statusLabel_->setText("Failed to approve connection");
        }

        return;
    }

    // Rejecting the connection clears any connection-specific transfer permission.
    autoAcceptIncomingTransfers_ = false;

    if(!connectionManager_.rejectConnection(connection)) {
        statusLabel_->setText("Failed to reject connection");
        return;
    }

    statusLabel_->setText("Connection rejected");
}


/**
 * handlePeerReady()
 * Updates the interface after a peer completes the FileBridge handshake
 */
void MainWindow::handlePeerReady(PeerConnection *connection) {
    // Remember which validated peer may receive file-transfer messages
    activePeer_ = connection;

    statusLabel_->setText("Connected");
    connectButton_->setEnabled(false);
    disconnectButton_->setEnabled(true);

    // File selection becomes available only after the peer is ready for transfer messages
    chooseFileButton_->setEnabled(true);
}


/**
 * handleConnectionRejected()
 * Updates the interface when a remote user rejects an outgoing connection request
 */
void MainWindow::handleConnectionRejected(PeerConnection *connection) {
    Q_UNUSED(connection);

    // The rejected connection never became an active transfer peer.
    autoAcceptIncomingTransfers_ = false;

    statusLabel_->setText("Connection rejected");
    connectButton_->setEnabled(true);
    disconnectButton_->setEnabled(false);
    chooseFileButton_->setEnabled(false);
}


/**
 * handlePeerDisconnected()
 * Updates the interface after an established or pending peer disconnects
 */
void MainWindow::handlePeerDisconnected(PeerConnection *connection) {
    if(connection != activePeer_) {
        return;
    }

    // The PeerConnection will be destroyed by ConnectionManager after this signal returns.
    activePeer_ = nullptr;

    // Automatic file acceptance is trusted only for the lifetime of this connection.
    autoAcceptIncomingTransfers_ = false;

    statusLabel_->setText("Disconnected");
    connectButton_->setEnabled(true);
    disconnectButton_->setEnabled(false);
    chooseFileButton_->setEnabled(false);

    acceptTransferButton_->setEnabled(false);
    rejectTransferButton_->setEnabled(false);
}


/**
 * handleChooseFileClicked()
 * Opens a file picker and asks the transfer manager to offer the selected file.
 */
void MainWindow::handleChooseFileClicked() {
    // Open file chooser to select file to send
    const QString filePath = QFileDialog::getOpenFileName(this, "Choose file to send");
    
    // Closing the dialog without selecting a file is not an error.
    if(filePath.isEmpty()) {
        return;
    }

    // A transfer requires a currently validated FileBridge peer.
    if(activePeer_ == nullptr) {
        statusLabel_->setText("No connected peer");
        return;
    }

    // Delegate file validation, transfer-ID generation, and FileOffer creation to TransferManager.
    if(!transferManager_.offerFile(activePeer_, filePath)) {
        statusLabel_->setText("Failed to offer selected file");
        return;
    }
}


/**
 * handleOutgoingTransferOffered()
 * Displays metadata for a file successfully offered to the connected peer.
 */
void MainWindow::handleOutgoingTransferOffered(std::uint64_t transferId, const QString& fileName, std::uint64_t fileSize) {
    Q_UNUSED(transferId);

    // Display the outgoing transfer while the dedicated transfer UI is still under development.
    statusLabel_->setText(
        "Offered: " +
        fileName +
        " (" +
        QString::number(fileSize) +
        " bytes)"
    );
}


/**
 * handleOutgoingTransferAccepted()
 * Displays that the remote peer accepted an outgoing transfer
 */
void MainWindow::handleOutgoingTransferAccepted(std::uint64_t transferId) {
    // The transfer ID will be used for more detailed transfer tracking later
    Q_UNUSED(transferId);

    // Inform the sender that the receiving peer accepted the offered file
    statusLabel_->setText("Transfer accepted");
}


/**
 * handleOutgoingTransferRejected()
 * Displays that the remote peer rejected an outgoing transfer
 */
void MainWindow::handleOutgoingTransferRejected(std::uint64_t transferId) {
    // The transfer ID will be used for more detailed transfer tracking tracking later
    Q_UNUSED(transferId);

    // Inform the sender that the receiving peer declined the offered file
    statusLabel_->setText("Transfer rejected");
}


/**
 * handleOutgoingTransferCompleted()
 * Displays that an outgoing file transfer finished successfully.
 */
void MainWindow::handleOutgoingTransferCompleted(std::uint64_t transferId) {
    // The transfer ID will be used for more detailed per-transfer UI later.
    Q_UNUSED(transferId);

    // Inform the sender that the file transfer finished successfully.
    statusLabel_->setText("Transfer completed");
}

/**
 * handleIncomingTransferCompleted()
 * Displays that an incoming file transfer finished successfully.
 */
void MainWindow::handleIncomingTransferCompleted(std::uint64_t transferId) {
    // The transfer ID will be used for more detailed per-transfer UI later.
    Q_UNUSED(transferId);

    // Inform the receiver that the complete file was received and written successfully.
    statusLabel_->setText("Transfer completed");
}


/**
 * handleIncomingTransferOffered()
 * Automatically accepts or displays a newly received file offer according to the current connection policy
 */
void MainWindow::handleIncomingTransferOffered(const TransferManager::IncomingTransfer& transfer) {
    // Automatically accept the offer when the user trusted incoming files for this connection.
    if(autoAcceptIncomingTransfers_) {
        if(!transferManager_.acceptIncomingTransfer(transfer.transferId)) {
            statusLabel_->setText("Failed to automatically accept incoming transfer");
            return;
        }

        // No manual decision is pending because this offer was accepted immediately.
        pendingIncomingTransferId_ = 0;
        pendingIncomingFileName_.clear();

        acceptTransferButton_->setEnabled(false);
        rejectTransferButton_->setEnabled(false);

        statusLabel_->setText(
            "Receiving: " +
            transfer.fileName +
            " (" +
            QString::number(transfer.fileSize) +
            " bytes)"
        );

        return;
    }

    // Manual mode remembers the transfer so the existing Accept and Reject buttons can act on it.
    pendingIncomingTransferId_ = transfer.transferId;
    pendingIncomingFileName_ = transfer.fileName;

    acceptTransferButton_->setEnabled(true);
    rejectTransferButton_->setEnabled(true);

    statusLabel_->setText(
        "Incoming offer: " +
        transfer.fileName +
        " (" +
        QString::number(transfer.fileSize) +
        " bytes)"
    );
}


/**
 * handleAcceptTransferClicked()
 * Accepts the currently displayed incoming transfer using the configured download directory.
 */
void MainWindow::handleAcceptTransferClicked() {
    // A zero identifier means there is no incoming transfer awaiting a decision.
    if(pendingIncomingTransferId_ == 0) {
        return;
    }

    // Accept the transfer using TransferManager's configured default download directory.
    if(!transferManager_.acceptIncomingTransfer(pendingIncomingTransferId_)) {
        statusLabel_->setText("Failed to accept incoming transfer");
        return;
    }

    // The offer is no longer awaiting an Accept or Reject decision.
    pendingIncomingTransferId_ = 0;
    pendingIncomingFileName_.clear();

    acceptTransferButton_->setEnabled(false);
    rejectTransferButton_->setEnabled(false);

    statusLabel_->setText("Transfer accepted");
}


/**
 * handleRejectTransferClicked()
 * Rejects the currently displayed incoming transfer
 */
void MainWindow::handleRejectTransferClicked() {
    // A zero identifier means there is no incoming transfer awaiting a decision
    if(pendingIncomingTransferId_ == 0) {
        return;
    }

    // Ask TransferManager to send the FileReject response to the original sender
    if(!transferManager_.rejectIncomingTransfer(pendingIncomingTransferId_)) {
        statusLabel_->setText("Failed to reject incoming transfer");
        return;
    }

    // The rejected offer no longer needs a pending user decision
    pendingIncomingTransferId_ = 0;
    pendingIncomingFileName_.clear();
    acceptTransferButton_->setEnabled(false);
    rejectTransferButton_->setEnabled(false);

    statusLabel_->setText("Transfer rejected");
}


/**
 * handleConnectionFailed()
 * Displays an outgoing connection failure in the interface
 */
void MainWindow::handleConnectionFailed(const QString& errorMessage) {
    statusLabel_->setText("Connection failed:" + errorMessage);
    connectButton_->setEnabled(true);
}


/**
 * handlePeerDiscovered()
 * Adds a newly discovered FileBridge instance to the nearby-devices list.
 */
void MainWindow::handlePeerDiscovered(const DiscoveredPeer& peer) {
    // Create one visible entry for the newly discovered FileBridge instance.
    QListWidgetItem *item = new QListWidgetItem(
        peer.deviceName + " - " + peer.address.toString(),
        nearbyDevicesList_
    );
    // Store the unique running-instance ID inside the list item itself.
    // Qt::UserRole is reserved for application-specific data that is not displayed to the user.
    item->setData(
        NEARBY_DEVICE_INSTANCE_ID_ROLE,     // Role: Hidden field reserved for this peer's running-instance ID
        peer.instanceId                     // Value: Unique ID generated by the remote FileBridge process
    );

    item->setData(
        NEARBY_DEVICE_ADDRESS_ROLE,         // Role: Hidden field reserved for this peer's network address.
        peer.address.toString()             // Value: IPv4 address learned from the discovery datagram.
    );

    item->setData(
        NEARBY_DEVICE_PORT_ROLE,            // Role: Hidden field reserved for this peer's TCP listening port.
        static_cast<unsigned int>(peer.port) // Value: Port advertised by the remote FileBridge instance.
    );
}


/**
 * handlePeerLost()
 * Removes a FileBridge instance that is no longer advertising itself.
 */
void MainWindow::handlePeerLost(const DiscoveredPeer& peer) {
    // Search every visible device entry for the unique instance ID of the expired peer.
    for(int index = 0; index < nearbyDevicesList_->count(); ++index) {
        QListWidgetItem *item = nearbyDevicesList_->item(index);
        // data(Qt::UserRole) retrieves the hidden instance ID stored when the item was created.
        if(item->data(NEARBY_DEVICE_INSTANCE_ID_ROLE).toString() != peer.instanceId) {
            continue;
        }
        // takeItem() removes the matching entry from the list but does not delete it.
        // Delete the returned item so the removed QListWidgetItem does not leak memory.
        delete nearbyDevicesList_->takeItem(index);
        return;
    }
}

/**
 * handleNearbyDeviceDoubleClicked()
 * Starts a connection request to the discovered device selected by the user.
 */
void MainWindow::handleNearbyDeviceDoubleClicked(QListWidgetItem *item) {
    // Double-clicking and pressing Connect on a selected device use the same connection path.
    connectToNearbyDevice(item);
}


/**
 * connectToNearbyDevice()
 * Starts an outgoing connection using connection data stored in a nearby-device item.
 */
void MainWindow::connectToNearbyDevice(QListWidgetItem *item) {
    // A null item means no discovered peer was supplied.
    if(item == nullptr) {
        return;
    }

    // Recover the peer address stored invisibly when discovery created this list item.
    const QHostAddress remoteAddress(
        item->data(NEARBY_DEVICE_ADDRESS_ROLE).toString()
    );

    bool validPort = false;

    // QVariant::toUInt() converts the hidden port value back to an unsigned integer.
    // validPort becomes false if the stored value cannot be converted successfully.
    const unsigned int remotePort =
        item->data(NEARBY_DEVICE_PORT_ROLE).toUInt(&validPort);

    // Reject corrupted or incomplete discovery data before reaching ConnectionManager.
    if(remoteAddress.isNull() || !validPort || remotePort == 0 || remotePort > 65535) {
        statusLabel_->setText("Invalid discovered peer");
        return;
    }

    statusLabel_->setText("Connecting...");
    connectButton_->setEnabled(false);

    connectionManager_.connectToPeer(
        remoteAddress,
        static_cast<std::uint16_t>(remotePort)
    );
}
