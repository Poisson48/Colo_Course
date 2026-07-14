import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ColoCourse

// Dialogue sombre commun à toute l'app : un titre, du contenu libre, deux boutons.
// `destructive: true` colore l'action de validation en rouge (quitter une liste).
Dialog {
    id: dlg

    property string acceptText: "OK"
    property bool   acceptEnabled: true
    property bool   destructive: false

    default property alias body: content.data

    // Dans l'overlay : le dialogue reste centré même quand le clavier redimensionne
    // la page, et n'est jamais rogné par le StackView.
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - 48, 400)
    // La largeur du dialogue est imposée : sans ce contentWidth, Popup la redemande
    // au contenu, dont les enfants Layout.fillWidth la redemandent au dialogue.
    contentWidth: availableWidth

    modal: true
    focus: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Theme.surface
        radius: 20
        border.color: Theme.outline
        border.width: 1
    }

    header: Label {
        text: dlg.title
        color: Theme.text
        font.pixelSize: 19
        font.weight: Font.DemiBold
        elide: Text.ElideRight
        padding: 20
        bottomPadding: 4
    }

    contentItem: ColumnLayout {
        id: content
        spacing: Theme.gap
    }

    footer: RowLayout {
        spacing: 4

        Item { Layout.fillWidth: true }

        Button {
            flat: true
            text: "Annuler"
            implicitHeight: Theme.touchTarget
            contentItem: Label {
                text: parent.text
                color: Theme.textDim
                font.pixelSize: 15
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: dlg.reject()
        }

        Button {
            flat: true
            text: dlg.acceptText
            enabled: dlg.acceptEnabled
            implicitHeight: Theme.touchTarget
            Layout.rightMargin: 12
            Layout.bottomMargin: 8
            contentItem: Label {
                text: parent.text
                color: !parent.enabled ? Theme.textDim
                                       : (dlg.destructive ? Theme.danger : Theme.accent)
                opacity: parent.enabled ? 1.0 : 0.5
                font.pixelSize: 15
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: dlg.accept()
        }
    }
}
