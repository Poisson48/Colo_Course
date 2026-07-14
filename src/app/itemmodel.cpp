#include "itemmodel.h"

#include <QUuid>
#include <QDateTime>
#include <algorithm>
#include <atomic>

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

bool ItemModel::rowLessThan(const Row &a, const Row &b) {
    // Non-done before done.
    if (a.item.done != b.item.done)
        return !a.item.done; // false < true
    // Same done status: earlier created first; use itemId as tiebreaker for stability.
    if (a.item.created != b.item.created)
        return a.item.created < b.item.created;
    return a.item.itemId < b.item.itemId;
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

    m_items = db.getItems(listId);
    rebuildRows();
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
    std::stable_sort(m_rows.begin(), m_rows.end(), rowLessThan);
    endResetModel();
    emit countChanged();
    emit doneCountChanged();
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
    case QtyRole:     return QString::fromStdString(row.item.qty);
    case NoteRole:    return QString::fromStdString(row.item.note);
    case DoneRole:    return row.item.done;
    case DoneAtRole:  return static_cast<qlonglong>(row.item.doneAt);
    case CreatedRole: return static_cast<qlonglong>(row.item.created);
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
        { NoteRole,    "note"    },
        { DoneRole,    "done"    },
        { DoneAtRole,  "doneAt"  },
        { CreatedRole, "created" },
        { ByNameRole,  "byName"  },
    };
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

void ItemModel::addItem(const QString &name, const QString &qty, const QString &note) {
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
    item.done    = false;
    item.doneVer = ver;
    item.doneAt  = 0;
    item.del     = false;
    item.delVer  = ver;
    item.touched = ts;

    if (!m_db->upsertItem(item)) return;

    emit localChanged(m_listId);

    // Insert into m_items (full set).
    m_items.push_back(item);

    // Un filtre de recherche est actif et le nouvel article n'y répond pas : il existe,
    // mais n'a rien à faire dans la vue courante.
    if (!matchesFilter(item))
        return;

    // Find sorted insertion position (upper_bound to append after equals).
    Row newRow{ item };
    auto it = std::upper_bound(m_rows.begin(), m_rows.end(), newRow, rowLessThan);
    const int pos = static_cast<int>(it - m_rows.begin());

    beginInsertRows({}, pos, pos);
    m_rows.insert(it, newRow);
    endInsertRows();
    emit countChanged();
    emit doneCountChanged();
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

    it->done    = !it->done;
    it->doneVer = ver;
    it->doneAt  = it->done ? now : 0;  // décoché → plus de date de cochage
    it->touched = now;

    if (!m_db->upsertItem(*it)) return;

    emit localChanged(m_listId);

    // Find current position in visible rows.
    const int oldPos = findRow(itemId);
    if (oldPos < 0) return;

    // Compute new position.
    Row updatedRow{ *it };
    // Remove from old position to compute new position.
    std::vector<Row> scratch = m_rows;
    scratch.erase(scratch.begin() + oldPos);

    auto newIt = std::upper_bound(scratch.begin(), scratch.end(), updatedRow, rowLessThan);
    int newPos = static_cast<int>(newIt - scratch.begin());

    if (oldPos == newPos) {
        // Position unchanged — just update data.
        m_rows[static_cast<size_t>(oldPos)].item = *it;
        const QModelIndex idx = index(oldPos);
        emit dataChanged(idx, idx, { DoneRole, DoneAtRole });
    } else {
        // Move row.
        // Qt's beginMoveRows destination is the row BEFORE which we insert.
        // If moving down: destination = newPos + 1 (in original indexing).
        int dest = (oldPos < newPos) ? newPos + 1 : newPos;
        beginMoveRows({}, oldPos, oldPos, {}, dest);
        m_rows.erase(m_rows.begin() + oldPos);
        // Re-insert at correct position (scratch newPos may shift by 1).
        m_rows.insert(m_rows.begin() + newPos, updatedRow);
        endMoveRows();

        // Update done flag in newly positioned row.
        const QModelIndex idx = index(newPos);
        emit dataChanged(idx, idx, { DoneRole, DoneAtRole });
    }

    // Le nombre de lignes n'a pas bougé, mais la progression du mode Courses si :
    // c'est le seul moment où elle avance.
    emit doneCountChanged();
}

void ItemModel::editItem(const QString &itemId, const QString &name,
                         const QString &qty, const QString &note) {
    if (!m_db) return;

    const std::string id      = itemId.toStdString();
    const std::string newName = name.trimmed().toStdString();
    const std::string newQty  = qty.trimmed().toStdString();
    const std::string newNote = note.trimmed().toStdString();

    if (newName.empty()) return; // un article sans nom n'a rien à afficher

    auto it = std::find_if(m_items.begin(), m_items.end(),
                           [&](const core::Item &i){ return i.itemId == id; });
    if (it == m_items.end()) return;

    const bool nameChanged = (it->name != newName);
    const bool qtyChanged  = (it->qty  != newQty);
    const bool noteChanged = (it->note != newNote);
    if (!nameChanged && !qtyChanged && !noteChanged) return;

    const int64_t lamport = m_db->bumpLamport(m_listId);
    const core::Ver ver{ lamport, m_deviceId };

    if (nameChanged) { it->name = newName; it->nameVer = ver; }
    if (qtyChanged)  { it->qty  = newQty;  it->qtyVer  = ver; }
    if (noteChanged) { it->note = newNote; it->noteVer = ver; }
    it->touched = QDateTime::currentMSecsSinceEpoch();

    if (!m_db->upsertItem(*it)) return;

    emit localChanged(m_listId);

    // Sous filtre, l'édition peut faire sortir la ligne de la recherche (ou l'y faire
    // entrer) : seule une reconstruction donne le bon résultat.
    if (!m_filter.isEmpty()) {
        rebuildRows();
        return;
    }

    // Aucun de ces champs n'entre dans le tri (done, created) : la ligne ne bouge
    // pas, seul son contenu change.
    const int pos = findRow(itemId);
    if (pos >= 0) {
        m_rows[static_cast<size_t>(pos)].item = *it;
        const QModelIndex idx = index(pos);
        emit dataChanged(idx, idx, { NameRole, QtyRole, NoteRole });
    }
}

void ItemModel::removeItems(const QStringList &itemIds) {
    for (const QString &id : itemIds)
        removeItem(id);
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
