#ifndef FILEBRIDGE_PEER_DISCOVERY_HPP
#define FILEBRIDGE_PEER_DISCOVERY_HPP

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUdpSocket>
#include <QUuid>

#include <cstdint>


/**
 * DiscoveredPeer
 * Describes one FileBridge instance discovered on the local network
 */
struct DiscoveredPeer {
    QString instanceId;
    QString deviceName;
    QHostAddress address;
    std::uint64_t port;
};


/**
 * DiscoveryAnnouncement
 * Contains the information advertised by one FileBridge instance during 
 * LAN discovery
 */
struct DiscoveryAnnouncement {
    std::uint16_t protocolVersion;
    QString instanceId;
    QString deviceName;
    std::uint16_t listeningPort;
};


/**
 * PeerDiscovery
 * Discovers nearby FileBridge instances using UDP broadcast messages
 */
class PeerDiscovery : public QObject {
    Q_OBJECT

    public:

        /**
         * PeerDiscovery()
         * Creates a peer discovery service for the specified FileBridge TCP listening 
         * port
         */
        explicit PeerDiscovery(std::uint16_t listeningPort, QObject *parent = nullptr);

        /**
         * start()
         * Starts listening for FileBridge discovery broadcasts
         */
        bool start();

        /**
         * void stop()
         * Stops local peer discovery
         */
        void stop();

        /**
         * isRunning()
         * Returns whether the discovery socket is currently active
         */
        bool isRunning() const;

    signals:

        /**
         * peerDiscovered()
         * Reports a FileBridge peer discovered on the local network
         */
        void peerDiscovered(const DiscoveredPeer& peer);

        /**
         * peerLost()
         * Reports that a previously discovered FileBridge peer is no longer advertising itself
         */
        void peerLost(const DiscoveredPeer& peer);

    private slots:

        /**
         * handleReadyRead()
         * Processes pending UDP discovery datagrams
         */
        void handleReadyRead();

        /**
         * removeExpiredPeers()
         * Removes peers that have not advertised themselves within the timeout period
         */
        void removeExpiredPeers();

    private:

        // Version of the UDP discovery message format used by FileBridge
        static constexpr std::uint16_t DISCOVERY_PROTOCOL_VERSION = 1;

        // UDP port shared by FileBridge instances for local peer discovery
        static constexpr std::uint16_t DISCOVERY_PORT = 45454;

        // IPv4 multicast group used by FileBridge instances for local peer discovery
        inline static const QHostAddress DISCOVERY_MULTICAST_GROUP { QStringLiteral("239.255.43.21") };

        // Time between repeated FileBridge discovery broadcasts
        static constexpr int ANNOUNCEMENT_INTERVAL_MS = 3000;

        // Time without a discovery heartbeat before a peer is considered unavailable
        static constexpr qint64 PEER_TIMEOUT_MS = 10000;

        // Unique identifier for this running FileBridge instance
        QString instanceId_;

        // TCP port advertised to nearby FileBridge instances
        std::uint16_t listeningPort_;

        // Time that periodically broadcasts this FileBridge instance
        QTimer announcementTimer_;

        // Periodically checks known peers for expired discover heartbeats
        QTimer cleanupTimer_;

        // UDP socket used to receive FileBridge discovery broadcasts
        QUdpSocket socket_;

        /**
         * KnownPeer
         * Stores one discovered peer together with the last time its discovery heartbeat
         * was received
         */
        struct KnownPeer {
            DiscoveredPeer peer;
            qint64 lastSeenMs;
        };

        // Stores peers already discovered, keyed by their unique running instance ID
        QHash<QString, KnownPeer> knownPeers_;

        /**
         * serializeAnnouncement()
         * Encodes a discovery announcement into UDP payload bytes
         */
        QByteArray serializeAnnouncement(const DiscoveryAnnouncement& announcement);

        /**
         * deserializeAnnouncement()
         * Decodes UDP payload bytes into a discovery announcement
         */
        bool deserializeAnnouncement(const QByteArray& data, DiscoveryAnnouncement& announcement) const;

        /**
         * broadcastAnnouncement()
         * Sends this FileBridge instance discovery accouncement over the local network
         */
        void broadcastAnnouncement();
};


#endif
