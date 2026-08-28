#include "recipe_library_loader.h"

#include "../core/recipe_library.h"

#include <QFile>

namespace app {

bool loadRecipeLibraryFromResource() {
    QFile f(QStringLiteral(":/data/recipe_library.json"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return core::RecipeLibrary::loadFromJson(f.readAll());
}

} // namespace app
