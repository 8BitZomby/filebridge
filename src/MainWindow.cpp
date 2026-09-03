#include "MainWindow.hpp"

#include "ConnectionManager.hpp"
#include "FileSizeFormatter.hpp"
#include "TransferWidget.hpp"
#include "NetworkInfo.hpp"
#include "PeerDiscovery.hpp"
#include "Protocol.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QIntValidator>
#include <QLabel>
#include <QProgressBar>
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
    outgoingQueueSending_(false),
    connectionInfoButton_(new QToolButton(this)),
    nearbyDevicesList_(new QListWidget(this)),
    connectButton_(new QPushButton("Connect", this)),
    disconnectButton_(new QPushButton("Disconnect", this)),
    manualConnectionButton_(new QPushButton("Manual", this)),
    transferModeButtonGroup_(new QButtonGroup(this)),
    sendModeButton_(new QPushButton("Send", this)),
    receiveModeButton_(new QPushButton("Receive", this)),
    transferStack_(new QStackedWidget(this)),
    outgoingFilesList_(new QListWidget(this)),
    addFilesButton_(new QPushButton("Add files...", this)),
    removeSelectedFilesButton_(new QPushButton("Remove Selected", this)),
    sendFilesButton_(new QPushButton("Send", this)),
    incomingFilesList_(new QListWidget(this)),
    acceptTransferButton_(new QPushButton("Accept All", this)),
    rejectTransferButton_(new QPushButton("Reject All", this)),
    removeSelectedIncomingButton_(new QPushButton("Remove Selected", this)),
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

        // Make the connection-information control a small, unobtrusive square
        connectionInfoButton_->setText("i");
        connectionInfoButton_->setAutoRaise(true);
        connectionInfoButton_->setFixedSize(20, 20);
        connectionInfoButton_->setToolTip("Show connection information");

        // Make nearby devices list small but scrollable if more devices show up
        nearbyDevicesList_->setMaximumHeight(50);

        // The two transfer-mode buttons behave as one exclusive Send/Receive selector
        sendModeButton_->setCheckable(true);
        receiveModeButton_->setCheckable(true);
        transferModeButtonGroup_->setExclusive(true);

        transferModeButtonGroup_->addButton(sendModeButton_);
        transferModeButtonGroup_->addButton(receiveModeButton_);



        // FileBridge starts on the outgoing-file page
        sendModeButton_->setChecked(true);

        // Keep the active page selector visually pressed so Send/Receive reads
        // as navigation rather than as a pair of transfer action buttons.
        sendModeButton_->setDown(true);

        // Allow several queued files to be selected and removed together
        outgoingFilesList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        outgoingFilesList_->setFixedHeight(100);

        // The incoming list will eventually support selecting seval offers at once
        incomingFilesList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        incomingFilesList_->setFixedHeight(100);

        // Queue controls are updated from the current connection, queue contents, and sending state
        updateOutgoingQueueControls();

        // Disconnect is only available after a peer completes the FileBridge handshake
        disconnectButton_->setEnabled(false);

        // Incoming transfer decisions are only shown when manual approval is actually required
        acceptTransferButton_->setEnabled(false);
        rejectTransferButton_->setEnabled(false);
        removeSelectedIncomingButton_->setEnabled(false);

        // Display local connection information when the user requests it.
        QObject::connect(
            connectionInfoButton_,                          // Sender: Button that exposes manual connection information.
            &QToolButton::clicked,                          // Signal: Emitted when the user clicks Connection Info.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleConnectionInfoClicked        // Slot: Opens the local address and port dialog.
        );

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

        // Open the fallback dialog for connecting directly by IP address and port.
        QObject::connect(
            manualConnectionButton_,                        // Sender: Manual connection button.
            &QPushButton::clicked,                          // Signal: Emitted when the user clicks Manual.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleManualConnectionClicked      // Slot: Opens the manual connection dialog.
        );

        // Start a connection request when the user double-clicks a discovered device.
        QObject::connect(
            nearbyDevicesList_,                             // Sender: List displaying discovered FileBridge devices.
            &QListWidget::itemDoubleClicked,                // Signal: Emitted when the user double-clicks one device entry.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleNearbyDeviceDoubleClicked    // Slot: Connects using the selected peer's stored address and port.
        );

                // Add one or more local files to the outgoing queue.
        QObject::connect(
            addFilesButton_,                                // Sender: Add Files button below the outgoing queue.
            &QPushButton::clicked,                          // Signal: Emitted when the user clicks Add Files.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleAddFilesClicked              // Slot: Opens the multi-file picker and queues selections.
        );

        // Remove files selected in the outgoing queue.
        QObject::connect(
            removeSelectedFilesButton_,                     // Sender: Remove Selected button below the outgoing queue.
            &QPushButton::clicked,                          // Signal: Emitted when the user clicks Remove Selected.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleRemoveSelectedFilesClicked   // Slot: Removes each selected queue entry.
        );

        // Start processing the queued files when the user clicks Send.
        QObject::connect(
            sendFilesButton_,                               // Sender: Send button below the outgoing queue.
            &QPushButton::clicked,                          // Signal: Emitted when the user clicks Send.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleSendFilesClicked             // Slot: Starts sequential processing of the outgoing queue.
        );

        // Display the outgoing transfer controls when the user selects Send mode.
        QObject::connect(
            sendModeButton_,                                // Sender: Send side of the transfer-mode selector.
            &QPushButton::clicked,                          // Signal: Emitted when the Send mode is selected.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleSendModeClicked              // Slot: Displays the outgoing transfer page.
        );

        // Display incoming offers when the user selects Receive mode.
        QObject::connect(
            receiveModeButton_,                             // Sender: Receive side of the transfer-mode selector.
            &QPushButton::clicked,                          // Signal: Emitted when the Receive mode is selected.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleReceiveModeClicked           // Slot: Displays the incoming transfer page.
        );

        // Accept the currently displayed incoming transfer when the user clicks Accept
        QObject::connect(
            acceptTransferButton_,                          // Sender: Accept button in the FileBridge window
            &QPushButton::clicked,                          // Signal: Emitted when the user clicks the button
            this,                                           // Receiver: This MainWindow instance
            &MainWindow::handleAcceptAllIncomingClicked // Slot: Accepts each selected incoming transfer
        );

        // Reject the currently displayed incoming transfer when the user clicks Reject
        QObject::connect(
            rejectTransferButton_,                          // Sender: Reject button in the FileBridge window
            &QPushButton::clicked,                          // Signal: Emitted when the user clicks the button
            this,                                           // Receiver: This MainWindow instance
            &MainWindow::handleRejectAllIncomingClicked // Slot: Rejects each selected incoming transfer
        );

        // Remove completed or rejected incoming-transfer rows selected by the user.
        QObject::connect(
            removeSelectedIncomingButton_,                  // Sender: Remove Selected button on the Receive page.
            &QPushButton::clicked,                          // Signal: Emitted when the user clicks Remove Selected.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleRemoveSelectedIncomingClicked // Slot: Removes eligible selected rows.
        );

        // Recalculate whether Remove Selected is available when the incoming selection changes.
        QObject::connect(
            incomingFilesList_,                             // Sender: Incoming-transfer list.
            &QListWidget::itemSelectionChanged,             // Signal: Emitted whenever the selected rows change.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::updateIncomingRemoveControl        // Slot: Updates Remove Selected availability.
        );

        // Ask the local user whether an incoming FileBridge connection should be trusted.
        QObject::connect(
            &connectionManager_,                                  // Sender: Manager validating incoming FileBridge peers.
            &ConnectionManager::connectionApprovalRequested,    // Signal: Emitted after an incoming peer completes its handshake
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

        // Report outgoing transfers that the remote peer could not complete.
        QObject::connect(
            &transferManager_,                              // Sender: Manager that owns outgoing transfer state.
            &TransferManager::outgoingTransferFailed,       // Signal: Emitted when the remote peer reports transfer failure.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleOutgoingTransferFailed       // Slot: Marks the matching sender row as Failed.
        );

        // Report incoming transfers that fail while writing the destination file.
        QObject::connect(
            &transferManager_,                              // Sender: Manager that owns incoming transfer state.
            &TransferManager::incomingTransferFailed,       // Signal: Emitted when the local receive operation fails.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleIncomingTransferFailed       // Slot: Marks the matching receiver row as Failed.
        );

        // Report when an incoming transfer has been completely received and written.
        QObject::connect(
            &transferManager_,                              // Sender: Manager that owns transfer state.
            &TransferManager::incomingTransferCompleted,    // Signal: Emitted after all expected file data is received.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleIncomingTransferCompleted    // Slot: Updates the receiver's completion status.
        );

        // Update the Receive-page progress display as incoming file bytes are written.
        QObject::connect(
            &transferManager_,                              // Sender: Manager that tracks incoming transfer byte counts.
            &TransferManager::incomingTransferProgress,     // Signal: Emitted after a chunk is written successfully.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleIncomingTransferProgress     // Slot: Updates the matching incoming transfer row.
        );

        // Update the Send-page progress display as outgoing file bytes are queued.
        QObject::connect(
            &transferManager_,                              // Sender: Manager that tracks outgoing transfer byte counts.
            &TransferManager::outgoingTransferProgress,     // Signal: Emitted after a file chunk is queued successfully.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleOutgoingTransferProgress     // Slot: Updates the matching outgoing transfer row.
        );

        // Mark the outgoing row as Sent once all file data and FileComplete are queued.
        QObject::connect(
            &transferManager_,                          // Sender: Manager tracking outgoing transfer lifecycle.
            &TransferManager::outgoingTransferSent,     // Signal: Emitted after all outgoing data is queued successfully.
            this,                                       // Receiver: This MainWindow instance.
            &MainWindow::handleOutgoingTransferSent     // Slot: Shows Sent while awaiting receiver confirmation.
        );

        // Use a central widget because QMainWindow reserves its outer structure for menus and toolbars
        QWidget *centralWidget = new QWidget(this);
        QVBoxLayout *layout = new QVBoxLayout(centralWidget);

        // Keep the section title and connection-info control on the same row
        QHBoxLayout *nearbyHeaderLayout = new QHBoxLayout();
        nearbyHeaderLayout->addWidget(new QLabel("Nearby devices:", centralWidget));
        nearbyHeaderLayout->addStretch();
        nearbyHeaderLayout->addWidget(connectionInfoButton_);
        layout->addLayout(nearbyHeaderLayout);
        layout->addWidget(nearbyDevicesList_);

        // Group all peer-connection actions together on one row
        QHBoxLayout *connectionButtonLayout = new QHBoxLayout();
        connectionButtonLayout->addWidget(connectButton_);
        connectionButtonLayout->addWidget(disconnectButton_);
        connectionButtonLayout->addWidget(manualConnectionButton_);
        
        layout->addLayout(connectionButtonLayout);

        // Keep Send and Receive together as a compact mode selector
        QHBoxLayout *transferModeLayout = new QHBoxLayout();
        transferModeLayout->addStretch();
        transferModeLayout->addWidget(sendModeButton_);
        transferModeLayout->addWidget(receiveModeButton_);
        transferModeLayout->addStretch();

        // Built the outgoing-file page shown while Send mode is selected
        layout->addLayout(transferModeLayout);

        // Build the outgoing-file page shown while Send mode is selected
        QWidget *sendPage = new QWidget(transferStack_);
        QVBoxLayout *sendPageLayout = new QVBoxLayout(sendPage);

        sendPageLayout->setContentsMargins(0, 0, 0, 0);
        sendPageLayout->addWidget(new QLabel("Files to send:", sendPage));
        sendPageLayout->addWidget(outgoingFilesList_);

        QHBoxLayout *transferButtonLayout = new QHBoxLayout();
        transferButtonLayout->addWidget(addFilesButton_);
        transferButtonLayout->addWidget(removeSelectedFilesButton_);
        transferButtonLayout->addWidget(sendFilesButton_);

        sendPageLayout->addLayout(transferButtonLayout);

        // Build the incoming-file page shown while Receive mode is selected
        QWidget *receivePage = new QWidget(transferStack_);
        QVBoxLayout *receivePageLayout = new QVBoxLayout(receivePage);

        receivePageLayout->setContentsMargins(0, 0, 0, 0);
        receivePageLayout->addWidget(new QLabel("Files to receive:", receivePage));
        receivePageLayout->addWidget(incomingFilesList_);

        // These existing buttons temporarily preserve the current one-offer approval path
        QHBoxLayout *incomingDecisionLayout = new QHBoxLayout();
        incomingDecisionLayout->addWidget(acceptTransferButton_);
        incomingDecisionLayout->addWidget(rejectTransferButton_);
        incomingDecisionLayout->addWidget(removeSelectedIncomingButton_);

        receivePageLayout->addLayout(incomingDecisionLayout);

        // QStackedWidget keeps both transfer pages in the same fixed region and displays
        // at one time
        transferStack_->addWidget(sendPage);
        transferStack_->addWidget(receivePage);
        transferStack_->setCurrentWidget(sendPage);

        layout->addWidget(transferStack_);

        // Keep the status heading and current status value on one line
        QHBoxLayout *statusLayout = new QHBoxLayout();

        statusLayout->addWidget(new QLabel("Status:", centralWidget));
        statusLayout->addWidget(statusLabel_, 1);

        layout->addLayout(statusLayout);

        setCentralWidget(centralWidget);

        setWindowTitle("FileBridge");
        resize(500, 350);
}


