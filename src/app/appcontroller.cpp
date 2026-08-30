#include "appcontroller.h"

#include "../core/recipe_library.h"
#include "recipe_library_loader.h"
#include "../core/recipe_scale.h"
#include "../core/ingredient_norm.h"

#include <QRegularExpression>
#include "platform.h"

#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <optional>
#include <algorithm>
#include <map>
#include <limits>

#include "../core/types.h"
#include "../core/crdt.h"
#include "../core/pairing.h"
#include "../core/csv.h"
#include "../core/zip.h"
#include "../net/crypto.h"
#include "../net/relaypool.h"
#include "../net/crypto.h"
#include "../net/pushclient.h"

namespace app {

// ---------------------------------------------------------------------------
// ListsModel
// ---------------------------------------------------------------------------

ListsModel::ListsModel(QObject *parent)
    : QAbstractListModel(parent)
{}

void ListsModel::setKindFilter(const QString &kind) {
    m_kindFilter = kind;
}

bool ListsModel::matchesKind(const std::string &kind) const {
    const bool recipe = (kind == "recipe");
    if (m_kindFilter == QLatin1String("recipe"))
        return recipe;
    return !recipe;  // shopping : '' / shopping / autre
}

int ListsModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_rows.size());
}

QVariant ListsModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= (int)m_rows.size())
        return {};
    const auto &row = m_rows[static_cast<size_t>(index.row())];
    switch (role) {
    case ListIdRole:     return row.listId;
    case NameRole:       return row.name;
    case CountRole:      return row.count;
    case TotalRole:      return row.total;
    case GroupIdRole:    return row.groupId;
    case GroupNameRole:  return row.groupName;
    case MembersRole:    return row.members;
    case MemberCountRole:return row.memberCount;
    case KindRole:       return row.kind;
    default:             return {};
    }
}

QHash<int, QByteArray> ListsModel::roleNames() const {
    return {
        { ListIdRole,     "listId"      },
        { NameRole,       "name"        },
        { CountRole,      "count"       },
        { TotalRole,      "total"       },
        { GroupIdRole,    "groupId"     },
        { GroupNameRole,  "groupName"   },
        { MembersRole,    "members"     },
        { MemberCountRole,"memberCount" },
        { KindRole,       "kind"        },
    };
}

void ListsModel::reload(store::Database &db, const std::string &deviceId) {
    m_allRows.clear();

    // Table des groupes : id → (nom, ordre). Un rang par défaut très grand range les
    // listes non rangées après tous les groupes.
    std::map<std::string, std::pair<QString, int64_t>> groups;
    for (const auto &g : db.getGroups())
        groups[g.groupId] = { QString::fromStdString(g.name), g.sortOrder };

    for (const auto &meta : db.getLists()) {
        if (!matchesKind(meta.kind))
            continue;

        int unchecked = 0;
        int total     = 0;
        QString ingredientBlob;
        for (const auto &item : db.getItems(meta.listId)) {
            if (item.del) continue;
            ++total;
            if (!item.done) ++unchecked;
            if (meta.isRecipe()) {
                ingredientBlob += QLatin1Char(' ')
                    + core::RecipeLibrary::normalizeSearchText(
                          QString::fromStdString(item.name));
                ingredientBlob += QLatin1Char(' ')
                    + core::RecipeLibrary::normalizeSearchText(
                          QString::fromStdString(item.note));
            }
        }

        // Avec qui c'est partagé : les membres connus, soi-même exclu. Un membre =
        // quelqu'un dont on a reçu au moins un événement, donc un vrai participant.
        QStringList names;
        for (const auto &[devId, name] : db.getMembers(meta.listId)) {
            if (devId == deviceId || name.empty()) continue;
            names << QString::fromStdString(name);
        }
        names.removeDuplicates();

        Row row;
        row.listId      = QString::fromStdString(meta.listId);
        row.name        = QString::fromStdString(meta.title);
        row.count       = unchecked;
        row.total       = total;
        row.listOrder   = meta.listOrder;
        row.members     = names.join(QStringLiteral(", "));
        row.memberCount = static_cast<int>(names.size());
        row.kind        = QString::fromStdString(meta.kind);

        QString searchBlob = core::RecipeLibrary::normalizeSearchText(row.name);
        if (meta.isRecipe()) {
            searchBlob += ingredientBlob;
            const auto instr = db.getSetting(
                (QStringLiteral("recipeInstructions/") + row.listId).toStdString());
            if (instr)
                searchBlob += QLatin1Char(' ')
                    + core::RecipeLibrary::normalizeSearchText(
                          QString::fromStdString(*instr));
        }
        row.searchBlob = searchBlob;

        const auto git = groups.find(meta.groupId);
        if (!meta.groupId.empty() && git != groups.end()) {
            row.groupId    = QString::fromStdString(meta.groupId);
            row.groupName  = git->second.first;
            row.groupOrder = git->second.second;
        } else {
            // Non rangée (ou groupe supprimé) : après tous les groupes.
            row.groupOrder = std::numeric_limits<int64_t>::max();
        }
        m_allRows.push_back(std::move(row));
    }

    // Trier par groupe pour que les sections soient contiguës ; l'ordre d'origine
    // (création) est préservé à l'intérieur d'un groupe par le tri stable.
    std::stable_sort(m_allRows.begin(), m_allRows.end(), [](const Row &a, const Row &b) {
        if (a.groupOrder != b.groupOrder) return a.groupOrder < b.groupOrder;
        return a.groupName < b.groupName;
    });

    applyFilter();
}

void ListsModel::setFilter(const QString &filter) {
    if (m_filter == filter)
        return;
    m_filter = filter;
    applyFilter();
    emit filterChanged();
}

void ListsModel::applyFilter() {
    beginResetModel();
    m_rows.clear();

    const QStringList tokens = core::RecipeLibrary::normalizeSearchText(m_filter)
        .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);

    for (const auto &row : m_allRows) {
        bool match = true;
        for (const QString &tok : tokens) {
            if (!row.searchBlob.contains(tok)) {
                match = false;
                break;
            }
        }
        if (match)
            m_rows.push_back(row);
    }

    endResetModel();
}

void ListsModel::rename(const QString &listId, const QString &name) {
    const auto update = [&](Row &r) {
        if (r.listId != listId || r.name == name)
            return false;
        const QString oldTitleNorm = core::RecipeLibrary::normalizeSearchText(r.name);
        r.name = name;
        const QString newTitleNorm = core::RecipeLibrary::normalizeSearchText(name);
        if (r.searchBlob.startsWith(oldTitleNorm))
            r.searchBlob = newTitleNorm + r.searchBlob.mid(oldTitleNorm.length());
        else
            r.searchBlob = newTitleNorm + QLatin1Char(' ') + r.searchBlob;
        return true;
    };

    bool changed = false;
    for (auto &r : m_allRows)
        changed = update(r) || changed;
    if (!changed)
        return;

    for (auto &r : m_rows)
        update(r);

    const auto it = std::find_if(m_rows.begin(), m_rows.end(),
                                 [&](const Row &r) { return r.listId == listId; });
    if (it != m_rows.end()) {
        const int row = static_cast<int>(std::distance(m_rows.begin(), it));
        emit dataChanged(index(row), index(row), { NameRole });
    }
}

void ListsModel::remove(const QString &listId) {
    const auto allIt = std::find_if(m_allRows.begin(), m_allRows.end(),
                                    [&](const Row &r) { return r.listId == listId; });
    if (allIt != m_allRows.end())
        m_allRows.erase(allIt);

    const auto it = std::find_if(m_rows.begin(), m_rows.end(),
                                 [&](const Row &r){ return r.listId == listId; });
    if (it == m_rows.end()) return;

    const int row = static_cast<int>(std::distance(m_rows.begin(), it));
    beginRemoveRows({}, row, row);
    m_rows.erase(it);
    endRemoveRows();
}

