#include "recipe_library_loader.h"

#include "../core/recipe_catalog_db.h"
#include "../core/recipe_library.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QCryptographicHash>
#include <QSqlDatabase>
#include <QSqlQuery>
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
RecipeCatalogState s_catalogState = RecipeCatalogState::Idle;
QString s_catalogError;
std::vector<std::function<void(bool)>> s_pendingCallbacks;
std::function<void()> s_stateListener;

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

void setCatalogState(RecipeCatalogState state, const QString &error = {})
{
    s_catalogState = state;
    s_catalogError = error;
    qWarning() << "[RecipeCatalog] état:" << recipeCatalogStateString(state)
               << (error.isEmpty() ? QString() : QStringLiteral("— ") + error);
    if (s_stateListener)
        s_stateListener();
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
    QString err;
    if (core::RecipeCatalogDb::validateFile(cache, &err))
        return cache;

    // Migration : ancien cache JSON → SQLite une fois.
    const QString jsonCache = recipeLibraryCachePath();
    QFile cachedJson(jsonCache);
    if (cachedJson.exists() && cachedJson.open(QIODevice::ReadOnly)) {
        const QByteArray json = cachedJson.readAll();
        cachedJson.close();
        if (looksLikeRecipeLibraryJson(json) && importJsonToDbFile(json, cache)
            && core::RecipeCatalogDb::validateFile(cache, &err)) {
            qWarning() << "[RecipeCatalog] migration JSON cache → SQLite";
            return cache;
        }
    }

    if (copyBundledDbTo(cache) && core::RecipeCatalogDb::validateFile(cache, &err))
        return cache;

    qWarning() << "[RecipeCatalog] résolution échouée:" << err;
    return {};
}

