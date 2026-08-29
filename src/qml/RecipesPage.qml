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
            visible: tabBar.currentIndex === 0
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

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            Material.accent: Theme.accent

            TabButton {
                text: "Mes recettes"
                width: implicitWidth + Theme.gap
            }
            TabButton {
                text: "Catalogue (" + AppController.recipeLibraryCount + ")"
                width: implicitWidth + Theme.gap
            }
        }

        // --- Mes recettes ---
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: tabBar.currentIndex === 0
            spacing: 0

            ColoTextField {
                id: myRecipesSearch
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gap
                Layout.rightMargin: Theme.gap
                Layout.topMargin: Theme.gap
                hint: "Rechercher une recette, un ingrédient…"
            }

            Timer {
                id: myRecipesSearchDebounce
                interval: 150
                onTriggered: AppController.recipes.filter = myRecipesSearch.text
            }

            Connections {
                target: myRecipesSearch
                function onTextChanged() {
                    myRecipesSearchDebounce.restart()
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.margins: Theme.gap
                visible: recipesView.count === 0 && myRecipesSearch.text.length === 0
                text: "Aucune recette. Créez-en une, parcourez le catalogue, ou rejoignez-en une via un lien partagé."
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 15
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gap
                Layout.rightMargin: Theme.gap
                visible: recipesView.count === 0 && myRecipesSearch.text.length > 0
                text: "Aucun résultat pour « " + myRecipesSearch.text + " »."
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
                    height: Math.max(76, card.implicitHeight)

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
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            lineHeight: 1.25
                            lineHeightMode: Text.ProportionalHeight
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
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }

                    onClicked: AppController.openList(card.listId)
                    onPressAndHold: recipeMenu.openFor(card.listId, card.name)
                }
            }
        }

        // --- Catalogue intégré ---
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: tabBar.currentIndex === 1
            spacing: Theme.gap

            ColoTextField {
                id: catalogSearch
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gap
                Layout.rightMargin: Theme.gap
                Layout.topMargin: Theme.gap
                hint: "Rechercher titre, ingrédient, préparation…"
            }

            Timer {
                id: catalogSearchDebounce
                interval: 150
                onTriggered: AppController.recipeLibrary.filter = catalogSearch.text
            }

            Connections {
                target: catalogSearch
                function onTextChanged() {
                    catalogSearchDebounce.restart()
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gap
                Layout.rightMargin: Theme.gap
                visible: AppController.recipeLibraryCount === 0
                text: "Catalogue indisponible."
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 15
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gap
                Layout.rightMargin: Theme.gap
                visible: AppController.recipeLibraryCount > 0 && catalogView.count === 0
                text: "Aucun résultat pour « " + catalogSearch.text + " »."
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 15
            }

            ListView {
                id: catalogView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: AppController.recipeLibrary
                spacing: 8
                leftMargin: Theme.gap
                rightMargin: Theme.gap
                bottomMargin: Theme.gap

                delegate: ItemDelegate {
                    id: libCard
                    required property string libraryId
                    required property string title
                    required property string category
                    required property string servings
                    required property int ingredientCount

                    width: catalogView.width - catalogView.leftMargin - catalogView.rightMargin
                    height: Math.max(76, libCard.implicitHeight)

                    background: Rectangle {
                        radius: 12
                        color: libCard.pressed ? Theme.surfaceHigh : Theme.surface
                        border.color: Theme.outline
                        border.width: 1
                    }

                    contentItem: ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 2

                        Label {
                            Layout.fillWidth: true
                            text: libCard.title
                            color: Theme.text
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            lineHeight: 1.25
                            lineHeightMode: Text.ProportionalHeight
                        }

                        Label {
                            Layout.fillWidth: true
                            text: {
                                const parts = []
                                parts.push(libCard.ingredientCount
                                           + (libCard.ingredientCount > 1
                                              ? " ingrédients" : " ingrédient"))
                                if (libCard.category.length > 0)
                                    parts.push(libCard.category)
                                if (libCard.servings.length > 0)
                                    parts.push(libCard.servings)
                                return parts.join(" · ")
                            }
                            color: Theme.textDim
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }

                    onClicked: catalogDetail.openFor(libCard.libraryId, libCard.title)
                }
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
        id: catalogDetail
        title: "Recette du catalogue"
        acceptText: "Ajouter à ma bibliothèque"
        showAccept: true

        property string libraryId: ""
        property string recipeTitle: ""
        property int targetServings: 4
        property string instructionsText: libraryId.length > 0
                ? AppController.libraryInstructions(libraryId) : ""

        function openFor(id, title) {
            libraryId = id
            recipeTitle = title
            targetServings = AppController.libraryBaseServings(id)
            open()
        }

        Label {
            Layout.fillWidth: true
            text: catalogDetail.recipeTitle
            wrapMode: Text.WordWrap
            color: Theme.text
            font.pixelSize: 20
            font.weight: Font.DemiBold
            lineHeight: 1.28
            lineHeightMode: Text.ProportionalHeight
        }

        ServingsStepper {
            Layout.fillWidth: true
            value: catalogDetail.targetServings
            minimum: 1
            onValueChanged: catalogDetail.targetServings = value
        }

        Label {
            Layout.fillWidth: true
            text: "Ingrédients"
            color: Theme.textDim
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }

        ScrollView {
            id: ingredientsScroll
            Layout.fillWidth: true
            Layout.maximumHeight: Math.min(ingredientsCol.implicitHeight + 8,
                                            catalogDetail.scrollMaxHeight)

            ColumnLayout {
                id: ingredientsCol
                width: ingredientsScroll.availableWidth
                spacing: 4

                Repeater {
                    model: AppController.libraryIngredients(catalogDetail.libraryId,
                                                           catalogDetail.targetServings)
                    delegate: Label {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.maximumWidth: ingredientsScroll.availableWidth
                        text: {
                            let s = modelData.name
                            if (modelData.qty && modelData.qty.length > 0)
                                s = modelData.qty + " " + s
                            if (modelData.note && modelData.note.length > 0)
                                s += " (" + modelData.note + ")"
                            return s
                        }
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        font.pixelSize: 15
                        lineHeight: 1.35
                        lineHeightMode: Text.ProportionalHeight
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: catalogDetail.instructionsText.length > 0
            text: "Préparation"
            color: Theme.textDim
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }

        ScrollView {
            id: prepScroll
            Layout.fillWidth: true
            Layout.maximumHeight: Math.min(prepLabel.implicitHeight + 8,
                                            catalogDetail.scrollMaxHeight)
            visible: catalogDetail.instructionsText.length > 0

            Label {
                id: prepLabel
                width: prepScroll.availableWidth
                text: catalogDetail.instructionsText
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: 15
                lineHeight: 1.45
                lineHeightMode: Text.ProportionalHeight
            }
        }

        onAccepted: {
            AppController.addRecipeFromLibrary(catalogDetail.libraryId,
                                               catalogDetail.targetServings)
            catalogDetail.close()
            tabBar.currentIndex = 0
        }
    }

    ColoDialog {
        id: addToListPicker
        title: "Ajouter à une liste"
        showAccept: false

        property string recipeId: ""
        property string recipeName: ""
        property var destinations: []
        property int targetServings: 4

        function openFor(id, name) {
            recipeId = id
            recipeName = name
            targetServings = AppController.recipeTargetServings(id)
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

        ServingsStepper {
            Layout.fillWidth: true
            visible: addToListPicker.destinations.length > 0
            value: addToListPicker.targetServings
            onValueChanged: addToListPicker.targetServings = value
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
                    AppController.importListInto(modelData.id, addToListPicker.recipeId,
                                                 addToListPicker.targetServings)
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
