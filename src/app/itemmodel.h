#pragma once

#include <QAbstractListModel>
#include <QString>
#include <vector>
#include "../store/database.h"
#include "../core/types.h"

namespace app {

// ItemModel: QAbstractListModel for the items of a single list.
// Sorting (§7 SPEC): unchecked first, then by created ascending.
// Deleted items (del=true) are hidden (tombstone filter).
class ItemModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Articles déjà dans le panier : c'est la progression qu'affiche le mode Courses.
    Q_PROPERTY(int doneCount READ doneCount NOTIFY doneCountChanged)

public:
    enum Roles {
        ItemIdRole = Qt::UserRole + 1,
        NameRole,
        QtyRole,
        NoteRole,
        DoneRole,
        DoneAtRole,
        CreatedRole,
    };

    explicit ItemModel(QObject *parent = nullptr);

    // Load items from DB for a given list.
    void load(store::Database &db, const std::string &listId, const std::string &deviceId);

    // ---- QAbstractListModel ----
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int doneCount() const;

public slots:
    void addItem(const QString &name, const QString &qty, const QString &note = {});
    void toggleDone(const QString &itemId);
    void removeItem(const QString &itemId);
    // Suppression groupée (mode sélection) : un seul lot publié, pas N événements.
    void removeItems(const QStringList &itemIds);
    // Édition LWW du nom, de la quantité et de la description. Les champs inchangés
    // gardent leur version : réécrire une valeur identique ferait gagner ce champ
    // contre une modification distante concurrente qu'on n'a pas encore reçue.
    void editItem(const QString &itemId, const QString &name,
                  const QString &qty, const QString &note);

signals:
    void countChanged();
    void doneCountChanged();
    // Emitted after any local write (addItem, toggleDone, removeItem, editItem).
    void localChanged(const std::string& listId);

private:
    // Sorted visible rows (del=false only, sorted as §7).
    struct Row {
        core::Item item;
    };

    // Find visible row index by itemId (-1 if not found).
    int findRow(const QString &itemId) const;

    // Reorder: perform sorted insert of a row that changed position.
    // Compares (done, created) pairs.
    static bool rowLessThan(const Row &a, const Row &b);

    // Full reload from m_items into m_rows (sorted).
    void rebuildRows();

    store::Database *m_db     = nullptr;
    std::string      m_listId;
    std::string      m_deviceId;

    // Full set including tombstones (authoritative).
    std::vector<core::Item> m_items;

    // Visible, sorted view (no del=true).
    std::vector<Row> m_rows;
};

} // namespace app
