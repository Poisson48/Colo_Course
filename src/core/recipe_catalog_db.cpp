#include "recipe_catalog_db.h"

#include "ingredient_norm.h"

#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QVariant>
#include <QDebug>

namespace core {

namespace {

QMutex s_mutex;
QString s_connectionName;
QSqlDatabase s_db;
QHash<QString, LibraryRecipe> s_recipeCache;
int s_cacheGeneration = 0;

void bumpCacheGeneration()
{
    ++s_cacheGeneration;
    s_recipeCache.clear();
}

bool execSql(QSqlQuery &q, const QString &sql)
{
    if (q.exec(sql))
        return true;
    qWarning() << "[RecipeCatalogDb]" << q.lastError().text() << sql.left(120);
    return false;
}

bool bindTokensAndCategory(QSqlQuery &q, const QStringList &tokens, const QString &category)
{
    int idx = 0;
    for (const QString &tok : tokens) {
        q.bindValue(idx++, QStringLiteral("%") + tok + QStringLiteral("%"));
    }
    if (!category.isEmpty())
        q.bindValue(idx, category);
    return true;
}

bool ftsTableExists(QSqlDatabase &db)
{
    QSqlQuery q(db);
    return q.exec(QStringLiteral(
               "SELECT 1 FROM sqlite_master WHERE type='table' AND name='recipes_fts'"))
           && q.next();
}

QString ftsMatchExpression(const QStringList &tokens)
{
    QStringList parts;
    parts.reserve(tokens.size());
    for (const QString &tok : tokens) {
        QString escaped = tok;
        escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        parts.push_back(QStringLiteral("\"") + escaped + QStringLiteral("\"*"));
    }
    return parts.join(QStringLiteral(" AND "));
}

LibraryRecipe rowToRecipe(const QSqlRecord &rec, const std::vector<LibraryIngredient> &ings)
{
    LibraryRecipe out;
    out.id            = rec.value(QStringLiteral("id")).toString();
    out.title         = rec.value(QStringLiteral("title")).toString();
    out.category      = rec.value(QStringLiteral("category")).toString();
    out.servings      = rec.value(QStringLiteral("servings")).toString();
    out.servingsCount = rec.value(QStringLiteral("servings_count")).toInt();
    out.instructions  = rec.value(QStringLiteral("instructions")).toString();
    out.searchBlob    = rec.value(QStringLiteral("search_blob")).toString();
    out.ingredients   = ings;
    return out;
}

std::vector<LibraryIngredient> loadIngredients(const QString &recipeId)
{
    std::vector<LibraryIngredient> out;
    QSqlQuery q(s_db);
    q.prepare(QStringLiteral(
        "SELECT name, qty, note FROM ingredients "
        "WHERE recipe_id = ? ORDER BY position"));
    q.addBindValue(recipeId);
    if (!q.exec())
        return out;
    while (q.next()) {
        LibraryIngredient ing;
        ing.name = q.value(0).toString();
        ing.qty  = q.value(1).toString();
        ing.note = q.value(2).toString();
        out.push_back(std::move(ing));
    }
    return out;
}

} // namespace

bool RecipeCatalogDb::open(const QString &path)
{
    QMutexLocker lock(&s_mutex);
    if (s_db.isOpen())
        close();

    s_connectionName = QStringLiteral("recipe_catalog_")
                       + QUuid::createUuid().toString(QUuid::WithoutBraces);
    s_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), s_connectionName);
    s_db.setDatabaseName(path);
    if (!s_db.open()) {
        qWarning() << "[RecipeCatalogDb] open failed:" << s_db.lastError().text();
        QSqlDatabase::removeDatabase(s_connectionName);
        s_connectionName.clear();
        return false;
    }

    QSqlQuery pragma(s_db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));

    if (!ensureSchema()) {
        close();
        return false;
    }
    if (!ensureFtsPopulatedUnlocked()) {
        qWarning() << "[RecipeCatalogDb] FTS populate failed";
        close();
        return false;
    }
    bumpCacheGeneration();
    return true;
}

