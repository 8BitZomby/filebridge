#ifndef FILEBRIDGE_TCP_LISTENER_HPP
#define FILEBRIDGE_TCP_LISTENER_HPP

#include <QTcpServer>

#include <cstdint>


// Owns FileBridge's local TCP server and manages its listening state
class TcpListener {
    
    public:
        /**
         * Constructor: TcpListener()
         * Constructs a TCP listener that is initially stopped
         */
        TcpListener();

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

    private:

        // Qt TCP server used to bind a local port and accept incoming connections
        QTcpServer server_;
};


#endif
