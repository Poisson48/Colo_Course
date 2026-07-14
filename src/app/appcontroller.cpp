#include "appcontroller.h"

#include <QStandardPaths>
#include <QDir>
#include <QSettings>
#include <QUuid>
#include <QDateTime>

#include "../core/types.h"
#include "../core/crdt.h"

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
    default:         return {};
    }
}

QHash<int, QByteArray> ListsModel::roleNames() const {
    return {
        { ListIdRole, "listId" },
        { NameRole,   "name"   },
        { CountRole,  "count"  },
    };
}

void ListsModel::reload(store::Database &db) {
    beginResetModel();
    m_rows.clear();
    const auto lists = db.getLists();
    for (const auto &meta : lists) {
        int unchecked = 0;
        for (const auto &item : db.getItems(meta.listId)) {
            if (!item.del && !item.done) ++unchecked;
        }
        m_rows.push_back({
            QString::fromStdString(meta.listId),
            QString::fromStdString(meta.title),
            unchecked
        });
    }
    endResetModel();
}

void ListsModel::prepend(const core::ListMeta &meta, int uncheckedCount) {
    beginInsertRows({}, 0, 0);
    m_rows.insert(m_rows.begin(), {
        QString::fromStdString(meta.listId),
        QString::fromStdString(meta.title),
        uncheckedCount
    });
    endInsertRows();
}

// ---------------------------------------------------------------------------
// AppController
// ---------------------------------------------------------------------------

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_listsModel(new ListsModel(this))
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

    return true;
}

QAbstractListModel *AppController::lists() const {
    return m_listsModel;
}

bool AppController::online() const {
    return m_online;
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
    meta.title  = title.toStdString();
    meta.titleVer = core::Ver{ 1, m_deviceId.toStdString() };
    meta.lamport  = 1;
    meta.created  = QDateTime::currentMSecsSinceEpoch();

    if (m_db.createList(meta)) {
        m_listsModel->prepend(meta, 0);
    }
}

void AppController::openList(const QString &listId) {
    auto metaOpt = m_db.getList(listId.toStdString());
    if (!metaOpt) return;
    emit listOpened(listId, QString::fromStdString(metaOpt->title));
}

} // namespace app