void ListsModel::moveRow(store::Database &db, int from, int to) {
    const int n = static_cast<int>(m_rows.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;

    Row moved = m_rows[static_cast<size_t>(from)];

    // Voisines à l'arrivée, une fois la ligne retirée de sa place actuelle.
    std::vector<Row> without = m_rows;
    without.erase(without.begin() + from);

    const Row *before = (to > 0) ? &without[static_cast<size_t>(to - 1)] : nullptr;
    const Row *after  = (to < static_cast<int>(without.size()))
                        ? &without[static_cast<size_t>(to)] : nullptr;

    // La liste prend le groupe de la ligne survolée (même raisonnement que le rayon
    // d'un article) : en descendant on se pose APRÈS elle (`before`), en montant on
    // prend sa place (`after`).
    const Row *hovered = (from < to) ? before : after;
    const QString targetGroupId    = hovered ? hovered->groupId    : moved.groupId;
    const QString targetGroupName  = hovered ? hovered->groupName  : moved.groupName;
    const int64_t targetGroupOrder = hovered ? hovered->groupOrder : moved.groupOrder;

    // Les voisines qui comptent pour la position sont celles du MÊME groupe.
    const auto sameGroup = [&](const Row *r) {
        return r && r->groupId == targetGroupId;
    };
    const int64_t lo = sameGroup(before) ? before->listOrder : 0;
    const int64_t hi = sameGroup(after)  ? after->listOrder  : 0;

    int64_t order;
    if (lo && hi)      order = lo + (hi - lo) / 2;   // entre les deux
    else if (lo)       order = lo + 1000;            // en fin de groupe
    else if (hi)       order = hi - 1000;            // en tête de groupe
    else               order = moved.listOrder;      // seule de son groupe

    // Intervalle épuisé : renuméroter le groupe à grands pas, puis rejouer le geste.
    if (lo && hi && (order == lo || order == hi)) {
        renumberGroup(db, targetGroupId);
        moveRow(db, from, to);
        return;
    }

    const bool groupChanged = (moved.groupId != targetGroupId);
    if (groupChanged)
        db.setListGroup(moved.listId.toStdString(), targetGroupId.toStdString());
    db.setListOrder(moved.listId.toStdString(), order);

    moved.listOrder  = order;
    moved.groupId    = targetGroupId;
    moved.groupName  = targetGroupName;
    moved.groupOrder = targetGroupOrder;

    // Déplacement de ligne (pas de reset : la ligne glissée ne doit pas être détruite).
    const int dest = (from < to) ? to + 1 : to;   // Qt insère AVANT `dest`
    beginMoveRows({}, from, from, {}, dest);
    m_rows.erase(m_rows.begin() + from);
    m_rows.insert(m_rows.begin() + to, moved);
    endMoveRows();

    if (groupChanged) {
        const QModelIndex idx = index(to);
        emit dataChanged(idx, idx, { GroupIdRole, GroupNameRole });
    }
}

void ListsModel::renumberGroup(store::Database &db, const QString &groupId) {
    int64_t next = 1000;
    for (auto &row : m_rows) {
        if (row.groupId != groupId) continue;
        row.listOrder = next;
        db.setListOrder(row.listId.toStdString(), next);
        next += 1000;
    }
}

// ---------------------------------------------------------------------------
// AppController
// ---------------------------------------------------------------------------

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_listsModel(new ListsModel(this))
    , m_recipesModel(new ListsModel(this))
    , m_recipeLibraryModel(new RecipeLibraryModel(this))
    , m_relayPool(this)
    , m_syncEngine(this)
{
    m_recipesModel->setKindFilter(QStringLiteral("recipe"));
}

AppController::~AppController() = default;

QString AppController::databasePath() {
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    return dataDir + QStringLiteral("/colocourse.db");
}

bool AppController::init() {
    // --- DB path ---
    if (!m_db.open(databasePath())) {
        return false;
    }

    if (!loadRecipeLibraryFromResource())
        qWarning() << "Bibliothèque de recettes intégrée introuvable ou vide";
    m_recipeLibraryModel->reloadFromLibrary();


    // Blobs orphelins (photo retirée, liste quittée…) : les purger au démarrage.
    m_db.purgeOrphanImages();

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
    } else if (settings.contains(kDisplayName)) {
        m_displayName = settings.value(kDisplayName).toString();
    } else {
        // Repli tant que l'utilisateur n'a rien choisi. Non persisté : il sera redemandé.
        m_displayName = QStringLiteral("Moi");
    }

    // Drapeau explicite, et pas « displayName existe » : les installations d'avant
    // portent un « Moi » persisté que personne n'a choisi. Elles passent ici une fois,
    // l'écran d'accueil redemande le nom, et le drapeau se pose au premier choix.
    m_hasDisplayName = m_db.getSetting("displayNameSet").has_value();

    // --- Load lists ---
    m_listsModel->reload(m_db, m_deviceId.toStdString());
    m_recipesModel->reload(m_db, m_deviceId.toStdString());

    // --- Setup relay pool ---
    // Load relay URLs from settings (or use defaults).
    static const QString kLegacyRelays =
        QStringLiteral("wss://relay.damus.io,wss://nos.lol,wss://relay.nostr.band,"
                       "wss://offchain.pub");
    auto relaysSetting = m_db.getSetting("relays");
    QList<QUrl> relayUrls;
    if (relaysSetting && !relaysSetting->empty()) {
        QString relaysStr = QString::fromStdString(*relaysSetting);
        if (relaysStr == kLegacyRelays)
            relaysStr.clear(); // bascule vers le relais Colo Course par défaut
        for (const QString &u : relaysStr.split(QLatin1Char(','), Qt::SkipEmptyParts))
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
    connect(&m_syncEngine, &SyncEngine::listTitleChanged,
            this,          &AppController::onRemoteTitleChanged);
    connect(&m_syncEngine, &SyncEngine::listMetaChanged, this, [this](const QString &) {
        m_listsModel->reload(m_db, m_deviceId.toStdString());
        m_recipesModel->reload(m_db, m_deviceId.toStdString());
    });
    connect(&m_syncEngine, &SyncEngine::outboxChanged,
            this,          &AppController::onOutboxChanged);
    // Un blob de photo vient d'arriver : invalider le cache des vignettes.
    connect(&m_syncEngine, &SyncEngine::imageArrived,
            this, [this](const QString&, const QString&) {
        ++m_imageRevision;
        emit imageRevisionChanged();
    });

    // Des modifications peuvent dormir dans l'outbox depuis la session précédente
    // (app fermée hors ligne) : purger celles déjà livrées avant d'afficher le bandeau.
    m_syncEngine.reconcileOutbox();
    m_pendingChanges = m_db.outboxCount();
    if (m_pendingChanges > 0)
        m_syncEngine.catchUpOnForeground();

    // Toute écriture locale (ajout, cochage, suppression) doit partir au relais.
    // Sans cette connexion, l'app modifie sa base et ne synchronise jamais rien.
    connect(&m_itemModel, &ItemModel::localChanged,
            this,         &AppController::onLocalItemChange);
    // Un ajout manuel enrichit les favoris fréquents : rafraîchir la barre de suggestions.
    connect(&m_itemModel, &ItemModel::itemAdded,
            this,         &AppController::favoritesChanged);

    // --- Connect and subscribe ---
    m_relayPool.connectAll();
    m_syncEngine.subscribeAllLists();
    m_online = m_relayPool.isOnline();

    const bool active = QGuiApplication::applicationState() == Qt::ApplicationActive;
    m_syncEngine.setAppInForeground(active);
    m_syncEngine.setDeferBackgroundNotificationsToPush(pushEnabled());
    if (active) {
        m_pushLifecycleReady = true;
        platformConfigurePush(QString(), {}, QString());
        resumeSync();
    }

    return true;
}

void AppController::resumeSync() {
    m_relayPool.connectAll();
    m_syncEngine.catchUpOnForeground();
}

void AppController::onApplicationStateChanged(Qt::ApplicationState state)
{
    const bool active = (state == Qt::ApplicationActive);
    m_syncEngine.setAppInForeground(active);
    m_syncEngine.setDeferBackgroundNotificationsToPush(pushEnabled());

    if (active) {
        m_pushLifecycleReady = true;
        platformConfigurePush(QString(), {}, QString());
        resumeSync();
    } else if (m_pushLifecycleReady) {
        refreshPushTopics();
    }
}

QAbstractListModel *AppController::lists() const {
    return m_listsModel;
}

QAbstractListModel *AppController::recipes() const {
    return m_recipesModel;
}

QAbstractListModel *AppController::recipeLibrary() const {
    return m_recipeLibraryModel;
}

int AppController::recipeLibraryCount() const {
    return core::RecipeLibrary::count();
}

ItemModel *AppController::items() {
    return &m_itemModel;
}

bool AppController::online() const {
    return m_online;
}

int AppController::pendingChanges() const {
    return m_pendingChanges;
}

void AppController::onOutboxChanged() {
    const int pending = m_db.outboxCount();
    if (pending == m_pendingChanges) return;

    const int previous = m_pendingChanges;
    m_pendingChanges = pending;
    emit pendingChangesChanged();

    // On avait des modifications en attente, et un relais vient d'accuser réception de
    // la dernière : le confirmer explicitement. C'est le « synchro réussie » demandé —
    // le bandeau discret ne suffit pas à rassurer sur un envoi ponctuel.
    if (previous > 0 && pending == 0 && m_online)
        emit toast(QStringLiteral("Modifications synchronisées"));
}

void AppController::onLocalItemChange(const std::string& listId) {
    m_syncEngine.onLocalChange(listId);
    // Les compteurs de l'écran des listes ("2 sur 7") dépendent des items.
    m_listsModel->reload(m_db, m_deviceId.toStdString());
    m_recipesModel->reload(m_db, m_deviceId.toStdString());
}

void AppController::onSyncOnlineChanged(bool online) {
    if (m_online == online) return;
    m_online = online;
    emit onlineChanged();

    if (online) {
        // Réafficher l'état local immédiatement ; les événements distants
        // mettront à jour via onRemoteChanges.
        m_listsModel->reload(m_db, m_deviceId.toStdString());
        m_recipesModel->reload(m_db, m_deviceId.toStdString());
        if (!m_openListId.empty())
            m_itemModel.load(m_db, m_openListId, m_deviceId.toStdString());
    }
}

void AppController::onRemoteChanges(const QString& /*listId*/, int /*count*/, const QString& /*authorName*/) {
    // Refresh the lists model so counts update.
    m_listsModel->reload(m_db, m_deviceId.toStdString());
    m_recipesModel->reload(m_db, m_deviceId.toStdString());
}

void AppController::onRemoteTitleChanged(const QString& listId, const QString& title) {
    m_listsModel->rename(listId, title);
    m_recipesModel->rename(listId, title);
    emit listRenamed(listId, title);
}

QString AppController::deviceId() const {
    return m_deviceId;
}

QString AppController::displayName() const {
    return m_displayName;
}

bool AppController::hasDisplayName() const {
    return m_hasDisplayName;
}

namespace {

bool isValidRelayUrl(const QUrl &url)
{
    if (!url.isValid() || url.host().isEmpty())
        return false;
    const QString scheme = url.scheme().toLower();
    return scheme == QLatin1String("wss") || scheme == QLatin1String("ws");
}

QStringList parseRelayUrlText(const QString &text)
{
    QStringList out;
    for (const QString &part :
         text.split(QRegularExpression(QStringLiteral("[\\n,]+")), Qt::SkipEmptyParts)) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            out.append(trimmed);
    }
    return out;
}

} // namespace

