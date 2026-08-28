#pragma once

#include <QByteArray>
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

// Bibliothèque intégrée (JSON embarqué au build). Lecture seule.
class RecipeLibrary {
public:
    static bool loadFromJson(const QByteArray &json);

    static int count();
    static const LibraryRecipe *recipeAt(int index);
    static const LibraryRecipe *recipeById(const QString &id);

    // Normalise pour la recherche (minuscules, sans accents).
    static QString normalizeSearchText(const QString &s);

    // Filtre titre, ingrédients et préparation (tokens AND, insensible casse/accents).
    // Tri par pertinence : titre > catégorie > ingrédients > préparation.
    static std::vector<int> filterIndices(const QString &query);

private:
    static std::vector<LibraryRecipe> s_recipes;
};

} // namespace core
