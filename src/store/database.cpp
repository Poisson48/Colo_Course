#include "database.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>
#include <QUuid>

namespace store {

namespace {
// Helpers to convert between std::string and QString.
inline QString qs(const std::string& s) { return QString::fromStdString(s); }
inline std::string ss(const QString& s) { return s.toStdString(); }

// Cast int64_t to qlonglong to resolve QVariant overload ambiguity.
inline qlonglong ll(int64_t v) { return static_cast<qlonglong>(v); }

core::Ver verFromCols(int64_t l, const QString& d)
{
    return {l, d.toStdString()};
}
} // anonymous namespace

Database::~Database()
{
    close();
}

bool Database::open(const QString& path)
{
    // Each instance gets a unique connection name so tests can open multiple DBs.
    m_connectionName = QStringLiteral("colocourse_") +
                       QUuid::createUuid().toString(QUuid::WithoutBraces);

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(path);

    if (!m_db.open()) {
        qWarning() << "Database::open failed:" << m_db.lastError().text();
        return false;
    }

    // Enable WAL mode for better concurrency (§6).
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA foreign_keys=ON"));

    return createSchema();
}

void Database::close()
{
    if (m_db.isOpen())
        m_db.close();
    if (!m_connectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
    }
}

bool Database::createSchema()
{
    QSqlQuery q(m_db);

    // §6 exact schema.
    const QStringList ddl = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS lists ("
            "  list_id TEXT PRIMARY KEY,"
            "  key BLOB,"
            "  title TEXT,"
            "  title_ver_l INT,"
            "  title_ver_d TEXT,"
            "  lamport INT,"
            "  last_sync INT,"
            "  created INT"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS items ("
            "  list_id TEXT,"
            "  item_id TEXT,"
            "  created INT,"
            "  by TEXT,"
            "  name TEXT,  name_l INT,  name_d TEXT,"
            "  qty  TEXT,  qty_l  INT,  qty_d  TEXT,"
            "  done INT,   done_l INT,  done_d TEXT,"
            "  del  INT,   del_l  INT,  del_d  TEXT,"
            "  touched INT,"
            "  PRIMARY KEY(list_id, item_id)"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS members ("
            "  list_id TEXT,"
            "  device_id TEXT,"
            "  name TEXT,"
            "  ver_l INT,"
            "  ver_d TEXT,"
            "  PRIMARY KEY(list_id, device_id)"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS outbox ("
            "  rowid INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  list_id TEXT,"
            "  event_json TEXT,"
            "  created INT"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS seen_events ("
            "  event_id TEXT PRIMARY KEY,"
            "  seen INT"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS settings ("
            "  key TEXT PRIMARY KEY,"
            "  value TEXT"
            ")"),
    };

    for (const QString& stmt : ddl) {
        if (!q.exec(stmt)) {
            qWarning() << "createSchema error:" << q.lastError().text() << stmt;
            return false;
        }
    }
    return true;
}

// --- Lists ---

bool Database::createList(const core::ListMeta& meta)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO lists"
        " (list_id, key, title, title_ver_l, title_ver_d, lamport, last_sync, created)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(qs(meta.listId));
    q.addBindValue(QByteArray(reinterpret_cast<const char*>(meta.key.data()),
                              static_cast<int>(meta.key.size())));
    q.addBindValue(qs(meta.title));
    q.addBindValue(ll(meta.titleVer.lamport));
    q.addBindValue(qs(meta.titleVer.deviceId));
    q.addBindValue(ll(meta.lamport));
    q.addBindValue(ll(meta.lastSync));
    q.addBindValue(ll(meta.created));

    if (!q.exec()) {
        qWarning() << "createList error:" << q.lastError().text();
        return false;
    }
    return true;
}

bool Database::updateListTitle(const std::string& listId,
                               const std::string& title,
                               const core::Ver& ver)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE lists SET title = ?, title_ver_l = ?, title_ver_d = ?"
        " WHERE list_id = ?"));
    q.addBindValue(qs(title));
    q.addBindValue(ll(ver.lamport));
    q.addBindValue(qs(ver.deviceId));
    q.addBindValue(qs(listId));

    if (!q.exec()) {
        qWarning() << "updateListTitle error:" << q.lastError().text();
        return false;
    }
    return true;
}

std::vector<core::ListMeta> Database::getLists()
{
    std::vector<core::ListMeta> result;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral(
        "SELECT list_id, key, title, title_ver_l, title_ver_d, lamport, last_sync, created"
        " FROM lists ORDER BY created ASC"));
    while (q.next()) {
        core::ListMeta m;
        m.listId        = ss(q.value(0).toString());
        QByteArray blob = q.value(1).toByteArray();
        m.key.assign(reinterpret_cast<const uint8_t*>(blob.constData()),
                     reinterpret_cast<const uint8_t*>(blob.constData()) + blob.size());
        m.title         = ss(q.value(2).toString());
        m.titleVer      = verFromCols(q.value(3).toLongLong(), q.value(4).toString());
        m.lamport       = q.value(5).toLongLong();
        m.lastSync      = q.value(6).toLongLong();
        m.created       = q.value(7).toLongLong();
        result.push_back(std::move(m));
    }
    return result;
}

