#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QString>
#include <vector>

namespace app {

// Modèle QML pour parcourir la bibliothèque intégrée (pas les recettes utilisateur).
class RecipeLibraryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString categoryFilter READ categoryFilter WRITE setCategoryFilter
               NOTIFY categoryFilterChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    enum Roles {
        LibraryIdRole = Qt::UserRole + 1,
        TitleRole,
        CategoryRole,
        ServingsRole,
        BaseServingsRole,
        IngredientCountRole,
    };

    explicit RecipeLibraryModel(QObject *parent = nullptr);
    ~RecipeLibraryModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool loading() const { return m_loading; }
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    QString categoryFilter() const { return m_categoryFilter; }
    void setCategoryFilter(const QString &category);

    Q_INVOKABLE QString libraryIdAt(int row) const;
    Q_INVOKABLE int ingredientCount(const QString &libraryId) const;

    // Marque le catalogue comme modifié ; rebuild seulement si l'onglet est ouvert.
    void reloadFromLibrary();
    void activateCatalog();
    void deactivateCatalog();

signals:
    void countChanged();
    void filterChanged();
    void categoryFilterChanged();
    void loadingChanged();

private:
    void rebuildAsync();
    void setLoading(bool loading);

    QString m_filter;
    QString m_categoryFilter;
    std::vector<int> m_indices;
    bool m_catalogActive = false;
    bool m_stale = false;
    bool m_loading = false;
    QFutureWatcher<std::vector<int>> *m_rebuildWatcher = nullptr;
};

} // namespace app
