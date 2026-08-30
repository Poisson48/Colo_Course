#include "recipe_library_loader.h"

#include "../core/recipe_library.h"

#include <QFile>
#include <QFutureWatcher>
#include <QtConcurrent>

namespace app {

bool loadRecipeLibraryFromResource() {
    QFile f(QStringLiteral(":/data/recipe_library.json"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return core::RecipeLibrary::loadFromJson(f.readAll());
}

void loadRecipeLibraryFromResourceAsync(QObject *context,
                                        std::function<void(bool ok)> onDone)
{
    auto *watcher = new QFutureWatcher<bool>(context);
    QObject::connect(watcher, &QFutureWatcher<bool>::finished, context,
                     [watcher, onDone]() {
                         const bool ok = watcher->result();
                         if (onDone)
                             onDone(ok);
                         watcher->deleteLater();
                     });

    watcher->setFuture(QtConcurrent::run([]() -> bool {
        QFile f(QStringLiteral(":/data/recipe_library.json"));
        if (!f.open(QIODevice::ReadOnly))
            return false;
        return core::RecipeLibrary::loadFromJson(f.readAll());
    }));
}

} // namespace app
