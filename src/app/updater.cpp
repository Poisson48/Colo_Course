#include "updater.h"
#include "platform.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QDebug>

namespace app {

namespace {

constexpr const char* kUpdateManifest =
    "https://colo-apps.les-crevettes-cevenoles.fr/releases/manifest.json";

constexpr int kRecheckIntervalMs = 15 * 60 * 1000; // 15 min

constexpr const char* kSeenNotesKey = "updater/seenNotesVersion";

#ifndef COLO_APP_VERSION
#  define COLO_APP_VERSION "0.0.0"
#endif

QString stripV(QString v)
{
    if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
        v.remove(0, 1);
    return v;
}

} // namespace

Updater::Updater(QObject *parent)
    : QObject(parent)
{
    m_recheckTimer = new QTimer(this);
    m_recheckTimer->setInterval(kRecheckIntervalMs);
    connect(m_recheckTimer, &QTimer::timeout, this, [this]() {
        if (m_state == Idle || m_state == Available)
            check();
    });
}

bool Updater::parseManifest(const QByteArray &json, ManifestData *out)
{
    if (!out)
        return false;
    *out = {};

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    const QString version = stripV(root.value(QStringLiteral("version")).toString());
    if (version.isEmpty())
        return false;

    out->version     = version;
    out->notes       = root.value(QStringLiteral("notes")).toString().trimmed();
    out->publishedAt = root.value(QStringLiteral("publishedAt")).toString();
    out->apkUrl      = root.value(QStringLiteral("apkUrl")).toString();
    out->appImageUrl = root.value(QStringLiteral("appImageUrl")).toString();
    out->releaseUrl  = root.value(QStringLiteral("releaseUrl")).toString();

    const QJsonArray hist = root.value(QStringLiteral("changelog")).toArray();
    if (!hist.isEmpty()) {
        for (const QJsonValue &v : hist) {
            const QJsonObject obj = v.toObject();
            const QString ver = stripV(obj.value(QStringLiteral("version")).toString());
            if (ver.isEmpty())
                continue;
            QVariantMap entry;
            entry.insert(QStringLiteral("version"), ver);
            entry.insert(QStringLiteral("notes"),
                         obj.value(QStringLiteral("notes")).toString().trimmed());
            entry.insert(QStringLiteral("publishedAt"),
                         obj.value(QStringLiteral("publishedAt")).toString());
            out->changelog.append(entry);
        }
    } else {
        QVariantMap entry;
        entry.insert(QStringLiteral("version"), version);
        entry.insert(QStringLiteral("notes"), out->notes);
        entry.insert(QStringLiteral("publishedAt"), out->publishedAt);
        out->changelog.append(entry);
    }

    return true;
}

void Updater::applyManifestData(const ManifestData &data)
{
    m_changelog.clear();
    m_apkUrl.clear();
    m_releaseUrl.clear();
    m_latestVersion.clear();

    m_changelog = data.changelog;
    rebuildDerivedNotes();

    const QString current = currentVersion();
    if (!isNewer(data.version, current)) {
        setState(Idle);
        return;
    }

    m_latestVersion = data.version;
    m_apkUrl = data.apkUrl;
    m_releaseUrl = data.releaseUrl.isEmpty() ? data.appImageUrl : data.releaseUrl;
    setState(Available);
}

void Updater::startRecheckTimer()
{
    if (m_recheckTimer && !m_recheckTimer->isActive())
        m_recheckTimer->start();
}

QString Updater::currentVersion() const
{
    return QStringLiteral(COLO_APP_VERSION);
}

bool Updater::canInstall() const
{
#ifdef Q_OS_ANDROID
    return true;
#else
    return false;
#endif
}

QString Updater::notesFromBody(const QString &body)
{
    QStringList kept;
    for (const QString &line : body.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed == QStringLiteral("---"))
            break;
        QString clean = line;
        while (clean.startsWith(QLatin1Char('#')))
            clean.remove(0, 1);
        kept << clean.trimmed();
    }

    while (!kept.isEmpty() && kept.last().isEmpty())
        kept.removeLast();

    return kept.join(QLatin1Char('\n')).trimmed();
}

bool Updater::isNewer(const QString &candidate, const QString &current)
{
    const auto parts = [](QString v) {
        if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
            v.remove(0, 1);
        QList<int> out;
        for (const QString &p : v.split(QLatin1Char('.'))) {
            int digits = 0;
            while (digits < p.size() && p.at(digits).isDigit())
                ++digits;
            out << p.left(digits).toInt();
        }
        return out;
    };

    const QList<int> a = parts(candidate);
    const QList<int> b = parts(current);
    if (a.isEmpty())
        return false;

    for (int i = 0; i < std::max(a.size(), b.size()); ++i) {
        const int x = i < a.size() ? a[i] : 0;
        const int y = i < b.size() ? b[i] : 0;
        if (x != y)
            return x > y;
    }
    return false;
}

