#pragma once

#include <QColor>
#include <QObject>

namespace app {

// Palette de l'app (thème sombre), exposée à QML comme propriété de contexte `Theme`.
//
// Volontairement en C++ et non en singleton QML : un `pragma Singleton` dépend de la
// résolution du qmldir du module, qui s'est révélée différente entre le Qt du desktop
// et celui d'Android — Theme y arrivait comme type ordinaire, toutes les couleurs et
// les espacements passaient à `undefined`, et les dialogues se disloquaient. Une
// propriété de contexte ne peut pas échouer ainsi.
class Theme : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QColor background  MEMBER m_background  CONSTANT)
    Q_PROPERTY(QColor surface     MEMBER m_surface     CONSTANT)
    Q_PROPERTY(QColor surfaceHigh MEMBER m_surfaceHigh CONSTANT)
    Q_PROPERTY(QColor outline     MEMBER m_outline     CONSTANT)
    Q_PROPERTY(QColor accent      MEMBER m_accent      CONSTANT)
    Q_PROPERTY(QColor accentDim   MEMBER m_accentDim   CONSTANT)
    Q_PROPERTY(QColor text        MEMBER m_text        CONSTANT)
    Q_PROPERTY(QColor textDim     MEMBER m_textDim     CONSTANT)
    Q_PROPERTY(QColor danger      MEMBER m_danger      CONSTANT)
    Q_PROPERTY(QColor warning     MEMBER m_warning     CONSTANT)

    // Rythme vertical et cibles tactiles : 48 est le minimum tactile Android.
    Q_PROPERTY(int radius      MEMBER m_radius      CONSTANT)
    Q_PROPERTY(int touchTarget MEMBER m_touchTarget CONSTANT)
    Q_PROPERTY(int gap         MEMBER m_gap         CONSTANT)
    Q_PROPERTY(int pad         MEMBER m_pad         CONSTANT)

public:
    using QObject::QObject;

private:
    QColor m_background  { QStringLiteral("#101311") };
    QColor m_surface     { QStringLiteral("#1A1F1C") };
    QColor m_surfaceHigh { QStringLiteral("#232A25") };
    QColor m_outline     { QStringLiteral("#2E3630") };
    QColor m_accent      { QStringLiteral("#6FCF7A") };
    QColor m_accentDim   { QStringLiteral("#2E7D32") };
    QColor m_text        { QStringLiteral("#ECF0ED") };
    QColor m_textDim     { QStringLiteral("#93A099") };
    QColor m_danger      { QStringLiteral("#E5534B") };
    QColor m_warning     { QStringLiteral("#E9B44C") };

    int m_radius      = 16;
    int m_touchTarget = 48;
    int m_gap         = 12;
    int m_pad         = 16;
};

} // namespace app
