#pragma once

#include "nostr.h"

#include <QObject>
#include <QString>
#include <cstdint>

namespace net {

// Abstract transport interface for sync events.
//
// Both RelayPool (Nostr relay) and WebRTCTransport (P2P data channels)
// implement this interface so SyncEngine can work with either backend.
//
// The event envelope is always net::NostrEvent — the transport is agnostic
// to the payload format (encrypted JSON blobs).
class SyncTransport : public QObject
{
    Q_OBJECT

public:
    explicit SyncTransport(QObject* parent = nullptr) : QObject(parent) {}
    ~SyncTransport() override = default;

    // Connect to the transport network (relays, signaling server, etc.).
    virtual void connectAll() = 0;

    // Disconnect gracefully.
    virtual void disconnectAll() = 0;

    // Ordered shutdown (cut connections, release resources).
    virtual void shutdown() = 0;

    // Publish an event to all connected peers/relays.
    virtual void publishToAll(const NostrEvent& ev) = 0;

    // Subscribe to a channel (identified by channelTag).
    // For Nostr: REQ subscription on all relays.
    // For WebRTC: join the signaling room for this channel.
    virtual void subscribeAll(const QString& channelTag, int64_t since) = 0;

    // True if at least one relay/peer is reachable.
    virtual bool isOnline() const = 0;

signals:
    // A sync event was received from a remote peer.
    void eventReceived(const net::NostrEvent& ev);

    // Online state changed (at least one relay/peer connected or disconnected).
    void onlineChanged(bool online);

    // A publish was acknowledged (relay OK, or peer confirmed receipt).
    // For WebRTC data channels (reliable ordered), this is emitted immediately
    // after the send buffer accepts the message.
    void publishAck(const QString& eventId, bool accepted, const QString& msg);
};

} // namespace net