void RecipeCatalogDb::close()
{
    bumpCacheGeneration();
    if (s_db.isOpen())
        s_db.close();
    if (!s_connectionName.isEmpty()) {
        const QString name = s_connectionName;
        s_connectionName.clear();
        QSqlDatabase::removeDatabase(name);
    }
}

bool RecipeCatalogDb::isOpen()
{
    QMutexLocker lock(&s_mutex);
    return s_db.isOpen();
}

bool RecipeCatalogDb::ensureSchema()
{
    QSqlQuery q(s_db);
    const QStringList ddl = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS catalog_meta ("
            "  key TEXT PRIMARY KEY,"
            "  value TEXT NOT NULL"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS recipes ("
            "  sort_key INTEGER PRIMARY KEY,"
            "  id TEXT NOT NULL UNIQUE,"
            "  title TEXT NOT NULL,"
            "  category TEXT NOT NULL DEFAULT '',"
            "  servings TEXT NOT NULL DEFAULT '',"
            "  servings_count INTEGER NOT NULL DEFAULT 4,"
            "  instructions TEXT NOT NULL DEFAULT '',"
            "  search_blob TEXT NOT NULL DEFAULT ''"
            ")"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS ingredients ("
            "  recipe_id TEXT NOT NULL,"
            "  position INTEGER NOT NULL,"
            "  name TEXT NOT NULL,"
            "  qty TEXT NOT NULL DEFAULT '',"
            "  note TEXT NOT NULL DEFAULT '',"
            "  PRIMARY KEY (recipe_id, position),"
            "  FOREIGN KEY (recipe_id) REFERENCES recipes(id) ON DELETE CASCADE"
            ")"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_recipes_category ON recipes(category)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_recipes_id ON recipes(id)"),
        QStringLiteral(
            "CREATE VIRTUAL TABLE IF NOT EXISTS recipes_fts USING fts5("
            "sort_key UNINDEXED, search_blob)"),
    };
    for (const QString &sql : ddl) {
        if (!execSql(q, sql))
            return false;
    }
    return true;
}

bool RecipeCatalogDb::ensureFtsPopulatedUnlocked()
{
    if (!s_db.isOpen() || !ftsTableExists(s_db))
        return true;

    QSqlQuery q(s_db);
    if (!q.exec(QStringLiteral(
            "SELECT (SELECT COUNT(*) FROM recipes), (SELECT COUNT(*) FROM recipes_fts)"))
        || !q.next()) {
        return rebuildFtsIndexUnlocked();
    }

    const int recipes = q.value(0).toInt();
    const int fts     = q.value(1).toInt();
    if (recipes > 0 && fts < recipes) {
        qWarning() << "[RecipeCatalogDb] index FTS incomplet" << fts << "/" << recipes
                   << "— reconstruction";
        return rebuildFtsIndexUnlocked();
    }
    return true;
}

bool RecipeCatalogDb::rebuildFtsIndexUnlocked()
{
    if (!s_db.isOpen() || !ftsTableExists(s_db))
        return true;
    QSqlQuery q(s_db);
    if (!execSql(q, QStringLiteral("DELETE FROM recipes_fts")))
        return false;
    return execSql(q, QStringLiteral(
        "INSERT INTO recipes_fts(sort_key, search_blob) "
        "SELECT sort_key, search_blob FROM recipes"));
}

bool RecipeCatalogDb::rebuildFtsIndex()
{
    QMutexLocker lock(&s_mutex);
    return rebuildFtsIndexUnlocked();
}

bool RecipeCatalogDb::importFromParsed(RecipeLibraryParseResult &&parsed)
{
    if (!parsed.ok)
        return false;

    QMutexLocker lock(&s_mutex);
    if (!s_db.isOpen())
        return false;

    if (!s_db.transaction()) {
        qWarning() << "[RecipeCatalogDb] transaction:" << s_db.lastError().text();
        return false;
    }

    QSqlQuery q(s_db);
    if (!execSql(q, QStringLiteral("DELETE FROM ingredients"))
        || !execSql(q, QStringLiteral("DELETE FROM recipes"))) {
        s_db.rollback();
        return false;
    }

    q.prepare(QStringLiteral(
        "INSERT INTO recipes (sort_key, id, title, category, servings, servings_count, "
        "instructions, search_blob) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    QSqlQuery ingQ(s_db);
    ingQ.prepare(QStringLiteral(
        "INSERT INTO ingredients (recipe_id, position, name, qty, note) "
        "VALUES (?, ?, ?, ?, ?)"));

    int sortKey = 0;
    for (const LibraryRecipe &rec : parsed.recipes) {
        q.bindValue(0, sortKey);
        q.bindValue(1, rec.id);
        q.bindValue(2, rec.title);
        q.bindValue(3, rec.category);
        q.bindValue(4, rec.servings);
        q.bindValue(5, rec.servingsCount);
        q.bindValue(6, rec.instructions);
        q.bindValue(7, rec.searchBlob);
        if (!q.exec()) {
            qWarning() << "[RecipeCatalogDb] insert recipe:" << q.lastError().text();
            s_db.rollback();
            return false;
        }

        int pos = 0;
        for (const LibraryIngredient &ing : rec.ingredients) {
            ingQ.bindValue(0, rec.id);
            ingQ.bindValue(1, pos++);
            ingQ.bindValue(2, ing.name);
            ingQ.bindValue(3, ing.qty);
            ingQ.bindValue(4, ing.note);
            if (!ingQ.exec()) {
                qWarning() << "[RecipeCatalogDb] insert ingredient:" << ingQ.lastError().text();
                s_db.rollback();
                return false;
            }
        }
        ++sortKey;
    }

    QSqlQuery meta(s_db);
    meta.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO catalog_meta (key, value) VALUES (?, ?)"));
    meta.bindValue(0, QStringLiteral("recipe_count"));
    meta.bindValue(1, QString::number(sortKey));
    if (!meta.exec()) {
        s_db.rollback();
        return false;
    }

    if (!s_db.commit()) {
        s_db.rollback();
        return false;
    }

    QSqlQuery metaVer(s_db);
    metaVer.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO catalog_meta (key, value) VALUES (?, ?)"));
    metaVer.bindValue(0, QStringLiteral("schema_version"));
    metaVer.bindValue(1, QStringLiteral("2"));
    metaVer.exec();

    if (!rebuildFtsIndexUnlocked()) {
        qWarning() << "[RecipeCatalogDb] FTS rebuild failed";
        return false;
    }

    bumpCacheGeneration();
    return sortKey > 0;
}

bool RecipeCatalogDb::importFromJson(const QByteArray &json)
{
    return importFromParsed(RecipeLibrary::parseJsonData(json));
}

int RecipeCatalogDb::count()
{
    QMutexLocker lock(&s_mutex);
    if (!s_db.isOpen())
        return 0;
    QSqlQuery q(s_db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM recipes")) || !q.next())
        return 0;
    return q.value(0).toInt();
}

const LibraryRecipe *RecipeCatalogDb::fetchCached(const QString &id)
{
    auto it = s_recipeCache.constFind(id);
    if (it != s_recipeCache.constEnd())
        return &it.value();

    QSqlQuery q(s_db);
    q.prepare(QStringLiteral(
        "SELECT sort_key, id, title, category, servings, servings_count, instructions, search_blob "
        "FROM recipes WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return nullptr;

    const std::vector<LibraryIngredient> ings = loadIngredients(id);
    s_recipeCache.insert(id, rowToRecipe(q.record(), ings));
    return &s_recipeCache[id];
}

const LibraryRecipe *RecipeCatalogDb::recipeAt(int index)
{
    QMutexLocker lock(&s_mutex);
    if (!s_db.isOpen() || index < 0)
        return nullptr;

    QSqlQuery q(s_db);
    q.prepare(QStringLiteral(
        "SELECT sort_key, id, title, category, servings, servings_count, instructions, search_blob "
        "FROM recipes WHERE sort_key = ?"));
    q.addBindValue(index);
    if (!q.exec() || !q.next())
        return nullptr;

    const QString id = q.value(QStringLiteral("id")).toString();
    return fetchCached(id);
}

const LibraryRecipe *RecipeCatalogDb::recipeById(const QString &id)
{
    QMutexLocker lock(&s_mutex);
    if (!s_db.isOpen() || id.isEmpty())
        return nullptr;
    return fetchCached(id);
}

std::vector<int> RecipeCatalogDb::filterIndices(const QString &query, const QString &category,
                                                int maxResults, int *totalMatchesOut)
{
    QMutexLocker lock(&s_mutex);
    std::vector<int> out;
    if (!s_db.isOpen()) {
        if (totalMatchesOut)
            *totalMatchesOut = 0;
        return out;
    }

    const QString qNorm = normalizeIngredientKey(query);
    const QStringList tokens = qNorm.split(QRegularExpression(QStringLiteral("\\s+")),
                                           Qt::SkipEmptyParts);

    const bool useFts = !tokens.isEmpty() && ftsTableExists(s_db);
    QString sql;
    if (useFts) {
        sql = QStringLiteral(
            "SELECT r.sort_key, r.id, r.title, r.category, r.instructions, r.search_blob "
            "FROM recipes_fts f "
            "JOIN recipes r ON r.sort_key = f.sort_key "
            "WHERE f.search_blob MATCH ?");
        if (!category.isEmpty())
            sql += QStringLiteral(" AND r.category = ?");
    } else {
        sql = QStringLiteral(
            "SELECT sort_key, id, title, category, instructions, search_blob FROM recipes WHERE 1=1");
        if (!category.isEmpty())
            sql += QStringLiteral(" AND category = ?");
        for (int i = 0; i < tokens.size(); ++i)
            sql += QStringLiteral(" AND search_blob LIKE ?");
    }

    QSqlQuery sqlQ(s_db);
    sqlQ.prepare(sql);
    if (useFts) {
        sqlQ.addBindValue(ftsMatchExpression(tokens));
        if (!category.isEmpty())
            sqlQ.addBindValue(category);
    } else {
        bindTokensAndCategory(sqlQ, tokens, category);
    }

    if (!sqlQ.exec()) {
        if (useFts) {
            qWarning() << "[RecipeCatalogDb] FTS MATCH échoué, repli LIKE :"
                       << sqlQ.lastError().text();
            sql = QStringLiteral(
                "SELECT sort_key, id, title, category, instructions, search_blob FROM recipes "
                "WHERE 1=1");
            if (!category.isEmpty())
                sql += QStringLiteral(" AND category = ?");
            for (int i = 0; i < tokens.size(); ++i)
                sql += QStringLiteral(" AND search_blob LIKE ?");
            sqlQ = QSqlQuery(s_db);
            sqlQ.prepare(sql);
            bindTokensAndCategory(sqlQ, tokens, category);
            if (!sqlQ.exec()) {
                if (totalMatchesOut)
                    *totalMatchesOut = 0;
                return out;
            }
        } else {
            if (totalMatchesOut)
                *totalMatchesOut = 0;
            return out;
        }
    }

    struct Scored { int index; int score; };
    std::vector<Scored> scored;

    while (sqlQ.next()) {
        const int sortKey = sqlQ.value(0).toInt();
        const QString title = sqlQ.value(2).toString();
        const QString cat   = sqlQ.value(3).toString();
        const QString instr = sqlQ.value(4).toString();
        const QString blob  = sqlQ.value(5).toString();
        const QString recipeId = sqlQ.value(1).toString();

        int score = 0;
        if (!tokens.isEmpty()) {
            const QString titleNorm = normalizeIngredientKey(title);
            const QString catNorm   = normalizeIngredientKey(cat);
            const QString instrNorm = normalizeIngredientKey(instr);
            for (const QString &tok : tokens) {
                if (titleNorm.contains(tok))
                    score += 20;
                if (catNorm.contains(tok))
                    score += 8;
                if (instrNorm.contains(tok))
                    score += 3;
                if (blob.contains(tok))
                    score += 1;
            }
            // Ingrédients : requête légère seulement si score bas (évite 30k requêtes).
            if (score < 25) {
                QSqlQuery ingQ(s_db);
                ingQ.prepare(QStringLiteral(
                    "SELECT name FROM ingredients WHERE recipe_id = ?"));
                ingQ.addBindValue(recipeId);
                if (ingQ.exec()) {
                    while (ingQ.next()) {
                        const QString ingNorm = normalizeIngredientKey(ingQ.value(0).toString());
                        for (const QString &tok : tokens) {
                            if (ingNorm.contains(tok))
                                score += 5;
                        }
                    }
                }
            }
            scored.push_back({ sortKey, score });
        } else {
            scored.push_back({ sortKey, 0 });
        }
    }

    if (!tokens.isEmpty()) {
        std::stable_sort(scored.begin(), scored.end(),
                         [](const Scored &a, const Scored &b) {
                             if (a.score != b.score)
                                 return a.score > b.score;
                             return a.index < b.index;
                         });
    }

    if (totalMatchesOut)
        *totalMatchesOut = static_cast<int>(scored.size());

    const int limit = (maxResults > 0) ? maxResults : static_cast<int>(scored.size());
    out.reserve(static_cast<size_t>(limit));
    for (int i = 0; i < limit && i < static_cast<int>(scored.size()); ++i)
        out.push_back(scored[static_cast<size_t>(i)].index);

    return out;
}

std::vector<RecipeLibrary::CategoryStat> RecipeCatalogDb::categoryStats()
{
    QMutexLocker lock(&s_mutex);
    std::vector<RecipeLibrary::CategoryStat> out;
    if (!s_db.isOpen())
        return out;

    QSqlQuery q(s_db);
    if (!q.exec(QStringLiteral(
            "SELECT category, COUNT(*) AS cnt FROM recipes "
            "WHERE category != '' GROUP BY category ORDER BY cnt DESC, category ASC")))
        return out;

    while (q.next()) {
        RecipeLibrary::CategoryStat stat;
        stat.name  = q.value(0).toString();
        stat.count = q.value(1).toInt();
        out.push_back(std::move(stat));
    }
    return out;
}

bool RecipeCatalogDb::validateFile(const QString &path, QString *errorOut)
{
    const QFileInfo info(path);
    if (!info.exists() || info.size() < 4096) {
        if (errorOut)
            *errorOut = QStringLiteral("fichier catalogue absent ou trop petit");
        return false;
    }

    const QString connName = QStringLiteral("recipe_catalog_validate_")
                             + QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(path);
        if (!db.open()) {
            if (errorOut)
                *errorOut = db.lastError().text();
            QSqlDatabase::removeDatabase(connName);
            return false;
        }

        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM recipes")) || !q.next()) {
            if (errorOut)
                *errorOut = QStringLiteral("table recipes illisible");
            db.close();
            QSqlDatabase::removeDatabase(connName);
            return false;
        }

        const int count = q.value(0).toInt();
        db.close();
        QSqlDatabase::removeDatabase(connName);

        if (count < 100) {
            if (errorOut)
                *errorOut = QStringLiteral("catalogue trop petit (") + QString::number(count)
                            + QStringLiteral(" recettes)");
            return false;
        }
    }
    return true;
}

} // namespace core
