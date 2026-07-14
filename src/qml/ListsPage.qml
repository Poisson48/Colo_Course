import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root

    readonly property string pageTitle: "Mes listes"

    // Boutons affichés dans la barre supérieure par Main.qml.
    property Component actions: Row {
        spacing: 0

        ToolButton {
            width: 96
            height: Theme.touchTarget
            contentItem: Label {
                text: "Rejoindre"
                color: Theme.accent
                font.pixelSize: 14
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: joinDialog.open()
        }

        ToolButton {
            width: Theme.touchTarget
            height: Theme.touchTarget
            contentItem: Label {
                text: "⋮"
                color: Theme.text
                font.pixelSize: 20
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: nameDialog.open()
        }
    }

    ListView {
        id: listView
        anchors.fill: parent
        anchors.margins: Theme.gap
        topMargin: Theme.gap
        bottomMargin: 96          // laisse respirer le bouton flottant
        spacing: Theme.gap
        clip: true
        model: AppController.lists

        delegate: ItemDelegate {
            id: card
            width: listView.width - 2 * Theme.gap
            height: 84
            padding: 0

            background: Rectangle {
                radius: Theme.radius
                color: card.pressed ? Theme.surfaceHigh : Theme.surface
                border.color: Theme.outline
                border.width: 1
            }

            contentItem: RowLayout {
                spacing: Theme.gap
                anchors.margins: Theme.pad

                // Pastille du nombre d'articles restants.
                Rectangle {
                    Layout.leftMargin: Theme.pad
                    Layout.alignment: Qt.AlignVCenter
                    width: 44; height: 44
                    radius: 22
                    color: model.count > 0 ? Theme.accentDim : Theme.surfaceHigh

                    Label {
                        anchors.centerIn: parent
                        text: model.count > 0 ? model.count : "✓"
                        color: model.count > 0 ? "#FFFFFF" : Theme.accent
                        font.pixelSize: model.count > 0 ? 17 : 18
                        font.weight: Font.DemiBold
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 3

                    Label {
                        Layout.fillWidth: true
                        text: model.name
                        color: Theme.text
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        color: Theme.textDim
                        font.pixelSize: 13
                        elide: Text.ElideRight
                        text: model.total === 0
                              ? "Liste vide"
                              : (model.count === 0
                                 ? "Tout est dans le panier"
                                 : model.count + " à acheter sur " + model.total)
                    }
                }

                ToolButton {
                    Layout.rightMargin: 4
                    Layout.alignment: Qt.AlignVCenter
                    width: Theme.touchTarget
                    height: Theme.touchTarget
                    contentItem: Label {
                        text: "⋮"
                        color: Theme.textDim
                        font.pixelSize: 20
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        cardMenu.listId = model.listId
                        cardMenu.listName = model.name
                        cardMenu.popup()
                    }
                }
            }

            onClicked: AppController.openList(model.listId)
        }
    }

    // État vide : c'est le premier écran d'un nouvel utilisateur, il doit dire quoi faire.
    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 80
        spacing: Theme.gap
        visible: listView.count === 0

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width: 88; height: 88; radius: 44
            color: Theme.surface
            border.color: Theme.outline
            border.width: 1

            Label {
                anchors.centerIn: parent
                text: "+"
                color: Theme.accent
                font.pixelSize: 40
            }
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: "Aucune liste"
            color: Theme.text
            font.pixelSize: 19
            font.weight: Font.DemiBold
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: "Créez une liste, puis partagez-la pour que vos courses restent synchronisées."
            color: Theme.textDim
            font.pixelSize: 14
        }
    }

    // Bouton flottant : créer une liste.
    RoundButton {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        width: 60; height: 60
        Material.background: Theme.accent
        Material.elevation: 6

        contentItem: Label {
            text: "+"
            color: "#0C1F10"
            font.pixelSize: 28
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        onClicked: createDialog.open()
    }

    Menu {
        id: cardMenu
        property string listId: ""
        property string listName: ""

        MenuItem {
            text: "Partager la liste"
            onTriggered: shareSheet.openFor(cardMenu.listId, cardMenu.listName)
        }
        MenuItem {
            text: "Quitter la liste"
            onTriggered: {
                leaveDialog.listId = cardMenu.listId
                leaveDialog.listName = cardMenu.listName
                leaveDialog.open()
            }
        }
    }

    ShareSheet { id: shareSheet }

    // --- Dialogues ---

    ColoDialog {
        id: createDialog
        title: "Nouvelle liste"
        acceptText: "Créer"
        acceptEnabled: nameField.text.trim().length > 0

        ColoTextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: "Courses de la semaine"
            onAccepted: if (createDialog.acceptEnabled) createDialog.accept()
        }

        onOpened: { nameField.text = ""; nameField.forceActiveFocus() }
        onAccepted: AppController.createList(nameField.text.trim())
    }

    // Scanner : plein écran, par-dessus tout le reste. Chargé seulement à l'ouverture —
    // instancié d'emblée, il imposerait QtMultimedia au chargement de cette page, et
    // un desktop sans ce module perdrait l'écran des listes entier.
    Popup {
        id: scanPopup
        parent: Overlay.overlay
        width: parent.width
        height: parent.height
        padding: 0
        modal: true
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: "black" }

        Loader {
            id: scanLoader
            anchors.fill: parent
            source: scanPopup.opened ? "ScanPage.qml" : ""

            onStatusChanged: {
                if (status === Loader.Error) {
                    scanPopup.close()
                    AppController.toast("Caméra indisponible sur cet appareil")
                }
            }
        }

        Connections {
            target: scanLoader.item
            ignoreUnknownSignals: true
            function onJoined() { scanPopup.close() }
            function onCloseRequested() { scanPopup.close() }
        }
    }

    ColoDialog {
        id: joinDialog
        title: "Rejoindre une liste"
        acceptText: "Rejoindre"
        acceptEnabled: uriField.text.trim().length > 0

        Button {
            Layout.fillWidth: true
            implicitHeight: 52
            text: "Scanner le QR code"
            background: Rectangle {
                radius: 12
                color: parent.pressed ? Theme.accentDim : Theme.accent
            }
            contentItem: Label {
                text: parent.text
                color: "#0C1F10"
                font.pixelSize: 15
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: { joinDialog.close(); scanPopup.open() }
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: "ou collez le lien reçu"
            color: Theme.textDim
            font.pixelSize: 13
        }

        ColoTextField {
            id: uriField
            Layout.fillWidth: true
            placeholderText: "colocourse://join/1/…"
            onAccepted: if (joinDialog.acceptEnabled) joinDialog.accept()
        }

        onOpened: { uriField.text = ""; uriField.forceActiveFocus() }
        onAccepted: {
            if (!AppController.joinList(uriField.text.trim()))
                AppController.toast("Lien d'invitation invalide")
        }
    }

    ColoDialog {
        id: nameDialog
        title: "Mon nom"
        acceptText: "Enregistrer"
        acceptEnabled: displayNameField.text.trim().length > 0

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Le nom que voient les autres participants : « 3 articles ajoutés par… »"
            color: Theme.textDim
            font.pixelSize: 13
        }

        ColoTextField {
            id: displayNameField
            Layout.fillWidth: true
            placeholderText: "Votre prénom"
            onAccepted: if (nameDialog.acceptEnabled) nameDialog.accept()
        }

        onOpened: {
            displayNameField.text = AppController.displayName
            displayNameField.forceActiveFocus()
            displayNameField.selectAll()
        }
        onAccepted: AppController.displayName = displayNameField.text.trim()
    }

    ColoDialog {
        id: leaveDialog
        title: "Quitter la liste ?"
        acceptText: "Quitter"
        destructive: true

        property string listId: ""
        property string listName: ""

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 14
            text: "« " + leaveDialog.listName + " » sera effacée de cet appareil. "
                  + "Les autres participants la gardent, et vous pourrez la rejoindre à nouveau avec le lien."
        }

        onAccepted: AppController.leaveList(leaveDialog.listId)
    }
}