QString AppController::relayUrls()
{
    const auto v = m_db.getSetting("relays");
    if (v && !v->empty())
        return QString::fromStdString(*v).replace(QLatin1Char(','), QLatin1Char('\n'));
    QStringList parts;
    for (const QUrl &u : net::RelayPool::defaultRelays())
        parts.append(u.toString());
    return parts.join(QLatin1Char('\n'));
}

QString AppController::defaultRelayUrls() const
{
    QStringList parts;
    for (const QUrl &u : net::RelayPool::defaultRelays())
        parts.append(u.toString());
    return parts.join(QLatin1Char('\n'));
}

void AppController::setRelayUrls(const QString &text)
{
    if (!m_db.isOpen())
        return;

    QList<QUrl> urls;
    for (const QString &line : parseRelayUrlText(text)) {
        const QUrl url(line);
        if (!isValidRelayUrl(url)) {
            emit toast(QStringLiteral("URL de relais invalide : %1").arg(line));
            return;
        }
        urls.append(url);
    }
    if (urls.isEmpty()) {
        emit toast(QStringLiteral("Indiquez au moins un relais wss://"));
        return;
    }

    QStringList stored;
    for (const QUrl &u : urls)
        stored.append(u.toString());
    m_db.setSetting("relays", stored.join(QLatin1Char(',')).toStdString());

    m_relayPool.setRelays(urls);
    m_relayPool.connectAll();
    m_syncEngine.subscribeAllLists();
    m_syncEngine.catchUpOnForeground();

    const bool online = m_relayPool.isOnline();
    if (online != m_online) {
        m_online = online;
        emit onlineChanged();
    }
    emit relayUrlsChanged();
    emit toast(urls.size() > 1
                   ? QStringLiteral("Relais mis à jour (%1)").arg(urls.size())
                   : QStringLiteral("Relais mis à jour"));
}

void AppController::resetRelayUrls()
{
    setRelayUrls(defaultRelayUrls());
}

bool AppController::pushEnabled()
{
    const auto v = m_db.getSetting("pushEnabled");
    return !v || *v != "0";
}

QString AppController::pushBaseUrl()
{
    const auto v = m_db.getSetting("pushBaseUrl");
    if (v && !v->empty())
        return QString::fromStdString(*v);
    return defaultPushBaseUrl();
}

QString AppController::defaultPushBaseUrl() const
{
    return QStringLiteral("https://colo-apps.les-crevettes-cevenoles.fr/ntfy");
}

void AppController::setPushSettings(bool enabled, const QString &baseUrl)
{
    if (!m_db.isOpen())
        return;

    const QString trimmed = baseUrl.trimmed();
    if (enabled && !trimmed.isEmpty()) {
        const QUrl u(trimmed);
        if (!u.isValid() || u.scheme().isEmpty()) {
            emit toast(QStringLiteral("URL push invalide"));
            return;
        }
    }

    m_db.setSetting("pushEnabled", enabled ? "1" : "0");
    if (!trimmed.isEmpty())
        m_db.setSetting("pushBaseUrl", trimmed.toStdString());

    refreshPushTopics();
    emit pushSettingsChanged();
    emit toast(enabled ? QStringLiteral("Notifications push activées")
                       : QStringLiteral("Notifications push désactivées"));
}

void AppController::refreshPushTopics()
{
    const bool pushOn = pushEnabled();
    m_syncEngine.setDeferBackgroundNotificationsToPush(pushOn);

    if (!pushOn) {
        platformConfigurePush(QString(), {}, QString());
        return;
    }

    if (QGuiApplication::applicationState() == Qt::ApplicationActive) {
        platformConfigurePush(QString(), {}, QString());
        return;
    }

    QStringList topics;
    for (const auto &meta : m_db.getLists()) {
        topics.append(net::pushTopicForChannel(
            QString::fromStdString(net::deriveChannelTag(meta.key))));
    }
    platformConfigurePush(pushBaseUrl(), topics, m_deviceId);
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
        m_listsModel->reload(m_db, m_deviceId.toStdString());
        m_recipesModel->reload(m_db, m_deviceId.toStdString());
        // Souscrire tout de suite : sans ça, la liste n'est écoutée qu'au prochain
        // lancement et les modifications des autres n'arrivent jamais.
        m_syncEngine.onListJoined(meta.listId);
    }
}