/**
 * handleConnectionInfoClicked()
 * Displays local connection information in a temporary popup window
 */
void MainWindow::handleConnectionInfoClicked() {
    const QHostAddress localAddress = NetworkInfo::preferredLocalIPv4Address();

    const QString addressText =
        localAddress.isNull()
            ? "Unavailable"
            : localAddress.toString();

    // Qt::Popup creates a lightweight temporary window that closes automatically
    // when the user clicks somewhere outside it.
    QFrame *popup = new QFrame(
        nullptr,                    // parent: A popup is positioned independently on the screen.
        Qt::Popup                   // flags: Temporary popup behavior with outside-click dismissal.
    );

    // Delete the temporary popup automatically when Qt closes it.
    popup->setAttribute(Qt::WA_DeleteOnClose);

    popup->setFrameShape(QFrame::StyledPanel);

    QLabel *titleLabel = new QLabel(
        "Connection Information",
        popup
    );

    // Make the popup heading visually distinct without creating another widget type.
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    QLabel *addressNameLabel = new QLabel(
        "Local IPv4 address:",
        popup
    );

    QLabel *addressValueLabel = new QLabel(
        addressText,
        popup
    );

    QLabel *portNameLabel = new QLabel(
        "Listening port:",
        popup
    );

    QLabel *portValueLabel = new QLabel(
        QString::number(connectionManager_.listeningPort()),
        popup
    );

    // A two-column grid keeps each value on the same line as its description.
    QGridLayout *infoLayout = new QGridLayout();

    infoLayout->addWidget(
        addressNameLabel,   // widget: Description of the address field.
        0,                  // row: First information row.
        0                   // column: Description column.
    );

    infoLayout->addWidget(
        addressValueLabel,  // widget: Actual local IPv4 address.
        0,                  // row: Same row as its description.
        1                   // column: Value column.
    );

    infoLayout->addWidget(
        portNameLabel,      // widget: Description of the listening-port field.
        1,                  // row: Second information row.
        0                   // column: Description column.
    );

    infoLayout->addWidget(
        portValueLabel,     // widget: Actual TCP listening port.
        1,                  // row: Same row as its description.
        1                   // column: Value column.
    );

    QVBoxLayout *popupLayout = new QVBoxLayout(popup);

    popupLayout->addWidget(titleLabel);
    popupLayout->addLayout(infoLayout);

    // Let the popup become only as large as its contents require.
    popup->adjustSize();

    // Position the popup immediately below the small connection-information control.
    const QPoint popupPosition = connectionInfoButton_->mapToGlobal(
        QPoint(
            connectionInfoButton_->width() - popup->width(),
            connectionInfoButton_->height()
        )
    );

    popup->move(popupPosition);
    popup->show();
}