std::optional<core::ListMeta> Database::getList(const std::string& listId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT list_id, key, title, title_ver_l, title_ver_d, lamport, last_sync, created"
        " FROM lists WHERE list_id = ?"));
    q.addBindValue(qs(listId));
    if (!q.exec() || !q.next())
        return std::nullopt;

    core::ListMeta m;
    m.listId        = ss(q.value(0).toString());
    QByteArray blob = q.value(1).toByteArray();
    m.key.assign(reinterpret_cast<const uint8_t*>(blob.constData()),
                 reinterpret_cast<const uint8_t*>(blob.constData()) + blob.size());
    m.title         = ss(q.value(2).toString());
    m.titleVer      = verFromCols(q.value(3).toLongLong(), q.value(4).toString());
    m.lamport       = q.value(5).toLongLong();
    m.lastSync      = q.value(6).toLongLong();
    m.created       = q.value(7).toLongLong();
    return m;
}

// --- Items ---

bool Database::upsertItem(const core::Item& item)
{
    if (!m_db.transaction()) {
        qWarning() << "upsertItem: cannot start transaction";
        return false;
    }

    QSqlQuery q(m_db);
    const int64_t nowMs = QDateTime::currentMSecsSinceEpoch();
    q.prepare(QStringLiteral(
        "INSERT INTO items"
        " (list_id, item_id, created, by,"
        "  name, name_l, name_d,"
        "  qty,  qty_l,  qty_d,"
        "  done, done_l, done_d,"
        "  del,  del_l,  del_d,"
        "  touched)"
        " VALUES (?,?,?,?, ?,?,?, ?,?,?, ?,?,?, ?,?,?, ?)"
        " ON CONFLICT(list_id, item_id) DO UPDATE SET"
        "  name   = excluded.name,   name_l = excluded.name_l,   name_d = excluded.name_d,"
        "  qty    = excluded.qty,    qty_l  = excluded.qty_l,    qty_d  = excluded.qty_d,"
        "  done   = excluded.done,   done_l = excluded.done_l,   done_d = excluded.done_d,"
        "  del    = excluded.del,    del_l  = excluded.del_l,    del_d  = excluded.del_d,"
        "  touched = excluded.touched"));
    q.addBindValue(qs(item.listId));
    q.addBindValue(qs(item.itemId));
    q.addBindValue(ll(item.created));
    q.addBindValue(qs(item.by));
    q.addBindValue(qs(item.name));
    q.addBindValue(ll(item.nameVer.lamport));
    q.addBindValue(qs(item.nameVer.deviceId));
    q.addBindValue(qs(item.qty));
    q.addBindValue(ll(item.qtyVer.lamport));
    q.addBindValue(qs(item.qtyVer.deviceId));
    q.addBindValue(item.done ? 1 : 0);
    q.addBindValue(ll(item.doneVer.lamport));
    q.addBindValue(qs(item.doneVer.deviceId));
    q.addBindValue(item.del ? 1 : 0);
    q.addBindValue(ll(item.delVer.lamport));
    q.addBindValue(qs(item.delVer.deviceId));
    q.addBindValue(ll(nowMs));

    if (!q.exec()) {
        qWarning() << "upsertItem error:" << q.lastError().text();
        m_db.rollback();
        return false;
    }

    if (!m_db.commit()) {
        qWarning() << "upsertItem commit error:" << m_db.lastError().text();
        m_db.rollback();
        return false;
    }
    return true;
}

