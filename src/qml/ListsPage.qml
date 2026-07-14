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

        // Qui je suis, en clair. Un « ⋮ » n'apprend à personne que son nom se change
        // là-dessous — et c'est ce nom que les autres voient sur chaque modification.
        ItemDelegate {
            id: profileButton
            width: Math.min(120, 44 + nameLabel.implicitWidth)
            height: Theme.touchTarget
            padding: 0

            background: Rectangle {
                radius: height / 2
                color: profileButton.pressed ? Theme.surfaceHigh : "transparent"
                border.color: Theme.outline
                border.width: 1
            }

            contentItem: Row {
                spacing: 6
                leftPadding: 6
                rightPadding: 10

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28; height: 28; radius: 14
                    color: Theme.accent

                    Label {
                        anchors.centerIn: parent
                        text: AppController.displayName.length > 0
                              ? AppController.displayName.charAt(0).toUpperCase() : "?"
                        color: Theme.onAccent
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                }

                Label {
                    id: nameLabel
                    anchors.verticalCenter: parent.verticalCenter
                    text: AppController.displayName
                    color: Theme.text
                    font.pixelSize: 14
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, 74)
                }
            }

            onClicked: nameDialog.open()
        }
    }

    // Pas encore de nom choisi (installation neuve, ou mise à jour depuis une version
    // qui ne le demandait pas) : on le demande avant tout le reste.
    Component.onCompleted: {
        if (!AppController.hasDisplayName)
            nameDialog.open()
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

                    // Reste à acheter, ou une coche quand tout est pris. La coche est
                    // dessinée : « ✓ » n'est pas garanti dans les polices d'Android.
                    Label {
                        anchors.centerIn: parent
                        visible: model.count > 0
                        text: model.count
                        color: "#FFFFFF"   // accentDim est foncé dans les deux thèmes
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }

                    Icon {
                        anchors.centerIn: parent
                        visible: model.count === 0
                        name: "check"
                        color: Theme.accent
                        size: 22
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
                    contentItem: Icon {
                        name: "menu"
                        color: Theme.textDim
                        size: 18
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
            color: Theme.onAccent
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
            text: "Renommer la liste"
            onTriggered: renameDialog.openFor(cardMenu.listId, cardMenu.listName)
        }
        MenuItem {
            text: "Dupliquer la liste"
            onTriggered: duplicateDialog.openFor(cardMenu.listId, cardMenu.listName)
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
            hint: "Courses de la semaine"
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
                color: Theme.onAccent
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
            hint: "colocourse://join/1/…"
            onAccepted: if (joinDialog.acceptEnabled) joinDialog.accept()
        }

        onOpened: { uriField.text = ""; uriField.forceActiveFocus() }
        onAccepted: {
            if (!AppController.joinList(uriField.text.trim()))
                AppController.toast("Lien d'invitation invalide")
        }
    }

    ColoDialog {
        id: renameDialog
        title: "Renommer la liste"
        acceptText: "Renommer"
        acceptEnabled: renameField.text.trim().length > 0

        property string listId: ""

        function openFor(id, currentName) {
            listId = id
            open()
            renameField.text = currentName
            renameField.forceActiveFocus()
            renameField.selectAll()
        }

        ColoTextField {
            id: renameField
            Layout.fillWidth: true
            hint: "Nom de la liste"
            onAccepted: if (renameDialog.acceptEnabled) renameDialog.accept()
        }

        onAccepted: AppController.renameList(renameDialog.listId, renameField.text.trim())
    }

    ColoDialog {
        id: duplicateDialog
        title: "Dupliquer la liste"
        acceptText: "Dupliquer"
        acceptEnabled: duplicateField.text.trim().length > 0

        property string listId: ""

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 13
            text: "Une nouvelle liste, avec les mêmes articles, tous à acheter. "
                  + "Elle est indépendante : la partager demande un nouveau lien."
        }

        ColoTextField {
            id: duplicateField
            Layout.fillWidth: true
            hint: "Nom de la copie"
            onAccepted: if (duplicateDialog.acceptEnabled) duplicateDialog.accept()
        }

        function openFor(id, currentName) {
            listId = id
            open()
            duplicateField.text = currentName + " (copie)"
            duplicateField.forceActiveFocus()
            duplicateField.selectAll()
        }

        onAccepted: AppController.duplicateList(duplicateDialog.listId,
                                                duplicateField.text.trim())
    }

    ColoDialog {
        id: nameDialog
        title: AppController.hasDisplayName ? "Mon nom" : "Comment vous appelez-vous ?"
        acceptText: "Enregistrer"
        acceptEnabled: displayNameField.text.trim().length > 0

        // Au premier lancement, ce nom n'a pas encore été choisi : tant qu'il vaut
        // « Moi », les autres participants reçoivent « 2 articles modifiés par Moi ».
        // On le demande donc d'emblée, et on ne laisse pas esquiver d'un appui à côté.
        closePolicy: AppController.hasDisplayName
                     ? (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
                     : Popup.NoAutoClose

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
            hint: "Votre prénom"
            onAccepted: if (nameDialog.acceptEnabled) nameDialog.accept()
        }

        onOpened: {
            // Premier lancement : champ vide plutôt que « Moi » prérempli, qu'on
            // validerait sans y penser — et tout le monde s'appellerait « Moi ».
            displayNameField.text = AppController.hasDisplayName
                                    ? AppController.displayName : ""
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
