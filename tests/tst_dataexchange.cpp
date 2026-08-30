// tst_dataexchange.cpp — CSV (RFC 4180) et ZIP « store ». Pur C++/STL, sans Qt.

#include "core/csv.h"
#include "core/zip.h"
#include "core/recipe_scale.h"
#include "core/ingredient_norm.h"

#include <QString>

#include <cstdio>
#include <string>
#include <vector>

static int g_total = 0, g_passed = 0, g_failed = 0;

#define EXPECT_TRUE(expr)                                                      \
    do { ++g_total;                                                           \
        if (!(expr)) { ++g_failed;                                            \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } \
        else ++g_passed; } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

using namespace core;
using Rows = std::vector<std::vector<std::string>>;

static void test_csvEscape() {
    EXPECT_EQ(csvEscape("Lait"), "Lait");
    EXPECT_EQ(csvEscape("1 kg, bio"), "\"1 kg, bio\"");          // virgule
    EXPECT_EQ(csvEscape("dit \"frais\""), "\"dit \"\"frais\"\"\""); // guillemets doublés
    EXPECT_EQ(csvEscape("deux\nlignes"), "\"deux\nlignes\"");    // saut de ligne
}

static void test_csvRoundTrip() {
    Rows rows = {
        { "nom", "quantite", "description" },
        { "Lait", "1 L", "demi-écrémé" },
        { "Papier, toilette", "2", "dit \"épais\"" },
        { "Note\navec saut", "", "" },
    };
    const std::string csv = csvWrite(rows);
    const Rows back = csvParse(csv);
    EXPECT_EQ(back.size(), size_t(4));
    EXPECT_EQ(back, rows);   // aller-retour exact, virgules/guillemets/sauts compris
}

static void test_csvParseTolerant() {
    // \n seul (pas \r\n), et pas de saut final : doit rester lisible.
    const Rows r = csvParse("Lait,1L\nPain,");
    EXPECT_EQ(r.size(), size_t(2));
    EXPECT_EQ(r[0][0], "Lait");
    EXPECT_EQ(r[1][0], "Pain");
    EXPECT_EQ(r[1][1], "");   // champ vide final préservé

    // Un fichier à une colonne (juste des noms) reste exploitable.
    const Rows one = csvParse("Bananes\nŒufs\nBeurre\n");
    EXPECT_EQ(one.size(), size_t(3));
    EXPECT_EQ(one[2][0], "Beurre");

    // Ligne vide finale ignorée, mais un champ vide au milieu conservé.
    const Rows mid = csvParse("a,,c\r\n\r\n");
    EXPECT_EQ(mid.size(), size_t(1));
    EXPECT_EQ(mid[0].size(), size_t(3));
    EXPECT_EQ(mid[0][1], "");
}

static void test_crc32() {
    // Vecteur de référence : CRC-32 de "123456789" == 0xCBF43926.
    EXPECT_EQ(zipCrc32("123456789"), 0xCBF43926u);
    EXPECT_EQ(zipCrc32(""), 0u);
}

static void test_zipRoundTrip() {
    std::vector<ZipEntry> entries = {
        { "Courses maison.csv", "nom,quantite\r\nLait,1L\r\n" },
        { "Boulot.csv", "nom\r\nStylos\r\n" },
        { "vide.csv", "" },
    };
    const std::string archive = zipWrite(entries);

    // En-tête d'un vrai ZIP : signature "PK\3\4".
    EXPECT_TRUE(archive.size() > 4);
    EXPECT_EQ(archive.substr(0, 4), std::string("PK\x03\x04", 4));

    auto back = zipRead(archive);
    EXPECT_TRUE(back.has_value());
    EXPECT_EQ(back->size(), size_t(3));
    EXPECT_EQ((*back)[0].name, "Courses maison.csv");
    EXPECT_EQ((*back)[0].data, "nom,quantite\r\nLait,1L\r\n");
    EXPECT_EQ((*back)[1].name, "Boulot.csv");
    EXPECT_EQ((*back)[2].data, "");   // entrée vide préservée
}

static void test_zipRejectsGarbage() {
    EXPECT_TRUE(!zipRead("pas un zip du tout").has_value());
    EXPECT_TRUE(!zipRead("").has_value());
    // Un ZIP tronqué (juste l'en-tête local, pas de répertoire central) est rejeté.
    auto full = zipWrite({ { "a.csv", "x" } });
    EXPECT_TRUE(!zipRead(full.substr(0, 10)).has_value());
}

static void test_recipeScale() {
    EXPECT_EQ(core::parseServingsCount(QString("4 personnes")), 4);
    EXPECT_EQ(core::parseServingsCount(QString("6")), 6);
    EXPECT_EQ(core::scaleQuantity(QString("700 g"), 1.5), QString("1050 g"));
    EXPECT_EQ(core::scaleQuantity(QString("2"), 1.5), QString("3"));
    EXPECT_EQ(core::scaleQuantity(QString("poivre"), 2.0), QString("poivre"));
}

