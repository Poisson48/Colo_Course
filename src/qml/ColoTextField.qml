import QtQuick
import QtQuick.Controls
import ColoCourse

// Champ de saisie sombre : fond plein plutôt que soulignement, pour rester lisible
// et offrir une cible tactile pleine hauteur.
TextField {
    id: field

    implicitHeight: 52
    leftPadding: 14
    rightPadding: 14
    color: Theme.text
    placeholderTextColor: Theme.textDim
    font.pixelSize: 16
    selectByMouse: true

    background: Rectangle {
        radius: 12
        color: Theme.surfaceHigh
        border.width: field.activeFocus ? 2 : 1
        border.color: field.activeFocus ? Theme.accent : Theme.outline
    }
}