namespace {

QString recipeServingsKey(const QString &listId) {
    return QStringLiteral("recipeServings/") + listId;
}

QString recipeTargetServingsKey(const QString &listId) {
    return QStringLiteral("recipeTargetServings/") + listId;
}

QString recipeInstructionsKey(const QString &listId) {
    return QStringLiteral("recipeInstructions/") + listId;
}

void applyRecipeDisplayScale(AppController *self, const QString &listId) {
    if (!self->isRecipe(listId))
        return;
    const int base = self->recipeBaseServings(listId);
    const int target = self->recipeTargetServings(listId);
    const double factor = base > 0 ? static_cast<double>(target) / base : 1.0;
    self->items()->setDisplayQtyScale(factor);
}

} // namespace

void AppController::createRecipe(const QString &title) {
    core::ListMeta meta;
    meta.listId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    meta.key      = net::generateListKey();
    meta.title    = title.toStdString();
    meta.titleVer = core::Ver{ 1, m_deviceId.toStdString() };
    meta.lamport  = 1;
    meta.created  = QDateTime::currentMSecsSinceEpoch();
    meta.kind     = "recipe";

    if (meta.key.size() != 32) {
        emit toast(QStringLiteral("Échec de la génération de la clé de chiffrement"));
        return;
    }

    if (m_db.createList(meta)) {
        m_db.setSetting(recipeServingsKey(QString::fromStdString(meta.listId)).toStdString(),
                        "4");
        m_db.setSetting(recipeTargetServingsKey(QString::fromStdString(meta.listId)).toStdString(),
                        "4");
        m_listsModel->reload(m_db, m_deviceId.toStdString());
        m_recipesModel->reload(m_db, m_deviceId.toStdString());
        m_syncEngine.onListJoined(meta.listId);
        const QString id = QString::fromStdString(meta.listId);
        m_pendingPrepListId = id;
        openList(id);
        emit toast(QStringLiteral("Recette créée — ajoutez les étapes"));
    }
}

bool AppController::takePendingPrepPrompt(const QString &listId) {
    if (m_pendingPrepListId != listId)
        return false;
    m_pendingPrepListId.clear();
    return true;
}

bool AppController::addRecipeFromLibrary(const QString &libraryId, int targetServings) {
    const core::LibraryRecipe *lib = core::RecipeLibrary::recipeById(libraryId);
    if (!lib) {
        emit toast(QStringLiteral("Recette introuvable dans le catalogue"));
        return false;
    }

    core::ListMeta meta;
    meta.listId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    meta.key      = net::generateListKey();
    meta.title    = lib->title.toStdString();
    meta.titleVer = core::Ver{ 1, m_deviceId.toStdString() };
    meta.lamport  = 1;
    meta.created  = QDateTime::currentMSecsSinceEpoch();
    meta.kind     = "recipe";

    if (meta.key.size() != 32) {
        emit toast(QStringLiteral("Échec de la génération de la clé de chiffrement"));
        return false;
    }
    if (!m_db.createList(meta)) {
        emit toast(QStringLiteral("Impossible d'ajouter la recette"));
        return false;
    }

    const int base = lib->servingsCount > 0 ? lib->servingsCount : 4;
    const int target = targetServings > 0 ? targetServings : base;
  m_db.setSetting(recipeServingsKey(QString::fromStdString(meta.listId)).toStdString(),
                    QString::number(base).toStdString());
    m_db.setSetting(recipeTargetServingsKey(QString::fromStdString(meta.listId)).toStdString(),
                    QString::number(target).toStdString());
    if (!lib->instructions.isEmpty())
        m_db.setSetting(recipeInstructionsKey(QString::fromStdString(meta.listId)).toStdString(),
                        lib->instructions.toStdString());

    int added = 0;
    for (const auto &ing : lib->ingredients) {
        const int64_t lamport = m_db.bumpLamport(meta.listId);
        const core::Ver ver{ lamport, m_deviceId.toStdString() };

        core::Item item;
        item.listId  = meta.listId;
        item.itemId  = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        item.created = meta.created + added;
        item.by      = m_deviceId.toStdString();
        item.name    = ing.name.toStdString();  item.nameVer  = ver;
        item.qty     = ing.qty.toStdString();   item.qtyVer   = ver;
        item.note    = ing.note.toStdString();  item.noteVer  = ver;
        item.aisle   = m_db.suggestAisleForName(item.name); item.aisleVer = ver;
        item.order   = item.created;            item.orderVer = ver;
        item.delVer  = ver;
        item.touched = meta.created;

        if (m_db.upsertItem(item))
            ++added;
    }

    m_listsModel->reload(m_db, m_deviceId.toStdString());
    m_recipesModel->reload(m_db, m_deviceId.toStdString());
    m_syncEngine.onListJoined(meta.listId);
    m_syncEngine.onLocalChange(meta.listId);

    emit toast(QStringLiteral("« %1 » ajoutée — %2 ingrédient(s) pour %3 personne(s)")
                   .arg(lib->title).arg(added).arg(target));
    openList(QString::fromStdString(meta.listId));
    return true;
}

QVariantList AppController::libraryIngredients(const QString &libraryId, int targetServings) {
    const core::LibraryRecipe *lib = core::RecipeLibrary::recipeById(libraryId);
    if (!lib)
        return {};

    const int base = lib->servingsCount > 0 ? lib->servingsCount : 4;
    const int target = targetServings > 0 ? targetServings : base;
    const double factor = static_cast<double>(target) / base;

    QVariantList out;
    for (const auto &ing : lib->ingredients) {
        out.append(QVariantMap{
            { QStringLiteral("name"), ing.name },
            { QStringLiteral("qty"), core::scaleQuantity(ing.qty, factor) },
            { QStringLiteral("note"), ing.note },
        });
    }
    return out;
}

int AppController::libraryBaseServings(const QString &libraryId) {
    const core::LibraryRecipe *lib = core::RecipeLibrary::recipeById(libraryId);
    if (!lib)
        return 4;
    return lib->servingsCount > 0 ? lib->servingsCount : 4;
}

QString AppController::libraryInstructions(const QString &libraryId) {
    const core::LibraryRecipe *lib = core::RecipeLibrary::recipeById(libraryId);
    return lib ? lib->instructions : QString();
}

QString AppController::recipeInstructions(const QString &listId) {
    if (!m_db.isOpen() || !isRecipe(listId))
        return {};
    const auto v = m_db.getSetting(recipeInstructionsKey(listId).toStdString());
    return v ? QString::fromStdString(*v) : QString();
}

void AppController::setRecipeInstructions(const QString &listId, const QString &text) {
    if (!m_db.isOpen() || !isRecipe(listId))
        return;
    m_db.setSetting(recipeInstructionsKey(listId).toStdString(), text.trimmed().toStdString());
    m_recipesModel->reload(m_db, m_deviceId.toStdString());
}

int AppController::recipeBaseServings(const QString &listId) {
    if (!m_db.isOpen() || !isRecipe(listId))
        return 4;
    const auto v = m_db.getSetting(recipeServingsKey(listId).toStdString());
    if (!v)
        return 4;
    const int n = QString::fromStdString(*v).toInt();
    return n > 0 ? n : 4;
}

int AppController::recipeTargetServings(const QString &listId) {
    if (!m_db.isOpen() || !isRecipe(listId))
        return 4;
    const auto v = m_db.getSetting(recipeTargetServingsKey(listId).toStdString());
    if (!v)
        return recipeBaseServings(listId);
    const int n = QString::fromStdString(*v).toInt();
    return n > 0 ? n : recipeBaseServings(listId);
}

