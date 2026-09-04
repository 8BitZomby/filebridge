#include "MainWindow.hpp"

#include "ConnectionManager.hpp"
#include "FileSizeFormatter.hpp"
#include "TransferWidget.hpp"
#include "NetworkInfo.hpp"
#include "PeerDiscovery.hpp"
#include "Protocol.hpp"

#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHostAddress>
#include <QIntValidator>
#include <QLabel>
#include <QProgressBar>
#include <QPainter>
#include <QPixmap>
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
    connectionOrigin_(ConnectionOrigin::None),
    outgoingQueueSending_(false),
    connectionInfoButton_(new QToolButton(this)),
    connectionStateIcon_(new QLabel(this)),
    nearbyDevicesList_(new QListWidget(this)),
    connectionModeButtonGroup_(new QButtonGroup(this)),
    devicesModeButton_(new QPushButton("Devices", this)),
    directModeButton_(new QPushButton("Direct", this)),
    connectionStack_(new QStackedWidget(this)),
    directInputWidget_(new QWidget(this)),
    directAddressEdit_(new QLineEdit(this)),
    directPortEdit_(new QLineEdit(this)),
    directStatusLabel_(new QLabel(this)),
    connectButton_(new QPushButton("Connect", this)),
    disconnectButton_(new QPushButton("Disconnect", this)),
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

        // Style the connection-information control as a compact circular utility
        // button so it remains secondary without looking like an unstyled widget.
        connectionInfoButton_->setText("i");
        connectionInfoButton_->setAutoRaise(false);
        connectionInfoButton_->setFixedSize(24, 24);
        connectionInfoButton_->setToolTip("Show connection information");

        // Keep the Direct-only control in the header layout so the centered
        // mode selector never shifts when switching connection modes.
        connectionInfoButton_->setEnabled(false);

        connectionInfoButton_->setStyleSheet(
            "QToolButton {"
            "background-color: #f4f4f5;"
            "color: #6b6f75;"
            "border: 1px solid #b8bbc0;"
            "border-radius: 12px;"
            "font-size: 14px;"
            "font-weight: 600;"
            "}"
            "QToolButton:hover {"
            "background-color: #ffffff;"
            "}"
            "QToolButton:pressed {"
            "background-color: #d7d8da;"
            "}"
        );

        // FileBridge starts in Devices mode, so hide the control visually while
        // preserving its fixed layout footprint.
        connectionInfoButton_->setStyleSheet(
            "QToolButton {"
            "background-color: transparent;"
            "color: transparent;"
            "border: 1px solid transparent;"
            "border-radius: 12px;"
            "}"
        );

        // Make nearby devices list small but scrollable if more devices show up
        nearbyDevicesList_->setMaximumHeight(70);

        // Initialize the shared indicator using the same drawing path that
        // later connection lifecycle changes will use.
        updateConnectionStateIcon(false);

        // The Devices and Direct buttons behave as one exclusive connection-mode selector.
        devicesModeButton_->setCheckable(true);
        directModeButton_->setCheckable(true);
        connectionModeButtonGroup_->setExclusive(true);

        connectionModeButtonGroup_->addButton(devicesModeButton_);
        connectionModeButtonGroup_->addButton(directModeButton_);

        // Use the largest natural selector size for every mode button so the
        // navigation controls remain identical regardless of their label text.
        QSize modeButtonSize = devicesModeButton_->sizeHint();

        modeButtonSize = modeButtonSize.expandedTo(directModeButton_->sizeHint());
        modeButtonSize = modeButtonSize.expandedTo(sendModeButton_->sizeHint());
        modeButtonSize = modeButtonSize.expandedTo(receiveModeButton_->sizeHint());

        devicesModeButton_->setFixedSize(modeButtonSize);
        directModeButton_->setFixedSize(modeButtonSize);
        sendModeButton_->setFixedSize(modeButtonSize);
        receiveModeButton_->setFixedSize(modeButtonSize);

        // FileBridge starts on the automatically discovered Devices page.
        devicesModeButton_->setChecked(true);

        // Keep the active connection selector visually pressed so Devices/Direct reads
        // as navigation rather than as a pair of connection action buttons.
        devicesModeButton_->setDown(true);

        // Guide direct connections toward an IPv4 address while still validating the
        // final value with QHostAddress before any network operation begins.
        directAddressEdit_->setPlaceholderText("192.168.0.10");

        // Restrict direct port input to the complete valid TCP port-number range.
        directPortEdit_->setPlaceholderText("Port");
        directPortEdit_->setValidator(
            new QIntValidator(
                1,                  // bottom: Lowest TCP port accepted by FileBridge.
                65535,              // top: Highest possible TCP port.
                directPortEdit_     // parent: The line edit owns and destroys its validator.
            )
        );

        // Reserve one line for Direct connection state at all times so the
        // Connect and Disconnect buttons never move when status text appears.
        directStatusLabel_->setMinimumHeight(directStatusLabel_->fontMetrics().height());
        directStatusLabel_->clear();

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

        // Display the automatically discovered-device page when Devices is selected.
        QObject::connect(
            devicesModeButton_,                             // Sender: Devices connection-mode selector.
            &QPushButton::clicked,                          // Signal: Emitted when the user selects Devices.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleDevicesModeClicked           // Slot: Displays the Devices connection page.
        );

        // Display the direct endpoint page when Direct is selected.
        QObject::connect(
            directModeButton_,                              // Sender: Direct connection-mode selector.
            &QPushButton::clicked,                          // Signal: Emitted when the user selects Direct.
            this,                                           // Receiver: This MainWindow instance.
            &MainWindow::handleDirectModeClicked            // Slot: Displays the Direct connection page.
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

        // Use a central widget because QMainWindow reserves its outer structure for menus and toolbars.
        QWidget *centralWidget = new QWidget(this);
        QVBoxLayout *layout = new QVBoxLayout(centralWidget);

        // Keep the two major areas visually separate while using QGroupBox titles
        // so each section heading remains integrated with its surrounding border.
        QGroupBox *connectionGroup = new QGroupBox("Connection", centralWidget);
        QGroupBox *transferGroup = new QGroupBox("Transfers", centralWidget);

        // Add a restrained shadow beneath each primary panel so the lighter
        // work areas sit slightly above the surrounding background.
        QGraphicsDropShadowEffect *connectionShadow =
            new QGraphicsDropShadowEffect(connectionGroup);

        connectionShadow->setBlurRadius(20.0);
        connectionShadow->setOffset(0.0, 4.0);
        connectionShadow->setColor(QColor(0, 0, 0, 55));

        QGraphicsDropShadowEffect *transferShadow =
            new QGraphicsDropShadowEffect(transferGroup);

        transferShadow->setBlurRadius(20.0);
        transferShadow->setOffset(0.0, 4.0);
        transferShadow->setColor(QColor(0, 0, 0, 55));

        // QWidget takes ownership of the graphics effect assigned to it.
        connectionGroup->setGraphicsEffect(connectionShadow);
        transferGroup->setGraphicsEffect(transferShadow);

        // Give clickable push buttons a smaller, softer shadow than the main
        // panels so controls appear slightly raised without competing with them.
        const auto addButtonShadow = [](QWidget *button) {
            QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(button);
            shadow->setBlurRadius(10.0);
            shadow->setOffset(0.0, 2.0);
            shadow->setColor(QColor(0, 0, 0, 35));

            // The button takes ownership of its assigned graphics effect.
            button->setGraphicsEffect(shadow);
        };

        addButtonShadow(connectionInfoButton_);
        addButtonShadow(devicesModeButton_);
        addButtonShadow(directModeButton_);
        addButtonShadow(connectButton_);
        addButtonShadow(disconnectButton_);

        addButtonShadow(sendModeButton_);
        addButtonShadow(receiveModeButton_);

        // Give the four mode selectors a stronger shadow so their raised state
        // remains visible across both their selected and unselected appearances.
        const auto addModeButtonShadow = [](QWidget *button) {
            QGraphicsDropShadowEffect *shadow =
                new QGraphicsDropShadowEffect(button);

            shadow->setBlurRadius(16.0);
            shadow->setOffset(0.0, 3.0);
            shadow->setColor(QColor(0, 0, 0, 65));

            // Assigning this effect replaces the softer general button shadow.
            button->setGraphicsEffect(shadow);
        };

        addModeButtonShadow(devicesModeButton_);
        addModeButtonShadow(directModeButton_);
        addModeButtonShadow(sendModeButton_);
        addModeButtonShadow(receiveModeButton_);

        addButtonShadow(addFilesButton_);
        addButtonShadow(removeSelectedFilesButton_);
        addButtonShadow(sendFilesButton_);

        addButtonShadow(acceptTransferButton_);
        addButtonShadow(rejectTransferButton_);
        addButtonShadow(removeSelectedIncomingButton_);

        // Give list and direct-entry surfaces a very soft shadow so the white
        // content areas sit slightly above the surrounding panel background.
        const auto addContentShadow = [](QWidget *widget) {
            QGraphicsDropShadowEffect *shadow =
                new QGraphicsDropShadowEffect(widget);

            shadow->setBlurRadius(12.0);
            shadow->setOffset(0.0, 2.0);
            shadow->setColor(QColor(0, 0, 0, 28));

            // The widget takes ownership of its assigned graphics effect.
            widget->setGraphicsEffect(shadow);
        };

        addContentShadow(outgoingFilesList_);
        addContentShadow(incomingFilesList_);
        addContentShadow(directAddressEdit_);
        addContentShadow(directPortEdit_);

        QVBoxLayout *connectionGroupLayout = new QVBoxLayout(connectionGroup);
        QVBoxLayout *transferGroupLayout = new QVBoxLayout(transferGroup);

        // Use a dark neutral background so the lighter Connection and Transfers
        // panels stand apart clearly from the main application surface.
        centralWidget->setObjectName("fileBridgeCentral");
        centralWidget->setStyleSheet(
            "QWidget#fileBridgeCentral {"
            "background-color: #d8dadd;"
            "}"
        );

        // Give both primary work areas a light gray surface with a stronger
        // heading and restrained border while preserving the native system font.
        const QString panelStyle =
            "QGroupBox {"
            "background-color: #e7e8ea;"
            "border: 1px solid #aeb2b8;"
            "border-radius: 10px;"
            "margin-top: 16px;"
            "font-size: 20px;"
            "font-weight: 600;"
            "color: #202124;"
            "}"
            "QGroupBox::title {"
            "subcontrol-origin: margin;"
            "subcontrol-position: top left;"
            "left: 14px;"
            "padding: 0 8px;"
            "color: #55585d;"
            "}"
            "QGroupBox QLabel, "
            "QGroupBox QPushButton, "
            "QGroupBox QListWidget, "
            "QGroupBox QLineEdit {"
            "font-family: \"Avenir Next\";"
            "}";

        connectionGroup->setStyleSheet(panelStyle);
        transferGroup->setStyleSheet(panelStyle);

        // Keep file and device lists as bright rounded content surfaces inside
        // the surrounding gray panels, with a subtle shared selection state.
        const QString listStyle =
            "QListWidget {"
            "background-color: #ffffff;"
            "color: #202124;"
            "border: 1px solid #b8bbc0;"
            "border-radius: 8px;"
            "}"
            "QListWidget::item:selected {"
            "background-color: #d7d8da;"
            "color: #202124;"
            "}";

        nearbyDevicesList_->setStyleSheet(listStyle);
        outgoingFilesList_->setStyleSheet(listStyle);
        incomingFilesList_->setStyleSheet(listStyle);

        // Use consistent spacing inside each section so related controls read
        // as one unit rather than as unrelated rows.
        connectionGroupLayout->setSpacing(10);
        transferGroupLayout->setSpacing(8);

        // Build a balanced three-column connection header. The invisible widget
        // on the left matches the information button on the right so the
        // Devices/Direct selector remains mathematically centered.
        QGridLayout *connectionHeaderLayout = new QGridLayout();

        QWidget *connectionHeaderSpacer = new QWidget(connectionGroup);
        connectionHeaderSpacer->setFixedSize(connectionInfoButton_->size());

        QHBoxLayout *connectionModeLayout = new QHBoxLayout();
        connectionModeLayout->setContentsMargins(0, 0, 0, 0);

        // Present Devices and Direct as two halves of one segmented selector
        // rather than as independent action buttons.
        QWidget *connectionModeSurface = new QWidget(connectionGroup);
        QHBoxLayout *connectionModeSurfaceLayout = new QHBoxLayout(connectionModeSurface);

        connectionModeSurfaceLayout->setContentsMargins(0, 0, 0, 0);
        connectionModeSurfaceLayout->setSpacing(0);

        // The left segment keeps only the outer-left corners rounded so the
        // adjoining edge meets the Direct segment without a visual gap.
        devicesModeButton_->setStyleSheet(
            "QPushButton {"
            "background-color: #d7d8da;"
            "border: 1px solid #b8bbc0;"
            "border-right: none;"
            "border-top-left-radius: 8px;"
            "border-bottom-left-radius: 8px;"
            "border-top-right-radius: 0;"
            "border-bottom-right-radius: 0;"
            "}"
            "QPushButton:checked {"
            "background-color: #969696;"
            "color: #ffffff;"
            "}"
        );

        // The right segment mirrors the left one so the pair reads as a
        // single control with one continuous outside edge.
        directModeButton_->setStyleSheet(
            "QPushButton {"
            "background-color: #d7d8da;"
            "border: 1px solid #b8bbc0;"
            "border-top-left-radius: 0;"
            "border-bottom-left-radius: 0;"
            "border-top-right-radius: 8px;"
            "border-bottom-right-radius: 8px;"
            "}"
            "QPushButton:checked {"
            "background-color: #969696;"
            "color: #ffffff;"
            "}"
        );

        // Remove the two independent selector shadows because a segmented
        // control should cast one shadow around its complete outside shape.
        devicesModeButton_->setGraphicsEffect(nullptr);
        directModeButton_->setGraphicsEffect(nullptr);

        connectionModeSurfaceLayout->addWidget(devicesModeButton_);
        connectionModeSurfaceLayout->addWidget(directModeButton_);

        QGraphicsDropShadowEffect *connectionModeShadow =
            new QGraphicsDropShadowEffect(connectionModeSurface);

        connectionModeShadow->setBlurRadius(16.0);
        connectionModeShadow->setOffset(0.0, 3.0);
        connectionModeShadow->setColor(QColor(0, 0, 0, 65));

        // The selector surface owns the shared shadow effect.
        connectionModeSurface->setGraphicsEffect(connectionModeShadow);

        connectionModeLayout->addStretch();
        connectionModeLayout->addWidget(connectionModeSurface);
        connectionModeLayout->addStretch();

        connectionHeaderLayout->addWidget(
            connectionHeaderSpacer,
            0,
            0,
            Qt::AlignLeft
        );

        connectionHeaderLayout->addLayout(
            connectionModeLayout,
            0,
            1
        );

        connectionHeaderLayout->addWidget(
            connectionInfoButton_,
            0,
            2,
            Qt::AlignRight
        );

        connectionHeaderLayout->setColumnStretch(1, 1);

        connectionGroupLayout->addLayout(connectionHeaderLayout);

        // Use one consistent secondary-heading style for labels that identify
        // content fields inside both primary FileBridge sections.
        const QString contentLabelStyle =
            "font-size: 15px;"
            "font-weight: 600;"
            "color: #55585d;";

        // Build the page used for devices FileBridge discovers automatically
        // on the local network.
        QWidget *devicesPage = new QWidget(connectionStack_);
        QVBoxLayout *devicesPageLayout = new QVBoxLayout(devicesPage);

        devicesPageLayout->setContentsMargins(0, 0, 0, 0);
        
        // Keep a small, deliberate gap between the Devices heading and its
        // content surface instead of allowing them to visually crowd together.
        devicesPageLayout->setSpacing(4);

        QLabel *availableDevicesLabel = new QLabel("Available devices:", devicesPage);

        availableDevicesLabel->setStyleSheet(contentLabelStyle);
        devicesPageLayout->addWidget(availableDevicesLabel);

        // Give the discovered-device surface transparent breathing room so its
        // drop shadow can extend beyond the white list without being clipped.
        QWidget *nearbyDevicesContainer = new QWidget(devicesPage);
        QVBoxLayout *nearbyDevicesContainerLayout = new QVBoxLayout(nearbyDevicesContainer);

        nearbyDevicesContainerLayout->setContentsMargins(8, 0, 8, 8);

        // Keep the actual shadow attached to an inner surface while the outer
        // container reserves the space needed for the blur to remain visible.
        QWidget *nearbyDevicesSurface = new QWidget(nearbyDevicesContainer);
        QGridLayout *nearbyDevicesSurfaceLayout = new QGridLayout(nearbyDevicesSurface);

        nearbyDevicesSurfaceLayout->setContentsMargins(0, 0, 0, 0);
        nearbyDevicesSurfaceLayout->addWidget(nearbyDevicesList_, 0, 0);

        // Explain the empty state directly inside the list surface rather than
        // leaving a blank white area that could look unfinished or broken.
        QLabel *noDevicesLabel = new QLabel("No devices found", nearbyDevicesSurface);

        noDevicesLabel->setAlignment(Qt::AlignCenter);
        noDevicesLabel->setStyleSheet(
            "color: #8a8e94;"
        );

        // The empty-state label is purely informational and must not block
        // mouse interaction with the discovered-device list beneath it.
        noDevicesLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        nearbyDevicesSurfaceLayout->addWidget(noDevicesLabel, 0, 0);

        // Keep the empty-state message synchronized with the actual number of
        // discovered devices without inserting artificial QListWidgetItems.
        const auto updateDevicesEmptyState = [this, noDevicesLabel]() {
            noDevicesLabel->setVisible(nearbyDevicesList_->count() == 0);
        };

        QObject::connect(
            nearbyDevicesList_->model(),                    // Sender: Model backing the discovered-device list.
            &QAbstractItemModel::rowsInserted,               // Signal: Emitted when a discovered device is added.
            this,                                            // Receiver: This MainWindow instance.
            updateDevicesEmptyState                          // Slot: Hides the empty-state message when necessary.
        );

        QObject::connect(
            nearbyDevicesList_->model(),                    // Sender: Model backing the discovered-device list.
            &QAbstractItemModel::rowsRemoved,                // Signal: Emitted when a discovered device is removed.
            this,                                            // Receiver: This MainWindow instance.
            updateDevicesEmptyState                          // Slot: Restores the empty-state message when necessary.
        );

        updateDevicesEmptyState();

        QGraphicsDropShadowEffect *nearbyDevicesShadow = new QGraphicsDropShadowEffect(nearbyDevicesSurface);

        nearbyDevicesShadow->setBlurRadius(12.0);
        nearbyDevicesShadow->setOffset(0.0, 2.0);
        nearbyDevicesShadow->setColor(QColor(0, 0, 0, 28));

        // The inner surface owns the effect while the outer container provides
        // transparent space around all four sides for the shadow to render.
        nearbyDevicesSurface->setGraphicsEffect(nearbyDevicesShadow);

        nearbyDevicesContainerLayout->addWidget(nearbyDevicesSurface);
        devicesPageLayout->addWidget(nearbyDevicesContainer);

        // Build the page used when the user wants to connect directly to a known
        // IP address and TCP port instead of relying on LAN discovery.
        QWidget *directPage = new QWidget(connectionStack_);
        QVBoxLayout *directPageLayout = new QVBoxLayout(directPage);

        directPageLayout->setContentsMargins(0, 0, 0, 0);

        // Use the extra height imposed by the taller Devices page above the
        // Direct form so the endpoint fields sit closer to the shared
        // connection-state indicator and connection controls below.
        directPageLayout->addStretch(1);

        // Keep the complete Direct endpoint form inside one persistent widget.
        QGridLayout *directInputLayout = new QGridLayout(directInputWidget_);

        directInputLayout->setContentsMargins(0, 0, 8, 0);

        // Spread the two endpoint rows across approximately the same vertical
        // range as the enlarged Available devices surface on the Devices page.
        directInputLayout->setVerticalSpacing(16);

        QLabel *directAddressLabel = new QLabel("IP address:", directInputWidget_);
        directAddressLabel->setStyleSheet(contentLabelStyle);

        directInputLayout->addWidget(
            directAddressLabel,
            0,
            0
        );

        directInputLayout->addWidget(
            directAddressEdit_,
            0,
            1
        );

        QLabel *directPortLabel = new QLabel("Port:", directInputWidget_);
        directPortLabel->setStyleSheet(contentLabelStyle);

        directInputLayout->addWidget(
            directPortLabel,
            1,
            0
        );

        directInputLayout->addWidget(
            directPortEdit_,
            1,
            1
        );

        // Keep the endpoint form at its natural height so the preceding stretch
        // can move the complete form toward the shared connection controls below.
        directPageLayout->addWidget(
            directInputWidget_,
            0,
            Qt::AlignBottom
        );

        // Raise the complete endpoint form slightly so the bottom of the Port
        // field aligns with the enlarged Available devices surface.
        directPageLayout->addSpacing(15);

        // Both connection methods occupy the same region inside the Connection group.
        connectionStack_->addWidget(devicesPage);
        connectionStack_->addWidget(directPage);
        connectionStack_->setCurrentWidget(devicesPage);

        connectionGroupLayout->addWidget(connectionStack_);

        // Keep the connection-state indicator in the shared connection area so
        // Devices and Direct modes report status from the same visual location.
        connectionGroupLayout->addWidget(
            connectionStateIcon_,
            0,
            Qt::AlignHCenter
        );

        // Match the width proportions of the three transfer actions below:
        // each connection button occupies one third of the usable row width,
        // with equal outer margins centering the pair.
        QHBoxLayout *connectionActionLayout = new QHBoxLayout();

        connectionActionLayout->addStretch(1);
        connectionActionLayout->addWidget(connectButton_, 2);
        connectionActionLayout->addWidget(disconnectButton_, 2);
        connectionActionLayout->addStretch(1);

        connectionGroupLayout->addLayout(connectionActionLayout);

        // Present the transfer selector as one joined control so Send/Receive
        // visually matches the Devices/Direct selector above.
        QHBoxLayout *transferModeLayout = new QHBoxLayout();
        transferModeLayout->setContentsMargins(0, 0, 0, 0);

        // Present Send and Receive as two halves of one segmented selector so
        // transfer mode matches the connection-mode control above.
        QWidget *transferModeSurface = new QWidget(transferGroup);
        QHBoxLayout *transferModeSurfaceLayout = new QHBoxLayout(transferModeSurface);

        transferModeSurfaceLayout->setContentsMargins(0, 0, 0, 0);
        transferModeSurfaceLayout->setSpacing(0);

        // The left segment keeps only the outer-left corners rounded so the
        // adjoining edge meets the Receive segment without a visual gap.
        sendModeButton_->setStyleSheet(
            "QPushButton {"
            "background-color: #d7d8da;"
            "border: 1px solid #b8bbc0;"
            "border-right: none;"
            "border-top-left-radius: 8px;"
            "border-bottom-left-radius: 8px;"
            "border-top-right-radius: 0;"
            "border-bottom-right-radius: 0;"
            "}"
            "QPushButton:checked {"
            "background-color: #969696;"
            "color: #ffffff;"
            "}"
        );

        // The right segment mirrors the left one so the pair reads as a
        // single control with one continuous outside edge.
        receiveModeButton_->setStyleSheet(
            "QPushButton {"
            "background-color: #d7d8da;"
            "border: 1px solid #b8bbc0;"
            "border-top-left-radius: 0;"
            "border-bottom-left-radius: 0;"
            "border-top-right-radius: 8px;"
            "border-bottom-right-radius: 8px;"
            "}"
            "QPushButton:checked {"
            "background-color: #969696;"
            "color: #ffffff;"
            "}"
        );

        // Remove the two independent selector shadows because the segmented
        // control uses one shared shadow around the complete outside shape.
        sendModeButton_->setGraphicsEffect(nullptr);
        receiveModeButton_->setGraphicsEffect(nullptr);

        transferModeSurfaceLayout->addWidget(sendModeButton_);
        transferModeSurfaceLayout->addWidget(receiveModeButton_);

        QGraphicsDropShadowEffect *transferModeShadow =
            new QGraphicsDropShadowEffect(transferModeSurface);

        transferModeShadow->setBlurRadius(16.0);
        transferModeShadow->setOffset(0.0, 3.0);
        transferModeShadow->setColor(QColor(0, 0, 0, 65));

        // The selector surface owns the shared shadow effect.
        transferModeSurface->setGraphicsEffect(transferModeShadow);

        transferModeLayout->addStretch();
        transferModeLayout->addWidget(transferModeSurface);
        transferModeLayout->addStretch();

        transferGroupLayout->addLayout(transferModeLayout);

        // Build the outgoing-file page shown while Send mode is selected.
        QWidget *sendPage = new QWidget(transferStack_);
        QVBoxLayout *sendPageLayout = new QVBoxLayout(sendPage);

        sendPageLayout->setContentsMargins(0, 0, 0, 0);

        // Keep the heading and file surface together so their spacing matches
        // the Available devices heading and list without affecting action buttons.
        QVBoxLayout *sendContentLayout = new QVBoxLayout();

        sendContentLayout->setContentsMargins(0, 0, 0, 0);
        sendContentLayout->setSpacing(0);

        QLabel *filesToSendLabel = new QLabel("Files to send:", sendPage);
        filesToSendLabel->setStyleSheet(contentLabelStyle);

        sendContentLayout->addWidget(filesToSendLabel);

        // Match the visual gap between Available devices and its list surface.
        sendContentLayout->addSpacing(6);

        // Overlay an informational empty-state message without inserting a
        // placeholder QListWidgetItem that would interfere with transfer logic.
        QWidget *outgoingFilesSurface = new QWidget(sendPage);
        QGridLayout *outgoingFilesSurfaceLayout = new QGridLayout(outgoingFilesSurface);

        outgoingFilesSurfaceLayout->setContentsMargins(0, 0, 0, 0);
        outgoingFilesSurfaceLayout->addWidget(outgoingFilesList_, 0, 0);

        QLabel *noOutgoingFilesLabel = new QLabel("No files added", outgoingFilesSurface);

        noOutgoingFilesLabel->setAlignment(Qt::AlignCenter);
        noOutgoingFilesLabel->setStyleSheet(
            "color: #8a8e94;"
        );

        // The empty-state label must not intercept mouse input intended for
        // the outgoing-file list beneath it.
        noOutgoingFilesLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        outgoingFilesSurfaceLayout->addWidget(noOutgoingFilesLabel, 0, 0);

        // Keep the empty-state message synchronized with the actual outgoing
        // queue without changing QListWidget's real item count.
        const auto updateOutgoingEmptyState = [this, noOutgoingFilesLabel]() {
            noOutgoingFilesLabel->setVisible(outgoingFilesList_->count() == 0);
        };

        QObject::connect(
            outgoingFilesList_->model(),                    // Sender: Model backing the outgoing-file list.
            &QAbstractItemModel::rowsInserted,               // Signal: Emitted when files are added to the queue.
            this,                                            // Receiver: This MainWindow instance.
            updateOutgoingEmptyState                         // Slot: Hides the empty-state message when necessary.
        );

        QObject::connect(
            outgoingFilesList_->model(),                    // Sender: Model backing the outgoing-file list.
            &QAbstractItemModel::rowsRemoved,                // Signal: Emitted when files are removed from the queue.
            this,                                            // Receiver: This MainWindow instance.
            updateOutgoingEmptyState                         // Slot: Restores the message when the queue becomes empty.
        );

        updateOutgoingEmptyState();

        sendContentLayout->addWidget(outgoingFilesSurface);
        sendPageLayout->addLayout(sendContentLayout);

        QHBoxLayout *transferButtonLayout = new QHBoxLayout();

        transferButtonLayout->addWidget(addFilesButton_, 1);
        transferButtonLayout->addWidget(removeSelectedFilesButton_, 1);
        transferButtonLayout->addWidget(sendFilesButton_, 1);

        sendPageLayout->addLayout(transferButtonLayout);

        // Build the incoming-file page shown while Receive mode is selected.
        QWidget *receivePage = new QWidget(transferStack_);
        QVBoxLayout *receivePageLayout = new QVBoxLayout(receivePage);

        receivePageLayout->setContentsMargins(0, 0, 0, 0);

        // Keep the heading and file surface together so Receive uses the same
        // heading-to-list spacing as Devices and Send without moving its buttons.
        QVBoxLayout *receiveContentLayout = new QVBoxLayout();

        receiveContentLayout->setContentsMargins(0, 0, 0, 0);
        receiveContentLayout->setSpacing(0);

        QLabel *filesToReceiveLabel = new QLabel("Files to receive:", receivePage);
        filesToReceiveLabel->setStyleSheet(contentLabelStyle);

        receiveContentLayout->addWidget(filesToReceiveLabel);

        // Match the visual gap between Available devices and its list surface.
        receiveContentLayout->addSpacing(6);

        // Overlay an informational empty-state message without inserting a
        // placeholder QListWidgetItem that would interfere with transfer logic.
        QWidget *incomingFilesSurface = new QWidget(receivePage);
        QGridLayout *incomingFilesSurfaceLayout = new QGridLayout(incomingFilesSurface);

        incomingFilesSurfaceLayout->setContentsMargins(0, 0, 0, 0);
        incomingFilesSurfaceLayout->addWidget(incomingFilesList_, 0, 0);

        QLabel *noIncomingFilesLabel = new QLabel("No files received", incomingFilesSurface);

        noIncomingFilesLabel->setAlignment(Qt::AlignCenter);
        noIncomingFilesLabel->setStyleSheet(
            "color: #8a8e94;"
        );

        // The empty-state label must not intercept mouse input intended for
        // the incoming-file list beneath it.
        noIncomingFilesLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        incomingFilesSurfaceLayout->addWidget(noIncomingFilesLabel, 0, 0);

        // Keep the empty-state message synchronized with the actual incoming
        // transfer list without changing QListWidget's real item count.
        const auto updateIncomingEmptyState = [this, noIncomingFilesLabel]() {
            noIncomingFilesLabel->setVisible(incomingFilesList_->count() == 0);
        };

        QObject::connect(
            incomingFilesList_->model(),                    // Sender: Model backing the incoming-file list.
            &QAbstractItemModel::rowsInserted,               // Signal: Emitted when incoming transfers are added.
            this,                                            // Receiver: This MainWindow instance.
            updateIncomingEmptyState                         // Slot: Hides the empty-state message when necessary.
        );

        QObject::connect(
            incomingFilesList_->model(),                    // Sender: Model backing the incoming-file list.
            &QAbstractItemModel::rowsRemoved,                // Signal: Emitted when incoming transfers are removed.
            this,                                            // Receiver: This MainWindow instance.
            updateIncomingEmptyState                         // Slot: Restores the message when the list becomes empty.
        );

        updateIncomingEmptyState();

        receiveContentLayout->addWidget(incomingFilesSurface);
        receivePageLayout->addLayout(receiveContentLayout);

       // These existing buttons preserve explicit approval of incoming transfers.
        QHBoxLayout *incomingDecisionLayout = new QHBoxLayout();

        incomingDecisionLayout->addWidget(acceptTransferButton_);
        incomingDecisionLayout->addWidget(rejectTransferButton_);
        incomingDecisionLayout->addWidget(removeSelectedIncomingButton_);

        receivePageLayout->addLayout(incomingDecisionLayout);

        // QStackedWidget keeps both transfer pages in one fixed region while only
        // the currently selected Send or Receive page is visible.
        transferStack_->addWidget(sendPage);
        transferStack_->addWidget(receivePage);
        transferStack_->setCurrentWidget(sendPage);

        transferGroupLayout->addWidget(transferStack_);

        // Keep a modest separation between the two primary work areas.
        layout->setSpacing(12);
        layout->addWidget(connectionGroup);
        layout->addWidget(transferGroup);

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

    // Keep the native popup window transparent and frameless so its rectangular
    // background cannot appear behind the rounded information surface.
    QWidget *popup = new QWidget(
        nullptr,                                        // parent: The popup is positioned independently on screen.
        Qt::Popup | Qt::FramelessWindowHint             // flags: Temporary frameless popup with outside-click dismissal.
    );

    // Delete the temporary popup automatically when Qt closes it.
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setAttribute(Qt::WA_TranslucentBackground);

    // Give the popup window transparent margins so the rounded surface and its
    // shadow can render without being clipped by the top-level window bounds.
    QVBoxLayout *windowLayout = new QVBoxLayout(popup);
    windowLayout->setContentsMargins(8, 8, 8, 8);

    // Draw the visible popup as a separate opaque frame so translucency applies
    // only to the surrounding top-level window, not to the information itself.
    QFrame *popupSurface = new QFrame(popup);
    popupSurface->setFrameShape(QFrame::NoFrame);
    popupSurface->setStyleSheet(
        "QFrame {"
        "background-color: #ffffff;"
        "border: 1px solid #b8bbc0;"
        "border-radius: 10px;"
        "}"
        "QLabel {"
        "background-color: transparent;"
        "border: none;"
        "}"
    );

    QVBoxLayout *popupLayout = new QVBoxLayout(popupSurface);
    popupLayout->setContentsMargins(20, 16, 20, 16);

    QLabel *titleLabel = new QLabel(
        "Connection Information",
        popupSurface
    );

    // Make the popup heading visually distinct from the connection details.
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    QLabel *addressNameLabel = new QLabel(
        "Local IPv4 address:",
        popupSurface
    );

    QLabel *addressValueLabel = new QLabel(
        addressText,
        popupSurface
    );

    QLabel *portNameLabel = new QLabel(
        "Listening port:",
        popupSurface
    );

    QLabel *portValueLabel = new QLabel(
        QString::number(connectionManager_.listeningPort()),
        popupSurface
    );

    // A two-column grid keeps each value aligned with its corresponding label.
    QGridLayout *infoLayout = new QGridLayout();
    infoLayout->setHorizontalSpacing(18);
    infoLayout->setVerticalSpacing(8);

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

    popupLayout->addWidget(titleLabel);
    popupLayout->addLayout(infoLayout);

    windowLayout->addWidget(popupSurface);

    // Let the complete popup window become only as large as its contents and
    // transparent shadow margins require.
    popup->adjustSize();

    // Position the popup below the information button while accounting for the
    // transparent margin surrounding the visible rounded surface.
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
 * Starts an outgoing connection using the currently selected connection mode.
 */
void MainWindow::handleConnectClicked() {
    // Devices mode connects using endpoint information stored with the selected
    // peer discovered automatically on the local network.
    if(devicesModeButton_->isChecked()) {
        QListWidgetItem *selectedPeer = nearbyDevicesList_->currentItem();

        if(selectedPeer == nullptr) {
            statusLabel_->setText("Select a nearby device");
            return;
        }

        connectToNearbyDevice(selectedPeer);
        return;
    }

    // Direct mode validates the endpoint entered explicitly by the user.
    const QHostAddress remoteAddress(directAddressEdit_->text());

    bool validPort = false;
    const unsigned int remotePort = directPortEdit_->text().toUInt(&validPort);

    // QIntValidator constrains normal keyboard input, but this explicit validation
    // keeps malformed endpoint information from reaching ConnectionManager.
    if(remoteAddress.isNull() ||
        !validPort ||
        remotePort == 0 ||
        remotePort > 65535) {

        statusLabel_->setText("Invalid IP address or port");
        return;
    }

    const QString endpoint =
        remoteAddress.toString() +
        ":" +
        QString::number(remotePort);

    // Record the actual connection source independently from whichever page the
    // user later chooses to view while the connection remains active.
    connectionOrigin_ = ConnectionOrigin::Direct;

    directStatusLabel_->setText("Connecting to " + endpoint + "...");
    directStatusLabel_->setVisible(true);

    statusLabel_->setText("Connecting...");

    // Prevent another connection attempt while ConnectionManager is establishing
    // this one, while allowing Disconnect to cancel the pending attempt.
    connectButton_->setEnabled(false);
    disconnectButton_->setEnabled(true);
    directAddressEdit_->setEnabled(false);
    directPortEdit_->setEnabled(false);

    connectionManager_.connectToPeer(
        remoteAddress,
        static_cast<std::uint16_t>(remotePort)
    );
}


/**
 * handleDevicesModeClicked()
 * Displays the automatically discovered-device connection page.
 */
void MainWindow::handleDevicesModeClicked() {
    // The Devices page is the first page added to connectionStack_ in the constructor.
    connectionStack_->setCurrentIndex(0);

    // Keep the header geometry unchanged while hiding the Direct-only control.
    connectionInfoButton_->setEnabled(false);
    connectionInfoButton_->setStyleSheet(
        "QToolButton {"
        "background-color: transparent;"
        "color: transparent;"
        "border: 1px solid transparent;"
        "border-radius: 12px;"
        "}"
    );

    // Keep only the active selector visually pressed so the controls read as navigation.
    devicesModeButton_->setDown(true);
    directModeButton_->setDown(false);
}


/**
 * handleDirectModeClicked()
 * Displays the direct IP-address and port connection page.
 */
void MainWindow::handleDirectModeClicked() {
    // The Direct page is the second page added to connectionStack_ in the constructor.
    connectionStack_->setCurrentIndex(1);

    // Restore the Direct-only connection-information control without changing
    // the header layout dimensions.
    connectionInfoButton_->setEnabled(true);
    connectionInfoButton_->setStyleSheet(
        "QToolButton {"
        "background-color: #f4f4f5;"
        "color: #6b6f75;"
        "border: 1px solid #b8bbc0;"
        "border-radius: 12px;"
        "font-size: 14px;"
        "font-weight: 600;"
        "}"
        "QToolButton:hover {"
        "background-color: #ffffff;"
        "}"
        "QToolButton:pressed {"
        "background-color: #d7d8da;"
        "}"
    );

    // Keep only the active selector visually pressed so the controls read as navigation.
    devicesModeButton_->setDown(false);
    directModeButton_->setDown(true);
}


/**
 * handleDisconnectClicked()
 * Disconnects an active peer or cancels the current outgoing connection attempt.
 */
void MainWindow::handleDisconnectClicked() {
    // Before a connection becomes a validated PeerConnection, Disconnect acts
    // as cancellation for the currently pending outgoing connection lifecycle.
    if(activePeer_ == nullptr) {
        if(!connectionManager_.cancelConnection()) {
            statusLabel_->setText("Failed to cancel connection");
            return;
        }

        statusLabel_->setText("Connection cancelled");

        connectButton_->setEnabled(true);
        disconnectButton_->setEnabled(false);

        // Restore the Direct endpoint form when its pending connection attempt
        // is cancelled so the user can immediately correct or retry the endpoint.
        if(connectionOrigin_ == ConnectionOrigin::Direct) {
            directAddressEdit_->setEnabled(true);
            directPortEdit_->setEnabled(true);
            directStatusLabel_->clear();
        }

        // The cancelled attempt no longer owns any connection lifecycle state.
        connectionOrigin_ = ConnectionOrigin::None;
        return;
    }

    statusLabel_->setText("Disconnecting...");
    disconnectButton_->setEnabled(false);

    // ConnectionManager owns established peer shutdown and emits peerDisconnected
    // after the underlying connection has actually ended.
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
 * Displays an approval dialog for an incoming FileBridge connection request.
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

    // Request attention if the operating system does not immediately grant this application focus.
    QApplication::alert(this);

    // Parent the dialog directly to this receiving MainWindow.
    // Unlike the native QMessageBox implementation on macOS, QDialog allows
    // FileBridge to control the dialog's placement relative to its parent.
    QDialog approvalDialog(this);

    approvalDialog.setWindowTitle("Connection request");
    approvalDialog.setModal(true);

    // Explain which remote device is requesting permission to establish the FileBridge connection.
    QLabel *messageLabel = new QLabel(
        deviceName + " wants to connect to FileBridge.",
        &approvalDialog
    );

    messageLabel->setWordWrap(true);

    // Reject closes the pending peer connection without granting FileBridge access.
    QPushButton *rejectButton = new QPushButton(
        "Reject",
        &approvalDialog
    );

    // Accept approves only the peer connection itself.
    // Individual incoming file offers still require their own explicit approval.
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

    // Keep the two connection-decision buttons together on one horizontal row.
    QHBoxLayout *buttonLayout = new QHBoxLayout();

    buttonLayout->addWidget(rejectButton);
    buttonLayout->addWidget(acceptButton);

    // Build the complete connection-request dialog vertically.
    QVBoxLayout *dialogLayout = new QVBoxLayout(&approvalDialog);

    dialogLayout->addWidget(messageLabel);
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
        // Connection approval grants communication with this peer but does not
        // automatically approve any files that the peer may later offer.
        if(!connectionManager_.approveConnection(connection)) {
            statusLabel_->setText("Failed to approve connection");
        }

        return;
    }

    if(!connectionManager_.rejectConnection(connection)) {
        statusLabel_->setText("Failed to reject connection");
        return;
    }

    statusLabel_->setText("Connection rejected");
}


