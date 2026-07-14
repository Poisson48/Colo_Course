#pragma once

#include <QObject>
#include <QAbstractListModel>
#include <QString>
#include <QVariant>
#include <vector>
#include "../store/database.h"
#include "../core/types.h"

namespace app {

// ListsModel: exposes the list-of-lists to QML.
// Roles: listId, name, count (unchecked items).
class ListsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        ListIdRole = Qt::UserRole + 1,
        NameRole,
        CountRole,
    };

    explicit ListsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Reload from DB
    void reload(store::Database &db);

    // Add a new list entry
    void prepend(const core::ListMeta &meta, int uncheckedCount);

private:
    struct Row {
        QString listId;
        QString name;
        int     count = 0;
    };
    std::vector<Row> m_rows;
};

// AppController: singleton QObject exposed to QML as a context property.
class AppController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QAbstractListModel* lists READ lists CONSTANT)
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    // Initialize: open DB, generate/load deviceId + displayName.
    bool init();

    QAbstractListModel *lists() const;
    bool online() const;

    QString deviceId() const;
    QString displayName() const;

    store::Database &db() { return m_db; }

public slots:
    void createList(const QString &title);
    // openList is called from QML to open a list; emits listOpened.
    void openList(const QString &listId);

signals:
    void onlineChanged();
    // Emitted when QML should push the item page.
    void listOpened(const QString &listId, const QString &title);

private:
    store::Database m_db;
    ListsModel     *m_listsModel;
    bool            m_online = false;
    QString         m_deviceId;
    QString         m_displayName;
};

} // namespace app
