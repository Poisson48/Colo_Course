// tst_recipe_library.cpp — core::RecipeLibrary::loadFromJson et consorts.
// Comme tst_dataexchange : exécutable simple (pas de QTest), mais QString est
// nécessaire ici (core::RecipeLibrary l'utilise pour rester compatible avec le
// reste de l'app — voir src/CMakeLists.txt, `core` lie Qt6::Core).

#include "core/recipe_library.h"

#include <QString>

#include <cstdio>

static int g_total = 0, g_passed = 0, g_failed = 0;

#define EXPECT_TRUE(expr)                                                      \
    do { ++g_total;                                                           \
        if (!(expr)) { ++g_failed;                                            \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } \
        else ++g_passed; } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

using core::RecipeLibrary;

static const char *kValidJson = R"JSON({
  "recipes": [
    {
      "id": "r1",
      "title": "Omelette nature",
      "category": "Plat principal",
      "servings": "2 personnes",
      "servings_count": 2,
      "instructions": "1. Battre les oeufs. 2. Cuire dans la poele.",
      "ingredients": [
        {"name": "Oeufs", "qty": "3", "note": ""},
        {"name": "Beurre", "qty": "10 g", "note": ""},
        {"name": "Sel", "qty": "", "note": ""}
      ]
    },
    {
      "id": "r2",
      "title": "Quenelles de pommes de terre",
      "category": "Entrée",
      "servings": "4 personnes",
      "servings_count": 4,
      "ingredients": [
        {"name": "Pommes de terre", "qty": "700 g", "note": ""},
        {"name": "Oeufs", "qty": "2", "note": ""},
        {"name": "Crème double", "qty": "2 dl", "note": ""}
      ]
    },
    {
      "id": "r3",
      "title": "Sel seul (invalide, un seul ingrédient)",
      "ingredients": [
        {"name": "Sel", "qty": "", "note": ""}
      ]
    },
    {
      "id": "",
      "title": "Sans id (invalide)",
      "ingredients": [
        {"name": "A", "qty": "", "note": ""},
        {"name": "B", "qty": "", "note": ""}
      ]
    },
    {
      "id": "r4",
      "title": "  Doublon d'ingrédients  ",
      "servings_count": 3,
      "ingredients": [
        {"name": "Farine", "qty": "200 g", "note": ""},
        {"name": " farine ", "qty": "100 g", "note": "insensible à la casse/accents"},
        {"name": "Lait", "qty": "1 L", "note": ""}
      ]
    }
  ]
})JSON";

static void test_loadValidJson() {
    EXPECT_TRUE(RecipeLibrary::loadFromJson(QByteArray(kValidJson)));
    // r3 (1 seul ingrédient) et l'entrée sans id sont rejetées → 3 recettes valides.
    EXPECT_EQ(RecipeLibrary::count(), 3);
}

static void test_recipeByIdAndAt() {
    RecipeLibrary::loadFromJson(QByteArray(kValidJson));

    const core::LibraryRecipe *r1 = RecipeLibrary::recipeById(QStringLiteral("r1"));
    EXPECT_TRUE(r1 != nullptr);
    if (r1) {
        EXPECT_EQ(r1->title, QStringLiteral("Omelette nature"));
        EXPECT_EQ(static_cast<int>(r1->ingredients.size()), 3);
        EXPECT_EQ(r1->servingsCount, 2);
    }

    EXPECT_TRUE(RecipeLibrary::recipeById(QStringLiteral("inconnu")) == nullptr);
    EXPECT_TRUE(RecipeLibrary::recipeAt(-1) == nullptr);
    EXPECT_TRUE(RecipeLibrary::recipeAt(RecipeLibrary::count()) == nullptr);
}

static void test_dedupIngredients() {
    RecipeLibrary::loadFromJson(QByteArray(kValidJson));
    const core::LibraryRecipe *r4 = RecipeLibrary::recipeById(QStringLiteral("r4"));
    EXPECT_TRUE(r4 != nullptr);
    if (r4) {
        // "Farine" et " farine " (accents/casse/espaces ignorés) sont le même ingrédient :
        // seule la première occurrence est gardée → 2 ingrédients (farine, lait).
        EXPECT_EQ(static_cast<int>(r4->ingredients.size()), 2);
        EXPECT_EQ(r4->ingredients[0].name, QStringLiteral("Farine"));
    }
}

static void test_filterIndices() {
    RecipeLibrary::loadFromJson(QByteArray(kValidJson));

    // Vide → tout.
    EXPECT_EQ(static_cast<int>(RecipeLibrary::filterIndices(QString()).size()),
              RecipeLibrary::count());

    // Insensible aux accents/casse : "creme" doit trouver « Crème double ».
    const auto byIngredient = RecipeLibrary::filterIndices(QStringLiteral("creme"));
    EXPECT_EQ(static_cast<int>(byIngredient.size()), 1);

    // Par titre, insensible à la casse.
    const auto byTitle = RecipeLibrary::filterIndices(QStringLiteral("OMELETTE"));
    EXPECT_EQ(static_cast<int>(byTitle.size()), 1);

    // Tokens AND : « pomme terre » matche « Pommes de terre ».
    const auto multi = RecipeLibrary::filterIndices(QStringLiteral("pomme terre"));
    EXPECT_EQ(static_cast<int>(multi.size()), 1);

    // Recherche dans la préparation.
    const auto byPrep = RecipeLibrary::filterIndices(QStringLiteral("poele"));
    EXPECT_EQ(static_cast<int>(byPrep.size()), 1);

    // Aucun résultat pour une requête absente du catalogue.
    EXPECT_TRUE(RecipeLibrary::filterIndices(QStringLiteral("introuvable-xyz")).empty());
}

static void test_rejectsMalformedJson() {
    EXPECT_TRUE(!RecipeLibrary::loadFromJson(QByteArray("")));
    EXPECT_TRUE(!RecipeLibrary::loadFromJson(QByteArray("{ pas du json valide")));
    EXPECT_TRUE(!RecipeLibrary::loadFromJson(QByteArray("{}"))); // pas de clé "recipes"
    EXPECT_TRUE(!RecipeLibrary::loadFromJson(QByteArray(R"({"recipes": "pas un tableau"})")));
    // Après un échec, la bibliothèque doit rester vide (pas de résidu d'un chargement précédent).
    EXPECT_EQ(RecipeLibrary::count(), 0);
}

static void test_reloadReplacesPreviousLibrary() {
    RecipeLibrary::loadFromJson(QByteArray(kValidJson));
    EXPECT_EQ(RecipeLibrary::count(), 3);

    // Un second chargement (ex. relancer l'appli) remplace entièrement le catalogue.
    static const char *kSmaller = R"JSON({
      "recipes": [
        {
          "id": "only",
          "title": "Seule recette",
          "ingredients": [
            {"name": "A", "qty": "", "note": ""},
            {"name": "B", "qty": "", "note": ""}
          ]
        }
      ]
    })JSON";
    EXPECT_TRUE(RecipeLibrary::loadFromJson(QByteArray(kSmaller)));
    EXPECT_EQ(RecipeLibrary::count(), 1);
    EXPECT_TRUE(RecipeLibrary::recipeById(QStringLiteral("r1")) == nullptr);
}

int main() {
    std::printf("=== tst_recipe_library ===\n");
    test_loadValidJson();
    test_recipeByIdAndAt();
    test_dedupIngredients();
    test_filterIndices();
    test_rejectsMalformedJson();
    test_reloadReplacesPreviousLibrary();
    std::printf("\nResults: %d/%d passed, %d failed\n", g_passed, g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