/**
 * handlePeerReady()
 * Updates the interface after a peer completes the FileBridge handshake.
 */
void MainWindow::handlePeerReady(PeerConnection *connection) {
    // Remember which validated peer may receive file-transfer messages.
    activePeer_ = connection;

    const QString remoteDeviceName = connectionManager_.deviceName(connection);

    // Identify the connected peer whenever the validated handshake supplied a
    // device name, while retaining a safe generic fallback if it did not.
    statusLabel_->setText(
        remoteDeviceName.isEmpty()
            ? "Connected"
            : "Connected to " + remoteDeviceName
    );

    // A single active FileBridge connection prevents either connection method
    // from starting another connection until this peer disconnects.
    connectButton_->setEnabled(false);
    disconnectButton_->setEnabled(true);

    // Show the shared connection indicator in its established state.
    updateConnectionStateIcon(true);

    // Direct initiators already contain the endpoint they entered. Keep those
    // fields disabled while the active connection owns that endpoint.
    if(connectionOrigin_ == ConnectionOrigin::Direct) {
        directAddressEdit_->setEnabled(false);
        directPortEdit_->setEnabled(false);
        directStatusLabel_->clear();

        // Prevent switching back to discovery while a Direct connection is active.
        devicesModeButton_->setEnabled(false);
    }
    else if(connectionOrigin_ == ConnectionOrigin::None &&
            connection->socket() != nullptr) {

        // An incoming connection reached this FileBridge instance through its
        // local listening endpoint, so preserve that endpoint for the Direct
        // page without allowing the user to switch connection methods.
        directAddressEdit_->setText(
            connection->socket()->localAddress().toString()
        );

        directPortEdit_->setText(
            QString::number(connection->socket()->localPort())
        );

        directAddressEdit_->setEnabled(false);
        directPortEdit_->setEnabled(false);
        directStatusLabel_->clear();

        // Incoming connections arrive through FileBridge's listening endpoint,
        // so present the active connection using the Devices workflow regardless
        // of which connection page the receiver was viewing beforehand.
        devicesModeButton_->setChecked(true);
        handleDevicesModeClicked();

        // Match the incoming peer against its current discovery entry so the
        // receiving side also marks the connected device in the Devices list.
        const QString remoteAddress = connection->socket()->peerAddress().toString();

        for(int index = 0; index < nearbyDevicesList_->count(); ++index) {
            QListWidgetItem *item = nearbyDevicesList_->item(index);

            const QString discoveredDeviceName =
                item->data(NEARBY_DEVICE_NAME_ROLE).toString();

            const QString discoveredAddress =
                item->data(NEARBY_DEVICE_ADDRESS_ROLE).toString();

            if(discoveredDeviceName != remoteDeviceName ||
               discoveredAddress != remoteAddress) {

                continue;
            }

            item->setData(
                NEARBY_DEVICE_STATE_ROLE,
                static_cast<int>(NearbyDeviceState::Connected)
            );

            updateNearbyDeviceDisplay(item);
            break;
        }

        // The receiving side is already connected through the Devices workflow,
        // so Direct must remain unavailable until that peer disconnects.
        directModeButton_->setEnabled(false);
    }
    else if(connectionOrigin_ == ConnectionOrigin::Devices) {
        // Promote the discovered peer that initiated this successful outgoing
        // connection from Connecting to Connected.
        for(int index = 0; index < nearbyDevicesList_->count(); ++index) {
            QListWidgetItem *item = nearbyDevicesList_->item(index);

            const NearbyDeviceState state = static_cast<NearbyDeviceState>(
                item->data(NEARBY_DEVICE_STATE_ROLE).toInt()
            );

            if(state != NearbyDeviceState::Connecting) {
                continue;
            }

            item->setData(
                NEARBY_DEVICE_STATE_ROLE,
                static_cast<int>(NearbyDeviceState::Connected)
            );

            updateNearbyDeviceDisplay(item);
            break;
        }

        // Keep the connection method fixed while a discovered-device
        // connection remains active.
        directModeButton_->setEnabled(false);
    }

    updateOutgoingQueueControls();
}


