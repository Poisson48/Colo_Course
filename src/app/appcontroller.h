#pragma once

#include <QObject>
#include <QAbstractListModel>
#include <QString>
#include <QVariant>
#include <vector>
#include "../store/database.h"
#include "../core/types.h"
#include "../core/pairing.h"
#include "../net/relaypool.h"
#include "itemmodel.h"
#include "syncengine.h"

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
        TotalRole,
    };

    explicit ListsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Reload from DB
    void reload(store::Database &db);

    // Add a new list entry
    void prepend(const core::ListMeta &meta, int uncheckedCount);

    // Remove a row by listId (no-op if absent).
    void remove(const QString &listId);

private:
    struct Row {
        QString listId;
        QString name;
        int     count = 0;  // articles restant à acheter
        int     total = 0;  // articles visibles (tombstones exclus)
    };
    std::vector<Row> m_rows;
};

// AppController: singleton QObject exposed to QML as a context property.
class AppController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QAbstractListModel* lists READ lists CONSTANT)
    // Articles de la liste ouverte. Chargé par openList(), branché au SyncEngine.
    Q_PROPERTY(app::ItemModel* items READ items CONSTANT)
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    // Nom affiché aux autres participants ("3 articles ajoutés par Marie").
    Q_PROPERTY(QString displayName READ displayName WRITE setDisplayName NOTIFY displayNameChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    // Initialize: open DB, generate/load deviceId + displayName.
    bool init();

    QAbstractListModel *lists() const;
    ItemModel *items();
    bool online() const;

    QString deviceId() const;
    QString displayName() const;

    store::Database &db() { return m_db; }

public slots:
    void createList(const QString &title);
    // openList is called from QML to open a list; emits listOpened.
    void openList(const QString &listId);
    // Parse URI → create list with provided key → true on success
    bool joinList(const QString &uri);
    // Build pairing URI for an existing list
    QString joinUri(const QString &listId);
    // Quitter une liste : effacement local uniquement (les autres la gardent).
    void leaveList(const QString &listId);

    // Deep link colocourse://join/... (lien tapé dans WhatsApp, ou QR scanné).
    void handleJoinUrl(const QUrl &url);

    void setDisplayName(const QString &name);

    // Presse-papiers, et partage natif (feuille de partage Android ; ailleurs :
    // copie dans le presse-papiers). Retourne false si le partage a échoué.
    void copyToClipboard(const QString &text);
    bool shareText(const QString &text);

    // Access SyncEngine (for ItemModel integration).
    SyncEngine *syncEngine() { return &m_syncEngine; }

signals:
    void onlineChanged();
    void displayNameChanged();
    // Emitted when QML should push the item page.
    void listOpened(const QString &listId, const QString &title);
    // Message court à afficher en bas de l'écran (snackbar).
    void toast(const QString &message);

private slots:
    void onSyncOnlineChanged(bool online);
    void onRemoteChanges(const QString& listId, int count, const QString& authorName);
    // Écriture locale dans la liste ouverte → publier + rafraîchir les compteurs.
    void onLocalItemChange(const std::string& listId);

private:
    store::Database  m_db;
    ListsModel      *m_listsModel;
    ItemModel        m_itemModel;
    net::RelayPool   m_relayPool;
    SyncEngine       m_syncEngine;
    bool             m_online = false;
    QString          m_deviceId;
    QString          m_displayName;
    std::string      m_openListId;   // liste actuellement chargée dans m_itemModel
};

} // namespace app