bool openResolvedCatalog(const QString &path, QString *errorOut)
{
    QString err;
    if (!core::RecipeCatalogDb::validateFile(path, &err)) {
        if (errorOut)
            *errorOut = err;
        return false;
    }
    if (!core::RecipeLibrary::openCatalogDb(path)) {
        if (errorOut)
            *errorOut = QStringLiteral("ouverture catalogue impossible");
        return false;
    }
    return true;
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

void markCatalogReady(QObject *context, bool ok, const QString &error,
                      std::function<void(bool)> onInitialLoad,
                      std::function<void()> onRemoteUpdated)
{
    s_catalogLoaded = ok;
    s_catalogLoadStarted = false;

    if (ok) {
        setCatalogState(RecipeCatalogState::Ready);
        qWarning() << "[RecipeCatalog] catalogue prêt :"
                   << core::RecipeLibrary::count() << "recettes (SQLite)";
        scheduleRemoteCheck(context, [onRemoteUpdated](bool updated) {
            if (updated && onRemoteUpdated)
                onRemoteUpdated();
        });
        startRemoteCheckTimer(context, onRemoteUpdated);
    } else {
        setCatalogState(RecipeCatalogState::Error, error);
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

bool verifySha256(const QByteArray &data, const QString &expectedHex)
{
    if (expectedHex.isEmpty())
        return true;
    const QByteArray hash =
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    return hash.compare(expectedHex.toLatin1(), Qt::CaseInsensitive) == 0;
}

void applyDbUpdateOnMain(QObject *context, const QByteArray &dbData,
                         const RecipesManifest &manifest,
                         std::function<void(bool updated)> onDone)
{
    auto apply = [context, dbData, manifest, onDone]() {
        const QString cache = recipeCatalogCachePath();
        const QString tmp = cache + QStringLiteral(".download.tmp");

        QFile out(tmp);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (onDone)
                onDone(false);
            return;
        }
        if (out.write(dbData) != dbData.size()) {
            out.remove();
            if (onDone)
                onDone(false);
            return;
        }
        out.close();

        QString err;
        if (!core::RecipeCatalogDb::validateFile(tmp, &err)) {
            QFile::remove(tmp);
            qWarning() << "[RecipeCatalog] DB téléchargée invalide:" << err;
            if (onDone)
                onDone(false);
            return;
        }

        if (manifest.count > 0) {
            const QString connName = QStringLiteral("recipe_catalog_verify_")
                                     + QString::number(QDateTime::currentMSecsSinceEpoch());
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(tmp);
            if (db.open()) {
                QSqlQuery q(db);
                if (q.exec(QStringLiteral("SELECT COUNT(*) FROM recipes")) && q.next()) {
                    const int got = q.value(0).toInt();
                    if (got < manifest.count - 100) {
                        qWarning() << "[RecipeCatalog] count manifest"
                                   << manifest.count << "vs db" << got;
                        db.close();
                        QSqlDatabase::removeDatabase(connName);
                        QFile::remove(tmp);
                        if (onDone)
                            onDone(false);
                        return;
                    }
                }
                db.close();
                QSqlDatabase::removeDatabase(connName);
            }
        }

        if (QFile::exists(cache) && !QFile::remove(cache)) {
            QFile::remove(tmp);
            if (onDone)
                onDone(false);
            return;
        }
        if (!QFile::rename(tmp, cache)) {
            QFile::remove(tmp);
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

void applyJsonUpdateOnMain(QObject *context, const QByteArray &json,
                           std::function<void(bool updated)> onDone)
{
    auto apply = [json, onDone]() {
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
        qWarning() << "[RecipeCatalog] MAJ JSON legacy appliquée :"
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

    const bool wantsSqlite = manifest.format == QStringLiteral("sqlite")
                             || manifest.url.endsWith(QStringLiteral(".db"),
                                                      Qt::CaseInsensitive);

    QNetworkRequest req(QUrl(manifest.url));
    req.setRawHeader("User-Agent", "ColoCourse");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    setCatalogState(RecipeCatalogState::Updating);
    QNetworkReply *reply = netManager().get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, context, manifest, wantsSqlite, onDone]() {
                         reply->deleteLater();
                         const QByteArray body = reply->readAll();
                         if (reply->error() != QNetworkReply::NoError) {
                             qWarning() << "[RecipeCatalog] téléchargement échoué :"
                                        << reply->errorString();
                             setCatalogState(RecipeCatalogState::Ready);
                             if (onDone)
                                 onDone(false);
                             return;
                         }

                         if (wantsSqlite) {
                             if (!verifySha256(body, manifest.sha256)) {
                                 qWarning() << "[RecipeCatalog] hash SHA256 invalide";
                                 setCatalogState(RecipeCatalogState::Ready);
                                 if (onDone)
                                     onDone(false);
                                 return;
                             }
                             applyDbUpdateOnMain(context, body, manifest, [onDone](bool updated) {
                                 setCatalogState(RecipeCatalogState::Ready);
                                 if (onDone)
                                     onDone(updated);
                             });
                             return;
                         }

                         if (!looksLikeRecipeLibraryJson(body)) {
                             setCatalogState(RecipeCatalogState::Ready);
                             if (onDone)
                                 onDone(false);
                             return;
                         }
                         applyJsonUpdateOnMain(context, body, [onDone](bool updated) {
                             setCatalogState(RecipeCatalogState::Ready);
                             if (onDone)
                                 onDone(updated);
                         });
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
    setCatalogState(RecipeCatalogState::Resolving);

    auto *watcher = new QFutureWatcher<QString>(context);
    QObject::connect(watcher, &QFutureWatcher<QString>::finished, context,
                     [watcher, context, onInitialLoad, onRemoteUpdated]() {
                         const QString path = watcher->result();
                         watcher->deleteLater();
                         QString err;
                         const bool ok = !path.isEmpty() && openResolvedCatalog(path, &err);
                         if (!ok && err.isEmpty())
                             err = QStringLiteral("catalogue introuvable");
                         markCatalogReady(context, ok, err, onInitialLoad, onRemoteUpdated);
                     },
                     Qt::QueuedConnection);

    watcher->setFuture(QtConcurrent::run([]() -> QString {
        return resolveCatalogDbPath();
    }));
}

} // namespace

QString recipeCatalogStateString(RecipeCatalogState state)
{
    switch (state) {
    case RecipeCatalogState::Idle: return QStringLiteral("idle");
    case RecipeCatalogState::Resolving: return QStringLiteral("resolving");
    case RecipeCatalogState::Ready: return QStringLiteral("ready");
    case RecipeCatalogState::Error: return QStringLiteral("error");
    case RecipeCatalogState::Updating: return QStringLiteral("updating");
    }
    return QStringLiteral("idle");
}

RecipeCatalogState recipeCatalogState()
{
    return s_catalogState;
}

QString recipeCatalogError()
{
    return s_catalogError;
}

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
    out->version       = root.value(QStringLiteral("version")).toInt();
    out->count         = root.value(QStringLiteral("count")).toInt();
    out->schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(1);
    out->byteSize      = static_cast<qint64>(root.value(QStringLiteral("byteSize")).toDouble());
    out->updatedAt     = root.value(QStringLiteral("updatedAt")).toString();
    out->url           = root.value(QStringLiteral("url")).toString();
    out->sha256        = root.value(QStringLiteral("sha256")).toString();
    out->format        = root.value(QStringLiteral("format")).toString();
    return out->count > 0 && !out->url.isEmpty();
}

bool loadRecipeLibraryFromResource()
{
    const QString cache = recipeCatalogCachePath();
    if (copyBundledDbTo(cache))
        return core::RecipeLibrary::openCatalogDb(cache);
    return false;
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

void retryRecipeCatalog(QObject *context,
                        std::function<void(bool ok)> onDone,
                        std::function<void()> onRemoteUpdated)
{
    core::RecipeLibrary::closeCatalogDb();
    s_catalogLoaded = false;
    s_catalogLoadStarted = false;
    setCatalogState(RecipeCatalogState::Idle);
    loadRecipeCatalogAsync(context, std::move(onDone), std::move(onRemoteUpdated), 0);
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

void setRecipeCatalogStateListener(std::function<void()> onChanged)
{
    s_stateListener = std::move(onChanged);
}

} // namespace app