/**
 * handleConnectClicked()
 * Starts an outgoing connection to the selected nearby FileBridge device
 */
void MainWindow::handleConnectClicked() {
    QListWidgetItem *selectedPeer = nearbyDevicesList_->currentItem();

    // Discovery-based connection requires the user to select one nearby device.
    if(selectedPeer == nullptr) {
        statusLabel_->setText("Select a nearby device");
        return;
    }

    connectToNearbyDevice(selectedPeer);
}


/**
 * handleManualConnectionClicked()
 * Displays a dialog for connecting directly to a peer by IP address and port
 */
void MainWindow::handleManualConnectionClicked() {
    QDialog manualDialog(this);

    manualDialog.setWindowTitle("Manual Connection");
    manualDialog.setModal(true);

    QLabel *addressLabel = new QLabel("IP address:", &manualDialog);
    QLabel *portLabel = new QLabel("Port:", &manualDialog);

    QLineEdit *addressEdit = new QLineEdit(&manualDialog);
    QLineEdit *portEdit = new QLineEdit(&manualDialog);

    addressEdit->setPlaceholderText("192.168.0.10");
    portEdit->setPlaceholderText("Port");

    // Restrict manual port input to the valid TCP port-number range.
    portEdit->setValidator(
        new QIntValidator(
            1,          // bottom: Lowest valid TCP port accepted by this dialog.
            65535,      // top: Highest possible TCP port.
            portEdit    // parent: The line edit owns and destroys its validator.
        )
    );

    QPushButton *cancelButton = new QPushButton("Cancel", &manualDialog);
    QPushButton *connectManualButton = new QPushButton("Connect", &manualDialog);

    connectManualButton->setDefault(true);

    // Cancel closes the dialog without starting any network operation.
    QObject::connect(
        cancelButton,                    // Sender: Cancel button in the manual connection dialog.
        &QPushButton::clicked,           // Signal: Emitted when the user clicks Cancel.
        &manualDialog,                   // Receiver: The temporary manual connection dialog.
        &QDialog::reject                 // Slot: Closes the dialog with a rejected result.
    );

    // Connect closes the dialog so its entered values can be validated below.
    QObject::connect(
        connectManualButton,             // Sender: Connect button in the manual connection dialog.
        &QPushButton::clicked,           // Signal: Emitted when the user clicks Connect.
        &manualDialog,                   // Receiver: The temporary manual connection dialog.
        &QDialog::accept                 // Slot: Closes the dialog with an accepted result.
    );

    // Keep labels and their corresponding input fields aligned in two columns.
    QGridLayout *inputLayout = new QGridLayout();

    inputLayout->addWidget(addressLabel, 0, 0);
    inputLayout->addWidget(addressEdit, 0, 1);
    inputLayout->addWidget(portLabel, 1, 0);
    inputLayout->addWidget(portEdit, 1, 1);

    QHBoxLayout *buttonLayout = new QHBoxLayout();

    buttonLayout->addStretch();
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(connectManualButton);

    QVBoxLayout *dialogLayout = new QVBoxLayout(&manualDialog);

    dialogLayout->addLayout(inputLayout);
    dialogLayout->addLayout(buttonLayout);

    // Closing or cancelling the dialog does not change the current connection state.
    if(manualDialog.exec() != QDialog::Accepted) {
        return;
    }

    const QHostAddress remoteAddress(addressEdit->text());

    bool validPort = false;
    const unsigned int remotePort = portEdit->text().toUInt(&validPort);

    // Reject malformed manual connection information before reaching ConnectionManager.
    if(remoteAddress.isNull() || !validPort || remotePort == 0 || remotePort > 65535) {
        statusLabel_->setText("Invalid IP address or port");
        return;
    }

    statusLabel_->setText("Connecting...");
    connectButton_->setEnabled(false);
    manualConnectionButton_->setEnabled(false);

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
 * handleSendModeClicked()
 * Displays the outgoing-file page in the transfer area
 */
void MainWindow::handleSendModeClicked() {
    // The Send page is the first page added to transferStack_ in the constructor.
    transferStack_->setCurrentIndex(0);

    // Keep only the visible page's selector visually pressed.
    sendModeButton_->setDown(true);
    receiveModeButton_->setDown(false);

    // Pending incoming offers require attention again while the Receive page is hidden.
    updateReceiveModeAttention();
}


/**
 * handleReceiveModeClicked()
 * Displays the incoming-file page in the transfer area
 */
void MainWindow::handleReceiveModeClicked() {
    // The Receive page is the second page added to transferStack_ in the constructor.
    transferStack_->setCurrentIndex(1);

    // Keep only the visible page's selector visually pressed.
    sendModeButton_->setDown(false);
    receiveModeButton_->setDown(true);

    // Viewing the Receive page clears the attention appearance but preserves the pending count.
    updateReceiveModeAttention();
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
    manualConnectionButton_->setEnabled(false);

    updateOutgoingQueueControls();
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
    manualConnectionButton_->setEnabled(true);
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

    // Preserve the visible queue, but stop processing until another peer is connected.
    outgoingQueueSending_ = false;

    // Transfer IDs belong to the disconnected peer and must not be reused if
    // these queued files are offered again after a later reconnection.
    for(int index = 0; index < outgoingFilesList_->count(); ++index) {
        QListWidgetItem *item = outgoingFilesList_->item(index);

        item->setData(
            OUTGOING_TRANSFER_ID_ROLE,
            QVariant()
        );
    }

    statusLabel_->setText("Disconnected");
    connectButton_->setEnabled(true);
    disconnectButton_->setEnabled(false);
    manualConnectionButton_->setEnabled(true);

    acceptTransferButton_->setEnabled(false);
    rejectTransferButton_->setEnabled(false);

    updateOutgoingQueueControls();
}


/**
 * handleAddFilesClicked()
 * Opens a multi-file picker and adds the selected files to the outgoing queue
 */
void MainWindow::handleAddFilesClicked() {
    // Native QFileDialog remains in use, but getOpenFileNames() allows several files
    // to be selected during the same picker operation.
    const QStringList filePaths = QFileDialog::getOpenFileNames(
        this,
        "Choose files to send"
    );

    // Closing the picker without selecting anything does not change the queue.
    if(filePaths.isEmpty()) {
        return;
    }

    for(const QString& filePath : filePaths) {
        const QFileInfo fileInfo(filePath);

        // Ignore paths that no longer identify a regular file.
        if(!fileInfo.exists() || !fileInfo.isFile()) {
            continue;
        }

        bool alreadyQueued = false;

        // Compare full paths rather than filenames because two different directories
        // may legitimately contain files with the same visible name.
        for(int index = 0; index < outgoingFilesList_->count(); ++index) {
            QListWidgetItem *existingItem = outgoingFilesList_->item(index);

            if(existingItem->data(OUTGOING_FILE_PATH_ROLE).toString() == fileInfo.absoluteFilePath()) {
                alreadyQueued = true;
                break;
            }
        }

        if(alreadyQueued) {
            continue;
        }

        // Create one persistent logical row for this outgoing file.
        QListWidgetItem *item = new QListWidgetItem(outgoingFilesList_);

        // Use the same transfer-row widget as the Receive page so sender and
        // receiver state presentation follow the same mechanism.
        TransferWidget *rowWidget = new TransferWidget(
            fileInfo.fileName(),
            static_cast<std::uint64_t>(fileInfo.size()),
            outgoingFilesList_
        );

        outgoingFilesList_->setItemWidget(item, rowWidget);
        item->setSizeHint(rowWidget->sizeHint());

        // Keep the full path out of the visible UI while preserving it for sending later.
        item->setData(
            OUTGOING_FILE_PATH_ROLE,       // Role: Hidden field reserved for the local file path.
            fileInfo.absoluteFilePath()    // Value: Absolute path required when FileBridge later opens the file.
        );

        // Store the metadata and initial lifecycle state used by the shared row widget.
        item->setData(
            OUTGOING_FILE_NAME_ROLE,
            fileInfo.fileName()
        );

        item->setData(
            OUTGOING_FILE_SIZE_ROLE,
            static_cast<qulonglong>(fileInfo.size())
        );

        item->setData(
            OUTGOING_BYTES_SENT_ROLE,
            static_cast<qulonglong>(0)
        );

        item->setData(
            OUTGOING_TRANSFER_STATE_ROLE,
            static_cast<int>(OutgoingTransferState::Pending)
        );

        updateOutgoingTransferDisplay(item);
    }
    updateOutgoingQueueControls();
}


/**
 * handleRemoveSelectedFilesClicked()
 * Removes the selected files from the outgoing queue before they are sent
 */
void MainWindow::handleRemoveSelectedFilesClicked() {
    const QList<QListWidgetItem *> selectedItems = outgoingFilesList_->selectedItems();

    // Each QListWidgetItem is owned by the list until takeItem() removes it.
    for(QListWidgetItem *item : selectedItems) {
        const int row = outgoingFilesList_->row(item);

        delete outgoingFilesList_->takeItem(row);
    }

    updateOutgoingQueueControls();
}


/**
 * handleSendFilesClicked()
 * Starts sending the queued files to the active peer one at a time
 */
void MainWindow::handleSendFilesClicked() {
    if(activePeer_ == nullptr) {
        statusLabel_->setText("No connected peer");
        return;
    }

    if(outgoingFilesList_->count() == 0 || outgoingQueueSending_) {
        return;
    }

    // Lock queue editing until all currently queued files have been processed
    outgoingQueueSending_ = true;
    updateOutgoingQueueControls();
    offerQueuedFiles();
}


/**
 * offerQueuedFiles()
 * Sends offers for unresolved queued files and finishes the batch when all transfers are resolved
 */
void MainWindow::offerQueuedFiles() {
    if(outgoingFilesList_->count() == 0) {
        outgoingQueueSending_ = false;
        statusLabel_->setText("Transfer completed");
        updateOutgoingQueueControls();
        return;
    }

    if(activePeer_ == nullptr) {
        outgoingQueueSending_ = false;
        statusLabel_->setText("Disconnected");
        updateOutgoingQueueControls();
        return;
    }

    int offeredCount = 0;
    int failedCount = 0;
    int unresolvedCount = 0;

    for(int index = 0; index < outgoingFilesList_->count(); ++index) {
        QListWidgetItem *item = outgoingFilesList_->item(index);

        const OutgoingTransferState state =
            static_cast<OutgoingTransferState>(
                item->data(OUTGOING_TRANSFER_STATE_ROLE).toInt()
            );

        // Final-state rows remain visible as history but no longer participate
        // in the active outgoing batch.
        if(state == OutgoingTransferState::Completed ||
            state == OutgoingTransferState::Rejected ||
            state == OutgoingTransferState::Failed) {
            
            continue;
        }

        ++unresolvedCount;

        // A valid transfer ID means this unresolved file has already been
        // offered and is waiting for another lifecycle event.
        if(item->data(OUTGOING_TRANSFER_ID_ROLE).isValid()) {
            continue;
        }

        const QString filePath =
            item->data(OUTGOING_FILE_PATH_ROLE).toString();

        // Revalidate the file because it may have changed since it was queued.
        const std::optional<std::uint64_t> transferId =
            transferManager_.offerFile(activePeer_, filePath);

        if(!transferId.has_value()) {
            ++failedCount;
            continue;
        }

        // Associate this row with the exact protocol transfer created for it so
        // later progress, completion, and rejection events can find the same row.
        item->setData(
            OUTGOING_TRANSFER_ID_ROLE,
            static_cast<qulonglong>(*transferId)
        );

        // The offer has been sent successfully, so this file is now waiting
        // for the remote peer's decision or its turn to begin transmission.
        item->setData(
            OUTGOING_TRANSFER_STATE_ROLE,
            static_cast<int>(OutgoingTransferState::Waiting)
        );

        updateOutgoingTransferDisplay(item);

        ++offeredCount;
    }

    // Visible history rows may remain even after every transfer in the current
    // batch has reached a final state.
    if(unresolvedCount == 0) {
        outgoingQueueSending_ = false;
        statusLabel_->setText("Transfer completed");
        updateOutgoingQueueControls();
        return;
    }

    // If every newly attempted offer failed, unlock the queue so the user can
    // correct or remove the affected files.
    if(offeredCount == 0 && failedCount > 0) {
        outgoingQueueSending_ = false;
        statusLabel_->setText("Failed to offer queued file(s)");
        updateOutgoingQueueControls();
        return;
    }

    if(failedCount > 0) {
        statusLabel_->setText(
            QString::number(offeredCount) +
            " file(s) offered, " +
            QString::number(failedCount) +
            " failed"
        );
        return;
    }

    if(offeredCount > 0) {
        statusLabel_->setText(
            QString::number(offeredCount) + " file(s) offered"
        );
    }
}


void MainWindow::updateOutgoingQueueControls() {
    const bool hasQueuedFiles = outgoingFilesList_->count() > 0;

    // Queue contents should not be modified while the current batch is being processed
    addFilesButton_->setEnabled(!outgoingQueueSending_);
    removeSelectedFilesButton_->setEnabled(hasQueuedFiles && !outgoingQueueSending_);

    // Sending requires both queued files and an approved active peer
    sendFilesButton_->setEnabled(
        hasQueuedFiles &&
        activePeer_ != nullptr &&
        !outgoingQueueSending_
    );
}


/**
 * updateOutgoingTransferDisplay()
 * Updates one outgoing transfer row to match its current state and progress
 */
void MainWindow::updateOutgoingTransferDisplay(QListWidgetItem *item) {
    if(item == nullptr) {
        return;
    }

    TransferWidget *rowWidget =
        qobject_cast<TransferWidget *>(
            outgoingFilesList_->itemWidget(item)
        );

    // Each outgoing transfer should have exactly one persistent custom row widget.
    if(rowWidget == nullptr) {
        return;
    }

    const OutgoingTransferState state =
        static_cast<OutgoingTransferState>(
            item->data(OUTGOING_TRANSFER_STATE_ROLE).toInt()
        );

    const std::uint64_t bytesSent =
        item->data(OUTGOING_BYTES_SENT_ROLE).toULongLong();

    const std::uint64_t fileSize =
        item->data(OUTGOING_FILE_SIZE_ROLE).toULongLong();

    switch(state) {
        case OutgoingTransferState::Pending:
            rowWidget->setStatus("Pending");
            rowWidget->hideProgress();
            break;

        case OutgoingTransferState::Waiting:
            rowWidget->setStatus("Waiting");
            rowWidget->hideProgress();
            break;

        case OutgoingTransferState::Sending:
            rowWidget->setStatus("Sending");
            rowWidget->setProgress(bytesSent, fileSize);
            break;

        case OutgoingTransferState::Sent:
            rowWidget->setStatus("Sent");
            rowWidget->hideProgress();
            break;

        case OutgoingTransferState::Completed:
            rowWidget->setStatus("Completed");
            rowWidget->hideProgress();
            break;

        case OutgoingTransferState::Rejected:
            rowWidget->setStatus("Rejected");
            rowWidget->hideProgress();
            break;

        case OutgoingTransferState::Failed:
            rowWidget->setStatus("Failed");
            rowWidget->hideProgress();
            break;
    }

    // Keep the QListWidget row height synchronized with the persistent custom widget.
    item->setSizeHint(rowWidget->sizeHint());
}


/**
 * updateReceiveModeAttention()
 * Updates the Receive selector text and attention styling from active incoming transfers
 */
void MainWindow::updateReceiveModeAttention() {
    int activeCount = 0;

    // Count every incoming transfer that still requires either a user decision
    // or additional transfer work before reaching a final state.
    for(int index = 0; index < incomingFilesList_->count(); ++index) {
        QListWidgetItem *item = incomingFilesList_->item(index);

        const IncomingTransferState state =
            static_cast<IncomingTransferState>(
                item->data(INCOMING_TRANSFER_STATE_ROLE).toInt()
            );

        if(
            state == IncomingTransferState::Pending ||
            state == IncomingTransferState::Waiting ||
            state == IncomingTransferState::Receiving
        ) {
            ++activeCount;
        }
    }

    // Keep active incoming work visible in the selector even after offers are accepted.
    receiveModeButton_->setText(
        activeCount > 0
            ? "Receive (" + QString::number(activeCount) + ")"
            : "Receive"
    );

    // Draw attention to active incoming work whenever the Receive page is hidden.
    const bool needsAttention =
        activeCount > 0 &&
        !receiveModeButton_->isChecked();

    // No icon is needed because the inverted button itself is the attention signal.
    receiveModeButton_->setIcon(QIcon());

    if(needsAttention) {
        // Use a high-contrast inverted appearance while preserving rounded corners
        // so the attention state still fits the surrounding macOS-style controls.
        receiveModeButton_->setStyleSheet(
            "QPushButton {"
            "background-color: black;"
            "color: white;"
            "border: 1px solid black;"
            "border-radius: 6px;"
            "padding: 4px 10px;"
            "}"
        );
    }
    else {
        // Clearing the stylesheet restores the normal platform button appearance.
        receiveModeButton_->setStyleSheet(QString());
    }
}


/**
 * handleOutgoingTransferOffered()
 * Displays metadata for a file successfully offered to the connected peer
 */
void MainWindow::handleOutgoingTransferOffered(std::uint64_t transferId, const QString& fileName, std::uint64_t fileSize) {
    // Row-to-transfer association is performed directly by the code that called offerFile().
    Q_UNUSED(transferId);

    statusLabel_->setText(
        "Offered: " +
        fileName +
        " (" +
        formatFileSize(fileSize) +
        ")"
    );
}


/**
 * handleOutgoingTransferAccepted()
 * Marks the matching outgoing transfer as actively sending
 */
void MainWindow::handleOutgoingTransferAccepted(std::uint64_t transferId) {
    for(int index = 0; index < outgoingFilesList_->count(); ++index) {
        QListWidgetItem *item = outgoingFilesList_->item(index);

        const std::uint64_t itemTransferId =
            item->data(OUTGOING_TRANSFER_ID_ROLE).toULongLong();

        if(itemTransferId != transferId) {
            continue;
        }

        // Acceptance resolves the remote decision, but transmission may still
        // wait behind another active outgoing transfer for this peer.
        item->setData(
            OUTGOING_TRANSFER_STATE_ROLE,
            static_cast<int>(OutgoingTransferState::Waiting)
        );

        // Show Waiting until the first actual outgoing progress update arrives.
        updateOutgoingTransferDisplay(item);
        break;
    }

    statusLabel_->setText("Transfer accepted");
}


/**
 * handleOutgoingTransferRejected()
 * Marks the matching outgoing transfer as rejected and keeps its history row visible
 */
void MainWindow::handleOutgoingTransferRejected(std::uint64_t transferId) {
    for(int index = 0; index < outgoingFilesList_->count(); ++index) {
        QListWidgetItem *item = outgoingFilesList_->item(index);

        const std::uint64_t itemTransferId =
            item->data(OUTGOING_TRANSFER_ID_ROLE).toULongLong();

        if(itemTransferId != transferId) {
            continue;
        }

        // Rejection resolves the transfer without removing its visible history row.
        item->setData(
            OUTGOING_TRANSFER_STATE_ROLE,
            static_cast<int>(OutgoingTransferState::Rejected)
        );

        // Show the final Rejected state using the same persistent row widget.
        updateOutgoingTransferDisplay(item);
        break;
    }

    statusLabel_->setText("Transfer rejected");

    // Re-evaluate the active batch while preserving this rejected history row.
    if(outgoingQueueSending_) {
        offerQueuedFiles();
    }
}


/**
 * handleOutgoingTransferCompleted()
 * Marks the matching outgoing transfer as completed and keeps its history row visible
 */
void MainWindow::handleOutgoingTransferCompleted(std::uint64_t transferId) {
    for(int index = 0; index < outgoingFilesList_->count(); ++index) {
        QListWidgetItem *item = outgoingFilesList_->item(index);

        const std::uint64_t itemTransferId =
            item->data(OUTGOING_TRANSFER_ID_ROLE).toULongLong();

        if(itemTransferId != transferId) {
            continue;
        }

        // Remote acknowledgement is the authoritative end of the outgoing transfer.
        item->setData(
            OUTGOING_TRANSFER_STATE_ROLE,
            static_cast<int>(OutgoingTransferState::Completed)
        );

        // Remove the active progress presentation and show the final Completed state.
        updateOutgoingTransferDisplay(item);
        break;
    }

    statusLabel_->setText("Transfer completed");

    // Re-evaluate the active batch while preserving this completed history row.
    if(outgoingQueueSending_) {
        offerQueuedFiles();
    }
}


/**
 * handleOutgoingTransferFailed()
 * Marks an outgoing transfer as failed and keeps its history row visible.
 */
void MainWindow::handleOutgoingTransferFailed(
    std::uint64_t transferId,
    const QString& errorMessage
) {
    for(int index = 0; index < outgoingFilesList_->count(); ++index) {
        QListWidgetItem *item = outgoingFilesList_->item(index);

        const std::uint64_t itemTransferId =
            item->data(OUTGOING_TRANSFER_ID_ROLE).toULongLong();

        if(itemTransferId != transferId) {
            continue;
        }

        item->setData(
            OUTGOING_TRANSFER_STATE_ROLE,
            static_cast<int>(OutgoingTransferState::Failed)
        );

        updateOutgoingTransferDisplay(item);
        break;
    }

    // Re-evaluate the active batch while preserving the failed history row.
    if(outgoingQueueSending_) {
        offerQueuedFiles();
    }

    // Keep the actual failure visible even if batch reevaluation reaches a final state.
    statusLabel_->setText("Transfer failed: " + errorMessage);
}


/**
 * handleIncomingTransferFailed()
 * Marks an incoming transfer as failed and allows its history row to be removed.
 */
void MainWindow::handleIncomingTransferFailed(
    std::uint64_t transferId,
    const QString& errorMessage
) {
    for(int index = 0; index < incomingFilesList_->count(); ++index) {
        QListWidgetItem *item = incomingFilesList_->item(index);

        const std::uint64_t itemTransferId =
            item->data(INCOMING_TRANSFER_ID_ROLE).toULongLong();

        if(itemTransferId != transferId) {
            continue;
        }

        item->setData(INCOMING_TRANSFER_PENDING_ROLE, false);
        item->setData(INCOMING_TRANSFER_REMOVABLE_ROLE, true);

        item->setData(
            INCOMING_TRANSFER_STATE_ROLE,
            static_cast<int>(IncomingTransferState::Failed)
        );

        updateIncomingTransferDisplay(item);
        break;
    }

    updateReceiveModeAttention();
    updateIncomingDecisionControls();
    updateIncomingRemoveControl();

    statusLabel_->setText("Transfer failed: " + errorMessage);
}


/**
 * handleIncomingTransferCompleted()
 * Marks an incoming transfer as completed and allows its list entry to be removed
 */
void MainWindow::handleIncomingTransferCompleted(std::uint64_t transferId) {
    // Locate the list row representing the completed protocol transfer.
    for(int index = 0; index < incomingFilesList_->count(); ++index) {
        QListWidgetItem *item = incomingFilesList_->item(index);

        const std::uint64_t itemTransferId =
            item->data(INCOMING_TRANSFER_ID_ROLE).toULongLong();

        if(itemTransferId != transferId) {
            continue;
        }

        // Completed transfers are no longer pending and may now be removed from the visible history.
        item->setData(INCOMING_TRANSFER_PENDING_ROLE, false);
        item->setData(INCOMING_TRANSFER_REMOVABLE_ROLE, true);

        // Record the final state so the Receive list can replace any progress
        // display with a simple Completed status.
        item->setData(
            INCOMING_TRANSFER_STATE_ROLE,
            static_cast<int>(IncomingTransferState::Completed)
        );

        // Rebuild the row so the progress bar is removed and the final
        // Completed state is shown as a simple one-line entry
        updateIncomingTransferDisplay(item);

        break;
    }

    updateReceiveModeAttention();
    updateIncomingRemoveControl();

    // Inform the receiver that the complete file was received and written successfully.
    statusLabel_->setText("Transfer completed");
}


/**
 * handleOutgoingTransferSent()
 * Marks the matching outgoing transfer as sent while awaiting receiver confirmation
 */
void MainWindow::handleOutgoingTransferSent(std::uint64_t transferId) {
    for(int index = 0; index < outgoingFilesList_->count(); ++index) {
        QListWidgetItem *item = outgoingFilesList_->item(index);

        const std::uint64_t itemTransferId =
            item->data(OUTGOING_TRANSFER_ID_ROLE).toULongLong();

        if(itemTransferId != transferId) {
            continue;
        }

        // All local file data has been queued, but the receiver has not yet
        // confirmed that the transfer was finalized successfully.
        item->setData(
            OUTGOING_TRANSFER_STATE_ROLE,
            static_cast<int>(OutgoingTransferState::Sent)
        );

        // Replace the progress bar with the intermediate Sent state.
        updateOutgoingTransferDisplay(item);
        break;
    }

    statusLabel_->setText("Transfer sent, waiting for confirmation");
}


/**
 * handleIncomingTransferProgress()
 * Updates the visible progress for an incoming file transfer
 */
void MainWindow::handleIncomingTransferProgress(
    std::uint64_t transferId,
    std::uint64_t bytesReceived,
    std::uint64_t fileSize) {
    
    for(int index = 0; index < incomingFilesList_->count(); ++index) {
        QListWidgetItem *item = incomingFilesList_->item(index);

        const std::uint64_t itemTransferId = item->data(INCOMING_TRANSFER_ID_ROLE).toULongLong();

        if(itemTransferId != transferId) {
            continue;
        }

        // Keep the latest transfer counters in the item so presentation remains
        // independent from TransferManager's internal transfer-state storage.
        item->setData(
            INCOMING_TRANSFER_BYTES_RECEIVED_ROLE,
            static_cast<qulonglong>(bytesReceived)
        );

        item->setData(
            INCOMING_TRANSFER_FILE_SIZE_ROLE,
            static_cast<qulonglong>(fileSize)
        );

        // Progress signals belong to an active receiving transfer.
        item->setData(
            INCOMING_TRANSFER_STATE_ROLE,
            static_cast<int>(IncomingTransferState::Receiving)
        );

        // Rebuild the matching row so the progress bar reflects the latest received-byte count
        updateIncomingTransferDisplay(item);
        break;
    }
}


/**
 * handleIncomingTransferOffered()
 * Adds a newly offered file to the incoming list and applies the current connection approval policy
 */
void MainWindow::handleIncomingTransferOffered(const TransferManager::IncomingTransfer& transfer) {
    // Create the QListWidgetItem as the persistent logical row for this transfer.
    QListWidgetItem *item = new QListWidgetItem(incomingFilesList_);

    // Give the logical row one persistent custom widget that will change its
    // status and progress display throughout the transfer lifecycle.
    TransferWidget *rowWidget = new TransferWidget(
        transfer.fileName,
        transfer.fileSize,
        incomingFilesList_
    );

    incomingFilesList_->setItemWidget(item, rowWidget);
    item->setSizeHint(rowWidget->sizeHint());

    // Store the protocol transfer ID invisibly in the row so later Accept/Reject
    // actions can identify the exact TransferManager transfer represented by it.
    item->setData(
        INCOMING_TRANSFER_ID_ROLE,                         // Role: Hidden field reserved for the incoming transfer ID.
        static_cast<qulonglong>(transfer.transferId)       // Value: Sender-assigned identifier used by transfer messages.
    );

    // Keep the transfer metadata available independently of the row's visible presentation.
    item->setData(
        INCOMING_TRANSFER_FILE_NAME_ROLE,
        transfer.fileName
    );

    item->setData(
        INCOMING_TRANSFER_FILE_SIZE_ROLE,
        static_cast<qulonglong>(transfer.fileSize)
    );

    // No file bytes have been written yet when the offer first arrives.
    item->setData(
        INCOMING_TRANSFER_BYTES_RECEIVED_ROLE,
        static_cast<qulonglong>(0)
    );

    // Manual incoming offer begin in the pending state until the receiver accepts or rejects them
    item->setData(
        INCOMING_TRANSFER_PENDING_ROLE,
        !autoAcceptIncomingTransfers_
    );

    // A new incoming transfer must remain visible until it completes or is rejected.
    item->setData(
        INCOMING_TRANSFER_REMOVABLE_ROLE,
        false
    );

    // A manually reviewed offer remains Pending until the receiver accepts or rejects it.
    item->setData(
        INCOMING_TRANSFER_STATE_ROLE,
        static_cast<int>(IncomingTransferState::Pending)
    );

    updateIncomingTransferDisplay(item);

    // Refresh the Receive selector before processing the connection's approval policy
    updateReceiveModeAttention();

    // Automatically accept the offer when the user trusted incoming files for this connection.
    if(autoAcceptIncomingTransfers_) {
        if(!transferManager_.acceptIncomingTransfer(transfer.transferId)) {
            statusLabel_->setText("Failed to automatically accept incoming transfer");
            return;
        }

        // Acceptance resolves the decision, but file data may still wait behind
        // another active transfer from this peer.
        item->setData(
            INCOMING_TRANSFER_STATE_ROLE,
            static_cast<int>(IncomingTransferState::Waiting)
        );

        // Automatically accepted incoming files are active work, so show their
        // transfer page immediately instead of leaving the user on the Send page.
        receiveModeButton_->setChecked(true);
        handleReceiveModeClicked();

        statusLabel_->setText(
            "Receiving: " +
            transfer.fileName +
            " (" +
            formatFileSize(transfer.fileSize) +
            ")"
        );

        return;
    }

    // Manual mode leaves this row pending until the user explicitly selects and resolves it.
    updateIncomingDecisionControls();

    statusLabel_->setText(
        "Incoming offer: " +
        transfer.fileName +
        " (" +
        formatFileSize(transfer.fileSize) +
        ")"
    );
}


/**
 * handleAcceptAllIncomingClicked()
 * Accepts every pending incoming transfer in the Receive list
 */
void MainWindow::handleAcceptAllIncomingClicked() {
    int acceptedCount = 0;

    // Visit every visible incoming transfer because Accept All is independent
    // of the current QListWidget selection.
    for(int index = 0; index < incomingFilesList_->count(); ++index) {
        QListWidgetItem *item = incomingFilesList_->item(index);

        // Resolved transfers must not be accepted again.
        if(!item->data(INCOMING_TRANSFER_PENDING_ROLE).toBool()) {
            continue;
        }

        const std::uint64_t transferId =
            item->data(INCOMING_TRANSFER_ID_ROLE).toULongLong();

        // Leave this row pending if TransferManager cannot send its acceptance.
        if(!transferManager_.acceptIncomingTransfer(transferId)) {
            continue;
        }

        // Acceptance resolves the approval decision, but the row remains
        // non-removable until the actual file transfer completes.
        item->setData(INCOMING_TRANSFER_PENDING_ROLE, false);
        item->setData(INCOMING_TRANSFER_REMOVABLE_ROLE, false);

        // Acceptance resolves the decision, but file data may still be queued
        // behind another active transfer from this peer.
        item->setData(
            INCOMING_TRANSFER_STATE_ROLE,
            static_cast<int>(IncomingTransferState::Waiting)
        );

        // Show Waiting until the first incoming progress update confirms data is arriving.
        updateIncomingTransferDisplay(item);

        ++acceptedCount;
    }

    updateReceiveModeAttention();
    updateIncomingDecisionControls();
    updateIncomingRemoveControl();

    if(acceptedCount == 0) {
        statusLabel_->setText("No pending incoming transfers were accepted");
        return;
    }

    statusLabel_->setText(
        acceptedCount == 1
            ? "Transfer accepted"
            : QString::number(acceptedCount) + " transfers accepted"
    );
}


/**
 * handleRejectAllIncomingClicked()
 * Rejects every pending incoming transfer in the Receive list
 */
void MainWindow::handleRejectAllIncomingClicked() {
    int rejectedCount = 0;

    // Visit every visible incoming transfer because Reject All is independent
    // of the current QListWidget selection.
    for(int index = 0; index < incomingFilesList_->count(); ++index) {
        QListWidgetItem *item = incomingFilesList_->item(index);

        // Resolved transfers must not be rejected again.
        if(!item->data(INCOMING_TRANSFER_PENDING_ROLE).toBool()) {
            continue;
        }

        const std::uint64_t transferId =
            item->data(INCOMING_TRANSFER_ID_ROLE).toULongLong();

        // Leave this row pending if TransferManager cannot send its rejection.
        if(!transferManager_.rejectIncomingTransfer(transferId)) {
            continue;
        }

        // Rejection completely resolves the transfer, so its history row may
        // now be removed whenever the user chooses.
        item->setData(INCOMING_TRANSFER_PENDING_ROLE, false);
        item->setData(INCOMING_TRANSFER_REMOVABLE_ROLE, true);

        // Record the resolved state so the Receive list can display Rejected.
        item->setData(
            INCOMING_TRANSFER_STATE_ROLE,
            static_cast<int>(IncomingTransferState::Rejected)
        );

        // Rebuild the row so the resolved transfer immediately displays Rejected
        // and no longer shows any receiving progress presentation
        updateIncomingTransferDisplay(item);

        ++rejectedCount;
    }

    updateReceiveModeAttention();
    updateIncomingDecisionControls();
    updateIncomingRemoveControl();

    if(rejectedCount == 0) {
        statusLabel_->setText("No pending incoming transfers were rejected");
        return;
    }

    statusLabel_->setText(
        rejectedCount == 1
            ? "Transfer rejected"
            : QString::number(rejectedCount) + " transfers rejected"
    );
}


/**
 * handleRemoveSelectedIncomingClicked()
 * Rejects unresolved selected transfers and removes selected entries from the incoming list
 */
void MainWindow::handleRemoveSelectedIncomingClicked() {
    const QList<QListWidgetItem *> selectedItems = incomingFilesList_->selectedItems();

    for(QListWidgetItem *item : selectedItems) {
        // A pending transfer must be explicitly rejected before its visible row
        // is removed so the sender is not left waiting for a decision.
        if(item->data(INCOMING_TRANSFER_PENDING_ROLE).toBool()) {
            const std::uint64_t transferId =
                item->data(INCOMING_TRANSFER_ID_ROLE).toULongLong();

            // Keep the row visible if FileBridge cannot successfully send the rejection.
            if(!transferManager_.rejectIncomingTransfer(transferId)) {
                continue;
            }
        }
        else if(!item->data(INCOMING_TRANSFER_REMOVABLE_ROLE).toBool()) {
            // An accepted transfer that is still receiving must remain visible
            // until its transfer lifecycle reaches a removable state.
            continue;
        }

        const int row = incomingFilesList_->row(item);

        // takeItem() transfers ownership away from QListWidget, so delete the
        // returned item after removing it from the visible list.
        delete incomingFilesList_->takeItem(row);
    }

    updateReceiveModeAttention();
    updateIncomingDecisionControls();
    updateIncomingRemoveControl();
}


/**
 * updateIncomingRemoveControl()
 * Enables Remove Selected when at least one selected incoming row can be removed
 */
void MainWindow::updateIncomingRemoveControl() {
    bool hasRemovableSelection = false;

    const QList<QListWidgetItem *> selectedItems = incomingFilesList_->selectedItems();

    for(QListWidgetItem *item : selectedItems) {
        const bool isPending =
            item->data(INCOMING_TRANSFER_PENDING_ROLE).toBool();

        const bool isRemovable =
            item->data(INCOMING_TRANSFER_REMOVABLE_ROLE).toBool();

        // Pending rows can be removed by first rejecting them. Already-resolved
        // removable rows can be deleted immediately from the visible history.
        if(isPending || isRemovable) {
            hasRemovableSelection = true;
            break;
        }
    }

    removeSelectedIncomingButton_->setEnabled(hasRemovableSelection);
}


/**
 * updateIncomingDecisionControls()
 * Enables Accept All and Reject All when at least one incoming transfer is still pending
 */
void MainWindow::updateIncomingDecisionControls() {
    bool hasPendingTransfer = false;

    // Accept All and Reject All operate on every pending row, so button
    // availability depends on list contents rather than the current selection.
    for(int index = 0; index < incomingFilesList_->count(); ++index) {
        QListWidgetItem *item = incomingFilesList_->item(index);

        if(item->data(INCOMING_TRANSFER_PENDING_ROLE).toBool()) {
            hasPendingTransfer = true;
            break;
        }
    }

    acceptTransferButton_->setEnabled(hasPendingTransfer);
    rejectTransferButton_->setEnabled(hasPendingTransfer);
}


/**
 * updateIncomingTransferDisplay()
 * Updates one incoming transfer row to match its current state and progress
 */
void MainWindow::updateIncomingTransferDisplay(QListWidgetItem *item) {
    if(item == nullptr) {
        return;
    }

    TransferWidget *rowWidget = qobject_cast<TransferWidget *>(incomingFilesList_->itemWidget(item));

    // Each incoming transfer should have exactly one persistent custom row widget.
    if(rowWidget == nullptr) {
        return;
    }

    const IncomingTransferState state =
        static_cast<IncomingTransferState>(
            item->data(INCOMING_TRANSFER_STATE_ROLE).toInt()
        );

    const std::uint64_t bytesReceived =
        item->data(INCOMING_TRANSFER_BYTES_RECEIVED_ROLE).toULongLong();

    const std::uint64_t fileSize =
        item->data(INCOMING_TRANSFER_FILE_SIZE_ROLE).toULongLong();

    switch(state) {
        case IncomingTransferState::Pending:
            rowWidget->setStatus("Pending");
            rowWidget->hideProgress();
            break;

        case IncomingTransferState::Waiting:
            rowWidget->setStatus("Waiting");
            rowWidget->hideProgress();
            break;

        case IncomingTransferState::Receiving:
            rowWidget->setStatus("Receiving");
            rowWidget->setProgress(bytesReceived, fileSize);
            break;

        case IncomingTransferState::Completed:
            rowWidget->setStatus("Completed");
            rowWidget->hideProgress();
            break;

        case IncomingTransferState::Rejected:
            rowWidget->setStatus("Rejected");
            rowWidget->hideProgress();
            break;

        case IncomingTransferState::Failed:
            rowWidget->setStatus("Failed");
            rowWidget->hideProgress();
            break;
    }

    // Keep the QListWidget row height synchronized with the persistent custom widget.
    item->setSizeHint(rowWidget->sizeHint());
}


/**
 * handleOutgoingTransferProgress()
 * Updates the matching outgoing transfer row as additional file data is sent
 */
void MainWindow::handleOutgoingTransferProgress(
    std::uint64_t transferId,
    std::uint64_t bytesSent,
    std::uint64_t fileSize
) {
    for(int index = 0; index < outgoingFilesList_->count(); ++index) {
        QListWidgetItem *item = outgoingFilesList_->item(index);

        const std::uint64_t itemTransferId =
            item->data(OUTGOING_TRANSFER_ID_ROLE).toULongLong();

        if(itemTransferId != transferId) {
            continue;
        }

        // Keep the latest sender-side byte count stored with the logical transfer row.
        item->setData(
            OUTGOING_BYTES_SENT_ROLE,
            static_cast<qulonglong>(bytesSent)
        );

        item->setData(
            OUTGOING_FILE_SIZE_ROLE,
            static_cast<qulonglong>(fileSize)
        );

        // Progress updates belong to an actively sending transfer.
        item->setData(
            OUTGOING_TRANSFER_STATE_ROLE,
            static_cast<int>(OutgoingTransferState::Sending)
        );

        // Rebuild the matching row so the progress bar reflects the latest sent-byte count.
        updateOutgoingTransferDisplay(item);
        break;
    }
}


/**
 * handleConnectionFailed()
 * Displays an outgoing connection failure in the interface
 */
void MainWindow::handleConnectionFailed(const QString& errorMessage) {
    statusLabel_->setText("Connection failed: " + errorMessage);
    connectButton_->setEnabled(true);
    manualConnectionButton_->setEnabled(true);
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