/**
 * handleConnectionRejected()
 * Updates the interface when a remote user rejects an outgoing connection request.
 */
void MainWindow::handleConnectionRejected(PeerConnection *connection) {
    Q_UNUSED(connection);

    statusLabel_->setText("Connection rejected");

    connectButton_->setEnabled(true);
    disconnectButton_->setEnabled(false);

    // A rejected request leaves FileBridge disconnected.
    updateConnectionStateIcon(false);

    // Return any discovered peer involved in the rejected outgoing attempt
    // to its normal available state.
    if(connectionOrigin_ == ConnectionOrigin::Devices) {
        for(int index = 0; index < nearbyDevicesList_->count(); ++index) {
            QListWidgetItem *item = nearbyDevicesList_->item(index);

            const NearbyDeviceState state = static_cast<NearbyDeviceState>(
                item->data(NEARBY_DEVICE_STATE_ROLE).toInt()
            );

            if(state != NearbyDeviceState::Connecting) {
                continue;
            }

            item->setData(
                NEARBY_DEVICE_STATE_ROLE,
                static_cast<int>(NearbyDeviceState::Available)
            );

            updateNearbyDeviceDisplay(item);
            break;
        }
    }

    // A rejected Direct attempt returns the Direct page to its editable idle state.
    if(connectionOrigin_ == ConnectionOrigin::Direct) {
        directAddressEdit_->setEnabled(true);
        directPortEdit_->setEnabled(true);
        directStatusLabel_->clear();
    }

    // The rejected attempt no longer owns any connection lifecycle state.
    connectionOrigin_ = ConnectionOrigin::None;
}


