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

    // True when the address is IPv4 link-local and should normally be a fallback
    bool isLinkLocal;
};


// Provides information about the network interfaces available on the local device
class NetworkInfo {
    public:
        /**
         * localIPv4Addresses()
         * Returns IPv4 addresses belonging to active, running, non-loopback interfaces
         */
        static std::vector<LocalNetworkAddress> localIPv4Addresses();

        /**
         * preferredLocalIPv4Address()
         * Returns the best available local IPv4 address, preferring non-link-local addresses
         */
        static QHostAddress preferredLocalIPv4Address();
};


#endif