std::vector<core::Item> Database::getItems(const std::string& listId)
{
    std::vector<core::Item> result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT item_id, created, by,"
        "  name, name_l, name_d,"
        "  qty,  qty_l,  qty_d,"
        "  done, done_l, done_d,"
        "  del,  del_l,  del_d,"
        "  touched"
        " FROM items WHERE list_id = ? ORDER BY created ASC"));
    q.addBindValue(qs(listId));
    if (!q.exec()) {
        qWarning() << "getItems error:" << q.lastError().text();
        return result;
    }
    while (q.next()) {
        core::Item it;
        it.listId      = listId;
        it.itemId      = ss(q.value(0).toString());
        it.created     = q.value(1).toLongLong();
        it.by          = ss(q.value(2).toString());
        it.name        = ss(q.value(3).toString());
        it.nameVer     = verFromCols(q.value(4).toLongLong(), q.value(5).toString());
        it.qty         = ss(q.value(6).toString());
        it.qtyVer      = verFromCols(q.value(7).toLongLong(), q.value(8).toString());
        it.done        = q.value(9).toInt() != 0;
        it.doneVer     = verFromCols(q.value(10).toLongLong(), q.value(11).toString());
        it.del         = q.value(12).toInt() != 0;
        it.delVer      = verFromCols(q.value(13).toLongLong(), q.value(14).toString());
        it.touched     = q.value(15).toLongLong();
        result.push_back(std::move(it));
    }
    return result;
}

// --- Members ---

bool Database::upsertMember(const std::string& listId,
                             const std::string& deviceId,
                             const std::string& name,
                             const core::Ver&   ver)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO members (list_id, device_id, name, ver_l, ver_d)"
        " VALUES (?, ?, ?, ?, ?)"
        " ON CONFLICT(list_id, device_id) DO UPDATE SET"
        "  name = excluded.name, ver_l = excluded.ver_l, ver_d = excluded.ver_d"));
    q.addBindValue(qs(listId));
    q.addBindValue(qs(deviceId));
    q.addBindValue(qs(name));
    q.addBindValue(ll(ver.lamport));
    q.addBindValue(qs(ver.deviceId));
    if (!q.exec()) {
        qWarning() << "upsertMember error:" << q.lastError().text();
        return false;
    }
    return true;
}

std::vector<std::pair<std::string, std::string>> Database::getMembers(const std::string& listId)
{
    std::vector<std::pair<std::string, std::string>> result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT device_id, name FROM members WHERE list_id = ?"));
    q.addBindValue(qs(listId));
    if (!q.exec()) return result;
    while (q.next())
        result.emplace_back(ss(q.value(0).toString()), ss(q.value(1).toString()));
    return result;
}

// --- Outbox ---

bool Database::outboxRemove(int64_t rowid)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM outbox WHERE rowid = ?"));
    q.addBindValue(ll(rowid));
    if (!q.exec()) {
        qWarning() << "outboxRemove error:" << q.lastError().text();
        return false;
    }
    return true;
}

bool Database::updateLastSync(const std::string& listId, int64_t ms)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE lists SET last_sync = ? WHERE list_id = ? AND last_sync < ?"));
    q.addBindValue(ll(ms));
    q.addBindValue(qs(listId));
    q.addBindValue(ll(ms));
    if (!q.exec()) {
        qWarning() << "updateLastSync error:" << q.lastError().text();
        return false;
    }
    return true;
}

