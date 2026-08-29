#pragma once

#include <QString>

namespace net {

// Réveil push via ntfy (serveur Colo Course). Fire-and-forget HTTP POST.
void sendPushWake(const QString &baseUrl, const QString &topic,
                  const QString &title);

inline QString pushTopicForChannel(const QString &channelTag)
{
    return QStringLiteral("colo-%1").arg(channelTag);
}

} // namespace net
