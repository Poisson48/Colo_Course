#pragma once

#include <QObject>
#include <functional>

namespace app {

struct RecipesManifest {
    int     version = 0;
    int     count   = 0;
    QString updatedAt;
    QString url;
};

// Parse recipes-manifest.json hébergé sur colo-apps.
bool parseRecipesManifest(const QByteArray &json, RecipesManifest *out);

// Cache local du catalogue SQLite (AppDataLocation/recipe_catalog.db).
QString recipeCatalogCachePath();

// Ancien cache JSON (migration).
QString recipeLibraryCachePath();

// Ouvre le catalogue SQLite : cache local, copie embarquée, ou import JSON de secours.
// deferredMs > 0 : attend avant de charger (démarrage plus fluide).
void loadRecipeCatalogAsync(QObject *context,
                            std::function<void(bool ok)> onInitialLoad,
                            std::function<void()> onRemoteUpdated = nullptr,
                            int deferredMs = 2500);

// Charge immédiatement si pas encore fait (onglet Recettes / Catalogue).
void ensureRecipeCatalogLoaded(QObject *context,
                               std::function<void(bool ok)> onDone = nullptr);

bool isRecipeCatalogLoaded();

// Revérifie le serveur (retour 1er plan, timer). onDone(updated).
void refreshRecipeLibraryFromServer(QObject *context,
                                    std::function<void(bool updated)> onDone = nullptr);

// Tests uniquement : ouvre la copie embarquée ou importe le JSON.
bool loadRecipeLibraryFromResource();

} // namespace app
