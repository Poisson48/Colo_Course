#include "ingredient_norm.h"
#include "recipe_scale.h"
#include "recipe_library.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include <QHash>
#include <QMutex>
#include <QSet>
#include <QRegularExpression>

namespace core {

std::vector<LibraryRecipe> RecipeLibrary::s_recipes;
// QHash, pas std::unordered_map<QString, …> : std::hash<QString> n'est pas
// spécialisé (Qt fournit qHash, pas std::hash), et cette instanciation serait
// rejetée à la compilation (static_assert de <bits/hashtable_policy.h>).
static QHash<QString, size_t> s_idIndex;
static std::vector<RecipeLibrary::CategoryStat> s_categoryStats;
static QMutex s_mutex;

static QString jsonStringField(const nlohmann::json &node, const char *key) {
    if (!node.contains(key))
        return {};
    const auto &v = node[key];
    if (v.is_string())
        return QString::fromStdString(v.get<std::string>()).trimmed();
    if (v.is_number_integer())
        return QString::number(v.get<int64_t>());
    if (v.is_number_float())
        return QString::number(v.get<double>());
    return {};
}

static int jsonIntField(const nlohmann::json &node, const char *key, int fallback = 0) {
    if (!node.contains(key))
        return fallback;
    const auto &v = node[key];
    if (v.is_number_integer())
        return static_cast<int>(v.get<int64_t>());
    if (v.is_string()) {
        const QString s = QString::fromStdString(v.get<std::string>()).trimmed();
        return parseServingsCount(s);
    }
    return fallback;
}

static bool parseJsonInto(const QByteArray &json,
                          std::vector<LibraryRecipe> &recipes,
                          QHash<QString, size_t> &idIndex)
{
    recipes.clear();
    idIndex.clear();

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json.constData(), json.constData() + json.size());
    } catch (const nlohmann::json::exception &) {
        return false;
    }

    if (!root.contains("recipes") || !root["recipes"].is_array())
        return false;

    QSet<QString> seenTitles;

    for (const auto &node : root["recipes"]) {
        try {
            if (!node.is_object())
                continue;

            LibraryRecipe rec;
            rec.id       = jsonStringField(node, "id");
            rec.title    = jsonStringField(node, "title");
            rec.category = jsonStringField(node, "category");
            rec.servings = jsonStringField(node, "servings");
            rec.servingsCount = jsonIntField(node, "servings_count", 0);
            if (rec.servingsCount <= 0)
                rec.servingsCount = parseServingsCount(rec.servings);
            if (rec.servingsCount <= 0)
                rec.servingsCount = 4;

            if (rec.id.isEmpty() || rec.title.isEmpty())
                continue;

            const QString titleKey = normalizeIngredientKey(rec.title);
            if (seenTitles.contains(titleKey))
                continue;
            seenTitles.insert(titleKey);

            if (node.contains("ingredients") && node["ingredients"].is_array()) {
                QSet<QString> seenIng;
                for (const auto &ingNode : node["ingredients"]) {
                    if (!ingNode.is_object())
                        continue;
                    LibraryIngredient ing;
                    ing.name = jsonStringField(ingNode, "name");
                    ing.qty  = jsonStringField(ingNode, "qty");
                    ing.note = jsonStringField(ingNode, "note");
                    if (ing.name.isEmpty())
                        continue;
                    ing.name = canonicalIngredientName(ing.name);
                    const QString ingKey = ingredientMatchKey(ing.name);
                    if (seenIng.contains(ingKey))
                        continue;
                    seenIng.insert(ingKey);
                    rec.ingredients.push_back(ing);
                }
            }

            if (rec.ingredients.size() < 2)
                continue;

            rec.instructions = jsonStringField(node, "instructions");

            QString blob = normalizeIngredientKey(rec.title) + QLatin1Char(' ')
                           + normalizeIngredientKey(rec.category);
            for (const auto &ing : rec.ingredients)
                blob += QLatin1Char(' ') + normalizeIngredientKey(ing.name);
            if (!rec.instructions.isEmpty())
                blob += QLatin1Char(' ') + normalizeIngredientKey(rec.instructions);
            rec.searchBlob = blob;

            idIndex[rec.id] = recipes.size();
            recipes.push_back(std::move(rec));
        } catch (const nlohmann::json::exception &) {
            continue;
        }
    }
    return !recipes.empty();
}

QString RecipeLibrary::normalizeSearchText(const QString &s) {
    return normalizeIngredientKey(s);
}

