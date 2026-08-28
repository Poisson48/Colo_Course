#include "recipelibrarymodel.h"

#include "../core/recipe_library.h"

namespace app {

RecipeLibraryModel::RecipeLibraryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void RecipeLibraryModel::reloadFromLibrary() {
    rebuild();
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
    rebuild();
    emit filterChanged();
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

void RecipeLibraryModel::rebuild() {
    beginResetModel();
    m_indices = core::RecipeLibrary::filterIndices(m_filter);
    endResetModel();
    emit countChanged();
}

} // namespace app
