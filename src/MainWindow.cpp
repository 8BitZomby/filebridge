#include "MainWindow.hpp"
#include "ConnectionManager.hpp"
#include "NetworkInfo.hpp"

#include <QHostAddress>
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
    localAddressLabel_(new QLabel(this)),
    localPortLabel_(new QLabel(this)),
    remoteAddressEdit_(new QLineEdit(this)),
    remotePortEdit_(new QLineEdit(this)),
    connectButton_(new QPushButton("Connect", this)),
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

        // Start an outgoing connection when the user presses the Connect button
        QObject::connect(
            connectButton_,                      // Button that emits the signal
            &QPushButton::clicked,              // Signal emitted when the button is pressed
            this,                               // Window that handles the action
            &MainWindow::handleConnectClicked   // Slot that validates and connects to the peer
        );

        // Update the interface only after a peer completes a valid FileBridge handshake
        QObject::connect(
            &connectionManager_,                // Manager that emits peer readiness
            &ConnectionManager::peerReady,      // Signal emitted after handshake validation succeeds
            this,                               // Window that displays the connection state
            &MainWindow::handlePeerReady        // Slot that marks the peer as ready
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
    Q_UNUSED(connection);

    statusLabel_->setText("Connected");
    connectButton_->setEnabled(false);
}


/**
 * handleConnectionFailed()
 * Displays an outgoing connection failure in the interface
 */
void MainWindow::handleConnectionFailed(const QString& errorMessage) {
    statusLabel_->setText("Connection failed:" + errorMessage);
    connectButton_->setEnabled(true);
}
