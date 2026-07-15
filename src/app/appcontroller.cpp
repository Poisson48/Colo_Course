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
    case ListIdRole:     return row.listId;
    case NameRole:       return row.name;
    case CountRole:      return row.count;
    case TotalRole:      return row.total;
    case GroupIdRole:    return row.groupId;
    case GroupNameRole:  return row.groupName;
    case MembersRole:    return row.members;
    case MemberCountRole:return row.memberCount;
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
    };
}

void ListsModel::reload(store::Database &db, const std::string &deviceId) {
    beginResetModel();
    m_rows.clear();

    // Table des groupes : id → (nom, ordre). Un rang par défaut très grand range les
    // listes non rangées après tous les groupes.
    std::map<std::string, std::pair<QString, int64_t>> groups;
    for (const auto &g : db.getGroups())
        groups[g.groupId] = { QString::fromStdString(g.name), g.sortOrder };

    for (const auto &meta : db.getLists()) {
        int unchecked = 0;
        int total     = 0;
        for (const auto &item : db.getItems(meta.listId)) {
            if (item.del) continue;
            ++total;
            if (!item.done) ++unchecked;
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
        row.members     = names.join(QStringLiteral(", "));
        row.memberCount = static_cast<int>(names.size());

        const auto git = groups.find(meta.groupId);
        if (!meta.groupId.empty() && git != groups.end()) {
            row.groupId    = QString::fromStdString(meta.groupId);
            row.groupName  = git->second.first;
            row.groupOrder = git->second.second;
        } else {
            // Non rangée (ou groupe supprimé) : après tous les groupes.
            row.groupOrder = std::numeric_limits<int64_t>::max();
        }
        m_rows.push_back(std::move(row));
    }

    // Trier par groupe pour que les sections soient contiguës ; l'ordre d'origine
    // (création) est préservé à l'intérieur d'un groupe par le tri stable.
    std::stable_sort(m_rows.begin(), m_rows.end(), [](const Row &a, const Row &b) {
        if (a.groupOrder != b.groupOrder) return a.groupOrder < b.groupOrder;
        return a.groupName < b.groupName;
    });

    endResetModel();
}

void ListsModel::rename(const QString &listId, const QString &name) {
    const auto it = std::find_if(m_rows.begin(), m_rows.end(),
                                 [&](const Row &r){ return r.listId == listId; });
    if (it == m_rows.end() || it->name == name) return;

    it->name = name;
    const int row = static_cast<int>(std::distance(m_rows.begin(), it));
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { NameRole });
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
    connect(&m_syncEngine, &SyncEngine::listTitleChanged,
            this,          &AppController::onRemoteTitleChanged);
    connect(&m_syncEngine, &SyncEngine::outboxChanged,
            this,          &AppController::onOutboxChanged);

    // Des modifications peuvent dormir dans l'outbox depuis la session précédente
    // (app fermée hors ligne) : l'état de départ n'est pas forcément « à jour ».
    m_pendingChanges = m_db.outboxCount();

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
}

void AppController::onSyncOnlineChanged(bool online) {
    if (m_online == online) return;
    m_online = online;
    emit onlineChanged();
}

void AppController::onRemoteChanges(const QString& /*listId*/, int /*count*/, const QString& /*authorName*/) {
    // Refresh the lists model so counts update.
    m_listsModel->reload(m_db, m_deviceId.toStdString());
}

void AppController::onRemoteTitleChanged(const QString& listId, const QString& title) {
    m_listsModel->rename(listId, title);
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
        // Souscrire tout de suite : sans ça, la liste n'est écoutée qu'au prochain
        // lancement et les modifications des autres n'arrivent jamais.
        m_syncEngine.onListJoined(meta.listId);
    }
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

    if (copy.key.size() != 32) {
        emit toast(QStringLiteral("Échec de la génération de la clé de chiffrement"));
        return;
    }
    if (!m_db.createList(copy)) return;

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
    m_syncEngine.onListJoined(copy.listId);
    // Publier le contenu : sans ça, quelqu'un qui rejoindrait la copie par lien
    // trouverait un canal vide tant que personne n'y touche.
    m_syncEngine.onLocalChange(copy.listId);

    emit toast(copied > 0
        ? QStringLiteral("Liste dupliquée — %1 article(s) à acheter").arg(copied)
        : QStringLiteral("Liste dupliquée"));
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
    return groupId;
}

void AppController::renameGroup(const QString &groupId, const QString &name) {
    const QString trimmed = name.trimmed();
    if (groupId.isEmpty() || trimmed.isEmpty()) return;
    if (m_db.renameGroup(groupId.toStdString(), trimmed.toStdString()))
        m_listsModel->reload(m_db, m_deviceId.toStdString());
}

void AppController::deleteGroup(const QString &groupId) {
    if (groupId.isEmpty()) return;
    if (m_db.deleteGroup(groupId.toStdString())) {
        m_listsModel->reload(m_db, m_deviceId.toStdString());
        emit toast(QStringLiteral("Groupe supprimé — les listes sont conservées"));
    }
}

void AppController::setListGroup(const QString &listId, const QString &groupId) {
    if (m_db.setListGroup(listId.toStdString(), groupId.toStdString()))
        m_listsModel->reload(m_db, m_deviceId.toStdString());
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
        m_listsModel->reload(m_db, m_deviceId.toStdString());
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