/**
 * handlePeerDisconnected()
 * Updates the interface after an established or pending peer disconnects.
 */
void MainWindow::handlePeerDisconnected(PeerConnection *connection) {
    if(connection != activePeer_) {
        return;
    }

    // The PeerConnection will be destroyed by ConnectionManager after this signal returns.
    activePeer_ = nullptr;

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

    // Return the shared connection indicator to its idle state.
    updateConnectionStateIcon(false);

    // Return the previously connected discovered peer to its normal available
    // state while it remains present in the LAN discovery list.
    for(int index = 0; index < nearbyDevicesList_->count(); ++index) {
        QListWidgetItem *item = nearbyDevicesList_->item(index);

        const NearbyDeviceState state = static_cast<NearbyDeviceState>(
            item->data(NEARBY_DEVICE_STATE_ROLE).toInt()
        );

        if(state != NearbyDeviceState::Connected) {
            continue;
        }

        item->setData(
            NEARBY_DEVICE_STATE_ROLE,
            static_cast<int>(NearbyDeviceState::Available)
        );

        updateNearbyDeviceDisplay(item);
        break;
    }

    // Direct endpoint controls become editable again after the active connection
    // ends, including connections that were originally accepted from a remote peer.
    if(connectionOrigin_ == ConnectionOrigin::Direct ||
        connectionOrigin_ == ConnectionOrigin::None) {

        directAddressEdit_->setEnabled(true);
        directPortEdit_->setEnabled(true);
        directStatusLabel_->clear();

        // Discovery becomes available again once Direct no longer owns the connection.
        devicesModeButton_->setEnabled(true);
    }

    // Restore both connection methods after the active peer has gone away.
    devicesModeButton_->setEnabled(true);
    directModeButton_->setEnabled(true);

    // No connection origin remains meaningful after the peer has disconnected.
    connectionOrigin_ = ConnectionOrigin::None;

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
 * updateConnectionStateIcon()
 * Updates the shared connection-state icon for disconnected or connected state.
 */
void MainWindow::updateConnectionStateIcon(bool connected) {
    QPixmap statePixmap(42, 20);
    statePixmap.fill(Qt::transparent);

    QPainter painter(&statePixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QPen statePen(
        connected
            ? QColor("#55585d")
            : QColor("#8a8e94")
    );

    statePen.setWidthF(1.6);

    painter.setPen(statePen);
    painter.setBrush(Qt::NoBrush);

    // Draw the two device endpoints using the same geometry in both states.
    painter.drawRoundedRect(
        QRectF(2.0, 4.0, 10.0, 12.0),
        2.0,
        2.0
    );

    painter.drawRoundedRect(
        QRectF(30.0, 4.0, 10.0, 12.0),
        2.0,
        2.0
    );

    if(connected) {
        // A continuous line communicates one established connection between
        // the two endpoints.
        painter.drawLine(
            QPointF(12.0, 10.0),
            QPointF(30.0, 10.0)
        );
    }
    else {
        // A deliberate center gap distinguishes the idle state from an
        // established connection without changing the icon's overall shape.
        painter.drawLine(
            QPointF(12.0, 10.0),
            QPointF(18.0, 10.0)
        );

        painter.drawLine(
            QPointF(24.0, 10.0),
            QPointF(30.0, 10.0)
        );
    }

    painter.end();

    connectionStateIcon_->setPixmap(statePixmap);
    connectionStateIcon_->setAlignment(Qt::AlignCenter);
    connectionStateIcon_->setFixedSize(42, 20);
    connectionStateIcon_->setToolTip(
        connected
            ? "Connected"
            : "Not connected"
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
        // Preserve the Receive segment's joined geometry while using a
        // high-contrast appearance to draw attention to incoming activity.
        receiveModeButton_->setStyleSheet(
            "QPushButton {"
            "background-color: black;"
            "color: white;"
            "border: 1px solid black;"
            "border-top-left-radius: 0;"
            "border-bottom-left-radius: 0;"
            "border-top-right-radius: 8px;"
            "border-bottom-right-radius: 8px;"
            "}"
            "QPushButton:checked {"
            "background-color: #969696;"
            "color: #ffffff;"
            "border: 1px solid #b8bbc0;"
            "border-top-left-radius: 0;"
            "border-bottom-left-radius: 0;"
            "border-top-right-radius: 8px;"
            "border-bottom-right-radius: 8px;"
            "}"
        );
    }
    else {
        // Restore the normal Receive appearance without losing the right-hand
        // geometry that makes it part of the joined transfer-mode selector.
        receiveModeButton_->setStyleSheet(
            "QPushButton {"
            "background-color: #d7d8da;"
            "border: 1px solid #b8bbc0;"
            "border-top-left-radius: 0;"
            "border-bottom-left-radius: 0;"
            "border-top-right-radius: 8px;"
            "border-bottom-right-radius: 8px;"
            "}"
            "QPushButton:checked {"
            "background-color: #969696;"
            "color: #ffffff;"
            "}"
        );
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

    // Re-evaluate the active batch while preserving this rejected history row.
    if(outgoingQueueSending_) {
        offerQueuedFiles();
    }

    // Keep the rejection visible even if batch reevaluation reaches its final state.
    statusLabel_->setText("Transfer rejected");
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
 * Adds a newly offered file to the incoming list for explicit receiver approval.
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

    // Every incoming offer requires an explicit receiver decision before file data is accepted.
    item->setData(
        INCOMING_TRANSFER_PENDING_ROLE,
        true
    );

    // A new incoming transfer must remain visible until it completes or is rejected.
    item->setData(
        INCOMING_TRANSFER_REMOVABLE_ROLE,
        false
    );

    // New offers begin in Pending and remain there until the receiver accepts or rejects them.
    item->setData(
        INCOMING_TRANSFER_STATE_ROLE,
        static_cast<int>(IncomingTransferState::Pending)
    );

    // Build the visible row from the stored metadata and initial Pending state.
    updateIncomingTransferDisplay(item);

    // Update the Receive selector so the user can immediately see that a file requires attention.
    updateReceiveModeAttention();

    // Enable the appropriate Accept/Reject controls for the newly pending offer.
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
 * Displays an outgoing connection failure in the interface.
 */
void MainWindow::handleConnectionFailed(const QString& errorMessage) {
    statusLabel_->setText("Connection failed: " + errorMessage);

    connectButton_->setEnabled(true);
    disconnectButton_->setEnabled(false);

    // A failed outgoing attempt leaves FileBridge disconnected.
    updateConnectionStateIcon(false);

    // Return any discovered peer involved in the failed outgoing attempt
    // to its normal available state.
    if(connectionOrigin_ == ConnectionOrigin::Devices) {
        for(int index = 0; index < nearbyDevicesList_->count(); ++index) {
            QListWidgetItem *item = nearbyDevicesList_->item(index);

            const NearbyDeviceState state = static_cast<NearbyDeviceState>(
                item->data(NEARBY_DEVICE_STATE_ROLE).toInt()
            );

            if(state != NearbyDeviceState::Connecting) {
                continue;
            }

            item->setData(
                NEARBY_DEVICE_STATE_ROLE,
                static_cast<int>(NearbyDeviceState::Available)
            );

            updateNearbyDeviceDisplay(item);
            break;
        }
    }

    // A failed Direct attempt returns to the same editable endpoint form so the
    // user can correct the address or port and immediately try again.
    if(connectionOrigin_ == ConnectionOrigin::Direct) {
        directAddressEdit_->setEnabled(true);
        directPortEdit_->setEnabled(true);
        directStatusLabel_->clear();
    }

    // The failed attempt no longer has an active connection origin.
    connectionOrigin_ = ConnectionOrigin::None;
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

    // Store the device name separately so the visible row can be rebuilt
    // without parsing text after connection-state labels are appended.
    item->setData(
        NEARBY_DEVICE_NAME_ROLE,
        peer.deviceName
    );

    // Newly discovered peers begin in the normal available state.
    item->setData(
        NEARBY_DEVICE_STATE_ROLE,
        static_cast<int>(NearbyDeviceState::Available)
    );

    updateNearbyDeviceDisplay(item);
}


/**
 * updateNearbyDeviceDisplay()
 * Rebuilds one discovered-device row from its stored identity and connection state.
 */
void MainWindow::updateNearbyDeviceDisplay(QListWidgetItem *item) {
    if(item == nullptr) {
        return;
    }

    QWidget *rowWidget = nearbyDevicesList_->itemWidget(item);
    QLabel *deviceLabel = nullptr;
    QLabel *stateLabel = nullptr;

    // Create the row presentation once. Later state changes reuse these labels
    // instead of replacing the QListWidgetItem or reconstructing its metadata.
    if(rowWidget == nullptr) {
        rowWidget = new QWidget(nearbyDevicesList_);

        QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);

        // Keep the custom row aligned closely with the QListWidget's normal
        // item text while reserving the far-right edge for connection state.
        rowLayout->setContentsMargins(6, 0, 6, 0);
        rowLayout->setSpacing(8);

        deviceLabel = new QLabel(rowWidget);
        stateLabel = new QLabel(rowWidget);

        // Object names allow later display updates to recover the two labels
        // without storing widget pointers inside QListWidgetItem metadata.
        deviceLabel->setObjectName("nearbyDeviceName");
        stateLabel->setObjectName("nearbyDeviceState");

        stateLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        // Keep the state visually secondary to the device identity while still
        // making availability and connection state immediately visible.
        stateLabel->setStyleSheet(
            "color: #747980;"
            "font-weight: 500;"
        );

        rowLayout->addWidget(deviceLabel);
        rowLayout->addStretch(1);
        rowLayout->addWidget(stateLabel);

        nearbyDevicesList_->setItemWidget(item, rowWidget);

        // The custom row widget now owns all visible presentation for this item,
        // so clear the QListWidgetItem text to prevent Qt from drawing it underneath.
        item->setText(QString());
    }
    else {
        // Recover the labels created for this row so only their presentation
        // changes when the device moves through its transient connection states.
        deviceLabel = rowWidget->findChild<QLabel *>("nearbyDeviceName");
        stateLabel = rowWidget->findChild<QLabel *>("nearbyDeviceState");
    }

    if(deviceLabel == nullptr || stateLabel == nullptr) {
        return;
    }

    const QString deviceName = item->data(NEARBY_DEVICE_NAME_ROLE).toString();
    const QString address = item->data(NEARBY_DEVICE_ADDRESS_ROLE).toString();

    deviceLabel->setText(deviceName + " - " + address);

    // Convert the stored state back into the short label presented at the
    // far-right side of the discovered-device row.
    const NearbyDeviceState state = static_cast<NearbyDeviceState>(
        item->data(NEARBY_DEVICE_STATE_ROLE).toInt()
    );

    switch(state) {
        case NearbyDeviceState::Available:
            stateLabel->setText("Available");
            break;

        case NearbyDeviceState::Connecting:
            stateLabel->setText("Connecting...");
            break;

        case NearbyDeviceState::Connected:
            stateLabel->setText("Connected");
            break;
    }
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

    // Record the connection source independently from the currently displayed
    // Devices or Direct page so later lifecycle events update the correct UI.
    connectionOrigin_ = ConnectionOrigin::Devices;

    // Mark only the selected discovered peer as actively connecting.
    item->setData(
        NEARBY_DEVICE_STATE_ROLE,
        static_cast<int>(NearbyDeviceState::Connecting)
    );

    updateNearbyDeviceDisplay(item);

    statusLabel_->setText("Connecting...");
    connectButton_->setEnabled(false);
    disconnectButton_->setEnabled(true);

    connectionManager_.connectToPeer(
        remoteAddress,
        static_cast<std::uint16_t>(remotePort)
    );
}
