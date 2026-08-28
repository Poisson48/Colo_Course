#pragma once

#include <QString>

namespace app {

// Charge :/data/recipe_library.json (décompression transparente via QFile).
bool loadRecipeLibraryFromResource();

} // namespace app
