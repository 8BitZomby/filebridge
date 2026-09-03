#include "PeerDiscovery.hpp"

#include <QDataStream>
#include <QDebug>
#include <QIODevice>
#include <QSysInfo>

#include <cstdint>


/**
 * PeerDiscovery()
 * Creates a peer discovery service
 */
PeerDiscovery::PeerDiscovery(std::uint16_t listeningPort, QObject *parent) 
    : QObject(parent),
      instanceId_(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      listeningPort_(listeningPort) {

    // Broadcast another discovery announcement whenever the periodic timer expires
    QObject::connect(
        &announcementTimer_,                        // Sender: Timer controlling periodic discovery announcements
        &QTimer::timeout,                           // Signal: Emitted whenever the configured interval expires
        this,                                       // Receiver: This PeerDiscovery instance
        &PeerDiscovery::broadcastAnnouncement       // Slot: Advertises this FileBridge instance again
    );
    
    // Periodically check whether previously discovered peers have stopped advertising.
    QObject::connect(
        &cleanupTimer_,                         // Sender: Timer controlling stale-peer checks.
        &QTimer::timeout,                       // Signal: Emitted whenever the cleanup interval expires.
        this,                                   // Receiver: This PeerDiscovery instance.
        &PeerDiscovery::removeExpiredPeers      // Slot: Removes peers whose last heartbeat is too old.
    );

    // Advertise this FileBridge instance at the configured discovery interval.
    announcementTimer_.setInterval(ANNOUNCEMENT_INTERVAL_MS);

    // Check for expired peers once per second.
    cleanupTimer_.setInterval(1000);
}


/**
 * start()
 * Starts listening for FileBridge discovery broadcasts
 */
bool PeerDiscovery::start() {
    // Avoid binding the dicovery socket more than once
    if(isRunning()) {
        return true;
    }

    // Allow multiple FileBridge instances on the same machine to listen on
    // the shared discovery port while receiving multicast datagrams
    const bool bound = socket_.bind(
        QHostAddress::AnyIPv4,                              // Address: Listen for IPv4 datagrams on all local network interfaces
        DISCOVERY_PORT,                                     // Port: Fixed shared UDP port used by FileBridge peer discovery
        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint // MODE: Allow multiply FB instances to share discovery port
    );

    if(!bound) {
        return false;
    }

    // Join the FileBridge multicast group so this instance receives announcements
    // sent by every other FileBridge instance on the local network
    if(!socket_.joinMulticastGroup(DISCOVERY_MULTICAST_GROUP)) {
        socket_.close();
        return false;
    }

    // Process discovery datagrams whenever the UDP socket receives data
    QObject::connect(
        &socket_,                           // Sender: USP socket receiving discovery datagrams
        &QUdpSocket::readyRead,              // Signal: Emitted when one or more datagrams are available
        this,                               // Receiver: This PeerDiscovery instance
        &PeerDiscovery::handleReadyRead     // Slot: Read and processes the pending datagrams
    );

    // Announce immediately so nearby FileBridge instance do not have to wait for the timer
    broadcastAnnouncement();

    // Continue advertising periodically while discovery remains active
    announcementTimer_.start();

    // Periodically remove peers that stop sending discovery heartbeats
    cleanupTimer_.start();

    return true;
}


/**
 * void stop()
 * Stops local peer discovery
 */
void PeerDiscovery::stop() {
    // Stop periodic announcements before shutting down discovery networking
    announcementTimer_.stop();

    // Stop checking for expired peers while discovery is inactive
    cleanupTimer_.stop();

    // Leave the multicast group before closing the USP socket
    if(socket_.state() != QAbstractSocket::UnconnectedState) {
        socket_.leaveMulticastGroup(DISCOVERY_MULTICAST_GROUP);
    }

    // Stop receiving UDP discovery traffic
    socket_.close();
}

/**
 * isRunning()
 * Returns whether the discovery socket is currently active
 */
bool PeerDiscovery::isRunning() const {
    return socket_.state() != QAbstractSocket::UnconnectedState;
}


/**
 * handleReadyRead()
 * Processes pending UDP discovery datagrams
 */
void PeerDiscovery::handleReadyRead() {
    // Process every datagram currently waiting on the discovery socket
    while(socket_.hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(socket_.pendingDatagramSize()));

        QHostAddress senderAddress;
        quint16 senderPort = 0;

        // Read one complete UDP datagram and record where it came from
        const qint64 bytesRead = socket_.readDatagram(
            data.data(),        // Data: Buffer that receives the serialized discover announcement
            data.size(),        // Size: Max number of bytes that may be written into the buffer
            &senderAddress,     // Address: Receives the IPv4 address of the announcing peer
            &senderPort         // Port: Receives the UDP source port used by the annoucing peer
        );

        // The UDP source port is not needed because the peer advertises its TCP listening port separately
        Q_UNUSED(senderPort);

        // Ignore datagrams that could not be read successfully
        if(bytesRead < 0) {
            continue;
        }

        DiscoveryAnnouncement announcement  {
            0,                  // protocolVersion: Filee from the received discovery datagram
            QString(),          // instanceId: Filled from the received discovery datagram
            QString(),          // deviceName: Filled from the received discovery datagram
            0                   // listeningPort: Fille from the received discovery datagram
        };

        // Ignore malformed discovery packets
        if(!deserializeAnnouncement(data, announcement)) {
            continue;
        }

        // Ignore discovery format the FileBridge version does not understand
        if(announcement.protocolVersion != DISCOVERY_PROTOCOL_VERSION) {
            continue;
        }

        // Ignore announcements sent by this same unning FileBridge instance
        if(announcement.instanceId == instanceId_) {
            continue;
        }

        // A usable announcement must provide both a device name and TCP listening port
        if(announcement.deviceName.isEmpty() || announcement.deviceName.isEmpty() || announcement.listeningPort == 0) {
            continue;
        }

        const DiscoveredPeer peer {
            announcement.instanceId,    // instanceId: Unique identifier for this running remote FileBridge instance
            announcement.deviceName,    // deviceName: Readable name advertized by the remote FB instance
            senderAddress,              // address: Network address from which the UDP announcement was received
            announcement.listeningPort  // port: TCP listening port advertised by the remote FB instance
        };

        // Record the current time so this heartbeat can refresh the peer's last-seen timestamp
        const qint64 currentTimeMs = QDateTime::currentMSecsSinceEpoch();

        // Search for a previously discovered peer using the remote instance's unique ID
        // QHash::find() returns an iterator to the matching entry when found.
        // If no matching key exists, find() returns knownPeers_.end(), which is the
        // special iterator positioned one past the final element and means "not found"
        auto knownPeer = knownPeers_.find(announcement.instanceId);

        if(knownPeer == knownPeers_.end()) {
            // No existing entry was found, so this is the first announcement
            // received from this running FileBridge instance
            knownPeers_.insert(
                    announcement.instanceId,    // Key: Unique identifier advertised by the remote FileBridge instance
                    KnownPeer {
                        peer,                   // peer: Current network information for the discovered instance
                        currentTimeMs           // lastSeenMs: Time this discovery heartbeat was received
                    }
            );
            
            // Notify the rest of FileBridge only when this peer is first discovered
            emit peerDiscovered(peer);
        }
        else {
            // The peer already exists in knownPeers_. Refresh its stored network information
            // instead of treating this heartbeat as a new discovery
            knownPeer.value().peer = peer;
            knownPeer.value().lastSeenMs = currentTimeMs;
        }

    }
}


