#include "relayclient.h"

#include <QJsonDocument>
#include <QUuid>
#include <QDebug>

namespace net {

RelayClient::RelayClient(const QUrl& url, QObject* parent)
    : QObject(parent)
    , m_url(url)
{
    m_reconnectTimer.setSingleShot(true);

    connect(&m_socket, &QWebSocket::connected,
            this, &RelayClient::onConnected);
    connect(&m_socket, &QWebSocket::disconnected,
            this, &RelayClient::onDisconnected);
    connect(&m_socket, &QWebSocket::textMessageReceived,
            this, &RelayClient::onTextMessageReceived);
    connect(&m_reconnectTimer, &QTimer::timeout,
            this, &RelayClient::onReconnectTimer);
}

void RelayClient::connectToRelay()
{
    m_intentionalDisconnect = false;
    m_socket.open(m_url);
}

void RelayClient::disconnectFromRelay()
{
    m_intentionalDisconnect = true;
    m_reconnectTimer.stop();
    m_socket.close();
}

void RelayClient::publish(const NostrEvent& ev)
{
    sendJson(makeEventMsg(ev));
}

void RelayClient::subscribe(const QString& channelTag, int64_t since)
{
    for (auto& sub : m_subscriptions) {
        if (sub.channelTag == channelTag) {
            if (sub.since == since)
                return;
            if (m_socket.state() == QAbstractSocket::ConnectedState && !sub.subId.isEmpty())
                sendJson(makeCloseMsg(sub.subId));
            sub.since = since;
            sub.subId = makeSubId();
            if (m_socket.state() == QAbstractSocket::ConnectedState)
                sendReq(sub);
            return;
        }
    }

    ActiveSub sub;
    sub.channelTag = channelTag;
    sub.since      = since;
    sub.subId      = makeSubId();
    m_subscriptions.push_back(sub);

    if (m_socket.state() == QAbstractSocket::ConnectedState)
        sendReq(sub);
}

void RelayClient::closeSubscription()
{
    if (m_socket.state() == QAbstractSocket::ConnectedState) {
        for (const auto& sub : m_subscriptions) {
            if (!sub.subId.isEmpty())
                sendJson(makeCloseMsg(sub.subId));
        }
    }
    m_subscriptions.clear();
}

bool RelayClient::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

// ── Private slots ──────────────────────────────────────────────────────────

void RelayClient::onConnected()
{
    qDebug() << "[RelayClient]" << m_url.toString() << "connected";
    resetBackoff();
    emit connected();
    resubscribe();
}

void RelayClient::onDisconnected()
{
    qDebug() << "[RelayClient]" << m_url.toString() << "disconnected";
    emit disconnected();

    if (!m_intentionalDisconnect)
        scheduleReconnect();
}

void RelayClient::onTextMessageReceived(const QString& msg)
{
    const RelayMsg parsed = parseRelayMsg(msg);

    switch (parsed.type) {
    case RelayMsgType::Event:
        emit eventReceived(parsed.event);
        break;
    case RelayMsgType::Eose:
        emit eose();
        break;
    case RelayMsgType::Ok:
        emit publishAck(parsed.okEventId, parsed.okAccepted, parsed.okMessage);
        break;
    case RelayMsgType::Notice:
        qDebug() << "[RelayClient] NOTICE from" << m_url.toString()
                 << ":" << parsed.notice;
        break;
    case RelayMsgType::Unknown:
        qDebug() << "[RelayClient] Unknown relay message:" << msg.left(200);
        break;
    }
}

void RelayClient::onReconnectTimer()
{
    qDebug() << "[RelayClient] Reconnecting to" << m_url.toString()
             << "backoff=" << m_backoffMs << "ms";
    m_socket.open(m_url);
}

// ── Private helpers ────────────────────────────────────────────────────────

QString RelayClient::makeSubId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(16);
}

void RelayClient::sendReq(const ActiveSub& sub)
{
    const QStringList kinds = { QStringLiteral("4545") };
    const QStringList tVals = { sub.channelTag };
    sendJson(makeReqMsg(sub.subId, kinds, tVals, sub.since));
}

void RelayClient::sendJson(const QJsonArray& msg)
{
    if (m_socket.state() != QAbstractSocket::ConnectedState) {
        qWarning() << "[RelayClient] send called while not connected";
        return;
    }
    const QByteArray bytes = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    m_socket.sendTextMessage(QString::fromUtf8(bytes));
}

void RelayClient::scheduleReconnect()
{
    // Backoff: 1 s → 2 s → 4 s … capped at 60 s.
    m_reconnectTimer.start(m_backoffMs);
    m_backoffMs = std::min(m_backoffMs * 2, kMaxBackoffMs);
}

void RelayClient::resetBackoff()
{
    m_backoffMs = kInitBackoffMs;
}

void RelayClient::resubscribe()
{
    for (auto& sub : m_subscriptions) {
        if (!sub.subId.isEmpty())
            sendJson(makeCloseMsg(sub.subId));
        sub.subId = makeSubId();
        sendReq(sub);
    }
}

} // namespace net