void AppController::setRecipeTargetServings(const QString &listId, int servings) {
    if (!m_db.isOpen() || !isRecipe(listId) || servings <= 0)
        return;
    m_db.setSetting(recipeTargetServingsKey(listId).toStdString(),
                    QString::number(servings).toStdString());
    if (m_openListId == listId.toStdString())
        applyRecipeDisplayScale(this, listId);
}

bool AppController::isRecipe(const QString &listId) {
    if (!m_db.isOpen()) return false;
    auto meta = m_db.getList(listId.toStdString());
    return meta && meta->isRecipe();
}

void AppController::renameList(const QString &listId, const QString &title) {
    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty()) return;

    const std::string id = listId.toStdString();
    auto metaOpt = m_db.getList(id);
    if (!metaOpt || metaOpt->title == trimmed.toStdString()) return;

    // Écriture locale = tick du Lamport de la liste : la nouvelle version bat celle
    // qu'on connaissait, et gagne le merge LWW chez les autres participants.
    const int64_t lamport = m_db.bumpLamport(id);
    const core::Ver ver{ lamport, m_deviceId.toStdString() };

    if (!m_db.updateListTitle(id, trimmed.toStdString(), ver)) return;

    m_listsModel->rename(listId, trimmed);
    m_recipesModel->rename(listId, trimmed);
    emit listRenamed(listId, trimmed);
    m_syncEngine.onLocalChange(id);
}

void AppController::duplicateList(const QString &listId, const QString &title) {
    auto srcOpt = m_db.getList(listId.toStdString());
    if (!srcOpt) return;

    const QString trimmed = title.trimmed();

    core::ListMeta copy;
    copy.listId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    // Clé neuve : la copie est une liste à part, pas une seconde vue de l'originale.
    // Réutiliser la clé source la ferait écrire dans le canal de l'originale.
    copy.key      = net::generateListKey();
    copy.title    = (trimmed.isEmpty()
                        ? QString::fromStdString(srcOpt->title) + QStringLiteral(" (copie)")
                        : trimmed).toStdString();
    copy.titleVer = core::Ver{ 1, m_deviceId.toStdString() };
    copy.lamport  = 1;
    copy.created  = QDateTime::currentMSecsSinceEpoch();
    copy.kind     = srcOpt->kind;

    if (copy.key.size() != 32) {
        emit toast(QStringLiteral("Échec de la génération de la clé de chiffrement"));
        return;
    }
    if (!m_db.createList(copy)) return;

    if (copy.isRecipe()) {
        const QString srcId = QString::fromStdString(srcOpt->listId);
        const QString copyId = QString::fromStdString(copy.listId);
        const auto base = m_db.getSetting(QStringLiteral("recipeServings/").append(srcId).toStdString());
        const auto target = m_db.getSetting(QStringLiteral("recipeTargetServings/").append(srcId).toStdString());
        if (base)
            m_db.setSetting(QStringLiteral("recipeServings/").append(copyId).toStdString(), *base);
        if (target)
            m_db.setSetting(QStringLiteral("recipeTargetServings/").append(copyId).toStdString(), *target);
        const auto instr = m_db.getSetting(recipeInstructionsKey(srcId).toStdString());
        if (instr)
            m_db.setSetting(recipeInstructionsKey(copyId).toStdString(), *instr);
    }

    // Les articles sont recopiés à acheter (done=false) : on duplique une liste pour
    // la refaire. Les tombstones de l'originale ne sont pas repris.
    int copied = 0;
    for (const auto &src : m_db.getItems(srcOpt->listId)) {
        if (src.del) continue;

        const int64_t lamport = m_db.bumpLamport(copy.listId);
        const core::Ver ver{ lamport, m_deviceId.toStdString() };

        core::Item item;
        item.listId  = copy.listId;
        // itemId neuf : garder celui de la source ferait entrer en collision les deux
        // listes si l'une des deux était un jour fusionnée avec l'autre.
        item.itemId  = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        item.created = copy.created + copied; // conserve l'ordre d'affichage de la source
        item.by      = m_deviceId.toStdString();
        item.name    = src.name;
        item.nameVer = ver;
        item.qty     = src.qty;
        item.qtyVer  = ver;
        item.note    = src.note;
        item.noteVer = ver;
        item.aisle    = src.aisle;
        item.aisleVer = ver;
        // La photo suit : le blob est adressé par contenu, il est déjà en base.
        item.image    = src.image;
        item.imageVer = ver;
        item.order    = src.order;   // la copie garde le classement de l'originale
        item.orderVer = ver;
        item.done    = false;
        item.doneVer = ver;
        item.doneAt  = 0;
        item.del     = false;
        item.delVer  = ver;
        item.touched = copy.created;

        if (m_db.upsertItem(item)) ++copied;
    }

    m_listsModel->reload(m_db, m_deviceId.toStdString());
    m_recipesModel->reload(m_db, m_deviceId.toStdString());
    m_syncEngine.onListJoined(copy.listId);
    // Publier le contenu : sans ça, quelqu'un qui rejoindrait la copie par lien
    // trouverait un canal vide tant que personne n'y touche.
    m_syncEngine.onLocalChange(copy.listId);

    emit toast(copied > 0
        ? QStringLiteral("Liste dupliquée — %1 article(s) à acheter").arg(copied)
        : QStringLiteral("Liste dupliquée"));
}

void AppController::moveList(int from, int to) {
    m_listsModel->moveRow(m_db, from, to);
}

