#include "recipe_library_loader.h"

#include "../core/recipe_catalog_db.h"
#include "../core/recipe_library.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QDebug>
#include <QtConcurrent>

#include <vector>

Q_DECLARE_METATYPE(core::RecipeLibraryParseResult)

namespace app {

namespace {

constexpr const char *kRecipesManifestUrl =
    "https://colo-apps.les-crevettes-cevenoles.fr/releases/recipes-manifest.json";

constexpr int kRemoteCheckIntervalMs = 6 * 60 * 60 * 1000; // 6 h

bool s_catalogLoaded = false;
bool s_catalogLoadStarted = false;
std::vector<std::function<void(bool)>> s_pendingCallbacks;

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

void ensureParseResultMetaType()
{
    static const bool once = [] {
        qRegisterMetaType<core::RecipeLibraryParseResult>();
        return true;
    }();
    Q_UNUSED(once);
}

bool looksLikeRecipeLibraryJson(const QByteArray &json)
{
    return json.size() > 4096 && json.contains("\"recipes\"");
}

bool isValidCatalogDbFile(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() && info.size() > 4096;
}

bool copyFileAtomically(const QString &srcPath, const QString &destPath)
{
    QDir().mkpath(QFileInfo(destPath).absolutePath());
    const QString tmp = destPath + QStringLiteral(".tmp");
    if (QFile::exists(tmp) && !QFile::remove(tmp))
        return false;
    if (!QFile::copy(srcPath, tmp))
        return false;
    if (QFile::exists(destPath) && !QFile::remove(destPath))
        return false;
    return QFile::rename(tmp, destPath);
}

bool copyBundledDbTo(const QString &destPath)
{
    QFile bundled(QStringLiteral(":/data/recipe_catalog.db"));
    if (!bundled.exists()) {
        qWarning() << "[RecipeCatalog] recipe_catalog.db embarqué absent";
        return false;
    }
    if (!bundled.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = bundled.readAll();
    bundled.close();
    if (data.size() < 4096)
        return false;

    QDir().mkpath(QFileInfo(destPath).absolutePath());
    const QString tmp = destPath + QStringLiteral(".tmp");
    QFile out(tmp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (out.write(data) != data.size()) {
        out.remove();
        return false;
    }
    out.close();
    if (QFile::exists(destPath) && !QFile::remove(destPath))
        return false;
    return QFile::rename(tmp, destPath);
}

bool importJsonToDbFile(const QByteArray &json, const QString &destPath)
{
    const core::RecipeLibraryParseResult parsed = core::RecipeLibrary::parseJsonData(json);
    if (!parsed.ok)
        return false;

    QDir().mkpath(QFileInfo(destPath).absolutePath());
    const QString tmp = destPath + QStringLiteral(".import.tmp");
    if (QFile::exists(tmp))
        QFile::remove(tmp);

    if (!core::RecipeCatalogDb::open(tmp))
        return false;
    core::RecipeLibraryParseResult work = parsed;
    const bool ok = core::RecipeCatalogDb::importFromParsed(std::move(work));
    core::RecipeCatalogDb::close();
    if (!ok) {
        QFile::remove(tmp);
        return false;
    }
    if (QFile::exists(destPath) && !QFile::remove(destPath))
        return false;
    return QFile::rename(tmp, destPath);
}

QString resolveCatalogDbPath()
{
    const QString cache = recipeCatalogCachePath();
    if (isValidCatalogDbFile(cache))
        return cache;

    // Migration : ancien cache JSON → SQLite une fois.
    const QString jsonCache = recipeLibraryCachePath();
    QFile cachedJson(jsonCache);
    if (cachedJson.exists() && cachedJson.open(QIODevice::ReadOnly)) {
        const QByteArray json = cachedJson.readAll();
        cachedJson.close();
        if (looksLikeRecipeLibraryJson(json) && importJsonToDbFile(json, cache)) {
            qWarning() << "[RecipeCatalog] migration JSON cache → SQLite";
            return cache;
        }
    }

    if (copyBundledDbTo(cache) && isValidCatalogDbFile(cache))
        return cache;

    // Secours : JSON embarqué (première install ou DB absente du build).
    QFile bundledJson(QStringLiteral(":/data/recipe_library.json"));
    if (bundledJson.open(QIODevice::ReadOnly)) {
        const QByteArray json = bundledJson.readAll();
        bundledJson.close();
        if (looksLikeRecipeLibraryJson(json) && importJsonToDbFile(json, cache))
            return cache;
    }

    return {};
}

bool openResolvedCatalog(const QString &path)
{
    if (!isValidCatalogDbFile(path))
        return false;
    return core::RecipeLibrary::openCatalogDb(path);
}

void scheduleRemoteCheck(QObject *context, std::function<void(bool)> onUpdated);
void startRemoteCheckTimer(QObject *context, std::function<void()> onRemoteUpdated);

void flushPending(bool ok)
{
    auto pending = std::move(s_pendingCallbacks);
    s_pendingCallbacks.clear();
    for (const auto &cb : pending) {
        if (cb)
            cb(ok);
    }
}

void markCatalogReady(QObject *context, bool ok, std::function<void(bool)> onInitialLoad,
                      std::function<void()> onRemoteUpdated)
{
    s_catalogLoaded = ok;
    s_catalogLoadStarted = false;

    if (ok) {
        qWarning() << "[RecipeCatalog] catalogue prêt :"
                   << core::RecipeLibrary::count() << "recettes (SQLite)";
        scheduleRemoteCheck(context, [onRemoteUpdated](bool updated) {
            if (updated && onRemoteUpdated)
                onRemoteUpdated();
        });
        startRemoteCheckTimer(context, onRemoteUpdated);
    } else {
        qWarning() << "[RecipeCatalog] catalogue indisponible";
    }

    if (onInitialLoad)
        onInitialLoad(ok);
    flushPending(ok);
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

void applyJsonUpdateOnMain(QObject *context, const QByteArray &json,
                           std::function<void(bool updated)> onDone)
{
    auto apply = [context, json, onDone]() {
        const QString cache = recipeCatalogCachePath();
        if (!importJsonToDbFile(json, cache)) {
            if (onDone)
                onDone(false);
            return;
        }
        if (!core::RecipeLibrary::openCatalogDb(cache)) {
            if (onDone)
                onDone(false);
            return;
        }
        qWarning() << "[RecipeCatalog] MAJ serveur appliquée :"
                   << core::RecipeLibrary::count() << "recettes";
        if (onDone)
            onDone(true);
    };

    if (context)
        QTimer::singleShot(0, context, std::move(apply));
    else
        apply();
}

void downloadAndApply(const RecipesManifest &manifest,
                      QObject *context,
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
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, context, onDone]() {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[RecipeCatalog] téléchargement échoué :" << reply->errorString();
            if (onDone)
                onDone(false);
            return;
        }
        if (!looksLikeRecipeLibraryJson(body)) {
            if (onDone)
                onDone(false);
            return;
        }
        applyJsonUpdateOnMain(context, body, onDone);
    });
}

void scheduleRemoteCheck(QObject *context, std::function<void(bool)> onUpdated)
{
    fetchManifest([context, onUpdated](const RecipesManifest &manifest) {
        downloadAndApply(manifest, context, [onUpdated](bool updated) {
            if (updated && onUpdated)
                onUpdated(updated);
        });
    });
}

void startRemoteCheckTimer(QObject *context, std::function<void()> onRemoteUpdated)
{
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
}

void doLoadCatalog(QObject *context,
                   std::function<void(bool ok)> onInitialLoad,
                   std::function<void()> onRemoteUpdated)
{
    ensureParseResultMetaType();

    auto *watcher = new QFutureWatcher<QString>(context);
    QObject::connect(watcher, &QFutureWatcher<QString>::finished, context,
                     [watcher, context, onInitialLoad, onRemoteUpdated]() {
                         const QString path = watcher->result();
                         watcher->deleteLater();
                         const bool ok = openResolvedCatalog(path);
                         markCatalogReady(context, ok, onInitialLoad, onRemoteUpdated);
                     },
                     Qt::QueuedConnection);

    watcher->setFuture(QtConcurrent::run([]() -> QString {
        return resolveCatalogDbPath();
    }));
}

} // namespace

QString recipeCatalogCachePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + QStringLiteral("/recipe_catalog.db");
}

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
    const QString cache = recipeCatalogCachePath();
    if (copyBundledDbTo(cache))
        return core::RecipeLibrary::openCatalogDb(cache);

    QFile bundledJson(QStringLiteral(":/data/recipe_library.json"));
    if (!bundledJson.open(QIODevice::ReadOnly))
        return false;
    return importJsonToDbFile(bundledJson.readAll(), cache)
           && core::RecipeLibrary::openCatalogDb(cache);
}

