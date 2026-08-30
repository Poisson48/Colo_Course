#pragma once

#include <QString>

namespace core {

// Clé de comparaison : trim, minuscules, sans accents.
QString normalizeIngredientKey(const QString &s);

// Clé de correspondance pour détecter les doublons (alias + pluriels courants).
QString ingredientMatchKey(const QString &s);

// Nom d'affichage préféré pour un ingrédient (ex. « œufs », « oignons »).
QString canonicalIngredientName(const QString &s);

// Normalise un nom saisi à la main : alias canoniques, sinon minuscules uniformes
// (Lait, LAIT, lait → lait).
QString normalizeManualIngredientName(const QString &s);

// Extrait l'ingrédient de base en retirant préparations et découpes
// (« beurre fondu » → « beurre », « courgettes en tranches » → « courgettes »).
QString baseIngredientName(const QString &s);

// Ingrédient à ne pas copier d'une recette vers une liste de courses (ex. eau).
bool isShoppingListExcludedIngredient(const QString &s);

} // namespace core
