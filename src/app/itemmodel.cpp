#include "itemmodel.h"

#include "../core/recipe_scale.h"

#include <QUuid>
#include <QDateTime>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <set>

namespace app {

// Monotonically increasing created timestamp: ensures items added within the same
// millisecond get distinct, ordered values (critical for deterministic sort).
static int64_t nextCreated() {
    static std::atomic<int64_t> s_last{ 0 };
    const int64_t now = QDateTime::currentMSecsSinceEpoch();
    int64_t prev = s_last.load(std::memory_order_relaxed);
    int64_t next;
    do {
        next = (now > prev) ? now : prev + 1;
    } while (!s_last.compare_exchange_weak(prev, next,
                                           std::memory_order_relaxed,
                                           std::memory_order_relaxed));
    return next;
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

const QStringList &ItemModel::defaultAisles() {
    // L'ordre est celui d'un parcours de magasin, pas l'alphabet : on entre par les
    // fruits, on finit par l'entretien. C'est lui qui ordonne les sections.
    static const QStringList kAisles = {
        QStringLiteral("Fruits & légumes"),
        QStringLiteral("Boulangerie"),
        QStringLiteral("Boucherie"),
        QStringLiteral("Poissonnerie"),
        QStringLiteral("Crèmerie"),
        QStringLiteral("Épicerie salée"),
        QStringLiteral("Épicerie sucrée"),
        QStringLiteral("Boissons"),
        QStringLiteral("Surgelés"),
        QStringLiteral("Hygiène"),
        QStringLiteral("Entretien"),
    };
    return kAisles;
}

int ItemModel::aisleRank(const std::string &aisle) {
    const int known = defaultAisles().size();

    // Non classé : tout à la fin. Les rayons forment un parcours ; un article sans
    // rayon n'a pas de place dedans, on le vérifie en fin de course.
    if (aisle.empty())
        return known + 1;

    const int idx = defaultAisles().indexOf(QString::fromStdString(aisle));
    // Rayon inventé : après les rayons d'origine, avant les non classés. Entre eux,
    // c'est le libellé qui départage (rowLessThan) — donc le même ordre partout, sans
    // avoir à synchroniser un classement des rayons.
    return idx >= 0 ? idx : known;
}

QStringList ItemModel::aisleNames() const {
    QStringList names = defaultAisles();

    // Les rayons inventés se déduisent des articles : ils voyagent déjà avec eux (le
    // champ `aisle` porte le libellé), donc celui que l'un crée apparaît chez l'autre
    // sans rien ajouter au protocole.
    QStringList custom;
    for (const auto &item : m_items) {
        if (item.del || item.aisle.empty())
            continue;
        const QString aisle = QString::fromStdString(item.aisle);
        if (!names.contains(aisle) && !custom.contains(aisle))
            custom << aisle;
    }

    // Ordre alphabétique (sensible aux accents) : le même sur tous les appareils.
    std::sort(custom.begin(), custom.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });

    return names + custom;
}

bool ItemModel::isValidSortMode(const QString &mode) {
    return mode == QLatin1String("aisle")
        || mode == QLatin1String("manual")
        || mode == QLatin1String("name")
        || mode == QLatin1String("created");
}

bool ItemModel::rowLessThan(const Row &a, const Row &b, const QString &mode) {
    if (mode == QLatin1String("name")) {
        const int cmp = QString::localeAwareCompare(
            QString::fromStdString(a.item.name).toCaseFolded(),
            QString::fromStdString(b.item.name).toCaseFolded());
        if (cmp != 0)
            return cmp < 0;
        if (a.item.created != b.item.created)
            return a.item.created < b.item.created;
        return a.item.itemId < b.item.itemId;
    }

    if (mode == QLatin1String("created")) {
        if (a.item.created != b.item.created)
            return a.item.created < b.item.created;
        return a.item.itemId < b.item.itemId;
    }

    // Mode rayon : on regroupe d'abord par rayon (ordre du magasin). En mode manuel,
    // le rayon ne compte pas — la liste est une seule séquence ordonnée à la main.
    if (mode != QLatin1String("manual")) {
        const int rankA = aisleRank(a.item.aisle);
        const int rankB = aisleRank(b.item.aisle);
        if (rankA != rankB)
            return rankA < rankB;
        // Deux rayons de même rang mais de libellés différents (deux inconnus) : les
        // départager, sinon les sections s'entremêleraient.
        if (a.item.aisle != b.item.aisle)
            return a.item.aisle < b.item.aisle;
    }

    // L'état pris/à acheter n'entre PAS dans le tri : cocher ne déplace rien, ni chez
    // soi ni chez les autres. À plusieurs en magasin, une liste qui se réorganise à
    // chaque cochage est illisible ; un article pris s'estompe, il ne bouge pas.
    if (a.item.order != b.item.order)
        return a.item.order < b.item.order;
    return a.item.itemId < b.item.itemId;   // départage stable
}

// ---------------------------------------------------------------------------
// ItemModel
// ---------------------------------------------------------------------------

ItemModel::ItemModel(QObject *parent)
    : QAbstractListModel(parent)
{}

void ItemModel::load(store::Database &db, const std::string &listId, const std::string &deviceId) {
    m_db       = &db;
    m_listId   = listId;
    m_deviceId = deviceId;

    m_memberNames.clear();
    for (const auto &[devId, name] : db.getMembers(listId))
        m_memberNames[devId] = QString::fromStdString(name);

    const QString mode = loadLocalSortMode();
    if (mode != m_sortMode) {
        m_sortMode = mode;
        emit sortModeChanged();
    }

    m_items = db.getItems(listId);
    rebuildRows();
}

QString ItemModel::loadLocalSortMode() const {
    if (!m_db || m_listId.empty())
        return QStringLiteral("aisle");

    const std::string key = "sortMode/" + m_listId;
    auto local = m_db->getSetting(key);
    if (local && isValidSortMode(QString::fromStdString(*local)))
        return QString::fromStdString(*local);

    // Migration : importer une seule fois l'ancien sortMode répliqué s'il vaut "manual".
    auto meta = m_db->getList(m_listId);
    QString migrated = QStringLiteral("aisle");
    if (meta && meta->sortMode == "manual")
        migrated = QStringLiteral("manual");
    m_db->setSetting(key, migrated.toStdString());
    return migrated;
}

void ItemModel::saveLocalSortMode(const QString &mode) {
    if (!m_db || m_listId.empty()) return;
    m_db->setSetting("sortMode/" + m_listId, mode.toStdString());
}

void ItemModel::setSortMode(const QString &mode) {
    if (!isValidSortMode(mode) || mode == m_sortMode) return;
    // Préférence locale uniquement : pas de bump Lamport, pas de publication.
    // Changer de mode ne mute jamais `order`.
    saveLocalSortMode(mode);
    m_sortMode = mode;
    emit sortModeChanged();
    rebuildRows();
}

void ItemModel::setDisplayQtyScale(double scale) {
    if (scale <= 0.0)
        scale = 1.0;
    if (std::fabs(scale - m_displayQtyScale) < 1e-6)
        return;
    m_displayQtyScale = scale;
    emit displayQtyScaleChanged();
    if (!m_rows.empty())
        emit dataChanged(index(0), index(rowCount() - 1), { QtyRole });
}

bool ItemModel::canReorder() const {
    return m_sortMode == QLatin1String("aisle")
        || m_sortMode == QLatin1String("manual");
}

// Filtre d'affichage : nom, quantité ou description. Insensible à la casse — on tape
// « lait », pas « Lait ».
bool ItemModel::matchesFilter(const core::Item &item) const {
    if (m_filter.isEmpty())
        return true;

    const auto has = [&](const std::string &field) {
        return QString::fromStdString(field).contains(m_filter, Qt::CaseInsensitive);
    };
    return has(item.name) || has(item.qty) || has(item.note);
}

void ItemModel::setFilter(const QString &filter) {
    if (m_filter == filter)
        return;
    m_filter = filter;
    emit filterChanged();
    rebuildRows();
}

void ItemModel::rebuildRows() {
    beginResetModel();
    m_rows.clear();
    for (const auto &item : m_items) {
        if (!item.del && matchesFilter(item)) {
            m_rows.push_back(Row{ item });
        }
    }
    const QString mode = m_sortMode;
    std::stable_sort(m_rows.begin(), m_rows.end(),
                     [mode](const Row &a, const Row &b){ return rowLessThan(a, b, mode); });
    endResetModel();
    emit countChanged();
    emit doneCountChanged();
    // Un merge distant peut apporter un rayon qu'on ne connaissait pas.
    emit aisleNamesChanged();
    emit refreshed();
}

int ItemModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_rows.size());
}

