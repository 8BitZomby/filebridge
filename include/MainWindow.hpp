#ifndef FILEBRIDGE_MAIN_WINDOW_HPP
#define FILEBRIDGE_MAIN_WINDOW_HPP

#include "ConnectionManager.hpp"

#include <QLabel>
#include <QLineEdit>
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

        /**
         * handlePeerReady()
         * Updates the interface after a peer completes the FileBridge handshake
         */
        void handlePeerReady(PeerConnection *connection);

        /**
         * handleConnectionFailed()
         * Displays an outgoing connection failure in the interface
         */
        void handleConnectionFailed(const QString& errorMessage);

    private:

        // Coordinates all incoming and outgoing FileBridge peer connections
        ConnectionManager connectionManager_;

        // Displays the preferred local IPv4 address
        QLabel *localAddressLabel_;

        // Displays the automatically selected listening port
        QLabel *localPortLabel_;

        // Accepts the remote peer IPv4 address entered by the user
        QLineEdit *remoteAddressEdit_;

        // Accepts the remote peer listening port entered by the user
        QLineEdit *remotePortEdit_;

        // Starts an outgoing peer connection using the entered address and port
        QPushButton *connectButton_;

        // Displays the current FileBridge connection status
        QLabel *statusLabel_;
};


#endif
