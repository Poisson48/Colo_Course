#pragma once

#include <QObject>
#include <functional>

namespace app {

struct RecipesManifest {
    int     version       = 0;
    int     count         = 0;
    int     schemaVersion = 1;
    qint64  byteSize      = 0;
    QString updatedAt;
    QString url;
    QString sha256;
    QString format; // "sqlite" ou vide (legacy JSON)
};

enum class RecipeCatalogState {
    Idle = 0,
    Resolving,
    Ready,
    Error,
    Updating
};

QString recipeCatalogStateString(RecipeCatalogState state);

// Parse recipes-manifest.json hébergé sur colo-apps.
bool parseRecipesManifest(const QByteArray &json, RecipesManifest *out);

RecipeCatalogState recipeCatalogState();
QString recipeCatalogError();

// Cache local du catalogue SQLite (AppDataLocation/recipe_catalog.db).
QString recipeCatalogCachePath();

// Ancien cache JSON (migration).
QString recipeLibraryCachePath();

// Ouvre le catalogue SQLite : cache local, copie embarquée, ou import JSON migré.
// deferredMs > 0 : attend avant de charger (démarrage plus fluide).
void loadRecipeCatalogAsync(QObject *context,
                            std::function<void(bool ok)> onInitialLoad,
                            std::function<void()> onRemoteUpdated = nullptr,
                            int deferredMs = 2500);

// Charge immédiatement si pas encore fait (onglet Recettes / Catalogue).
void ensureRecipeCatalogLoaded(QObject *context,
                               std::function<void(bool ok)> onDone = nullptr);

bool isRecipeCatalogLoaded();

// Relance après erreur (réinitialise l'état et recharge).
void retryRecipeCatalog(QObject *context,
                        std::function<void(bool ok)> onDone = nullptr,
                        std::function<void()> onRemoteUpdated = nullptr);

// Revérifie le serveur (retour 1er plan, timer). onDone(updated).
void refreshRecipeLibraryFromServer(QObject *context,
                                    std::function<void(bool updated)> onDone = nullptr);

// Notifie l'UI (AppController) quand l'état change.
void setRecipeCatalogStateListener(std::function<void()> onChanged);

// Tests uniquement : ouvre la copie embarquée.
bool loadRecipeLibraryFromResource();

} // namespace app