QVariant ItemModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= (int)m_rows.size())
        return {};
    const auto &row = m_rows[static_cast<size_t>(index.row())];
    switch (role) {
    case ItemIdRole:  return QString::fromStdString(row.item.itemId);
    case NameRole:    return QString::fromStdString(row.item.name);
    case QtyRole: {
        const QString raw = QString::fromStdString(row.item.qty);
        if (m_displayQtyScale <= 0.0 || std::fabs(m_displayQtyScale - 1.0) < 1e-6)
            return raw;
        return core::scaleQuantity(raw, m_displayQtyScale);
    }
    case BaseQtyRole:
        return QString::fromStdString(row.item.qty);
    case NoteRole:    return QString::fromStdString(row.item.note);
    case DoneRole:    return row.item.done;
    case DoneAtRole:  return static_cast<qlonglong>(row.item.doneAt);
    case CreatedRole: return static_cast<qlonglong>(row.item.created);
    case AisleRole:   return QString::fromStdString(row.item.aisle);
    case ImageRole:   return QString::fromStdString(row.item.image);
    case ByNameRole: {
        if (row.item.by.empty())
            return QString{};
        if (row.item.by == m_deviceId)
            return QStringLiteral("vous");
        const auto it = m_memberNames.find(row.item.by);
        // Participant jamais vu passer d'événement : mieux vaut ne rien dire que
        // d'afficher un identifiant d'appareil.
        return it != m_memberNames.end() ? it->second : QString{};
    }
    default:          return {};
    }
}