QString Updater::formatEntries(const QVariantList &entries)
{
    QStringList blocks;
    for (const QVariant &v : entries) {
        const QVariantMap m = v.toMap();
        const QString ver = m.value(QStringLiteral("version")).toString();
        const QString notes = m.value(QStringLiteral("notes")).toString().trimmed();
        if (ver.isEmpty())
            continue;
        if (notes.isEmpty())
            blocks << QStringLiteral("Version %1").arg(ver);
        else
            blocks << QStringLiteral("Version %1\n\n%2").arg(ver, notes);
    }
    return blocks.join(QStringLiteral("\n\n————————————\n\n")).trimmed();
}

void Updater::rebuildDerivedNotes()
{
    const QString current = currentVersion();
    QSettings settings;
    const QString seen = stripV(
        settings.value(QLatin1String(kSeenNotesKey), QString()).toString());

    QVariantList pending;   // > current
    QVariantList whatsNew;  // <= current && > seen (ou tout <= current si never seen)

    for (const QVariant &v : m_changelog) {
        const QVariantMap m = v.toMap();
        const QString ver = m.value(QStringLiteral("version")).toString();
        if (ver.isEmpty())
            continue;
        if (isNewer(ver, current)) {
            pending.append(m);
        } else if (seen.isEmpty() || isNewer(ver, seen)) {
            // Inclure la version installée elle-même dans le « après maj ».
            whatsNew.append(m);
        }
    }

    m_releaseNotes  = formatEntries(pending);
    // Première install : pas de « quoi de neuf » (tout serait du bruit). On pose
    // seen = current silencieusement.
    if (seen.isEmpty()) {
        settings.setValue(QLatin1String(kSeenNotesKey), current);
        m_whatsNewNotes.clear();
    } else {
        m_whatsNewNotes = formatEntries(whatsNew);
    }
    emit changelogChanged();
}

void Updater::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;

    if (s == Available)
        qInfo() << "[Updater] version" << m_latestVersion
                << "disponible (nous sommes en" << currentVersion() << ")";
    else if (s == Failed)
        qWarning() << "[Updater] échec du téléchargement de" << m_apkUrl;

    emit stateChanged();
}

void Updater::check()
{
    if (m_state == Checking || m_state == Downloading)
        return;

    setState(Checking);

    QNetworkRequest req{ QUrl(QString::fromLatin1(kUpdateManifest)) };
    req.setRawHeader("User-Agent", "ColoCourse");
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);

    QNetworkReply *reply = m_net.get(req);
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[Updater] manifest inaccessible :" << reply->errorString();
            setState(Idle);
            startRecheckTimer();
            return;
        }

        ManifestData data;
        if (!parseManifest(reply->readAll(), &data)) {
            qWarning() << "[Updater] manifest invalide";
            setState(Idle);
            startRecheckTimer();
            return;
        }

        applyManifestData(data);
        startRecheckTimer();
    });
}

void Updater::download()
{
    if (m_apkUrl.isEmpty()) {
        install();
        return;
    }
    if (m_state == Downloading)
        return;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    m_apkPath = dir + QStringLiteral("/colocourse-") + m_latestVersion
              + QStringLiteral(".apk");

    QNetworkRequest req{ QUrl(m_apkUrl) };
    req.setRawHeader("User-Agent", "ColoCourse");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    m_progress = 0.0;
    emit progressChanged();
    setState(Downloading);

    QNetworkReply *reply = m_net.get(req);
    m_reply = reply;

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        m_progress = (total > 0) ? qreal(received) / qreal(total) : 0.0;
        emit progressChanged();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            setState(Failed);
            return;
        }

        QFile out(m_apkPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            setState(Failed);
            return;
        }
        const QByteArray body = reply->readAll();
        const qint64 written = out.write(body);
        out.close();

        if (written != body.size() || body.isEmpty()) {
            QFile::remove(m_apkPath);
            setState(Failed);
            return;
        }

        setState(Ready);
    });
}

void Updater::install()
{
    if (canInstall() && m_state == Ready && !m_apkPath.isEmpty()) {
        if (platformInstallApk(m_apkPath))
            return;
        setState(Failed);
        return;
    }

    if (!m_releaseUrl.isEmpty())
        QDesktopServices::openUrl(QUrl(m_releaseUrl));
}

void Updater::dismiss()
{
    if (m_reply)
        m_reply->abort();
    setState(Idle);
}

void Updater::acknowledgeNotes()
{
    QSettings settings;
    settings.setValue(QLatin1String(kSeenNotesKey), currentVersion());
    m_whatsNewNotes.clear();
    emit changelogChanged();
}

} // namespace app
