import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// Choix du rayon. La liste des rayons vient du modèle (C++), dans l'ordre où on
// traverse un magasin — c'est ce même ordre qui trie les sections de la liste.
// « Sans rayon » est une valeur réelle (chaîne vide), pas l'absence de choix.
ComboBox {
    id: box

    // Le rayon choisi, "" pour « Sans rayon ».
    property string aisle: ""

    readonly property var options: ["Sans rayon"].concat(AppController.items.aisleNames)

    implicitHeight: 44
    model: options

    // Assigner `aisle` de l'extérieur (dialogue d'édition) doit bouger la sélection.
    onAisleChanged: {
        const wanted = aisle.length > 0 ? aisle : "Sans rayon"
        const at = options.indexOf(wanted)
        // Rayon inconnu (article venu d'une version plus récente) : ne pas l'écraser
        // en le forçant sur « Sans rayon », le laisser tel quel.
        if (at >= 0 && at !== currentIndex)
            currentIndex = at
    }

    onActivated: function (index) {
        aisle = (index === 0) ? "" : options[index]
    }

    background: Rectangle {
        radius: 10
        color: Theme.surfaceHigh
        border.color: box.activeFocus ? Theme.accent : Theme.outline
        border.width: 1
    }

    contentItem: Label {
        leftPadding: 10
        rightPadding: box.indicator.width
        text: box.displayText
        color: box.aisle.length > 0 ? Theme.text : Theme.textDim
        font.pixelSize: 13
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    popup: Popup {
        y: box.height + 4
        width: Math.max(box.width, 190)
        implicitHeight: Math.min(contentItem.implicitHeight + 16, 320)
        padding: 8

        background: Rectangle {
            radius: 12
            color: Theme.surface
            border.color: Theme.outline
            border.width: 1
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: box.popup.visible ? box.delegateModel : null
            currentIndex: box.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }

    // Pas de `required property` ici : déclarer une propriété requise ferait basculer
    // le délégué en mode « propriétés requises », et Qt cesserait d'injecter le contexte
    // du modèle — `modelData` deviendrait introuvable et la liste des rayons s'afficherait
    // vide. Le contexte suffit largement pour un modèle qui n'est qu'un tableau.
    delegate: ItemDelegate {
        id: option
        width: box.popup.width - 16
        height: 42

        contentItem: Label {
            text: modelData
            color: Theme.text
            font.pixelSize: 14
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: 8
            color: option.hovered || box.currentIndex === index
                   ? Theme.surfaceHigh : "transparent"
        }

        onClicked: {
            box.aisle = (index === 0) ? "" : box.options[index]
            box.currentIndex = index
            box.popup.close()
        }
    }
}
