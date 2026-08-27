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
                address
            });
        }
    }

    return addresses;
}