void AppController::importListInto(const QString &destListId, const QString &sourceListId,
                                   int targetServings) {
    if (destListId == sourceListId) return;  // s'importer soi-même n'a pas de sens

    auto destOpt = m_db.getList(destListId.toStdString());
    auto srcOpt  = m_db.getList(sourceListId.toStdString());
    if (!destOpt || !srcOpt) return;

    double qtyFactor = 1.0;
    if (srcOpt->isRecipe()) {
        const int base = recipeBaseServings(sourceListId);
        int target = targetServings > 0 ? targetServings : recipeTargetServings(sourceListId);
        if (base > 0 && target > 0)
            qtyFactor = static_cast<double>(target) / base;
    }

    const int64_t now = QDateTime::currentMSecsSinceEpoch();

    // Cache local des articles destination (évite N lectures SQLite à l'import).
    std::vector<core::Item> destItems = m_db.getItems(destOpt->listId);
    const auto findDestItem = [&](const std::string &id) -> core::Item * {
        for (auto &it : destItems) {
            if (it.itemId == id && !it.del)
                return &it;
        }
        return nullptr;
    };

    std::map<QString, std::string> destByKey;
    for (const auto &existing : destItems) {
        if (existing.del)
            continue;
        const QString key = core::ingredientMatchKey(QString::fromStdString(existing.name));
        if (!key.isEmpty() && !destByKey.count(key))
            destByKey[key] = existing.itemId;
    }

    int added = 0;
    int merged = 0;
    int offset = 0;
    for (const auto &src : m_db.getItems(srcOpt->listId)) {
        if (src.del) continue;

        const QString srcName = core::canonicalIngredientName(QString::fromStdString(src.name));
        const QString matchKey = core::ingredientMatchKey(srcName);
        const QString srcQty = core::scaleQuantity(QString::fromStdString(src.qty), qtyFactor);

        const auto destIt = destByKey.find(matchKey);
        if (!matchKey.isEmpty() && destIt != destByKey.end()) {
            core::Item *found = findDestItem(destIt->second);
            if (found) {
                const int64_t lamport = m_db.bumpLamport(destOpt->listId);
                const core::Ver ver{ lamport, m_deviceId.toStdString() };

                const QString mergedQty = core::mergeQuantities(
                    QString::fromStdString(found->qty), srcQty);
                found->qty     = mergedQty.toStdString();
                found->qtyVer  = ver;
                found->done    = false;
                found->doneVer = ver;
                found->doneAt  = 0;
                found->touched = now;

                const QString srcNote = QString::fromStdString(src.note).trimmed();
                const QString destNote = QString::fromStdString(found->note).trimmed();
                if (!srcNote.isEmpty() && destNote != srcNote) {
                    const QString combined = destNote.isEmpty()
                        ? srcNote
                        : destNote + QStringLiteral(" · ") + srcNote;
                    found->note    = combined.toStdString();
                    found->noteVer = ver;
                }

                if (m_db.upsertItem(*found))
                    ++merged;
                continue;
            }
        }

        const int64_t lamport = m_db.bumpLamport(destOpt->listId);
        const core::Ver ver{ lamport, m_deviceId.toStdString() };

        core::Item item;
        item.listId  = destOpt->listId;
        item.itemId  = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        item.created = now + offset;
        item.by      = m_deviceId.toStdString();
        item.name    = srcName.toStdString();
        item.nameVer = ver;
        item.qty     = srcQty.toStdString();
        item.qtyVer  = ver;
        item.note    = src.note;
        item.noteVer = ver;
        item.aisle    = src.aisle.empty()
            ? m_db.suggestAisleForName(item.name)
            : src.aisle;
        item.aisleVer = ver;
        item.image    = src.image;
        item.imageVer = ver;
        item.order    = now + offset;
        item.orderVer = ver;
        item.done    = false;
        item.doneVer = ver;
        item.doneAt  = 0;
        item.del     = false;
        item.delVer  = ver;
        item.touched = now;

        if (m_db.upsertItem(item)) {
            ++added;
            destItems.push_back(item);
            if (!matchKey.isEmpty())
                destByKey[matchKey] = item.itemId;
            ++offset;
        }
    }

    if (m_openListId == destOpt->listId)
        m_itemModel.load(m_db, destOpt->listId, m_deviceId.toStdString());

    m_listsModel->reload(m_db, m_deviceId.toStdString());
    m_recipesModel->reload(m_db, m_deviceId.toStdString());
    m_syncEngine.onLocalChange(destOpt->listId);

    if (added == 0 && merged == 0) {
        emit toast(QStringLiteral("Rien à importer (liste vide)"));
    } else if (merged > 0 && added == 0) {
        emit toast(QStringLiteral("%1 article(s) fusionné(s) avec la liste depuis « %2 »")
                       .arg(merged).arg(QString::fromStdString(srcOpt->title)));
    } else if (merged > 0) {
        emit toast(QStringLiteral("%1 ajouté(s), %2 fusionné(s) depuis « %3 »")
                       .arg(added).arg(merged).arg(QString::fromStdString(srcOpt->title)));
    } else {
        emit toast(QStringLiteral("%1 article(s) importé(s) depuis « %2 »")
                       .arg(added).arg(QString::fromStdString(srcOpt->title)));
    }
}

QVariantList AppController::otherLists(const QString &exceptListId) {
    QVariantList out;
    if (!m_db.isOpen()) return out;
    const std::string except = exceptListId.toStdString();
    for (const auto &meta : m_db.getLists()) {
        if (meta.listId == except || meta.isRecipe()) continue;
        QVariantMap m;
        m.insert(QStringLiteral("id"),   QString::fromStdString(meta.listId));
        m.insert(QStringLiteral("name"), QString::fromStdString(meta.title));
        out.append(m);
    }
    return out;
}

QVariantList AppController::shoppingLists() {
    QVariantList out;
    if (!m_db.isOpen()) return out;
    for (const auto &meta : m_db.getLists()) {
        if (meta.isRecipe()) continue;
        QVariantMap m;
        m.insert(QStringLiteral("id"),   QString::fromStdString(meta.listId));
        m.insert(QStringLiteral("name"), QString::fromStdString(meta.title));
        out.append(m);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Export / import
// ---------------------------------------------------------------------------

namespace {

// En-tête CSV. L'import reconnaît une ligne d'en-tête à sa première cellule, et se
// passe d'en-tête si le fichier n'en a pas (une simple colonne de noms marche).
const std::vector<std::string> kCsvHeader =
    { "Article", "Quantite", "Description", "Rayon", "Pris" };

bool looksLikeHeader(const std::vector<std::string>& row) {
    if (row.empty()) return false;
    QString first = QString::fromStdString(row[0]).trimmed().toLower();
    return first == "article" || first == "nom" || first == "name" || first == "item";
}

bool truthy(const std::string& s) {
    const QString v = QString::fromStdString(s).trimmed().toLower();
    return v == "oui" || v == "1" || v == "true" || v == "x" || v == "vrai";
}

// Nom de fichier sûr : pas de séparateur de chemin ni de caractère interdit.
QString sanitizeFileName(const QString& title) {
    QString out;
    for (QChar c : title) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c < ' ')
            out += ' ';
        else
            out += c;
    }
    out = out.trimmed();
    return out.isEmpty() ? QStringLiteral("liste") : out;
}

} // namespace

QString AppController::listCsv(const QString &listId) {
    auto metaOpt = m_db.getList(listId.toStdString());
    if (!metaOpt) return {};

    std::vector<std::vector<std::string>> rows;
    rows.push_back(kCsvHeader);
    for (const auto &it : m_db.getItems(listId.toStdString())) {
        if (it.del) continue;
        rows.push_back({ it.name, it.qty, it.note, it.aisle,
                         it.done ? "oui" : "" });
    }
    return QString::fromStdString(core::csvWrite(rows));
}

QString AppController::suggestedFileName(const QString &listId) {
    auto metaOpt = m_db.getList(listId.toStdString());
    const QString title = metaOpt ? QString::fromStdString(metaOpt->title)
                                  : QStringLiteral("liste");
    return sanitizeFileName(title) + QStringLiteral(".csv");
}

// Écrit des octets dans l'URL choisie. Sur Android, le sélecteur renvoie une URI
// content:// que QFile sait ouvrir ; ailleurs, un chemin fichier classique.
static bool writeUrl(const QUrl &url, const QByteArray &bytes) {
    const QString target = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile f(target);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "writeUrl: open failed" << target << f.errorString();
        return false;
    }
    const bool ok = f.write(bytes) == bytes.size();
    f.close();
    return ok;
}

static std::optional<QByteArray> readUrl(const QUrl &url) {
    const QString target = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile f(target);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "readUrl: open failed" << target << f.errorString();
        return std::nullopt;
    }
    return f.readAll();
}

bool AppController::exportListCsv(const QUrl &fileUrl, const QString &listId) {
    const QString csv = listCsv(listId);
    if (csv.isEmpty() && !m_db.getList(listId.toStdString())) {
        emit toast(QStringLiteral("Liste introuvable"));
        return false;
    }
    if (!writeUrl(fileUrl, csv.toUtf8())) {
        emit toast(QStringLiteral("Échec de l'enregistrement"));
        return false;
    }
    emit toast(QStringLiteral("Liste exportée"));
    return true;
}

bool AppController::exportAllZip(const QUrl &fileUrl) {
    std::vector<core::ZipEntry> entries;
    QStringList usedNames;

    for (const auto &meta : m_db.getLists()) {
        const QString listId = QString::fromStdString(meta.listId);
        // Nom de fichier = titre nettoyé, rendu unique (deux listes peuvent partager
        // un titre) pour ne pas écraser une entrée par une autre dans l'archive.
        QString base = sanitizeFileName(QString::fromStdString(meta.title));
        QString name = base + QStringLiteral(".csv");
        int n = 2;
        while (usedNames.contains(name))
            name = base + QStringLiteral(" (%1).csv").arg(n++);
        usedNames << name;

        entries.push_back({ name.toStdString(), listCsv(listId).toStdString() });
    }

    if (entries.empty()) {
        emit toast(QStringLiteral("Aucune liste à exporter"));
        return false;
    }
    if (!writeUrl(fileUrl, QByteArray::fromStdString(core::zipWrite(entries)))) {
        emit toast(QStringLiteral("Échec de l'enregistrement"));
        return false;
    }
    emit toast(QStringLiteral("%1 liste(s) exportée(s)").arg(entries.size()));
    return true;
}

