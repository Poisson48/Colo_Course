#pragma once

#include <QString>

namespace core {

// Parse « 4 personnes », « 6 », « 4 » → entier > 0, ou 0 si illisible.
int parseServingsCount(const QString &text);

// Met à l'échelle une quantité libre (« 700 g », « 2 », « 1/2 sachet »).
// factor = cible / base (ex. 6 personnes pour une recette de 4 → 1.5).
QString scaleQuantity(const QString &qty, double factor);

// Fusionne deux quantités libres. Additionne si unités compatibles, sinon « a + b ».
QString mergeQuantities(const QString &existing, const QString &additional);

} // namespace core
