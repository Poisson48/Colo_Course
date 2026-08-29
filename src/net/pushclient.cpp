#include "pushclient.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrl>

namespace net {

void sendPushWake(const QString &baseUrl, const QString &topic, const QString &title)
{
    if (baseUrl.isEmpty() || topic.isEmpty())
        return;

    QUrl root(baseUrl.trimmed());
    if (!root.isValid() || root.scheme().isEmpty())
        return;

    const QUrl endpoint = root.resolved(QUrl(topic));

    static QNetworkAccessManager nam;
    QNetworkRequest req(endpoint);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain"));
    if (!title.isEmpty())
        req.setRawHeader("Title", title.toUtf8());
    req.setRawHeader("Priority", "3");
    req.setTransferTimeout(8000);

    nam.post(req, QByteArray("sync"));
}

} // namespace net
