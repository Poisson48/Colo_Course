#include "appcontroller.h"
#include "platform.h"

#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

#include "../core/types.h"
#include "../core/crdt.h"
#include "../core/pairing.h"
#include "../net/crypto.h"
#include "../net/relaypool.h"

namespace app {

// ---------------------------------------------------------------------------
// ListsModel
// ---------------------------------------------------------------------------

ListsModel::ListsModel(QObject *parent)
    : QAbstractListModel(parent)
{}

int ListsModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_rows.size());
}

QVariant ListsModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= (int)m_rows.size())
        return {};
    const auto &row = m_rows[static_cast<size_t>(index.row())];
    switch (role) {
    case ListIdRole: return row.listId;
    case NameRole:   return row.name;
    case CountRole:  return row.count;
    case TotalRole:  return row.total;
    default:         return {};
    }
}

QHash<int, QByteArray> ListsModel::roleNames() const {
    return {
        { ListIdRole, "listId" },
        { NameRole,   "name"   },
        { CountRole,  "count"  },
        { TotalRole,  "total"  },
    };
}

void ListsModel::reload(store::Database &db) {
    beginResetModel();
    m_rows.clear();
    const auto lists = db.getLists();
    for (const auto &meta : lists) {
        int unchecked = 0;
        int total     = 0;
        for (const auto &item : db.getItems(meta.listId)) {
            if (item.del) continue;
            ++total;
            if (!item.done) ++unchecked;
        }
        m_rows.push_back({
            QString::fromStdString(meta.listId),
            QString::fromStdString(meta.title),
            unchecked,
            total
        });
    }
    endResetModel();
}

void ListsModel::prepend(const core::ListMeta &meta, int uncheckedCount) {
    beginInsertRows({}, 0, 0);
    m_rows.insert(m_rows.begin(), {
        QString::fromStdString(meta.listId),
        QString::fromStdString(meta.title),
        uncheckedCount,
        uncheckedCount
    });
    endInsertRows();
}

void ListsModel::remove(const QString &listId) {
    const auto it = std::find_if(m_rows.begin(), m_rows.end(),
                                 [&](const Row &r){ return r.listId == listId; });
    if (it == m_rows.end()) return;

    const int row = static_cast<int>(std::distance(m_rows.begin(), it));
    beginRemoveRows({}, row, row);
    m_rows.erase(it);
    endRemoveRows();
}

// ---------------------------------------------------------------------------
// AppController
// ---------------------------------------------------------------------------

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_listsModel(new ListsModel(this))
    , m_relayPool(this)
    , m_syncEngine(this)
{}

AppController::~AppController() = default;

