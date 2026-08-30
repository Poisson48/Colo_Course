#include "ingredient_norm.h"

#include <QHash>

namespace core {

static QString deaccent(const QString &s) {
    QString out;
    for (const QChar c : s.normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing)
            out += c;
    }
    // Ligatures françaises non décomposées par NFD.
    out.replace(QChar(0x0153), QStringLiteral("oe"));  // œ
    out.replace(QChar(0x0152), QStringLiteral("Oe"));  // Œ → Oe, puis toLower()
    return out;
}

QString normalizeIngredientKey(const QString &s) {
    return deaccent(s.trimmed().toLower());
}

// Alias → clé de correspondance. Réservé aux variantes évidentes (singulier/pluriel,
// synonymes courts) — pas aux qualificatifs (« huile de tournesol » reste distinct).
static const QHash<QString, QString> &matchAliases() {
    static const QHash<QString, QString> kAliases = {
        {QStringLiteral("oeuf"), QStringLiteral("oeufs")},
        {QStringLiteral("oeufs"), QStringLiteral("oeufs")},
        {QStringLiteral("oignon"), QStringLiteral("oignons")},
        {QStringLiteral("oignons"), QStringLiteral("oignons")},
        {QStringLiteral("gousse d ail"), QStringLiteral("ail")},
        {QStringLiteral("gousses d ail"), QStringLiteral("ail")},
        {QStringLiteral("gousse d'ail"), QStringLiteral("ail")},
        {QStringLiteral("gousses d'ail"), QStringLiteral("ail")},
        {QStringLiteral("ail"), QStringLiteral("ail")},
        {QStringLiteral("tomate"), QStringLiteral("tomates")},
        {QStringLiteral("tomates"), QStringLiteral("tomates")},
        {QStringLiteral("pomme de terre"), QStringLiteral("pommes de terre")},
        {QStringLiteral("pommes de terre"), QStringLiteral("pommes de terre")},
        {QStringLiteral("patate"), QStringLiteral("pommes de terre")},
        {QStringLiteral("patates"), QStringLiteral("pommes de terre")},
        {QStringLiteral("carotte"), QStringLiteral("carottes")},
        {QStringLiteral("carottes"), QStringLiteral("carottes")},
        {QStringLiteral("courgette"), QStringLiteral("courgettes")},
        {QStringLiteral("courgettes"), QStringLiteral("courgettes")},
        {QStringLiteral("poivron"), QStringLiteral("poivrons")},
        {QStringLiteral("poivrons"), QStringLiteral("poivrons")},
        {QStringLiteral("champignon"), QStringLiteral("champignons")},
        {QStringLiteral("champignons"), QStringLiteral("champignons")},
        {QStringLiteral("citron"), QStringLiteral("citrons")},
        {QStringLiteral("citrons"), QStringLiteral("citrons")},
        {QStringLiteral("pate"), QStringLiteral("pates")},
        {QStringLiteral("pâte"), QStringLiteral("pates")},
        {QStringLiteral("pates"), QStringLiteral("pates")},
        {QStringLiteral("pâtes"), QStringLiteral("pates")},
        {QStringLiteral("epinard"), QStringLiteral("epinards")},
        {QStringLiteral("épinard"), QStringLiteral("epinards")},
        {QStringLiteral("epinards"), QStringLiteral("epinards")},
        {QStringLiteral("épinards"), QStringLiteral("epinards")},
        {QStringLiteral("yaourt"), QStringLiteral("yaourts")},
        {QStringLiteral("yaourts"), QStringLiteral("yaourts")},
        {QStringLiteral("banane"), QStringLiteral("bananes")},
        {QStringLiteral("bananes"), QStringLiteral("bananes")},
    };
    return kAliases;
}

static const QHash<QString, QString> &canonicalDisplayNames() {
    static const QHash<QString, QString> kNames = {
        {QStringLiteral("oeufs"), QStringLiteral("œufs")},
        {QStringLiteral("oignons"), QStringLiteral("oignons")},
        {QStringLiteral("ail"), QStringLiteral("ail")},
        {QStringLiteral("tomates"), QStringLiteral("tomates")},
        {QStringLiteral("pommes de terre"), QStringLiteral("pommes de terre")},
        {QStringLiteral("carottes"), QStringLiteral("carottes")},
        {QStringLiteral("courgettes"), QStringLiteral("courgettes")},
        {QStringLiteral("poivrons"), QStringLiteral("poivrons")},
        {QStringLiteral("champignons"), QStringLiteral("champignons")},
        {QStringLiteral("citrons"), QStringLiteral("citrons")},
        {QStringLiteral("pates"), QStringLiteral("pâtes")},
        {QStringLiteral("epinards"), QStringLiteral("épinards")},
        {QStringLiteral("yaourts"), QStringLiteral("yaourts")},
        {QStringLiteral("bananes"), QStringLiteral("bananes")},
    };
    return kNames;
}

QString ingredientMatchKey(const QString &s) {
    const QString key = normalizeIngredientKey(s);
    if (key.isEmpty())
        return key;
    const auto &aliases = matchAliases();
    const auto it = aliases.constFind(key);
    if (it != aliases.constEnd())
        return it.value();
    return key;
}

QString canonicalIngredientName(const QString &s) {
    const QString trimmed = s.trimmed();
    if (trimmed.isEmpty())
        return trimmed;

    const QString normKey = normalizeIngredientKey(trimmed);
    const auto &aliases = matchAliases();
    const auto aliasIt = aliases.constFind(normKey);
    if (aliasIt == aliases.constEnd())
        return trimmed;

    const QString matchKey = aliasIt.value();
    // Renommer seulement la forme courte (singulier) vers le libellé pluriel canonique.
    if (normKey == matchKey)
        return trimmed;

    const auto &names = canonicalDisplayNames();
    const auto nameIt = names.constFind(matchKey);
    if (nameIt != names.constEnd())
        return nameIt.value();
    return trimmed;
}

QString normalizeManualIngredientName(const QString &s) {
    const QString trimmed = s.trimmed();
    if (trimmed.isEmpty())
        return trimmed;

    const QString normKey = normalizeIngredientKey(trimmed);
    const auto &aliases = matchAliases();
    const auto aliasIt = aliases.constFind(normKey);
    if (aliasIt != aliases.constEnd() && normKey != aliasIt.value())
        return canonicalIngredientName(trimmed);

    return trimmed.toLower();
}

} // namespace core
