#include "recipe_scale.h"

#include <cmath>

#include <QRegularExpression>

namespace core {

int parseServingsCount(const QString &text) {
    const QString s = text.trimmed();
    if (s.isEmpty())
        return 0;
    static const QRegularExpression re(R"(^(\d+))");
    const auto m = re.match(s);
    if (!m.hasMatch())
        return 0;
    const int n = m.captured(1).toInt();
    return n > 0 ? n : 0;
}

static double parseNumberToken(const QString &token) {
    const QString t = token.trimmed();
    if (t.isEmpty())
        return -1.0;

    const int slash = t.indexOf('/');
    if (slash > 0) {
        const double a = t.left(slash).trimmed().toDouble();
        const double b = t.mid(slash + 1).trimmed().toDouble();
        if (b > 0.0)
            return a / b;
        return -1.0;
    }

    QString norm = t;
    norm.replace(',', '.');
    bool ok = false;
    const double v = norm.toDouble(&ok);
    return ok ? v : -1.0;
}

static QString formatNumber(double value, bool integerPreferred) {
    if (integerPreferred || std::fabs(value - std::round(value)) < 0.05)
        return QString::number(std::max(1.0, std::round(value)));

    const double rounded = std::round(value * 10.0) / 10.0;
    QString out = QString::number(rounded, 'f', 1);
    if (out.endsWith(QLatin1String(".0")))
        out.chop(2);
    return out;
}

QString scaleQuantity(const QString &qty, double factor) {
    if (qty.trimmed().isEmpty() || factor <= 0.0 || std::fabs(factor - 1.0) < 1e-6)
        return qty.trimmed();

    static const QRegularExpression re(
        R"(^(\d+(?:[.,]\d+)?|\d+\s*/\s*\d+)\s*(.*)$)");
    const auto m = re.match(qty.trimmed());
    if (!m.hasMatch())
        return qty.trimmed();

    const double base = parseNumberToken(m.captured(1));
    if (base < 0.0)
        return qty.trimmed();

    const QString rest = m.captured(2).trimmed();
    const double scaled = base * factor;

    static const QRegularExpression unitRe(
        R"(^(g|kg|mg|ml|cl|dl|l|litres?)$)",
        QRegularExpression::CaseInsensitiveOption);
    const bool massOrVolume = rest.isEmpty() || unitRe.match(rest).hasMatch();

    const QString num = formatNumber(scaled, massOrVolume || rest.isEmpty());
    if (rest.isEmpty())
        return num;
    return num + QLatin1Char(' ') + rest;
}

} // namespace core