// Crée une liste locale (clé neuve, non partagée tant qu'on n'a pas diffusé le lien),
// importe les articles décrits par `rows`, et retourne le nombre d'articles ajoutés.
int AppController::importRowsAsList(const QString &title,
                                    const std::vector<std::vector<std::string>> &rows) {
    core::ListMeta meta;
    meta.listId   = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    meta.key      = net::generateListKey();
    meta.title    = title.trimmed().isEmpty() ? std::string("Liste importée")
                                              : title.trimmed().toStdString();
    meta.titleVer = core::Ver{ 1, m_deviceId.toStdString() };
    meta.lamport  = 1;
    meta.created  = QDateTime::currentMSecsSinceEpoch();
    if (meta.key.size() != 32 || !m_db.createList(meta))
        return 0;

    int added = 0;
    for (const auto &row : rows) {
        if (looksLikeHeader(row)) continue;
        if (row.empty()) continue;
        const std::string name = QString::fromStdString(row[0]).trimmed().toStdString();
        if (name.empty()) continue;

        const int64_t lamport = m_db.bumpLamport(meta.listId);
        const core::Ver ver{ lamport, m_deviceId.toStdString() };

        core::Item item;
        item.listId  = meta.listId;
        item.itemId  = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        item.created = meta.created + added;
        item.by      = m_deviceId.toStdString();
        item.name    = name;                                     item.nameVer  = ver;
        item.qty     = row.size() > 1 ? row[1] : std::string();  item.qtyVer   = ver;
        item.note    = row.size() > 2 ? row[2] : std::string();  item.noteVer  = ver;
        item.aisle   = row.size() > 3 ? row[3] : std::string();  item.aisleVer = ver;
        item.order   = item.created;                             item.orderVer = ver;
        item.done    = row.size() > 4 && truthy(row[4]);         item.doneVer  = ver;
        item.doneAt  = item.done ? meta.created : 0;
        item.delVer  = ver;
        item.touched = meta.created;

        if (m_db.upsertItem(item)) ++added;
    }

    m_syncEngine.onListJoined(meta.listId);
    m_syncEngine.onLocalChange(meta.listId);   // publier le contenu importé
    return added;
}

