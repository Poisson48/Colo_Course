import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root

    readonly property string pageTitle: "Recettes"

    property Component actions: Row {
        spacing: 0

        ToolButton {
            width: 96
            height: Theme.touchTarget
            contentItem: Label {
                text: "Créer"
                color: Theme.accent
                font.pixelSize: 14
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: createDialog.open()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            Layout.fillWidth: true
            Layout.margins: Theme.gap
            visible: recipesView.count === 0
            text: "Aucune recette. Créez-en une, ou rejoignez-en une via un lien partagé."
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 15
        }

        ListView {
            id: recipesView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: AppController.recipes
            topMargin: Theme.gap
            bottomMargin: Theme.gap
            spacing: 8
            leftMargin: Theme.gap
            rightMargin: Theme.gap

            delegate: ItemDelegate {
                id: card
                required property string listId
                required property string name
                required property int total
                required property string members
                required property int memberCount

                width: recipesView.width - recipesView.leftMargin - recipesView.rightMargin
                height: 72

                background: Rectangle {
                    radius: 12
                    color: card.pressed ? Theme.surfaceHigh : Theme.surface
                    border.color: Theme.outline
                    border.width: 1
                }

                contentItem: ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 2

                    Label {
                        Layout.fillWidth: true
                        text: card.name
                        color: Theme.text
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: {
                            const parts = []
                            parts.push(card.total + (card.total > 1
                                        ? " ingrédients" : " ingrédient"))
                            if (card.memberCount > 0)
                                parts.push("partagée avec " + card.members)
                            return parts.join(" · ")
                        }
                        color: Theme.textDim
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }

                onClicked: AppController.openList(card.listId)
                onPressAndHold: recipeMenu.openFor(card.listId, card.name)
            }
        }
    }

    ColoMenu {
        id: recipeMenu
        property string listId: ""
        property string listName: ""

        function openFor(id, name) {
            listId = id
            listName = name
            popup()
        }

        MenuItem {
            text: "Partager"
            onTriggered: shareSheet.openFor(recipeMenu.listId, recipeMenu.listName)
        }
        MenuItem {
            text: "Ajouter à une liste…"
            onTriggered: addToListPicker.openFor(recipeMenu.listId, recipeMenu.listName)
        }
        MenuItem {
            text: "Supprimer de cet appareil"
            onTriggered: leaveDialog.openFor(recipeMenu.listId, recipeMenu.listName)
        }
    }

    ShareSheet { id: shareSheet }

    ColoDialog {
        id: createDialog
        title: "Nouvelle recette"
        acceptText: "Créer"
        acceptEnabled: recipeNameField.text.trim().length > 0

        ColoTextField {
            id: recipeNameField
            Layout.fillWidth: true
            placeholderText: "Nom de la recette"
        }

        onOpened: {
            recipeNameField.text = ""
            recipeNameField.forceActiveFocus()
        }
        onAccepted: AppController.createRecipe(recipeNameField.text.trim())
    }

    ColoDialog {
        id: addToListPicker
        title: "Ajouter à une liste"
        showAccept: false

        property string recipeId: ""
        property string recipeName: ""
        property var destinations: []

        function openFor(id, name) {
            recipeId = id
            recipeName = name
            destinations = AppController.shoppingLists()
            open()
        }

        Label {
            Layout.fillWidth: true
            visible: addToListPicker.destinations.length > 0
            text: "Les ingrédients de « " + addToListPicker.recipeName
                  + " » seront ajoutés à la liste, à acheter."
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 13
        }

        Label {
            Layout.fillWidth: true
            visible: addToListPicker.destinations.length === 0
            text: "Aucune liste de courses. Créez-en une d'abord."
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 15
        }

        Repeater {
            model: addToListPicker.destinations
            delegate: Button {
                required property var modelData
                Layout.fillWidth: true
                flat: true
                implicitHeight: 46
                contentItem: Label {
                    text: modelData.name
                    color: Theme.text
                    font.pixelSize: 15
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 10
                    color: parent.pressed ? Theme.surfaceHigh : "transparent"
                }
                onClicked: {
                    AppController.importListInto(modelData.id, addToListPicker.recipeId)
                    addToListPicker.close()
                }
            }
        }
    }

    ColoDialog {
        id: leaveDialog
        title: "Supprimer la recette ?"
        acceptText: "Supprimer"
        destructive: true

        property string listId: ""

        function openFor(id, name) {
            listId = id
            body.text = "« " + name + " » sera retirée de cet appareil. Les autres"
                        + " participants la gardent."
            open()
        }

        Label {
            id: body
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 14
        }

        onAccepted: AppController.leaveList(leaveDialog.listId)
    }
}