bool AppController::init() {
    // --- DB path ---
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    const QString dbPath = dataDir + QStringLiteral("/colocourse.db");

    if (!m_db.open(dbPath)) {
        return false;
    }

    // --- deviceId / displayName ---
    QSettings settings;
    const auto kDeviceId    = QStringLiteral("deviceId");
    const auto kDisplayName = QStringLiteral("displayName");

    // Also persist in our own settings table so they survive DB recreation.
    auto devIdOpt = m_db.getSetting("deviceId");
    if (devIdOpt) {
        m_deviceId = QString::fromStdString(*devIdOpt);
    } else {
        // Try QSettings fallback
        if (settings.contains(kDeviceId)) {
            m_deviceId = settings.value(kDeviceId).toString();
        } else {
            m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        m_db.setSetting("deviceId", m_deviceId.toStdString());
        settings.setValue(kDeviceId, m_deviceId);
    }

    auto dispOpt = m_db.getSetting("displayName");
    if (dispOpt) {
        m_displayName = QString::fromStdString(*dispOpt);
    } else {
        if (settings.contains(kDisplayName)) {
            m_displayName = settings.value(kDisplayName).toString();
        } else {
            m_displayName = QStringLiteral("Moi");
        }
        m_db.setSetting("displayName", m_displayName.toStdString());
        settings.setValue(kDisplayName, m_displayName);
    }

    // --- Load lists ---
    m_listsModel->reload(m_db);

    // --- Setup relay pool ---
    // Load relay URLs from settings (or use defaults).
    auto relaysSetting = m_db.getSetting("relays");
    QList<QUrl> relayUrls;
    if (relaysSetting && !relaysSetting->empty()) {
        const QString relaysStr = QString::fromStdString(*relaysSetting);
        for (const QString& u : relaysStr.split(QLatin1Char(','), Qt::SkipEmptyParts))
            relayUrls.append(QUrl(u.trimmed()));
    }
    if (relayUrls.isEmpty()) {
        relayUrls = net::RelayPool::defaultRelays();
        // Persist defaults for future modification.
        QStringList parts;
        for (const QUrl& u : relayUrls) parts.append(u.toString());
        m_db.setSetting("relays", parts.join(',').toStdString());
    }
    m_relayPool.setRelays(relayUrls);

    // --- Wire SyncEngine ---
    m_syncEngine.init(&m_db, &m_relayPool, m_deviceId, m_displayName);
    connect(&m_syncEngine, &SyncEngine::onlineChanged,
            this,          &AppController::onSyncOnlineChanged);
    connect(&m_syncEngine, &SyncEngine::remoteChanges,
            this,          &AppController::onRemoteChanges);

    // Toute écriture locale (ajout, cochage, suppression) doit partir au relais.
    // Sans cette connexion, l'app modifie sa base et ne synchronise jamais rien.
    connect(&m_itemModel, &ItemModel::localChanged,
            this,         &AppController::onLocalItemChange);

    // --- Connect and subscribe ---
    m_relayPool.connectAll();
    m_syncEngine.subscribeAllLists();

    return true;
}

QAbstractListModel *AppController::lists() const {
    return m_listsModel;
}

ItemModel *AppController::items() {
    return &m_itemModel;
}

bool AppController::online() const {
    return m_online;
}

void AppController::onLocalItemChange(const std::string& listId) {
    m_syncEngine.onLocalChange(listId);
    // Les compteurs de l'écran des listes ("2 sur 7") dépendent des items.
    m_listsModel->reload(m_db);
}

void AppController::onSyncOnlineChanged(bool online) {
    if (m_online == online) return;
    m_online = online;
    emit onlineChanged();
}

void AppController::onRemoteChanges(const QString& /*listId*/, int /*count*/, const QString& /*authorName*/) {
    // Refresh the lists model so counts update.
    m_listsModel->reload(m_db);
}

QString AppController::deviceId() const {
    return m_deviceId;
}

QString AppController::displayName() const {
    return m_displayName;
}

void AppController::createList(const QString &title) {
    core::ListMeta meta;
    // listId: 16 random bytes → base64url 22 chars  (§1)
    meta.listId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    // Clé E2E de la liste (§3.1). Sans elle : canal et chiffrement identiques pour
    // tout le monde, et URI d'appairage rejetée par parseJoinUri (32 octets exigés).
    meta.key      = net::generateListKey();
    meta.title    = title.toStdString();
    meta.titleVer = core::Ver{ 1, m_deviceId.toStdString() };
    meta.lamport  = 1;
    meta.created  = QDateTime::currentMSecsSinceEpoch();

    if (meta.key.size() != 32) {
        emit toast(QStringLiteral("Échec de la génération de la clé de chiffrement"));
        return;
    }

    if (m_db.createList(meta)) {
        m_listsModel->prepend(meta, 0);
        // Souscrire tout de suite : sans ça, la liste n'est écoutée qu'au prochain
        // lancement et les modifications des autres n'arrivent jamais.
        m_syncEngine.onListJoined(meta.listId);
    }
}

void AppController::leaveList(const QString &listId) {
    const std::string id = listId.toStdString();
    m_syncEngine.unregisterItemModel(id);
    if (m_openListId == id)
        m_openListId.clear();
    if (m_db.deleteList(id)) {
        m_listsModel->remove(listId);
        emit toast(QStringLiteral("Liste supprimée de cet appareil"));
    }
}

void AppController::handleJoinUrl(const QUrl &url) {
    if (joinList(url.toString()))
        emit toast(QStringLiteral("Liste rejointe — synchronisation en cours"));
    else
        emit toast(QStringLiteral("Lien d'invitation invalide"));
}

void AppController::setDisplayName(const QString &name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == m_displayName) return;

    m_displayName = trimmed;
    m_db.setSetting("displayName", m_displayName.toStdString());
    QSettings().setValue(QStringLiteral("displayName"), m_displayName);
    // Le SyncEngine embarque le nom dans les payloads : le lui repasser (surtout pas
    // via init(), qui rebrancherait les signaux du pool une deuxième fois).
    m_syncEngine.setDisplayName(m_displayName);
    emit displayNameChanged();
}

void AppController::copyToClipboard(const QString &text) {
    if (auto *cb = QGuiApplication::clipboard())
        cb->setText(text);
    emit toast(QStringLiteral("Lien copié"));
}

bool AppController::shareText(const QString &text) {
    if (app::platformShare(text))
        return true;
    // Pas de feuille de partage (desktop) : le presse-papiers fait le travail.
    copyToClipboard(text);
    return false;
}

void AppController::openList(const QString &listId) {
    const std::string id = listId.toStdString();
    auto metaOpt = m_db.getList(id);
    if (!metaOpt) return;

    // Un seul ItemModel pour toutes les listes : le rebrancher sur celle-ci, sinon
    // les événements distants d'une autre liste rafraîchiraient le mauvais écran.
    if (!m_openListId.empty() && m_openListId != id)
        m_syncEngine.unregisterItemModel(m_openListId);

    m_itemModel.load(m_db, id, m_deviceId.toStdString());
    m_syncEngine.registerItemModel(id, &m_itemModel);
    m_openListId = id;

    emit listOpened(listId, QString::fromStdString(metaOpt->title));
}

bool AppController::joinList(const QString &uri)
{
    auto infoOpt = core::parseJoinUri(uri.toStdString());
    if (!infoOpt) return false;

    const core::JoinInfo& info = *infoOpt;

    core::ListMeta meta;
    meta.listId   = info.listId;
    meta.key      = info.key;
    meta.title    = info.title;
    meta.titleVer = core::Ver{ 1, m_deviceId.toStdString() };
    meta.lamport  = 1;
    meta.created  = QDateTime::currentMSecsSinceEpoch();

    bool created = m_db.createList(meta);
    if (created) {
        m_listsModel->prepend(meta, 0);
    }
    // Subscribe to catch up full history (SPEC §3.4).
    m_syncEngine.onListJoined(info.listId);
    // Return true even if already exists (already member)
    return true;
}

QString AppController::joinUri(const QString &listId)
{
    auto metaOpt = m_db.getList(listId.toStdString());
    if (!metaOpt) return {};
    return QString::fromStdString(
        core::buildJoinUri(metaOpt->listId, metaOpt->key, metaOpt->title));
}

} // namespace app