QString AppController::importFile(const QUrl &fileUrl) {
    auto bytes = readUrl(fileUrl);
    if (!bytes) {
        const QString msg = QStringLiteral("Fichier illisible");
        emit toast(msg);
        return msg;
    }

    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    const std::string raw = bytes->toStdString();

    int lists = 0, items = 0;

    // ZIP (plusieurs listes) reconnu à sa signature, quel que soit le nom du fichier.
    const bool isZip = raw.size() > 4 && raw.compare(0, 4, "PK\x03\x04", 4) == 0;
    if (isZip || path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        auto archive = core::zipRead(raw);
        if (!archive) {
            const QString msg = QStringLiteral("Archive illisible");
            emit toast(msg);
            return msg;
        }
        for (const auto &entry : *archive) {
            QString title = QString::fromStdString(entry.name);
            if (title.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
                title.chop(4);
            const int n = importRowsAsList(title, core::csvParse(entry.data));
            if (n >= 0) { ++lists; items += n; }
        }
    } else {
        // CSV : une seule liste. Le titre vient du nom de fichier.
        QFileInfo info(path);
        QString title = info.completeBaseName();
        if (title.isEmpty()) title = QStringLiteral("Liste importée");
        const int n = importRowsAsList(title, core::csvParse(raw));
        lists = 1; items = n;
    }

    m_listsModel->reload(m_db, m_deviceId.toStdString());
    m_recipesModel->reload(m_db, m_deviceId.toStdString());

    const QString msg = (lists > 1)
        ? QStringLiteral("%1 listes importées (%2 articles)").arg(lists).arg(items)
        : QStringLiteral("Liste importée — %1 article(s)").arg(items);
    emit toast(msg);
    return msg;
}

QString AppController::createGroup(const QString &name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return {};

    const QString groupId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Ordre = date de création : les groupes apparaissent dans l'ordre où on les crée.
    if (!m_db.createGroup(groupId.toStdString(), trimmed.toStdString(),
                          QDateTime::currentMSecsSinceEpoch()))
        return {};

    m_listsModel->reload(m_db, m_deviceId.toStdString());
    m_recipesModel->reload(m_db, m_deviceId.toStdString());
    return groupId;
}

void AppController::renameGroup(const QString &groupId, const QString &name) {
    const QString trimmed = name.trimmed();
    if (groupId.isEmpty() || trimmed.isEmpty()) return;
    if (m_db.renameGroup(groupId.toStdString(), trimmed.toStdString())) {
        m_listsModel->reload(m_db, m_deviceId.toStdString());
        m_recipesModel->reload(m_db, m_deviceId.toStdString());
    }
}

void AppController::deleteGroup(const QString &groupId) {
    if (groupId.isEmpty()) return;
    if (m_db.deleteGroup(groupId.toStdString())) {
        m_listsModel->reload(m_db, m_deviceId.toStdString());
        m_recipesModel->reload(m_db, m_deviceId.toStdString());
        emit toast(QStringLiteral("Groupe supprimé — les listes sont conservées"));
    }
}

void AppController::setListGroup(const QString &listId, const QString &groupId) {
    if (m_db.setListGroup(listId.toStdString(), groupId.toStdString())) {
        m_listsModel->reload(m_db, m_deviceId.toStdString());
        m_recipesModel->reload(m_db, m_deviceId.toStdString());
    }
}

QVariantList AppController::groups() {
    QVariantList out;
    if (!m_db.isOpen()) return out;   // appelée avant init() (ou en test) : rien à lire
    for (const auto &g : m_db.getGroups()) {
        QVariantMap m;
        m.insert(QStringLiteral("id"),   QString::fromStdString(g.groupId));
        m.insert(QStringLiteral("name"), QString::fromStdString(g.name));
        out.append(m);
    }
    return out;
}

QString AppController::suggestAisle(const QString &name) {
    if (!m_db.isOpen()) return {};
    return QString::fromStdString(m_db.suggestAisleForName(name.toStdString()));
}

QStringList AppController::customAisles() {
    if (!m_db.isOpen()) return {};

    // Un rayon personnalisé = utilisé par un article, ou mémorisé pour une suggestion,
    // mais absent des rayons d'origine (ceux-là ne se gèrent pas).
    const QStringList defaults = app::ItemModel::defaultAisles();
    QStringList out;
    const auto add = [&](const std::vector<std::string> &src) {
        for (const auto &a : src) {
            const QString aisle = QString::fromStdString(a);
            if (!defaults.contains(aisle) && !out.contains(aisle))
                out << aisle;
        }
    };
    add(m_db.distinctItemAisles());
    add(m_db.distinctMemoryAisles());

    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    return out;
}

int AppController::countItemsInAisle(const QString &aisle) {
    if (!m_db.isOpen() || aisle.isEmpty()) return 0;
    const std::string a = aisle.toStdString();
    int n = 0;
    for (const auto &meta : m_db.getLists())
        for (const auto &it : m_db.getItems(meta.listId))
            if (!it.del && it.aisle == a) ++n;
    return n;
}

void AppController::reassignAisle(const QString &oldAisle, const QString &newAisle) {
    if (!m_db.isOpen() || oldAisle.isEmpty() || oldAisle == newAisle) return;

    const std::string from = oldAisle.toStdString();
    const std::string to   = newAisle.toStdString();
    const std::string dev  = m_deviceId.toStdString();
    const int64_t now = QDateTime::currentMSecsSinceEpoch();

    // Réaffecter les articles dans toutes les listes. Le rayon est un champ CRDT
    // répliqué : chaque changement bumpe le Lamport de sa liste, et un onLocalChange
    // par liste touchée publie la mise à jour aux autres participants.
    for (const auto &meta : m_db.getLists()) {
        bool listChanged = false;
        for (auto &it : m_db.getItems(meta.listId)) {
            if (it.del || it.aisle != from) continue;
            const int64_t lamport = m_db.bumpLamport(meta.listId);
            it.aisle    = to;
            it.aisleVer = core::Ver{ lamport, dev };
            it.touched  = now;
            if (m_db.upsertItem(it)) listChanged = true;
        }
        if (listChanged)
            m_syncEngine.onLocalChange(meta.listId);
    }

    // Mémoire des suggestions : renommer, ou oublier si le rayon disparaît.
    if (to.empty())
        m_db.forgetAisleInMemory(from);
    else
        m_db.renameAisleInMemory(from, to);

    // Rafraîchir la liste ouverte (ses articles ont pu changer de section).
    if (!m_openListId.empty())
        m_itemModel.load(m_db, m_openListId, dev);

    emit customAislesChanged();
}

void AppController::renameAisle(const QString &oldAisle, const QString &newAisle) {
    const QString trimmed = newAisle.trimmed();
    if (trimmed.isEmpty()) return;
    reassignAisle(oldAisle, trimmed);
}

void AppController::deleteAisle(const QString &aisle) {
    reassignAisle(aisle, QString());   // vers « sans rayon »
    emit toast(QStringLiteral("Rayon supprimé"));
}

QVariantList AppController::favorites() {
    QVariantList out;
    if (!m_db.isOpen()) return out;
    // Une douzaine suffit pour une barre qui se parcourt d'un pouce.
    for (const auto &f : m_db.getFavorites(12)) {
        QVariantMap m;
        m.insert(QStringLiteral("name"),   QString::fromStdString(f.name));
        m.insert(QStringLiteral("qty"),    QString::fromStdString(f.qty));
        m.insert(QStringLiteral("aisle"),  QString::fromStdString(f.aisle));
        m.insert(QStringLiteral("pinned"), f.pinned);
        out.append(m);
    }
    return out;
}

void AppController::pinFavorite(const QString &name, bool pinned) {
    if (m_db.setFavoritePinned(name.toStdString(), pinned))
        emit favoritesChanged();
}

void AppController::removeFavorite(const QString &name) {
    if (m_db.removeFavorite(name.toStdString()))
        emit favoritesChanged();
}

void AppController::leaveList(const QString &listId) {
    const std::string id = listId.toStdString();
    const bool recipe = isRecipe(listId);
    m_syncEngine.unregisterItemModel(id);
    if (m_openListId == id)
        m_openListId.clear();
    if (m_db.deleteList(id)) {
        m_listsModel->remove(listId);
        m_recipesModel->remove(listId);
        refreshPushTopics();
        emit toast(recipe
            ? QStringLiteral("Recette supprimée de cet appareil")
            : QStringLiteral("Liste supprimée de cet appareil"));
    }
}

void AppController::handleJoinUrl(const QUrl &url) {
    if (joinList(url.toString())) {
        refreshPushTopics();
        emit toast(QStringLiteral("Liste rejointe — synchronisation en cours"));
    } else {
        emit toast(QStringLiteral("Lien d'invitation invalide"));
    }
}

void AppController::setDisplayName(const QString &name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return;

    // Même nom mais premier choix explicite : il reste à poser le drapeau, sinon
    // l'écran d'accueil redemanderait le nom à chaque lancement.
    const bool firstChoice = !m_hasDisplayName;
    if (trimmed == m_displayName && !firstChoice) return;

    if (firstChoice) {
        m_hasDisplayName = true;
        m_db.setSetting("displayNameSet", "1");
    }

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

void AppController::vibrate(int ms) {
    app::platformVibrate(ms);
}

void AppController::setKeepScreenOn(bool on) {
    app::platformKeepScreenOn(on);
}

// ---------------------------------------------------------------------------
// Photo d'un article
// ---------------------------------------------------------------------------

namespace {
// Réduit une image en JPEG ≤ ~30 Ko. Le blob subit deux couches de base64 (payload
// puis contenu chiffré de l'événement), soit ×1,8 : au-delà, les relais publics
// (limite courante ~64 Ko par événement) refuseraient la photo.
QByteArray compressItemImage(const QString &path) {
    QImage img(path);
    if (img.isNull()) return {};
    constexpr int kMaxBytes = 30000;

    int side = 1024;
    for (;;) {
        const QImage scaled = (img.width() > side || img.height() > side)
            ? img.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            : img;
        for (const int quality : { 80, 70, 60, 50, 40 }) {
            QByteArray out;
            QBuffer buf(&out);
            buf.open(QIODevice::WriteOnly);
            scaled.save(&buf, "JPEG", quality);
            if (out.size() > 0 && out.size() <= kMaxBytes)
                return out;
        }
        if (side <= 320)
            return {};   // image irréductible (pathologique) : on renonce proprement
        side = side * 2 / 3;
    }
}
} // namespace

QString AppController::tempPhotoPath() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(
        QStringLiteral("colocourse-%1.jpg").arg(QDateTime::currentMSecsSinceEpoch()));
}

bool AppController::setItemImage(const QString &itemId, const QUrl &fileUrl) {
    if (m_openListId.empty()) return false;

    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    const QByteArray jpeg = compressItemImage(path);
    if (jpeg.isEmpty()) {
        emit toast(QStringLiteral("Impossible de lire cette image"));
        return false;
    }

    const QString sha = QString::fromLatin1(
        QCryptographicHash::hash(jpeg, QCryptographicHash::Sha256).toHex());

    if (!m_db.putImage(sha.toStdString(), jpeg)) {
        emit toast(QStringLiteral("Échec de l'enregistrement de l'image"));
        return false;
    }

    // Champ CRDT d'abord (delta débouncé), blob ensuite (événement img direct).
    // L'ordre d'arrivée chez les pairs est indifférent : la vignette s'affiche dès
    // que le blob ET le champ sont là.
    m_itemModel.addItemImage(itemId, sha);
    m_syncEngine.publishImage(m_openListId, sha.toStdString());

    ++m_imageRevision;
    emit imageRevisionChanged();
    return true;
}

void AppController::removeItemImage(const QString &itemId, const QString &sha) {
    // La photo sort de la liste (LWW) ; le blob orphelin sera purgé au démarrage.
    m_itemModel.removeItemImage(itemId, sha);
}

// ---------------------------------------------------------------------------
// Historique
// ---------------------------------------------------------------------------

QVariantList AppController::history(const QString &listId) {
    QVariantList out;
    if (!m_db.isOpen()) return out;
    // 500 : bien au-delà d'un an de courses quotidiennes, sans jamais peser à l'écran.
    for (const auto &e : m_db.getHistory(listId.toStdString(), 500)) {
        QVariantMap m;
        m.insert(QStringLiteral("name"),   QString::fromStdString(e.name));
        m.insert(QStringLiteral("aisle"),  QString::fromStdString(e.aisle));
        m.insert(QStringLiteral("doneAt"), static_cast<qlonglong>(e.doneAt));
        m.insert(QStringLiteral("byName"), QString::fromStdString(e.byName));
        out.append(m);
    }
    return out;
}

void AppController::clearHistory(const QString &listId) {
    if (m_db.clearHistory(listId.toStdString()))
        emit toast(QStringLiteral("Historique effacé"));
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

    if (metaOpt->isRecipe()) {
        applyRecipeDisplayScale(this, listId);
    } else {
        m_itemModel.setDisplayQtyScale(1.0);
    }

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
        m_listsModel->reload(m_db, m_deviceId.toStdString());
        m_recipesModel->reload(m_db, m_deviceId.toStdString());
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
