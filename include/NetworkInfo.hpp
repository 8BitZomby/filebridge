#ifndef FILEBRIDGE_NETWORK_INFO_HPP
#define FILEBRIDGE_NETWORK_INFO_HPP

#include <QHostAddress>
#include <QString>

#include <vector>


// Represents one usable IPv4 address exposed by a local network interface
struct LocalNetworkAddress {

    // Human-readable name of the interface (WiFi, Ethernet, etc)
    QString interfaceName;

    // IPv4 address assigned to the interface
    QHostAddress address;
};


// Provides information about the network interfaces available on the local device
class NetworkInfo {
    public:
        // Returns IPv4 addresses belonging to active, running, non-loopback interfaces
        static std::vector<LocalNetworkAddress> localIPv4Addresses();
};


#endif
