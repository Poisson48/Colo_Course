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

    m_items = db.getItems(listId);
    rebuildRows();
}

void ItemModel::rebuildRows() {
    beginResetModel();
    m_rows.clear();
    for (const auto &item : m_items) {
        if (!item.del) {
            m_rows.push_back(Row{ item });
        }
    }
    std::stable_sort(m_rows.begin(), m_rows.end(), rowLessThan);
    endResetModel();
    emit countChanged();
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
    case DoneRole:    return row.item.done;
    case CreatedRole: return static_cast<qlonglong>(row.item.created);
    default:          return {};
    }
}

QHash<int, QByteArray> ItemModel::roleNames() const {
    return {
        { ItemIdRole,  "itemId"  },
        { NameRole,    "name"    },
        { QtyRole,     "qty"     },
        { DoneRole,    "done"    },
        { CreatedRole, "created" },
    };
}

int ItemModel::count() const {
    return static_cast<int>(m_rows.size());
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

void ItemModel::addItem(const QString &name, const QString &qty) {
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
    item.done    = false;
    item.doneVer = ver;
    item.del     = false;
    item.delVer  = ver;
    item.touched = ts;

    if (!m_db->upsertItem(item)) return;

    // Insert into m_items (full set).
    m_items.push_back(item);

    // Find sorted insertion position (upper_bound to append after equals).
    Row newRow{ item };
    auto it = std::upper_bound(m_rows.begin(), m_rows.end(), newRow, rowLessThan);
    const int pos = static_cast<int>(it - m_rows.begin());

    beginInsertRows({}, pos, pos);
    m_rows.insert(it, newRow);
    endInsertRows();
    emit countChanged();
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
    it->touched = now;

    if (!m_db->upsertItem(*it)) return;

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
        emit dataChanged(idx, idx, { DoneRole });
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
        emit dataChanged(idx, idx, { DoneRole });
    }
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

    // Remove from visible rows.
    const int pos = findRow(itemId);
    if (pos >= 0) {
        beginRemoveRows({}, pos, pos);
        m_rows.erase(m_rows.begin() + static_cast<size_t>(pos));
        endRemoveRows();
        emit countChanged();
    }
}

} // namespace app