bool Database::outboxPush(const std::string& listId, const std::string& eventJson)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO outbox (list_id, event_json, created) VALUES (?, ?, ?)"));
    q.addBindValue(qs(listId));
    q.addBindValue(qs(eventJson));
    q.addBindValue(ll(QDateTime::currentMSecsSinceEpoch()));
    if (!q.exec()) {
        qWarning() << "outboxPush error:" << q.lastError().text();
        return false;
    }
    return true;
}

std::optional<std::pair<int64_t, std::string>> Database::outboxPop(const std::string& listId)
{
    // Find oldest entry.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT rowid, event_json FROM outbox WHERE list_id = ? ORDER BY rowid ASC LIMIT 1"));
    q.addBindValue(qs(listId));
    if (!q.exec() || !q.next())
        return std::nullopt;

    int64_t rowid       = q.value(0).toLongLong();
    std::string payload = ss(q.value(1).toString());

    QSqlQuery del(m_db);
    del.prepare(QStringLiteral("DELETE FROM outbox WHERE rowid = ?"));
    del.addBindValue(ll(rowid));
    del.exec();

    return std::make_pair(rowid, payload);
}

std::vector<std::pair<int64_t, std::string>> Database::outboxPeekAll(const std::string& listId)
{
    std::vector<std::pair<int64_t, std::string>> result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT rowid, event_json FROM outbox WHERE list_id = ? ORDER BY rowid ASC"));
    q.addBindValue(qs(listId));
    if (!q.exec()) return result;
    while (q.next())
        result.emplace_back(q.value(0).toLongLong(), ss(q.value(1).toString()));
    return result;
}

// --- Seen events ---

bool Database::markEventSeen(const std::string& eventId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO seen_events (event_id, seen) VALUES (?, ?)"));
    q.addBindValue(qs(eventId));
    q.addBindValue(ll(QDateTime::currentMSecsSinceEpoch()));
    if (!q.exec()) {
        qWarning() << "markEventSeen error:" << q.lastError().text();
        return false;
    }
    return true;
}

bool Database::isEventSeen(const std::string& eventId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT 1 FROM seen_events WHERE event_id = ?"));
    q.addBindValue(qs(eventId));
    return q.exec() && q.next();
}

bool Database::purgeSeenBefore(int64_t cutoffMs)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM seen_events WHERE seen < ?"));
    q.addBindValue(ll(cutoffMs));
    if (!q.exec()) {
        qWarning() << "purgeSeenBefore error:" << q.lastError().text();
        return false;
    }
    return true;
}

// --- Settings ---

std::optional<std::string> Database::getSetting(const std::string& key)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE key = ?"));
    q.addBindValue(qs(key));
    if (!q.exec() || !q.next())
        return std::nullopt;
    return ss(q.value(0).toString());
}

bool Database::setSetting(const std::string& key, const std::string& value)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO settings (key, value) VALUES (?, ?)"
        " ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    q.addBindValue(qs(key));
    q.addBindValue(qs(value));
    if (!q.exec()) {
        qWarning() << "setSetting error:" << q.lastError().text();
        return false;
    }
    return true;
}

// --- Lamport clock ---

int64_t Database::bumpLamport(const std::string& listId, int64_t atLeast)
{
    if (!m_db.transaction()) {
        qWarning() << "bumpLamport: cannot start transaction";
        return -1;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT lamport FROM lists WHERE list_id = ?"));
    q.addBindValue(qs(listId));
    if (!q.exec() || !q.next()) {
        m_db.rollback();
        return -1;
    }

    int64_t current = q.value(0).toLongLong();
    int64_t next    = std::max(current + 1, atLeast);

    QSqlQuery u(m_db);
    u.prepare(QStringLiteral("UPDATE lists SET lamport = ? WHERE list_id = ?"));
    u.addBindValue(ll(next));
    u.addBindValue(qs(listId));
    if (!u.exec()) {
        qWarning() << "bumpLamport update error:" << u.lastError().text();
        m_db.rollback();
        return -1;
    }

    if (!m_db.commit()) {
        m_db.rollback();
        return -1;
    }
    return next;
}

} // namespace store
