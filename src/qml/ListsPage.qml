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

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4

                    ColumnLayout {
                        Layout.fillWidth: true
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

                    // Bouton Partager (QR)
                    Button {
                        text: "⬡"
                        flat: true
                        font.pixelSize: 18
                        onClicked: {
                            shareDialog.currentListId = model.listId
                            shareDialog.currentUri = AppController.joinUri(model.listId)
                            shareDialog.open()
                        }
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

        // Bouton Rejoindre
        Button {
            Layout.fillWidth: true
            text: "Rejoindre une liste"
            onClicked: joinDialog.open()
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

    // Dialog Rejoindre une liste
    Dialog {
        id: joinDialog
        title: "Rejoindre une liste"
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 400)

        ColumnLayout {
            width: parent.width
            spacing: 12

            Text {
                text: "Collez l'URI de partage :"
                font.pixelSize: 14
                Layout.fillWidth: true
            }

            TextField {
                id: joinUriField
                Layout.fillWidth: true
                placeholderText: "colocourse://join/1/..."
                Material.accent: Material.primary
            }

            Text {
                id: joinErrorText
                text: ""
                color: "red"
                font.pixelSize: 12
                visible: text.length > 0
                Layout.fillWidth: true
            }
        }

        standardButtons: Dialog.Ok | Dialog.Cancel

        onAccepted: {
            joinErrorText.text = ""
            if (joinUriField.text.length > 0) {
                var ok = AppController.joinList(joinUriField.text)
                if (!ok) {
                    joinErrorText.text = "URI invalide, veuillez réessayer."
                    joinDialog.open()
                } else {
                    joinUriField.text = ""
                }
            }
        }

        onRejected: {
            joinUriField.text = ""
            joinErrorText.text = ""
        }
    }

    // Dialog Partager (QR + URI)
    Dialog {
        id: shareDialog
        title: "Partager la liste"
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 400)
        modal: true

        property string currentListId: ""
        property string currentUri: ""

        ColumnLayout {
            width: parent.width
            spacing: 12

            Image {
                id: qrImage
                Layout.alignment: Qt.AlignHCenter
                width: 240
                height: 240
                source: shareDialog.currentUri.length > 0
                    ? "image://qr/" + shareDialog.currentUri
                    : ""
                fillMode: Image.PreserveAspectFit
            }

            Text {
                text: "Lien de partage :"
                font.pixelSize: 13
                Layout.fillWidth: true
            }

            TextEdit {
                id: uriText
                text: shareDialog.currentUri
                readOnly: true
                wrapMode: TextEdit.WrapAnywhere
                selectByMouse: true
                font.pixelSize: 11
                Layout.fillWidth: true
                color: Material.foreground
            }
        }

        standardButtons: Dialog.Close
    }
}
