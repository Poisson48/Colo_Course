#include "relaypool.h"

#include <QDebug>

namespace net {

RelayPool::RelayPool(QObject* parent)
    : QObject(parent)
{}

// static
QList<QUrl> RelayPool::defaultRelays()
{
    return {
        QUrl("wss://relay.damus.io"),
        QUrl("wss://nos.lol"),
        QUrl("wss://relay.nostr.band"),
        QUrl("wss://offchain.pub"),
    };
}

void RelayPool::setRelays(const QList<QUrl>& urls)
{
    // Disconnect old clients.
    for (auto& c : m_clients)
        c->disconnectFromRelay();
    m_clients.clear();

    for (const QUrl& url : urls) {
        auto client = std::make_unique<RelayClient>(url, this);

        connect(client.get(), &RelayClient::connected,
                this, &RelayPool::onClientConnected);
        connect(client.get(), &RelayClient::disconnected,
                this, &RelayPool::onClientDisconnected);
        connect(client.get(), &RelayClient::eventReceived,
                this, &RelayPool::onClientEvent);
        connect(client.get(), &RelayClient::eose,
                this, &RelayPool::onClientEose);
        connect(client.get(), &RelayClient::publishAck,
                this, &RelayPool::onClientAck);

        m_clients.push_back(std::move(client));
    }

    // Ré-appliquer toutes les souscriptions actives (multi-listes).
    for (auto it = m_subscriptions.constBegin(); it != m_subscriptions.constEnd(); ++it) {
        for (auto& c : m_clients)
            c->subscribe(it.key(), it.value());
    }
}

void RelayPool::connectAll()
{
    for (auto& c : m_clients)
        c->connectToRelay();
}

void RelayPool::disconnectAll()
{
    for (auto& c : m_clients)
        c->disconnectFromRelay();
}

void RelayPool::publishToAll(const NostrEvent& ev)
{
    for (auto& c : m_clients) {
        if (c->isConnected())
            c->publish(ev);
    }
}

void RelayPool::subscribeAll(const QString& channelTag, int64_t since)
{
    m_subscriptions[channelTag] = since;
    for (auto& c : m_clients)
        c->subscribe(channelTag, since);
}

// ── Private slots ──────────────────────────────────────────────────────────

void RelayPool::onClientConnected()
{
    updateOnlineState();
}

void RelayPool::onClientDisconnected()
{
    updateOnlineState();
}

void RelayPool::onClientEvent(const NostrEvent& ev)
{
    // Dedup: skip events we've already forwarded.
    if (ev.id.isEmpty() || m_seenIds.contains(ev.id))
        return;

    m_seenIds.insert(ev.id);
    emit eventReceived(ev);
}

void RelayPool::onClientEose()
{
    emit eose();
}

void RelayPool::onClientAck(const QString& eventId, bool accepted, const QString& msg)
{
    emit publishAck(eventId, accepted, msg);
}

// ── Private helpers ────────────────────────────────────────────────────────

void RelayPool::updateOnlineState()
{
    bool anyConnected = false;
    for (const auto& c : m_clients) {
        if (c->isConnected()) {
            anyConnected = true;
            break;
        }
    }

    if (anyConnected != m_online) {
        m_online = anyConnected;
        emit onlineChanged(m_online);
    }
}

} // namespace net
