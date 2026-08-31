// tst_catalog_db.cpp — RecipeCatalogDb : open, filter, validateFile, FTS.

#include "core/recipe_catalog_db.h"
#include "core/recipe_library.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QString>

#include <cstdio>

static int g_total = 0, g_passed = 0, g_failed = 0;

#define EXPECT_TRUE(expr)                                                      \
    do { ++g_total;                                                           \
        if (!(expr)) { ++g_failed;                                            \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } \
        else ++g_passed; } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

using core::RecipeCatalogDb;

static const char *kFixtureJson = R"JSON({
  "recipes": [
    {
      "id": "c1",
      "title": "Omelette aux fines herbes",
      "category": "Plat principal",
      "servings": "2 personnes",
      "servings_count": 2,
      "instructions": "Battre les oeufs et cuire.",
      "ingredients": [
        {"name": "Oeufs", "qty": "3", "note": ""},
        {"name": "Persil", "qty": "1 c. à soupe", "note": ""}
      ]
    },
    {
      "id": "c2",
      "title": "Salade verte",
      "category": "Entrée",
      "servings": "4 personnes",
      "servings_count": 4,
      "instructions": "Laver et assaisonner.",
      "ingredients": [
        {"name": "Laitue", "qty": "1", "note": ""},
        {"name": "Vinaigrette", "qty": "3 c. à soupe", "note": ""}
      ]
    }
  ]
})JSON";

static void test_importAndFilter() {
    QTemporaryDir tmp;
    EXPECT_TRUE(tmp.isValid());
    const QString dbPath = tmp.path() + QStringLiteral("/catalog.db");

    EXPECT_TRUE(RecipeCatalogDb::open(dbPath));
    EXPECT_TRUE(RecipeCatalogDb::importFromJson(QByteArray(kFixtureJson)));
    EXPECT_EQ(RecipeCatalogDb::count(), 2);

    int total = 0;
    auto all = RecipeCatalogDb::filterIndices(QString(), QString(), 0, &total);
    EXPECT_EQ(static_cast<int>(all.size()), 2);
    EXPECT_EQ(total, 2);

    auto omelette = RecipeCatalogDb::filterIndices(QStringLiteral("omelette"), QString(), 0, &total);
    EXPECT_EQ(static_cast<int>(omelette.size()), 1);
    EXPECT_TRUE(omelette[0] >= 0);

    auto entree = RecipeCatalogDb::filterIndices(QString(), QStringLiteral("Entrée"), 0, &total);
    EXPECT_EQ(static_cast<int>(entree.size()), 1);

    const auto *rec = RecipeCatalogDb::recipeById(QStringLiteral("c1"));
    EXPECT_TRUE(rec != nullptr);
    EXPECT_EQ(rec->title, QStringLiteral("Omelette aux fines herbes"));

    RecipeCatalogDb::close();
}

static void test_validateFile() {
    QTemporaryDir tmp;
    EXPECT_TRUE(tmp.isValid());
    const QString dbPath = tmp.path() + QStringLiteral("/good.db");
    const QString badPath = tmp.path() + QStringLiteral("/bad.db");

    EXPECT_TRUE(RecipeCatalogDb::open(dbPath));
    EXPECT_TRUE(RecipeCatalogDb::importFromJson(QByteArray(kFixtureJson)));
    RecipeCatalogDb::close();

    QString err;
    EXPECT_TRUE(!RecipeCatalogDb::validateFile(badPath, &err));
    EXPECT_TRUE(!err.isEmpty());

    // validateFile exige >= 100 recettes ; notre fixture est petite → invalide en prod.
    EXPECT_TRUE(!RecipeCatalogDb::validateFile(dbPath, &err));
}

static void test_corruptFile() {
    QTemporaryDir tmp;
    EXPECT_TRUE(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/corrupt.db");
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("not a sqlite file");
    f.close();

    QString err;
    EXPECT_TRUE(!RecipeCatalogDb::validateFile(path, &err));
}

int main() {
    std::printf("=== tst_catalog_db ===\n");
    test_importAndFilter();
    test_validateFile();
    test_corruptFile();
    std::printf("\nResults: %d/%d passed, %d failed\n", g_passed, g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