QHash<int, QByteArray> ItemModel::roleNames() const {
    return {
        { ItemIdRole,  "itemId"  },
        { NameRole,    "name"    },
        { QtyRole,     "qty"     },
        { BaseQtyRole, "baseQty" },
        { NoteRole,    "note"    },
        { DoneRole,    "done"    },
        { DoneAtRole,  "doneAt"  },
        { CreatedRole, "created" },
        { ByNameRole,  "byName"  },
        { AisleRole,   "aisle"   },
        { ImageRole,   "image"   },
    };
}

int ItemModel::aisleCount() const {
    // Hors mode rayon, la liste n'a pas de sections : un seul « rayon » du point de
    // vue de la vue, pour que les en-têtes disparaissent.
    if (m_sortMode != QLatin1String("aisle")) return 1;
    std::set<std::string> distinct;
    for (const auto &row : m_rows)
        distinct.insert(row.item.aisle);
    return static_cast<int>(distinct.size());
}

QString ItemModel::existingName(const QString &name) const {
    const QString needle = name.trimmed();
    if (needle.isEmpty())
        return {};

    // On cherche dans m_items et pas dans m_rows : un doublon reste un doublon même
    // si un filtre de recherche le cache à l'écran.
    for (const auto &item : m_items) {
        if (item.del)
            continue;
        const QString existing = QString::fromStdString(item.name).trimmed();
        if (existing.compare(needle, Qt::CaseInsensitive) == 0)
            return existing;
    }
    return {};
}