static void test_ingredientNorm() {
    EXPECT_EQ(core::normalizeIngredientKey(QString("Œufs")), QString("oeufs"));
    EXPECT_EQ(core::normalizeIngredientKey(QString(" Lait ")), QString("lait"));
    EXPECT_EQ(core::ingredientMatchKey(QString("oeuf")), QString("oeufs"));
    EXPECT_EQ(core::ingredientMatchKey(QString("Oeufs")), QString("oeufs"));
    EXPECT_EQ(core::ingredientMatchKey(QString("Oignon")), QString("oignons"));
    EXPECT_EQ(core::canonicalIngredientName(QString("oeuf")), QString("œufs"));
}

static void test_baseIngredientName() {
    EXPECT_EQ(core::baseIngredientName(QString("beurre fondu")), QString("beurre"));
    EXPECT_EQ(core::baseIngredientName(QString("Beurre fondu")), QString("beurre"));
    EXPECT_EQ(core::baseIngredientName(QString("courgettes en tranches")), QString("courgettes"));
    EXPECT_EQ(core::baseIngredientName(QString("courgette en tranche")), QString("courgette"));
    EXPECT_EQ(core::baseIngredientName(QString("fromage râpé")), QString("fromage"));
    EXPECT_EQ(core::canonicalIngredientName(QString("beurre fondu")), QString("beurre"));
    EXPECT_EQ(core::canonicalIngredientName(QString("courgettes en tranches")), QString("courgettes"));
    EXPECT_EQ(core::ingredientMatchKey(QString("beurre fondu")), QString("beurre"));
    EXPECT_EQ(core::ingredientMatchKey(QString("beurre")), QString("beurre"));
    EXPECT_EQ(core::baseIngredientName(QString("crème fraîche")), QString("crème fraîche"));
    EXPECT_EQ(core::baseIngredientName(QString("bœuf haché")), QString("bœuf haché"));
    EXPECT_EQ(core::baseIngredientName(QString("huile d'olive")), QString("huile d'olive"));
    EXPECT_EQ(core::canonicalIngredientName(QString("jaune d'oeuf")), QString("œufs"));
    EXPECT_EQ(core::canonicalIngredientName(QString("jaunes d'œufs")), QString("œufs"));
    EXPECT_EQ(core::canonicalIngredientName(QString("bonne pincée de sel")), QString("sel"));
    EXPECT_EQ(core::canonicalIngredientName(QString("gruyère râpé")), QString("fromage"));
    EXPECT_EQ(core::canonicalIngredientName(QString("fromage râpé")), QString("fromage"));
    EXPECT_EQ(core::ingredientMatchKey(QString("gruyère râpé")),
              core::ingredientMatchKey(QString("fromage râpé")));
    EXPECT_EQ(core::baseIngredientName(QString("lardons selon préférence")), QString("lardons"));
    EXPECT_EQ(core::baseIngredientName(QString("pomme de terre roseval.")), QString("pomme de terre roseval"));
}

static void test_normalizeManualIngredientName() {
    EXPECT_EQ(core::normalizeManualIngredientName(QString("Lait")), QString("lait"));
    EXPECT_EQ(core::normalizeManualIngredientName(QString("LAIT")), QString("lait"));
    EXPECT_EQ(core::normalizeManualIngredientName(QString("  Lait  ")), QString("lait"));
    EXPECT_EQ(core::normalizeManualIngredientName(QString("oeuf")), QString("œufs"));
}

static void test_mergeQuantities() {
    EXPECT_EQ(core::mergeQuantities(QString("2"), QString("3")), QString("5"));
    EXPECT_EQ(core::mergeQuantities(QString("200 g"), QString("100 g")), QString("300 g"));
    EXPECT_EQ(core::mergeQuantities(QString(""), QString("2")), QString("2"));
    EXPECT_EQ(core::mergeQuantities(QString("1"), QString("")), QString("1"));
    EXPECT_EQ(core::mergeQuantities(QString("poivre"), QString("sel")),
              QString("poivre + sel"));
    EXPECT_EQ(core::mergeQuantities(QString("2"), QString("200 g")),
              QString("2 + 200 g"));
}

static void test_canonicalPreservesQualifier() {
    EXPECT_EQ(core::canonicalIngredientName(QString("huile de tournesol")),
              QString("huile de tournesol"));
    EXPECT_EQ(core::canonicalIngredientName(QString("oeuf")), QString("œufs"));
}

int main() {
    std::printf("=== tst_dataexchange ===\n");
    test_csvEscape();
    test_csvRoundTrip();
    test_csvParseTolerant();
    test_crc32();
    test_zipRoundTrip();
    test_zipRejectsGarbage();
    test_recipeScale();
    test_ingredientNorm();
    test_baseIngredientName();
    test_normalizeManualIngredientName();
    test_canonicalPreservesQualifier();
    test_mergeQuantities();
    std::printf("\nResults: %d/%d passed, %d failed\n", g_passed, g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
