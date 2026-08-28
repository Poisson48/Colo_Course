#include "recipe_library.h"

#include "recipe_scale.h"

#include <nlohmann/json.hpp>

#include <QHash>
#include <QSet>

namespace core {

std::vector<LibraryRecipe> RecipeLibrary::s_recipes;
// QHash, pas std::unordered_map<QString, …> : std::hash<QString> n'est pas
// spécialisé (Qt fournit qHash, pas std::hash), et cette instanciation serait
// rejetée à la compilation (static_assert de <bits/hashtable_policy.h>).
static QHash<QString, size_t> s_idIndex;

static QString deaccent(const QString &s) {
    QString out;
    for (const QChar c : s.normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing)
            out += c;
    }
    return out;
}

static QString normKey(const QString &s) {
    return deaccent(s.trimmed().toLower());
}

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

bool RecipeLibrary::loadFromJson(const QByteArray &json) {
    s_recipes.clear();
    s_idIndex.clear();

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

            const QString titleKey = normKey(rec.title);
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
                    const QString ingKey = normKey(ing.name);
                    if (seenIng.contains(ingKey))
                        continue;
                    seenIng.insert(ingKey);
                    rec.ingredients.push_back(ing);
                }
            }

            if (rec.ingredients.size() < 2)
                continue;

            QString blob = normKey(rec.title) + QLatin1Char(' ') + normKey(rec.category);
            for (const auto &ing : rec.ingredients)
                blob += QLatin1Char(' ') + normKey(ing.name);
            rec.searchBlob = blob;

            s_idIndex[rec.id] = s_recipes.size();
            s_recipes.push_back(std::move(rec));
        } catch (const nlohmann::json::exception &) {
            continue;
        }
    }
    return !s_recipes.empty();
}

int RecipeLibrary::count() {
    return static_cast<int>(s_recipes.size());
}

const LibraryRecipe *RecipeLibrary::recipeAt(int index) {
    if (index < 0 || index >= static_cast<int>(s_recipes.size()))
        return nullptr;
    return &s_recipes[static_cast<size_t>(index)];
}

const LibraryRecipe *RecipeLibrary::recipeById(const QString &id) {
    const auto it = s_idIndex.constFind(id);
    if (it == s_idIndex.constEnd())
        return nullptr;
    return recipeAt(static_cast<int>(it.value()));
}

std::vector<int> RecipeLibrary::filterIndices(const QString &query) {
    std::vector<int> out;
    const QString q = normKey(query);
    for (int i = 0; i < static_cast<int>(s_recipes.size()); ++i) {
        if (q.isEmpty() || s_recipes[static_cast<size_t>(i)].searchBlob.contains(q))
            out.push_back(i);
    }
    return out;
}

} // namespace core
