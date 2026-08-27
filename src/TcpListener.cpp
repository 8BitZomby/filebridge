#include "TcpListener.hpp"

#include <QHostAddress>
#include <cstdint>


/**
 * TcpListener()
 * Constructs a TCP listener that is initially stopped
 */
TcpListener::TcpListener() = default;


/**
 * start()
 * Starts listening on any available local TCP port
 */
bool TcpListener::start() {
    if(server_.isListening()) {
        return true;
    }

    // Port 0 asks the OS to assign an available TCP port
    return server_.listen(QHostAddress::AnyIPv4, 0);
}


/**
 * stop()
 * Stops accepting new TCP connections
 */
void TcpListener::stop() {
    server_.close();
}


/**
 * isListening()
 * Returns whether the listener is currently accepting connections
 */
bool TcpListener::isListening() const {
    return server_.isListening();
}


/**
 * port()
 * Returns the TCP port currently assigned to the listener
 */
std::uint16_t TcpListener::port() const {
    return static_cast<std::uint16_t>(server_.serverPort());
}
