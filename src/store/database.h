#pragma once

#include "../core/types.h"

#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <vector>
#include <optional>
#include <string>

namespace store {

// Persistent storage for Colo_Course. One SQLite file, WAL mode.
// All merge operations + lamport/last_sync updates are transactional (§6).
class Database
{
public:
    Database() = default;
    ~Database();

    // Open (or create) the database at the given file path.
    // Returns true on success.
    bool open(const QString& path);

    void close();

    // --- Lists ---
    bool createList(const core::ListMeta& meta);
    std::vector<core::ListMeta> getLists();
    std::optional<core::ListMeta> getList(const std::string& listId);
    // Persist an LWW-merged title (caller does the mergeTitle comparison).
    bool updateListTitle(const std::string& listId,
                         const std::string& title,
                         const core::Ver& ver);
    // Advance last_sync (ms epoch); only moves forward.
    bool updateLastSync(const std::string& listId, int64_t ms);
    // Quitter une liste : efface toute trace locale (liste, items, membres, outbox).
    // Purement local — aucune opération CRDT n'est émise, les autres participants
    // gardent la liste (§2.2 : pas de suppression de liste répliquée).
    bool deleteList(const std::string& listId);

    // --- Items ---
    // Insert or update an item. Transactional (updates lamport if needed).
    bool upsertItem(const core::Item& item);
    std::vector<core::Item> getItems(const std::string& listId);

    // --- Members ---
    // member: (listId, deviceId, name, ver)
    bool upsertMember(const std::string& listId,
                      const std::string& deviceId,
                      const std::string& name,
                      const core::Ver&   ver);
    // Returns pairs of (deviceId, name).
    std::vector<std::pair<std::string, std::string>> getMembers(const std::string& listId);

    // --- Outbox (FIFO queue of encrypted events) ---
    bool        outboxPush(const std::string& listId, const std::string& eventJson);
    // Remove one specific entry (targeted ack) — acks may arrive out of order,
    // so FIFO pop must never be used to acknowledge.
    bool        outboxRemove(int64_t rowid);
    // Returns the oldest entry's (rowid, eventJson); removes it from the queue.
    std::optional<std::pair<int64_t, std::string>> outboxPop(const std::string& listId);
    // Peek all entries for a list in FIFO order (rowid, eventJson).
    std::vector<std::pair<int64_t, std::string>> outboxPeekAll(const std::string& listId);

    // --- Seen-events dedup ---
    bool markEventSeen(const std::string& eventId);
    bool isEventSeen(const std::string& eventId);
    // Remove seen_events entries older than cutoffMs (ms epoch).
    bool purgeSeenBefore(int64_t cutoffMs);

    // --- Settings (key/value) ---
    std::optional<std::string> getSetting(const std::string& key);
    bool setSetting(const std::string& key, const std::string& value);

    // --- Lamport clock ---
    // Transactionally: new_clock = max(current_clock + 1, atLeast).
    // Returns the new value, or -1 on error.
    int64_t bumpLamport(const std::string& listId, int64_t atLeast = 0);

private:
    bool createSchema();

    QSqlDatabase m_db;
    QString      m_connectionName;
};

} // namespace store
