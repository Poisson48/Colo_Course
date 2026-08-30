#pragma once

#include <QObject>
#include <functional>

namespace app {

// Charge :/data/recipe_library.json (bloquant — tests uniquement).
bool loadRecipeLibraryFromResource();

// Lecture + parse en arrière-plan ; onDone appelé sur le thread UI.
void loadRecipeLibraryFromResourceAsync(QObject *context,
                                        std::function<void(bool ok)> onDone);

} // namespace app
