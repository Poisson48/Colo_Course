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

// Chemin du cache local (AppDataLocation/recipe_library.json).
QString recipeLibraryCachePath();

// Charge le catalogue : cache local si présent, sinon copie embarquée (qrc).
// Puis vérifie le serveur en arrière-plan ; onRemoteUpdated si une MAJ est appliquée.
void loadRecipeLibraryAsync(QObject *context,
                            std::function<void(bool ok)> onInitialLoad,
                            std::function<void()> onRemoteUpdated = nullptr);

// Revérifie le serveur (retour 1er plan, timer). onDone(updated).
void refreshRecipeLibraryFromServer(QObject *context,
                                    std::function<void(bool updated)> onDone = nullptr);

// Tests uniquement.
bool loadRecipeLibraryFromResource();

} // namespace app
