import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root

    // Signaux
    signal listSelected(string listId, string listName)
    signal createListRequested(string name)
    signal joinListRequested(string key)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        // Liste des listes (branchée sur AppController.lists)
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: AppController.lists
            spacing: 8

            delegate: ItemDelegate {
                id: delegate
                width: listView.width
                implicitHeight: 64

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4

                    Text {
                        text: model.name
                        font.pixelSize: 16
                        font.weight: Font.Medium
                        Layout.fillWidth: true
                    }

                    Text {
                        text: model.count + " article" + (model.count !== 1 ? "s" : "")
                        font.pixelSize: 13
                        color: Material.foreground
                        opacity: 0.7
                    }
                }

                onClicked: {
                    root.listSelected(model.listId, model.name)
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                color: "transparent"
                border.color: Material.foreground
                border.width: 1
                opacity: 0.1
            }
        }

        // Champ "Rejoindre une liste"
        TextField {
            id: joinField
            Layout.fillWidth: true
            placeholderText: "Rejoindre une liste (clé ou QR)"
            Material.accent: Material.primary

            onEditingFinished: {
                if (text.length > 0) {
                    root.joinListRequested(text)
                    text = ""
                }
            }
        }
    }

    // Bouton flottant +
    RoundButton {
        id: fabButton
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        width: 56
        height: 56

        text: "+"
        font.pixelSize: 24
        Material.accent: Material.primary
        Material.foreground: "white"

        onClicked: {
            createDialog.open()
        }
    }

    // Dialog de création de liste
    Dialog {
        id: createDialog
        title: "Créer une nouvelle liste"
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 400)

        ColumnLayout {
            width: parent.width
            spacing: 12

            TextField {
                id: newListName
                Layout.fillWidth: true
                placeholderText: "Nom de la liste"
                Material.accent: Material.primary
            }
        }

        standardButtons: Dialog.Ok | Dialog.Cancel

        onAccepted: {
            if (newListName.text.length > 0) {
                AppController.createList(newListName.text)
                newListName.text = ""
            }
        }

        onRejected: {
            newListName.text = ""
        }
    }
}
