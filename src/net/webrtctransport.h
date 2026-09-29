#pragma once

#include "synctransport.h"
#include "nostr.h"

#include <QObject>
#include <QHash>
#include <QSet>
#include <QString>
#include <QWebSocket>
#include <QTimer>
#include <QJsonObject>
#include <memory>
#include <cstdint>
#include <vector>
#include <functional>

// Forward-declare libdatachannel types to avoid exposing the header.
namespace rtc {
class PeerConnection;
class DataChannel;
struct IceServer;
} // namespace rtc

namespace net {

// WebRTC P2P transport for sync events.
//
// Architecture:
//   1. Signaling server (WebSocket on colo crevettes) handles peer discovery
//      and SDP/ICE exchange. It carries NO shopping list data.
//   2. Once peers are discovered, WebRTC data channels are established directly
//      between devices (P2P). All sync data flows through these channels.
//   3. ICE servers (STUN/TURN) on colo crevettes help with NAT traversal.
//
// Signaling protocol (JSON over WebSocket):
//   → register { device_id }
//   → join { channel_tag }       (one per list)
//   ← peer_joined { device_id, channel_tag }
//   → offer  { to, sdp }
//   ← offer  { from, sdp }
//   → answer { to, sdp }
//   ← answer { from, sdp }
//   → candidate { to, candidate, sdpMid }
//   ← candidate { from, candidate, sdpMid }
//
// Data channel protocol (JSON):
//   → event { id, created_at, kind, tags, content, sig }
//   → ack   { event_id }
//   → subscribe { channel_tag, since }
//
class WebRTCTransport : public SyncTransport
{
    Q_OBJECT

public:
    explicit WebRTCTransport(QObject* parent = nullptr);
    ~WebRTCTransport() override;

    // Configure the signaling server URL (wss://…).
    void setSignalingUrl(const QUrl& url);

    // Configure ICE servers (STUN/TURN URLs).
    // Example: { "stun:colo-apps.les-crevettes-cevenoles.fr:3478",
    //            "turn:colo-apps.les-crevettes-cevenoles.fr:3478" }
    void setIceServers(const QStringList& urls);

    // Our device identity (from AppController).
    void setDeviceId(const QString& deviceId);

    // SyncTransport interface
    void connectAll() override;
    void disconnectAll() override;
    void shutdown() override;
    void publishToAll(const NostrEvent& ev) override;
    void subscribeAll(const QString& channelTag, int64_t since) override;
    bool isOnline() const override;

private:
    // ── Signaling ──
    void connectSignaling();
    void disconnectSignaling();
    void sendSignaling(const QJsonObject& msg);

    void onSignalingConnected();
    void onSignalingDisconnected();
    void onSignalingError(QAbstractSocket::SocketError error);
    void onSignalingMessage(const QString& text);

    void handlePeerJoined(const QJsonObject& msg);
    void handleOffer(const QJsonObject& msg);
    void handleAnswer(const QJsonObject& msg);
    void handleCandidate(const QJsonObject& msg);
    void handlePeerLeft(const QJsonObject& msg);

    // ── Peer connections ──
    struct PeerConnection {
        QString peerDeviceId;
        QString channelTag;  // which list channel this peer joined
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel> dc;
        bool dcOpen = false;
    };

    // Find or create a peer connection for a given peer + channel.
    PeerConnection* ensurePeer(const QString& peerDeviceId,
                               const QString& channelTag,
                               bool polite);
    void removePeer(const QString& peerDeviceId, const QString& channelTag);
    void sendToPeer(PeerConnection& peer, const QJsonObject& msg);
    void broadcastToChannel(const QString& channelTag, const QJsonObject& msg);

    std::vector<rtc::IceServer> buildIceServers() const;

    // ── Data channel handling ──
    void onDataChannelMessage(const QString& peerDeviceId,
                              const QString& channelTag,
                              const std::string& msg);
    void handleEvent(const QJsonObject& obj);
    void handleAck(const QJsonObject& obj);

    // ── Helpers ──
    void wireDataChannel(PeerConnection* peer,
                         WebRTCTransport* transport,
                         const QString& peerKey);
    void setOnline(bool online);
    void updateOnlineState();

    // ── State ──
    QWebSocket m_signaling;
    QUrl m_signalingUrl;
    QStringList m_iceUrls;
    QString m_deviceId;

    // peer connections: keyed by (peerDeviceId + ":" + channelTag)
    QHash<QString, std::unique_ptr<PeerConnection>> m_peers;

    // Channels we've subscribed to (and announced on signaling).
    QSet<QString> m_subscribedChannels;

    bool m_online = false;
    bool m_shuttingDown = false;
    int m_reconnectDelay = 1000;  // ms, exponential backoff for signaling
    QTimer m_reconnectTimer;
};

} // namespace net