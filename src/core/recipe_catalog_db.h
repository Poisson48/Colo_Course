#pragma once

#include "recipe_library.h"

#include <QString>

namespace core {

// Catalogue de recettes en SQLite (lecture seule, fichier séparé de colocourse.db).
class RecipeCatalogDb {
public:
    static bool open(const QString &path);
    static void close();
    static bool isOpen();

    static bool importFromParsed(RecipeLibraryParseResult &&parsed);
    static bool importFromJson(const QByteArray &json);

    static int count();
    static const LibraryRecipe *recipeAt(int index);
    static const LibraryRecipe *recipeById(const QString &id);

    static std::vector<int> filterIndices(const QString &query, const QString &category = {},
                                          int maxResults = 0, int *totalMatchesOut = nullptr);

    static std::vector<RecipeLibrary::CategoryStat> categoryStats();

    // Vérifie qu'un fichier .db est un catalogue utilisable (sans ouvrir la connexion globale).
    static bool validateFile(const QString &path, QString *errorOut = nullptr);

private:
    static bool ensureSchema();
    static bool ensureFtsPopulatedUnlocked();
    static bool rebuildFtsIndex();
    static bool rebuildFtsIndexUnlocked();
    static const LibraryRecipe *fetchCached(const QString &id);
};

} // namespace core
