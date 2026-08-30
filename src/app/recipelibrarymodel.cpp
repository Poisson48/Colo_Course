#include "recipelibrarymodel.h"

#include "../core/recipe_library.h"

#include <QtConcurrent>

namespace app {

RecipeLibraryModel::RecipeLibraryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

RecipeLibraryModel::~RecipeLibraryModel()
{
    if (m_rebuildWatcher) {
        m_rebuildWatcher->cancel();
        m_rebuildWatcher->waitForFinished();
    }
}

void RecipeLibraryModel::reloadFromLibrary() {
    m_stale = true;
    if (m_catalogActive)
        rebuildAsync();
}

void RecipeLibraryModel::activateCatalog() {
    if (m_catalogActive) {
        if (m_stale)
            rebuildAsync();
        return;
    }
    m_catalogActive = true;
    rebuildAsync();
}

void RecipeLibraryModel::deactivateCatalog() {
    m_catalogActive = false;
    m_stale = false;
    if (m_rebuildWatcher) {
        m_rebuildWatcher->cancel();
        m_rebuildWatcher->waitForFinished();
        m_rebuildWatcher->deleteLater();
        m_rebuildWatcher = nullptr;
    }
    if (!m_indices.empty() || m_loading) {
        beginResetModel();
        m_indices.clear();
        endResetModel();
        emit countChanged();
        setLoading(false);
    }
    if (m_truncated) {
        m_truncated = false;
        emit truncatedChanged();
    }
    if (m_totalMatches != 0) {
        m_totalMatches = 0;
        emit totalMatchesChanged();
    }
}

int RecipeLibraryModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_indices.size());
}

QVariant RecipeLibraryModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(m_indices.size()))
        return {};

    const core::LibraryRecipe *rec =
        core::RecipeLibrary::recipeAt(m_indices[static_cast<size_t>(index.row())]);
    if (!rec)
        return {};

    switch (role) {
    case LibraryIdRole:
        return rec->id;
    case TitleRole:
        return rec->title;
    case CategoryRole:
        return rec->category;
    case ServingsRole:
        return rec->servings;
    case BaseServingsRole:
        return rec->servingsCount;
    case IngredientCountRole:
        return static_cast<int>(rec->ingredients.size());
    default:
        return {};
    }
}

QHash<int, QByteArray> RecipeLibraryModel::roleNames() const {
    return {
        { LibraryIdRole, "libraryId" },
        { TitleRole, "title" },
        { CategoryRole, "category" },
        { ServingsRole, "servings" },
        { BaseServingsRole, "baseServings" },
        { IngredientCountRole, "ingredientCount" },
    };
}

int RecipeLibraryModel::count() const {
    return static_cast<int>(m_indices.size());
}

void RecipeLibraryModel::setFilter(const QString &filter) {
    if (m_filter == filter)
        return;
    m_filter = filter;
    emit filterChanged();
    if (m_catalogActive)
        rebuildAsync();
}

void RecipeLibraryModel::setCategoryFilter(const QString &category) {
    if (m_categoryFilter == category)
        return;
    m_categoryFilter = category;
    emit categoryFilterChanged();
    if (m_catalogActive)
        rebuildAsync();
}

QString RecipeLibraryModel::libraryIdAt(int row) const {
    if (row < 0 || row >= static_cast<int>(m_indices.size()))
        return {};
    const core::LibraryRecipe *rec =
        core::RecipeLibrary::recipeAt(m_indices[static_cast<size_t>(row)]);
    return rec ? rec->id : QString();
}

int RecipeLibraryModel::ingredientCount(const QString &libraryId) const {
    const core::LibraryRecipe *rec = core::RecipeLibrary::recipeById(libraryId);
    return rec ? static_cast<int>(rec->ingredients.size()) : 0;
}

void RecipeLibraryModel::setLoading(bool loading) {
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void RecipeLibraryModel::rebuildAsync() {
    if (!m_catalogActive)
        return;

    if (m_rebuildWatcher && m_rebuildWatcher->isRunning())
        return;

    setLoading(true);
    const QString filter = m_filter;
    const QString category = m_categoryFilter;

    if (!m_rebuildWatcher) {
        m_rebuildWatcher = new QFutureWatcher<std::pair<std::vector<int>, int>>(this);
        connect(m_rebuildWatcher,
                &QFutureWatcher<std::pair<std::vector<int>, int>>::finished, this,
                [this]() {
                    if (!m_catalogActive) {
                        setLoading(false);
                        return;
                    }
                    const auto result = m_rebuildWatcher->result();
                    const std::vector<int> &indices = result.first;
                    const int total = result.second;
                    beginResetModel();
                    m_indices = indices;
                    endResetModel();
                    emit countChanged();
                    const bool trunc = total > kMaxVisibleRows;
                    if (m_truncated != trunc) {
                        m_truncated = trunc;
                        emit truncatedChanged();
                    }
                    if (m_totalMatches != total) {
                        m_totalMatches = total;
                        emit totalMatchesChanged();
                    }
                    m_stale = false;
                    setLoading(false);
                });
    }

    m_rebuildWatcher->setFuture(QtConcurrent::run([filter, category]() {
        int total = 0;
        std::vector<int> indices = core::RecipeLibrary::filterIndices(
            filter, category, kMaxVisibleRows, &total);
        return std::make_pair(std::move(indices), total);
    }));
}

} // namespace app