/**
 * serializeAnnouncement()
 * Encodes a discoverry annountment into UDP payload bytes
 */
QByteArray PeerDiscovery::serializeAnnouncement(const DiscoveryAnnouncement& announcement) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    // Use a fixed byte order so discovery messages are consistent across platforms
    stream.setByteOrder(QDataStream::BigEndian);
    // Pin Qt's serialization format so different Qt 6 releases use the same wire representation
    stream.setVersion(QDataStream::Qt_6_0);

    // Encode the discovery fields in a fixed order understood by every FileBridge instance
    stream << announcement.protocolVersion;
    stream << announcement.instanceId;
    stream << announcement.deviceName;
    stream << announcement.listeningPort;

    return data;
}


/**
 * deserializeAnnouncement()
 * Decodes UDP payload bytes into a discovery announcement
 */
bool PeerDiscovery::deserializeAnnouncement(const QByteArray& data, DiscoveryAnnouncement& announcement) const {
    QDataStream stream(data);

    // Match the byte order used when discovery announcements are serialized
    stream.setByteOrder(QDataStream::BigEndian);
    // Use the same fixed Qt serialization format used when the packet was written
    stream.setVersion(QDataStream::Qt_6_0);

    std::uint16_t protocolVersion = 0;
    QString instanceId;
    QString deviceName;
    std::uint16_t listeningPort = 0;

    // Decode fields in exactly the same order used during serialization
    stream >> protocolVersion;
    stream >> instanceId;
    stream >> deviceName;
    stream >> listeningPort;

    // Reject malformed or incomplete discovery packets
    if(stream.status() != QDataStream::Ok) {
        return false;
    }

    // Update the output object only after the complete packet was decoded successfully
    announcement.protocolVersion = protocolVersion;
    announcement.instanceId = instanceId;
    announcement.deviceName = deviceName;
    announcement.listeningPort = listeningPort;

    return true;
}


