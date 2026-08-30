#include "recipe_library_loader.h"

#include "../core/recipe_library.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QDebug>
#include <QtConcurrent>

namespace app {

namespace {

constexpr const char *kRecipesManifestUrl =
    "https://colo-apps.les-crevettes-cevenoles.fr/releases/recipes-manifest.json";

constexpr int kRemoteCheckIntervalMs = 6 * 60 * 60 * 1000; // 6 h

QNetworkAccessManager &netManager()
{
    static QNetworkAccessManager s_net;
    return s_net;
}

QTimer &remoteCheckTimer()
{
    static QTimer s_timer;
    return s_timer;
}

bool loadJsonFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return core::RecipeLibrary::loadFromJson(f.readAll());
}

bool loadBundled()
{
    QFile f(QStringLiteral(":/data/recipe_library.json"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return core::RecipeLibrary::loadFromJson(f.readAll());
}

bool saveCacheAtomically(const QByteArray &json)
{
    const QString path = recipeLibraryCachePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString tmp = path + QStringLiteral(".tmp");
    QFile out(tmp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (out.write(json) != json.size()) {
        out.remove();
        return false;
    }
    out.close();
    if (QFile::exists(path) && !QFile::remove(path))
        return false;
    return QFile::rename(tmp, path);
}

void fetchManifest(std::function<void(RecipesManifest)> onResult)
{
    QNetworkRequest req(QUrl(QString::fromLatin1(kRecipesManifestUrl)));
    req.setRawHeader("User-Agent", "ColoCourse");
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);

    QNetworkReply *reply = netManager().get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, onResult]() {
        reply->deleteLater();
        RecipesManifest manifest;
        if (reply->error() == QNetworkReply::NoError)
            parseRecipesManifest(reply->readAll(), &manifest);
        onResult(manifest);
    });
}

void downloadAndApply(const RecipesManifest &manifest,
                      std::function<void(bool updated)> onDone)
{
    if (manifest.url.isEmpty() || manifest.count <= 0) {
        if (onDone)
            onDone(false);
        return;
    }

    if (manifest.count <= core::RecipeLibrary::count()) {
        if (onDone)
            onDone(false);
        return;
    }

    QNetworkRequest req(QUrl(manifest.url));
    req.setRawHeader("User-Agent", "ColoCourse");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = netManager().get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, manifest, onDone]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[RecipeLibrary] téléchargement échoué :" << reply->errorString();
            if (onDone)
                onDone(false);
            return;
        }

        const QByteArray body = reply->readAll();
        if (!core::RecipeLibrary::loadFromJson(body)) {
            qWarning() << "[RecipeLibrary] JSON distant invalide";
            if (onDone)
                onDone(false);
            return;
        }

        if (!saveCacheAtomically(body)) {
            qWarning() << "[RecipeLibrary] impossible d'écrire le cache local";
            if (onDone)
                onDone(false);
            return;
        }

        qInfo() << "[RecipeLibrary] catalogue mis à jour :" << core::RecipeLibrary::count()
                << "recettes";
        if (onDone)
            onDone(true);
    });
}

void scheduleRemoteCheck(QObject *context, std::function<void(bool)> onUpdated)
{
    fetchManifest([context, onUpdated](const RecipesManifest &manifest) {
        downloadAndApply(manifest, [onUpdated](bool updated) {
            if (updated && onUpdated)
                onUpdated(updated);
        });
    });
}

} // namespace

QString recipeLibraryCachePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + QStringLiteral("/recipe_library.json");
}

bool parseRecipesManifest(const QByteArray &json, RecipesManifest *out)
{
    if (!out)
        return false;
    *out = {};

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    out->version   = root.value(QStringLiteral("version")).toInt();
    out->count     = root.value(QStringLiteral("count")).toInt();
    out->updatedAt = root.value(QStringLiteral("updatedAt")).toString();
    out->url       = root.value(QStringLiteral("url")).toString();
    return out->count > 0 && !out->url.isEmpty();
}

bool loadRecipeLibraryFromResource()
{
    return loadBundled();
}

void loadRecipeLibraryAsync(QObject *context,
                            std::function<void(bool ok)> onInitialLoad,
                            std::function<void()> onRemoteUpdated)
{
    auto *watcher = new QFutureWatcher<bool>(context);
    QObject::connect(watcher, &QFutureWatcher<bool>::finished, context,
                     [watcher, context, onInitialLoad, onRemoteUpdated]() {
                         const bool ok = watcher->result();
                         watcher->deleteLater();
                         if (onInitialLoad)
                             onInitialLoad(ok);

                         if (!ok)
                             return;

                         scheduleRemoteCheck(context, [onRemoteUpdated](bool updated) {
                             if (updated && onRemoteUpdated)
                                 onRemoteUpdated();
                         });

                         QTimer &timer = remoteCheckTimer();
                         if (!timer.parent())
                             timer.setParent(context);
                         timer.setInterval(kRemoteCheckIntervalMs);
                         QObject::disconnect(&timer, nullptr, context, nullptr);
                         QObject::connect(&timer, &QTimer::timeout, context, [context, onRemoteUpdated]() {
                             refreshRecipeLibraryFromServer(context, [onRemoteUpdated](bool updated) {
                                 if (updated && onRemoteUpdated)
                                     onRemoteUpdated();
                             });
                         });
                         if (!timer.isActive())
                             timer.start();
                     });

    watcher->setFuture(QtConcurrent::run([]() -> bool {
        const QString cache = recipeLibraryCachePath();
        if (QFile::exists(cache) && loadJsonFile(cache))
            return true;
        return loadBundled();
    }));
}

void refreshRecipeLibraryFromServer(QObject *context,
                                    std::function<void(bool updated)> onDone)
{
    scheduleRemoteCheck(context, onDone);
}

} // namespace app