bool isRecipeCatalogLoaded()
{
    return s_catalogLoaded;
}

void loadRecipeCatalogAsync(QObject *context,
                            std::function<void(bool ok)> onInitialLoad,
                            std::function<void()> onRemoteUpdated,
                            int deferredMs)
{
    if (s_catalogLoaded) {
        if (onInitialLoad)
            onInitialLoad(true);
        return;
    }
    if (s_catalogLoadStarted) {
        if (onInitialLoad)
            s_pendingCallbacks.push_back(std::move(onInitialLoad));
        return;
    }
    s_catalogLoadStarted = true;

    auto start = [context, onInitialLoad = std::move(onInitialLoad),
                  onRemoteUpdated = std::move(onRemoteUpdated)]() mutable {
        doLoadCatalog(context, std::move(onInitialLoad), std::move(onRemoteUpdated));
    };

    if (deferredMs > 0)
        QTimer::singleShot(deferredMs, context, std::move(start));
    else
        start();
}

void ensureRecipeCatalogLoaded(QObject *context, std::function<void(bool ok)> onDone)
{
    if (s_catalogLoaded) {
        if (onDone)
            onDone(true);
        return;
    }
    loadRecipeCatalogAsync(context, std::move(onDone), nullptr, 0);
}

void refreshRecipeLibraryFromServer(QObject *context,
                                    std::function<void(bool updated)> onDone)
{
    if (!s_catalogLoaded) {
        if (onDone)
            onDone(false);
        return;
    }
    scheduleRemoteCheck(context, onDone);
}

} // namespace app
