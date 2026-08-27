#include "NetworkInfo.hpp"

#include <QAbstractSocket>
#include <QNetworkInterface>


/**
 * localIPv4Addresses()
 * Returns IPv4 addresses belonging to active, running, non-loopback interfaces
 */
std::vector<LocalNetworkAddress> NetworkInfo::localIPv4Addresses() {
    std::vector<LocalNetworkAddress> addresses;

    // Retrieve every network interface currently known to the system
    const auto interfaces = QNetworkInterface::allInterfaces();

    for(const QNetworkInterface& interface : interfaces) {
        const auto flags = interface.flags();

        // Ignore interfaces that are unavailable or only provide loopback traffic
        if(!flags.testFlag(QNetworkInterface::IsUp) ||
           !flags.testFlag(QNetworkInterface::IsRunning) ||
           flags.testFlag(QNetworkInterface::IsLoopBack)) {

            continue;
        }

        // A single interface may have multiple assigned network addresses
        for(const QNetworkAddressEntry& entry : interface.addressEntries()) {
            const QHostAddress address = entry.ip();

            // FileBridge is starting with IPv4 support only
            if(address.protocol() != QAbstractSocket::IPv4Protocol) {
                continue;
            }

            addresses.push_back({
                interface.humanReadableName(),
                address,
                address.isLinkLocal()
            });
        }
    }

    return addresses;
}


/**
 * preferredLocalIPv4Address()
 * Returns the best available local IPv4 address, preferring non-link-local addresses
 */
QHostAddress NetworkInfo::preferredLocalIPv4Address() {
    const std::vector<LocalNetworkAddress> addresses = localIPv4Addresses();

    // Prefer a normal LAN address because it is usually the most useful 
    // address for another device
    for(const LocalNetworkAddress& address : addresses) {
        if(!address.isLinkLocal) {
            return address.address;
        }
    }

    // Fall back to IPv4 link-local when no normal LAN address is available
    if(!addresses.empty()) {
        return addresses.front().address;
    }

    // A null QHostAddress represents that no usable local IPv4 address was found
    return {};
}
