#include "MainWindow.hpp"
#include "ConnectionManager.hpp"
#include "NetworkInfo.hpp"
#include "Protocol.hpp"

#include <QFileDialog>
#include <QHostAddress>
#include <QStandardPaths>
#include <QIntValidator>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdint>


/**
 * Constructor: MainWindow()
 * Constructs the FileBridge window and initializes its connection controls
 */
MainWindow::MainWindow(QWidget *parent) : 
    QMainWindow(parent),
    transferManager_(&connectionManager_, this),
    activePeer_(nullptr),
    pendingIncomingTransferId_(0),
    localAddressLabel_(new QLabel(this)),
    localPortLabel_(new QLabel(this)),
    remoteAddressEdit_(new QLineEdit(this)),
    remotePortEdit_(new QLineEdit(this)),
    connectButton_(new QPushButton("Connect", this)),
    chooseFileButton_(new QPushButton("Choose File", this)),
    acceptTransferButton_(new QPushButton("Accept", this)),
    rejectTransferButton_(new QPushButton("Reject", this)),
    statusLabel_(new QLabel("Ready", this)) {

        // Start listening for incoming FileBridge connections before displaying connections
        if(!connectionManager_.start()) {
            statusLabel_->setText("Failed to start local listener");
            connectButton_->setEnabled(false);
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

        // File offers are only valid after a peer completes the FileBridge handshake
        chooseFileButton_->setEnabled(false);

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

        // Update the interface only after a peer completes a valid FileBridge handshake
        QObject::connect(   
            &connectionManager_,                            // Manager that emits peer readiness
            &ConnectionManager::peerReady,                  // Signal emitted after handshake validation succeeds
            this,                                           // Window that displays the connection state
            &MainWindow::handlePeerReady                    // Slot that marks the peer as ready
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

        layout->addWidget(new QLabel("Connect to peer:", centralWidget));
        layout->addWidget(remoteAddressEdit_);
        layout->addWidget(remotePortEdit_);
        layout->addWidget(connectButton_);

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
 * handlePeerReady()
 * Updates the interface after a peer completes the FileBridge handshake
 */
void MainWindow::handlePeerReady(PeerConnection *connection) {
    // Remember which validated peer may receive file-transfer messages
    activePeer_ = connection;

    statusLabel_->setText("Connected");
    connectButton_->setEnabled(false);

    // File selection becomes available only after the peer is ready for transfer messages
    chooseFileButton_->setEnabled(true);
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
 * Displays a pending incoming transfer reported by the transfer manager.
 */
void MainWindow::handleIncomingTransferOffered(const TransferManager::IncomingTransfer& transfer) {
    // Remember which transfer the Accept and Reject buttons should operate on
    pendingIncomingTransferId_ = transfer.transferId;
    
    // Remember the offered filename so the save dialog can suggest it later
    pendingIncomingFileName_ = transfer.fileName;

    // Allow the user to respond to the newly received file offer
    acceptTransferButton_->setEnabled(true);
    rejectTransferButton_->setEnabled(true);

    // Temporarily display the incoming transfer metadata in the status label.
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