RecipeLibraryParseResult RecipeLibrary::parseJsonData(const QByteArray &json)
{
    RecipeLibraryParseResult out;
    out.ok = parseJsonInto(json, out.recipes, out.idIndex);
    if (!out.ok) {
        out.recipes.clear();
        out.idIndex.clear();
    }
    return out;
}

bool RecipeLibrary::installParsed(RecipeLibraryParseResult &&parsed)
{
    if (!parsed.ok)
        return false;
    QMutexLocker lock(&s_mutex);
    s_recipes = std::move(parsed.recipes);
    s_idIndex = std::move(parsed.idIndex);
    RecipeLibrary::rebuildCategoryStatsLocked();
    return true;
}

bool RecipeLibrary::loadFromJson(const QByteArray &json) {
    return installParsed(parseJsonData(json));
}

void RecipeLibrary::rebuildCategoryStatsLocked()
{
    QHash<QString, int> counts;
    for (const LibraryRecipe &rec : s_recipes) {
        if (!rec.category.isEmpty())
            ++counts[rec.category];
    }

    s_categoryStats.clear();
    s_categoryStats.reserve(static_cast<size_t>(counts.size()));
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        s_categoryStats.push_back({ it.key(), it.value() });

    std::sort(s_categoryStats.begin(), s_categoryStats.end(),
              [](const CategoryStat &a, const CategoryStat &b) {
                  if (a.count != b.count)
                      return a.count > b.count;
                  return a.name < b.name;
              });
}

int RecipeLibrary::count() {
    QMutexLocker lock(&s_mutex);
    return static_cast<int>(s_recipes.size());
}

const LibraryRecipe *RecipeLibrary::recipeAt(int index) {
    QMutexLocker lock(&s_mutex);
    if (index < 0 || index >= static_cast<int>(s_recipes.size()))
        return nullptr;
    return &s_recipes[static_cast<size_t>(index)];
}

const LibraryRecipe *RecipeLibrary::recipeById(const QString &id) {
    QMutexLocker lock(&s_mutex);
    const auto it = s_idIndex.constFind(id);
    if (it == s_idIndex.constEnd())
        return nullptr;
    const int index = static_cast<int>(it.value());
    if (index < 0 || index >= static_cast<int>(s_recipes.size()))
        return nullptr;
    return &s_recipes[static_cast<size_t>(index)];
}

std::vector<int> RecipeLibrary::filterIndices(const QString &query, const QString &category) {
    QMutexLocker lock(&s_mutex);
    const QString q = normalizeIngredientKey(query);
    const QStringList tokens = q.split(QRegularExpression(QStringLiteral("\\s+")),
                                      Qt::SkipEmptyParts);

    struct Scored { int index; int score; };
    std::vector<Scored> scored;

    for (int i = 0; i < static_cast<int>(s_recipes.size()); ++i) {
        const LibraryRecipe &rec = s_recipes[static_cast<size_t>(i)];
        if (!category.isEmpty() && rec.category != category)
            continue;
        if (!tokens.isEmpty()) {
            bool allMatch = true;
            int score = 0;
            const QString titleNorm = normalizeIngredientKey(rec.title);
            const QString catNorm   = normalizeIngredientKey(rec.category);
            const QString instrNorm = normalizeIngredientKey(rec.instructions);
            for (const QString &tok : tokens) {
                if (!rec.searchBlob.contains(tok)) {
                    allMatch = false;
                    break;
                }
                if (titleNorm.contains(tok))
                    score += 20;
                if (catNorm.contains(tok))
                    score += 8;
                for (const auto &ing : rec.ingredients) {
                    if (normalizeIngredientKey(ing.name).contains(tok))
                        score += 5;
                }
                if (instrNorm.contains(tok))
                    score += 3;
            }
            if (!allMatch)
                continue;
            scored.push_back({ i, score });
        } else {
            scored.push_back({ i, 0 });
        }
    }

    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored &a, const Scored &b) {
                         if (a.score != b.score)
                             return a.score > b.score;
                         return a.index < b.index;
                     });

    std::vector<int> out;
    for (const auto &s : scored)
        out.push_back(s.index);
    return out;
}

std::vector<QString> RecipeLibrary::categories() {
    const auto stats = categoryStats();
    std::vector<QString> out;
    out.reserve(stats.size());
    for (const CategoryStat &c : stats)
        out.push_back(c.name);
    return out;
}

std::vector<RecipeLibrary::CategoryStat> RecipeLibrary::categoryStats()
{
    QMutexLocker lock(&s_mutex);
    return s_categoryStats;
}

} // namespace core
