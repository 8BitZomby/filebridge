#ifndef FILEBRIDGE_TCP_LISTENER_HPP
#define FILEBRIDGE_TCP_LISTENER_HPP

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

#include <cstdint>


// Owns FileBridge's local TCP server and reports newly accepted peer connections
class TcpListener : public QObject {
    Q_OBJECT    
    
    public:
        /**
         * Constructor: TcpListener()
         * Constructs a TCP listener that is initially stopped
         */
        explicit TcpListener(QObject* parent = nullptr);

        /**
         * Destructor: TcpListener()
         * Destroys the listener and releases its underlying TCP server resources
         */
        ~TcpListener() override = default;

        /**
         * start()
         * Starts listening on any available local TCP port
         */
        bool start();

        /**
         * stop()
         * Stops accepting new TCP connections
         */
        void stop();

        /**
         * isListening()
         * Returns whether the listener is currently accepting connections
         */
        bool isListening() const;

        /**
         * port()
         * Returns the TCP port currently assigned to the listener
         */
        std::uint16_t port() const;

    signals:

        /**
         * connectionAccepted()
         * Reports a newly accepted TCP connection to the rest of FileBridge
         */
        void connectionAccepted(QTcpSocket* socket);

    private slots:

        /**
         * handleNewConnection()
         * Accepts all connections currently waiting on the Qt TCP server
         */
        void handleNewConnection();

    private:

        // Qt TCP server used to bind a local port and accept incoming connections
        QTcpServer server_;
};


#endif
