#include "pushclient.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QNetworkReply>
#include <QDebug>
#include <QUrl>

namespace net {

void sendPushWake(const QString &baseUrl, const QString &topic, const QString &title,
                  const QString &senderDeviceId)
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
    if (!senderDeviceId.isEmpty()) {
        req.setRawHeader("Tags",
                         QStringLiteral("device:%1").arg(senderDeviceId).toUtf8());
    }
    req.setTransferTimeout(8000);

    nam.post(req, QByteArray("sync"));
}

void sendFcmPush(const QString &endpoint, const QString &topic)
{
    if (endpoint.isEmpty() || topic.isEmpty()) return;
    QUrl url(endpoint.trimmed());
    if (!url.isValid() || url.scheme().isEmpty()) return;

    QString app = QCoreApplication::applicationName();
    app = app.contains(QStringLiteral("Taches"), Qt::CaseInsensitive)
        ? QStringLiteral("colotaches") : QStringLiteral("colocourse");

    QJsonObject body;
    body[QStringLiteral("app")] = app;
    body[QStringLiteral("topic")] = topic;

    static QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(8000);
    auto* reply = nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, url, topic]() {
        reply->deleteLater();
        qInfo() << "[Push] FCM" << topic << "->" << url.toString()
                << "http=" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                << "err=" << reply->errorString();
    });
    qInfo() << "[Push] FCM POST" << url.toString() << topic;
}

} // namespace net