QString ItemModel::itemImage(const QString &itemId) const {
    const std::string id = itemId.toStdString();
    for (const auto &item : m_items) {
        if (item.itemId == id && !item.del)
            return QString::fromStdString(item.image);
    }
    return {};
}

int ItemModel::count() const {
    return static_cast<int>(m_rows.size());
}

int ItemModel::doneCount() const {
    return static_cast<int>(std::count_if(m_rows.begin(), m_rows.end(),
                                          [](const Row &r){ return r.item.done; }));
}

int ItemModel::findRow(const QString &itemId) const {
    const std::string id = itemId.toStdString();
    for (int i = 0; i < (int)m_rows.size(); ++i) {
        if (m_rows[static_cast<size_t>(i)].item.itemId == id) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Mutation slots
// ---------------------------------------------------------------------------

void ItemModel::addItem(const QString &name, const QString &qty,
                        const QString &note, const QString &aisle) {
    if (!m_db) return;

    const int64_t lamport = m_db->bumpLamport(m_listId);
    const core::Ver ver{ lamport, m_deviceId };
    const int64_t ts = nextCreated();

    core::Item item;
    item.listId  = m_listId;
    item.itemId  = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    item.created = ts;
    item.by      = m_deviceId;
    item.name    = name.toStdString();
    item.nameVer = ver;
    item.qty     = qty.toStdString();
    item.qtyVer  = ver;
    item.note    = note.trimmed().toStdString();
    item.noteVer = ver;
    // Le rayon est celui fourni par l'appelant, tel quel (la barre d'ajout le pré-remplit
    // côté UI, mais rien n'est assigné en douce : un rayon vide reste vide).
    item.aisle   = aisle.trimmed().toStdString();
    item.aisleVer = ver;
    // Position initiale = date de création : l'article se pose en bas de son groupe,
    // et l'écart avec le précédent (des millisecondes) laisse la place d'en insérer
    // d'autres entre les deux sans renuméroter.
    item.order    = ts;
    item.orderVer = ver;
    item.done    = false;
    item.doneVer = ver;
    item.doneAt  = 0;
    item.del     = false;
    item.delVer  = ver;
    item.touched = ts;

    if (!m_db->upsertItem(item)) return;

    // Apprendre l'habitude : cet article rejoint (ou renforce) les favoris fréquents.
    // Seulement sur un ajout manuel — imports et duplications écrivent en base
    // directement, sans passer par ici, donc sans polluer les favoris.
    m_db->recordFavoriteUse(item.name, item.qty, item.aisle, ts);
    // Et mémoriser où va cet article, pour que les suivants du même nom suivent.
    m_db->recordAisleForName(item.name, item.aisle, ts);

    emit localChanged(m_listId);
    emit aisleNamesChanged();
    emit itemAdded();

    // Insert into m_items (full set).
    m_items.push_back(item);

    // Un filtre de recherche est actif et le nouvel article n'y répond pas : il existe,
    // mais n'a rien à faire dans la vue courante.
    if (!matchesFilter(item))
        return;

    // Find sorted insertion position (upper_bound to append after equals).
    Row newRow{ item };
    const QString mode = m_sortMode;
    const auto less = [mode](const Row &a, const Row &b){ return rowLessThan(a, b, mode); };
    auto it = std::upper_bound(m_rows.begin(), m_rows.end(), newRow, less);
    const int pos = static_cast<int>(it - m_rows.begin());

    beginInsertRows({}, pos, pos);
    m_rows.insert(it, newRow);
    endInsertRows();
    emit countChanged();
    emit doneCountChanged();
    emit refreshed();
}

void ItemModel::toggleDone(const QString &itemId) {
    if (!m_db) return;

    const std::string id = itemId.toStdString();

    // Find in authoritative set.
    auto it = std::find_if(m_items.begin(), m_items.end(),
                           [&](const core::Item &i){ return i.itemId == id; });
    if (it == m_items.end()) return;

    const int64_t lamport = m_db->bumpLamport(m_listId);
    const core::Ver ver{ lamport, m_deviceId };
    const int64_t now = QDateTime::currentMSecsSinceEpoch();

    const bool newDone = !it->done;
    it->done    = newDone;
    it->doneVer = ver;
    it->doneAt  = newDone ? now : 0;  // décoché → plus de date de cochage
    it->touched = now;

    if (!m_db->upsertItem(*it)) return;

    // Historique : chaque article coché ici laisse une trace locale durable — elle
    // survivra à « Retirer les articles pris ». « vous » : c'est ce que l'UI affiche
    // pour soi-même partout ailleurs.
    if (newDone) {
        m_db->appendHistory(m_listId,
                            { it->itemId, it->name, it->aisle, now, "vous" });
    }

    emit localChanged(m_listId);
    emit aisleNamesChanged();

    // Cocher ne déplace pas la ligne : l'état pris/à acheter ne fait pas partie du
    // tri (rowLessThan). La ligne change d'apparence, pas de place.
    const int pos = findRow(itemId);
    if (pos < 0) return;
    m_rows[static_cast<size_t>(pos)].item = *it;
    const QModelIndex idx = index(pos);
    emit dataChanged(idx, idx, { DoneRole, DoneAtRole });

    // Le nombre de lignes n'a pas bougé, mais la progression du mode Courses si :
    // c'est le seul moment où elle avance.
    emit doneCountChanged();
    emit refreshed();
}

void ItemModel::editItem(const QString &itemId, const QString &name,
                         const QString &qty, const QString &note, const QString &aisle) {
    if (!m_db) return;

    const std::string id       = itemId.toStdString();
    const std::string newName  = name.trimmed().toStdString();
    const std::string newQty   = qty.trimmed().toStdString();
    const std::string newNote  = note.trimmed().toStdString();
    const std::string newAisle = aisle.trimmed().toStdString();

    if (newName.empty()) return; // un article sans nom n'a rien à afficher

    auto it = std::find_if(m_items.begin(), m_items.end(),
                           [&](const core::Item &i){ return i.itemId == id; });
    if (it == m_items.end()) return;

    const bool nameChanged  = (it->name  != newName);
    const bool qtyChanged   = (it->qty   != newQty);
    const bool noteChanged  = (it->note  != newNote);
    const bool aisleChanged = (it->aisle != newAisle);
    if (!nameChanged && !qtyChanged && !noteChanged && !aisleChanged) return;

    const int64_t lamport = m_db->bumpLamport(m_listId);
    const core::Ver ver{ lamport, m_deviceId };

    if (nameChanged)  { it->name  = newName;  it->nameVer  = ver; }
    if (qtyChanged)   { it->qty   = newQty;   it->qtyVer   = ver; }
    if (noteChanged)  { it->note  = newNote;  it->noteVer  = ver; }
    if (aisleChanged) { it->aisle = newAisle; it->aisleVer = ver; }
    it->touched = QDateTime::currentMSecsSinceEpoch();

    if (!m_db->upsertItem(*it)) return;

    // Ranger un article enseigne où va son nom, pour pré-remplir les prochains.
    if (aisleChanged && !newAisle.empty())
        m_db->recordAisleForName(it->name, newAisle, it->touched);

    emit localChanged(m_listId);
    emit aisleNamesChanged();

    // Le rayon / le nom peuvent entrer dans le tri : rebuild si le mode le demande.
    if (aisleChanged || nameChanged
        || m_sortMode == QLatin1String("aisle")
        || m_sortMode == QLatin1String("name")
        || !m_filter.isEmpty()) {
        rebuildRows();
        return;
    }

    // Aucun de ces champs n'entre dans le tri (done, created) : la ligne ne bouge
    // pas, seul son contenu change.
    const int pos = findRow(itemId);
    if (pos >= 0) {
        m_rows[static_cast<size_t>(pos)].item = *it;
        const QModelIndex idx = index(pos);
        emit dataChanged(idx, idx, { NameRole, QtyRole, NoteRole, AisleRole });
    }
    emit refreshed();
}

// Écrit la nouvelle liste de photos (LWW sur le champ entier : deux ajouts
// simultanés sur deux appareils → le dernier gagne, comme pour tout champ).
void ItemModel::writeImageList(const QString &itemId, const QStringList &shas) {
    const std::string id     = itemId.toStdString();
    const std::string joined = shas.join(QLatin1Char(' ')).toStdString();

    auto it = std::find_if(m_items.begin(), m_items.end(),
                           [&](const core::Item &i){ return i.itemId == id; });
    if (it == m_items.end() || it->image == joined) return;

    const int64_t lamport = m_db->bumpLamport(m_listId);
    it->image    = joined;
    it->imageVer = { lamport, m_deviceId };
    it->touched  = QDateTime::currentMSecsSinceEpoch();

    if (!m_db->upsertItem(*it)) return;

    emit localChanged(m_listId);

    // Les photos n'entrent pas dans le tri : la ligne ne bouge pas.
    const int pos = findRow(itemId);
    if (pos >= 0) {
        m_rows[static_cast<size_t>(pos)].item = *it;
        const QModelIndex idx = index(pos);
        emit dataChanged(idx, idx, { ImageRole });
    }
    emit refreshed();
}

QStringList ItemModel::imageList(const QString &itemId) const {
    const std::string id = itemId.toStdString();
    const auto it = std::find_if(m_items.begin(), m_items.end(),
                                 [&](const core::Item &i){ return i.itemId == id; });
    if (it == m_items.end()) return {};
    return QString::fromStdString(it->image)
        .split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

void ItemModel::addItemImage(const QString &itemId, const QString &sha) {
    if (!m_db || sha.trimmed().isEmpty()) return;
    QStringList shas = imageList(itemId);
    if (shas.contains(sha)) return;   // même contenu → même photo, rien à faire
    shas.append(sha);
    writeImageList(itemId, shas);
}

void ItemModel::removeItemImage(const QString &itemId, const QString &sha) {
    if (!m_db) return;
    QStringList shas = imageList(itemId);
    if (!shas.removeAll(sha)) return;
    writeImageList(itemId, shas);
}

void ItemModel::removeItems(const QStringList &itemIds) {
    for (const QString &id : itemIds)
        removeItem(id);
}

void ItemModel::setAisle(const QString &itemId, const QString &aisle) {
    if (!m_db) return;

    const std::string id       = itemId.toStdString();
    const std::string newAisle = aisle.trimmed().toStdString();

    auto it = std::find_if(m_items.begin(), m_items.end(),
                           [&](const core::Item &i){ return i.itemId == id; });
    if (it == m_items.end() || it->aisle == newAisle) return;

    const int64_t lamport = m_db->bumpLamport(m_listId);
    it->aisle    = newAisle;
    it->aisleVer = { lamport, m_deviceId };
    it->touched  = QDateTime::currentMSecsSinceEpoch();

    if (!m_db->upsertItem(*it)) return;

    if (!newAisle.empty())
        m_db->recordAisleForName(it->name, newAisle, it->touched);

    emit localChanged(m_listId);
    emit aisleNamesChanged();
    // Le rayon est la première clé de tri : l'article change de section.
    rebuildRows();
}

// Déplacement manuel. La nouvelle position se glisse entre les deux voisins de
// destination ; c'est un simple champ LWW de plus, donc deux personnes qui réordonnent
// en même temps convergent (le dernier gagne) au lieu de se marcher dessus.
void ItemModel::moveItem(int from, int to) {
    if (!m_db || !canReorder()) return;
    const int n = static_cast<int>(m_rows.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;

    core::Item moved = m_rows[static_cast<size_t>(from)].item;

    // Voisins à l'arrivée, une fois la ligne retirée de sa place actuelle.
    std::vector<Row> without = m_rows;
    without.erase(without.begin() + from);

    const Row *before = (to > 0) ? &without[static_cast<size_t>(to - 1)] : nullptr;
    const Row *after  = (to < static_cast<int>(without.size()))
                        ? &without[static_cast<size_t>(to)] : nullptr;

    // En mode rayon, l'article prend le rayon de la ligne qu'on a survolée pour le
    // déposer — c'est elle que le doigt désigne. Selon le sens du geste, cette ligne se
    // retrouve au-dessus ou en dessous de la place visée : en descendant, on vient se
    // poser APRÈS elle (elle est donc `before`) ; en montant, on vient prendre sa place
    // (elle est donc `after`). En mode manuel, glisser NE reclasse pas : le rayon ne
    // bouge pas, on ne fait que déplacer dans la séquence unique.
    const bool manual = (m_sortMode == QLatin1String("manual"));
    const Row *hovered = (from < to) ? before : after;
    const std::string targetAisle = manual
        ? moved.aisle
        : (hovered ? hovered->item.aisle : moved.aisle);

    // Les voisins qui comptent pour la position sont ceux du même groupe : même rayon
    // en mode rayon, toute la liste en mode manuel. Le tri les sépare avant de
    // regarder la position. L'état pris/à acheter ne compte pas : il n'est pas trié.
    const auto sameGroup = [&](const Row *r) {
        return r && (manual || r->item.aisle == targetAisle);
    };
    const int64_t lo = sameGroup(before) ? before->item.order : 0;
    const int64_t hi = sameGroup(after)  ? after->item.order  : 0;

    int64_t order;
    if (lo && hi)
        order = lo + (hi - lo) / 2;          // entre les deux
    else if (lo)
        order = lo + 1000;                   // en fin de groupe
    else if (hi)
        order = hi - 1000;                   // en tête de groupe
    else
        order = moved.created;               // seul de son groupe

    // Intervalle épuisé (des milliers de déplacements au même endroit) : renuméroter
    // le groupe à grands pas plutôt que d'écraser silencieusement l'ordre voulu.
    if (lo && hi && (order == lo || order == hi)) {
        renumber(targetAisle);
        moveItem(from, to);   // les positions sont ré-espacées : rejouer le geste
        return;
    }

    const int64_t lamport = m_db->bumpLamport(m_listId);
    const core::Ver ver{ lamport, m_deviceId };
    const int64_t now = QDateTime::currentMSecsSinceEpoch();

    auto it = std::find_if(m_items.begin(), m_items.end(),
                           [&](const core::Item &i){ return i.itemId == moved.itemId; });
    if (it == m_items.end()) return;

    it->order    = order;
    it->orderVer = ver;
    const bool aisleChanged = (it->aisle != targetAisle);
    if (aisleChanged) {
        it->aisle    = targetAisle;
        it->aisleVer = ver;
    }
    it->touched = now;

    if (!m_db->upsertItem(*it)) return;

    // Glisser un article dans un rayon enseigne aussi où va son nom.
    if (aisleChanged && !targetAisle.empty())
        m_db->recordAisleForName(it->name, targetAisle, now);

    emit localChanged(m_listId);
    emit aisleNamesChanged();

    // Déplacement de ligne, surtout pas beginResetModel : une reconstruction détruirait
    // tous les délégués — dont celui que le doigt est en train de glisser.
    // La position calculée place l'article exactement à `to` selon le tri : on peut
    // donc déplacer la ligne directement, sans retrier.
    const int dest = (from < to) ? to + 1 : to;   // Qt insère AVANT `dest`
    beginMoveRows({}, from, from, {}, dest);
    Row row = m_rows[static_cast<size_t>(from)];
    row.item = *it;
    m_rows.erase(m_rows.begin() + from);
    m_rows.insert(m_rows.begin() + to, row);
    endMoveRows();

    const QModelIndex idx = index(to);
    emit dataChanged(idx, idx, { AisleRole });
}

// Ré-espace les positions d'un groupe (même rayon ; toute la liste en mode manuel)
// par pas de 1000.
void ItemModel::renumber(const std::string &aisle) {
    if (!m_db) return;

    const int64_t lamport = m_db->bumpLamport(m_listId);
    const core::Ver ver{ lamport, m_deviceId };

    int64_t next = 1000;
    for (auto &row : m_rows) {
        // En mode manuel le rayon ne délimite pas le groupe : tout est renuméroté.
        if (m_sortMode != QLatin1String("manual") && row.item.aisle != aisle)
            continue;
        auto it = std::find_if(m_items.begin(), m_items.end(),
                               [&](const core::Item &i){ return i.itemId == row.item.itemId; });
        if (it == m_items.end()) continue;
        it->order    = next;
        it->orderVer = ver;
        m_db->upsertItem(*it);
        next += 1000;
    }
    rebuildRows();
}

void ItemModel::uncheckAll() {
    if (!m_db) return;

    // Un seul tick de Lamport pour tout le lot : c'est une seule intention (« on
    // recommence la liste »), et le SyncEngine n'en publiera qu'un delta.
    const int64_t lamport = m_db->bumpLamport(m_listId);
    const core::Ver ver{ lamport, m_deviceId };
    const int64_t now = QDateTime::currentMSecsSinceEpoch();

    bool any = false;
    for (auto &item : m_items) {
        if (item.del || !item.done)
            continue;
        item.done    = false;
        item.doneVer = ver;
        item.doneAt  = 0;
        item.touched = now;
        if (m_db->upsertItem(item))
            any = true;
    }

    if (!any) return;

    emit localChanged(m_listId);
    emit aisleNamesChanged();
    // Tout a changé de camp et donc de place dans le tri : reconstruire est plus
    // simple, et plus sûr, que d'orchestrer N déplacements de lignes.
    rebuildRows();
}

void ItemModel::removeDone() {
    if (!m_db) return;

    const int64_t lamport = m_db->bumpLamport(m_listId);
    const core::Ver ver{ lamport, m_deviceId };
    const int64_t now = QDateTime::currentMSecsSinceEpoch();

    bool any = false;
    for (auto &item : m_items) {
        if (item.del || !item.done)
            continue;
        // Tombstone, pas effacement : les autres appareils doivent apprendre la
        // suppression, sinon leur copie ferait réapparaître l'article au prochain merge.
        item.del     = true;
        item.delVer  = ver;
        item.touched = now;
        if (m_db->upsertItem(item))
            any = true;
    }

    if (!any) return;

    emit localChanged(m_listId);
    emit aisleNamesChanged();
    rebuildRows();
}

void ItemModel::removeItem(const QString &itemId) {
    if (!m_db) return;

    const std::string id = itemId.toStdString();

    // Find in authoritative set.
    auto it = std::find_if(m_items.begin(), m_items.end(),
                           [&](const core::Item &i){ return i.itemId == id; });
    if (it == m_items.end()) return;

    const int64_t lamport = m_db->bumpLamport(m_listId);
    const core::Ver ver{ lamport, m_deviceId };
    const int64_t now = QDateTime::currentMSecsSinceEpoch();

    it->del     = true;
    it->delVer  = ver;
    it->touched = now;

    if (!m_db->upsertItem(*it)) return;

    emit localChanged(m_listId);
    emit aisleNamesChanged();

    // Remove from visible rows.
    const int pos = findRow(itemId);
    if (pos >= 0) {
        beginRemoveRows({}, pos, pos);
        m_rows.erase(m_rows.begin() + static_cast<size_t>(pos));
        endRemoveRows();
        emit countChanged();
        emit doneCountChanged();
    }
}

} // namespace app
