#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <vector>

namespace core {

struct LibraryIngredient {
    QString name;
    QString qty;
    QString note;
};

struct LibraryRecipe {
    QString id;
    QString title;
    QString category;
    QString servings;       // libellé libre affiché ("4 personnes")
    int     servingsCount = 0; // nombre de personnes, 0 si inconnu (pour la mise à l'échelle)
    QString instructions;      // étapes de préparation (texte multi-lignes)
    QString searchBlob;        // titre + catégorie + ingrédients + préparation, normalisé
    std::vector<LibraryIngredient> ingredients;
};

// Résultat d'un parse hors thread UI (ne touche pas la bibliothèque globale).
struct RecipeLibraryParseResult {
    std::vector<LibraryRecipe> recipes;
    QHash<QString, size_t>     idIndex;
    bool                       ok = false;
};

// Bibliothèque intégrée (JSON embarqué au build). Lecture seule.
class RecipeLibrary {
public:
    static RecipeLibraryParseResult parseJsonData(const QByteArray &json);
    static bool installParsed(RecipeLibraryParseResult &&parsed);
    static bool loadFromJson(const QByteArray &json);

    struct CategoryStat {
        QString name;
        int     count = 0;
    };

    static int count();
    static const LibraryRecipe *recipeAt(int index);
    static const LibraryRecipe *recipeById(const QString &id);

    // Normalise pour la recherche (minuscules, sans accents).
    static QString normalizeSearchText(const QString &s);

    // Filtre titre, ingrédients et préparation (tokens AND, insensible casse/accents).
    // Tri par pertinence : titre > catégorie > ingrédients > préparation.
    // category : filtre exact sur le champ category (vide = toutes).
    static std::vector<int> filterIndices(const QString &query, const QString &category = {});

    // Catégories distinctes du catalogue, triées par effectif décroissant.
    static std::vector<QString> categories();

    // Catégories + effectifs en un seul passage (évite N× filterIndices).
    static std::vector<CategoryStat> categoryStats();

private:
    static std::vector<LibraryRecipe> s_recipes;
};

} // namespace core