/**
 * broadcastAnnouncement()
 * Send this FileBridge instance's discovery announcement over the local network
 */
void PeerDiscovery::broadcastAnnouncement() {
    const DiscoveryAnnouncement announcement {
        DISCOVERY_PROTOCOL_VERSION,     // protocolVersion: version of the UDP discovery message format
        instanceId_,                    // instanceId: Unique identifier for this running FileBridge instance
        QSysInfo::machineHostName(),    // deviceName: Host name advertised to nearby FileBridge instances
        listeningPort_                  // listeningPort: TCP port peers should use to connect to this instance
    };

    // Encode the announcement using FileBridge's defined discovery wire format
    const QByteArray data = serializeAnnouncement(announcement);

    // Broadcast the accouncement to FileBridge instances listening on the local network
    socket_.writeDatagram(
        data,                       // Data: Serialized FileBridge discovery announcement
        DISCOVERY_MULTICAST_GROUP,  // Address: Mulitcast group joined by nearby FileBridge instances
        DISCOVERY_PORT              // Port: Shared UDP port used by FileBridge discovery
    );
}


/**
 * removeExpiredPeers()
 * Removes peers that have not advertised themselves within the timeout period.
 */
void PeerDiscovery::removeExpiredPeers() {
    const qint64 currentTimeMs = QDateTime::currentMSecsSinceEpoch();

    // Iterate through the hash manually because expired entries may be erased in-place.
    auto peer = knownPeers_.begin();

    while(peer != knownPeers_.end()) {
        // Determine how long it has been since this peer's most recent discovery heartbeat.
        const qint64 elapsedMs = currentTimeMs - peer.value().lastSeenMs;

        if(elapsedMs > PEER_TIMEOUT_MS) {
            // Preserve the peer information before erase() invalidates this iterator.
            const DiscoveredPeer expiredPeer = peer.value().peer;

            // QHash::erase() removes the current entry and returns an iterator to the next one,
            // which lets this loop safely continue without incrementing an invalid iterator.
            peer = knownPeers_.erase(peer);

            emit peerLost(expiredPeer);
        }
        else {
            // This peer is still active, so advance to the next hash entry.
            ++peer;
        }
    }
}
