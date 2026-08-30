#include "ingredient_norm.h"

#include <QHash>
#include <QRegularExpression>

namespace core {

static QString deaccent(const QString &s) {
    QString out;
    for (const QChar c : s.normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing)
            out += c;
    }
    out.replace(QChar(0x0153), QStringLiteral("oe"));
    out.replace(QChar(0x0152), QStringLiteral("Oe"));
    return out;
}

QString normalizeIngredientKey(const QString &s) {
    return deaccent(s.trimmed().toLower());
}

static bool isProtectedCompound(const QString &normKey) {
    static const QStringList kProtected = {
        QStringLiteral("creme fraiche"),
        QStringLiteral("pomme de terre"),
        QStringLiteral("pommes de terre"),
        QStringLiteral("huile d'olive"),
        QStringLiteral("huile d olive"),
        QStringLiteral("beurre sale"),
        QStringLiteral("beurre doux"),
        QStringLiteral("fromage frais"),
        QStringLiteral("levure chimique"),
        QStringLiteral("levure de boulanger"),
        QStringLiteral("pate feuilletee"),
        QStringLiteral("pate brisee"),
        QStringLiteral("pate a pizza"),
        QStringLiteral("sauce tomate"),
        QStringLiteral("lait de coco"),
        QStringLiteral("lait de vache"),
        QStringLiteral("boeuf hache"),
        QStringLiteral("viande hachee"),
    };
    if (kProtected.contains(normKey))
        return true;
    for (const QString &p : kProtected) {
        if (normKey.startsWith(p + QLatin1Char(' ')))
            return true;
    }
    return false;
}

static bool tryStripSuffix(QString &name, const QRegularExpression &re) {
    const auto m = re.match(name);
    if (!m.hasMatch())
        return false;
    name = name.left(m.capturedStart()).trimmed();
    return true;
}

static QString stripOnePrepSuffix(QString name) {
    name = name.trimmed();
    if (name.isEmpty())
        return name;

    static const QList<QRegularExpression> kSuffixes = {
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en petits morceaux$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en petits dés$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en petits cubes$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en fines lamelles$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en fines tranches$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en tranches$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en tranche$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en lamelles$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en lamelle$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en rondelles$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en rondelle$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en morceaux$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en morceau$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en dés$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en cubes$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en cube$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en lanières$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en copeaux$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)coupé en [^,;]+$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)coupés en [^,;]+$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)coupée en [^,;]+$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)coupées en [^,;]+$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)finement émincé$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)finement émincée$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)émincé$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)émincée$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)pelé et haché$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)pelée et hachée$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)haché finement$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)hachée finement$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)fondu$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)fondue$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)fondus$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)en pommade$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)tiédi$)"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:\s*,\s*|\s+)ramolli$)"),
                           QRegularExpression::CaseInsensitiveOption),
    };

    for (const auto &re : kSuffixes) {
        if (tryStripSuffix(name, re))
            return name;
    }

    static const QRegularExpression kHerbFreshRe(
        QStringLiteral(R"(^((?:basilic|persil|coriandre|menthe|cerfeuil|ciboulette)"
                       R"(?:\s+\w+){0,2})\s+fra[iî]che?$)"),
        QRegularExpression::CaseInsensitiveOption);
    auto m = kHerbFreshRe.match(name);
    if (m.hasMatch())
        return m.captured(1).trimmed();

    static const QRegularExpression kGratedRe(
        QStringLiteral(R"(^(.+?)\s+r[aâ]p[eé](?:e|es|ée|ées)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    m = kGratedRe.match(name);
    if (m.hasMatch()) {
        const QString base = m.captured(1).trimmed();
        const QString baseKey = normalizeIngredientKey(base);
        if (baseKey.contains(QStringLiteral("fromage"))
            || baseKey.contains(QStringLiteral("parmesan"))
            || baseKey.contains(QStringLiteral("gruyere"))
            || baseKey.contains(QStringLiteral("cheddar"))
            || baseKey.contains(QStringLiteral("mozzarella"))
            || baseKey.contains(QStringLiteral("emmental"))
            || baseKey.contains(QStringLiteral("comte")))
            return base;
    }

    return name;
}

QString baseIngredientName(const QString &s) {
    const QString trimmed = s.trimmed();
    if (trimmed.isEmpty())
        return trimmed;

    if (isProtectedCompound(normalizeIngredientKey(trimmed)))
        return trimmed;

    QString result = trimmed;
    bool stripped = false;
    for (int i = 0; i < 4; ++i) {
        const QString next = stripOnePrepSuffix(result);
        if (next.isEmpty() || next.compare(result, Qt::CaseInsensitive) == 0)
            break;
        stripped = true;
        result = next;
    }

    result = result.trimmed();
    if (result.isEmpty())
        return trimmed;

    if (stripped)
        return result.toLower();

    return result;
}

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
        {QStringLiteral("beurre"), QStringLiteral("beurre")},
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
        {QStringLiteral("beurre"), QStringLiteral("beurre")},
    };
    return kNames;
}

QString ingredientMatchKey(const QString &s) {
    const QString key = normalizeIngredientKey(baseIngredientName(s));
    if (key.isEmpty())
        return key;
    const auto &aliases = matchAliases();
    const auto it = aliases.constFind(key);
    if (it != aliases.constEnd())
        return it.value();
    return key;
}

QString canonicalIngredientName(const QString &s) {
    const QString base = baseIngredientName(s);
    if (base.isEmpty())
        return base;

    const QString normKey = normalizeIngredientKey(base);
    const auto &aliases = matchAliases();
    const auto aliasIt = aliases.constFind(normKey);
    if (aliasIt == aliases.constEnd())
        return base;

    const QString matchKey = aliasIt.value();
    if (normKey == matchKey)
        return base;

    const auto &names = canonicalDisplayNames();
    const auto nameIt = names.constFind(matchKey);
    if (nameIt != names.constEnd())
        return nameIt.value();
    return base;
}

QString normalizeManualIngredientName(const QString &s) {
    const QString canon = canonicalIngredientName(s);
    if (canon.isEmpty())
        return canon;

    const QString normKey = normalizeIngredientKey(canon);
    const auto &aliases = matchAliases();
    const auto aliasIt = aliases.constFind(normKey);
    if (aliasIt != aliases.constEnd()) {
        const QString matchKey = aliasIt.value();
        if (normKey != matchKey) {
            const auto &names = canonicalDisplayNames();
            const auto nameIt = names.constFind(matchKey);
            if (nameIt != names.constEnd())
                return nameIt.value();
        }
    }
    return canon.toLower();
}

} // namespace core
