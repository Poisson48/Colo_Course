import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root

    readonly property string pageTitle: "Recettes"

    Rectangle {
        anchors.fill: parent
        color: Theme.background
        z: -1
    }

    Component.onDestruction: AppController.releaseRecipeLibraryCatalog()

    function handleBack() {
        if (catalogDetail.opened) {
            catalogDetail.close()
            return true
        }
        if (createDialog.opened) {
            createDialog.close()
            return true
        }
        if (leaveDialog.opened) {
            leaveDialog.close()
            return true
        }
        if (addToListPicker.opened) {
            addToListPicker.close()
            return true
        }
        return false
    }

    function escapeHtml(s) {
        return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    function highlightPlain(text, query) {
        if (!query || query.length === 0)
            return text
        const q = query.toLowerCase()
        const lower = text.toLowerCase()
        let out = ""
        let i = 0
        const accent = Theme.accent.toString()
        while (i < text.length) {
            const idx = lower.indexOf(q, i)
            if (idx < 0) {
                out += escapeHtml(text.substring(i))
                break
            }
            out += escapeHtml(text.substring(i, idx))
            out += '<font color="' + accent + '"><b>'
                 + escapeHtml(text.substring(idx, idx + query.length)) + '</b></font>'
            i = idx + query.length
        }
        return out
    }

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

            Button {
                Layout.alignment: Qt.AlignHCenter
                visible: recipesView.count === 0 && myRecipesSearch.text.length > 0
                flat: true
                implicitHeight: Theme.touchTarget
                contentItem: Label {
                    text: "Effacer la recherche"
                    color: Theme.accent
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                onClicked: {
                    myRecipesSearch.text = ""
                    AppController.recipes.filter = ""
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gap
                Layout.rightMargin: Theme.gap
                Layout.bottomMargin: 4
                visible: myRecipesSearch.text.length > 0 && recipesView.count > 0
                text: recipesView.count + (recipesView.count > 1 ? " recettes" : " recette")
                color: Theme.textDim
                font.pixelSize: 12
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

                    contentItem: RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 36
                            Layout.alignment: Qt.AlignVCenter
                            radius: 18
                            color: Theme.accent

                            Label {
                                anchors.centerIn: parent
                                text: card.name.length > 0 ? card.name.charAt(0).toUpperCase() : "R"
                                color: Theme.onAccent
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                Layout.fillWidth: true
                                text: myRecipesSearch.text.length > 0
                                      ? root.highlightPlain(card.name, myRecipesSearch.text)
                                      : card.name
                                textFormat: myRecipesSearch.text.length > 0
                                            ? Text.StyledText : Text.PlainText
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
                    }

                    onClicked: AppController.openList(card.listId)
                    onPressAndHold: recipeMenu.openFor(card.listId, card.name)
                }
            }
        }

        // --- Catalogue intégré (chargé à la demande : 30k recettes) ---
        Loader {
            id: catalogLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: tabBar.currentIndex === 1
            sourceComponent: catalogPanelComponent

            onActiveChanged: {
                if (active)
                    AppController.prepareRecipeLibraryCatalog()
                else
                    AppController.releaseRecipeLibraryCatalog()
            }
        }
    }

    Component {
        id: catalogPanelComponent
        ColumnLayout {
            id: catalogPanel
            anchors.fill: parent
            spacing: Theme.gap

            property string catalogCategory: ""

            function applyCatalogCategory(cat) {
                catalogCategory = cat
                AppController.setRecipeLibraryCategoryFilter(cat)
            }

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

            ListModel { id: categoryChipModel }

            function rebuildCategoryChips() {
                categoryChipModel.clear()
                const n = AppController.recipeLibraryCount
                categoryChipModel.append({
                    name: "",
                    label: "Toutes (" + n + ")"
                })
                const cats = AppController.recipeLibraryCategories()
                for (let i = 0; i < cats.length; i++) {
                    const c = cats[i]
                    categoryChipModel.append({
                        name: c.name,
                        label: c.name + " (" + c.count + ")"
                    })
                }
            }

            Component.onCompleted: rebuildCategoryChips()

            Connections {
                target: AppController
                function onRecipeLibraryCountChanged() {
                    catalogPanel.rebuildCategoryChips()
                }
            }

            ListView {
                id: categoryList
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 40 : 0
                Layout.leftMargin: Theme.gap
                Layout.rightMargin: Theme.gap
                visible: AppController.recipeLibraryCount > 0
                clip: true
                spacing: 8
                orientation: ListView.Horizontal
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick
                interactive: categoryChipModel.count > 0
                model: categoryChipModel

                ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                ScrollBar.horizontal.policy: ScrollBar.AsNeeded

                // Ne pas faire défiler le catalogue quand on glisse verticalement sur les chips.
                WheelHandler {
                    onWheel: (event) => {
                        if (Math.abs(event.angleDelta.y) >= Math.abs(event.angleDelta.x))
                            event.accepted = true
                    }
                }

                delegate: Button {
                    required property string name
                    required property string label

                    flat: true
                    height: categoryList.height
                    implicitWidth: chipLabel.implicitWidth + 20
                    padding: 0

                    background: Rectangle {
                        radius: 16
                        color: catalogPanel.catalogCategory === name
                               ? Theme.accent : Theme.surface
                        border.color: catalogPanel.catalogCategory === name
                                      ? Theme.accent : Theme.outline
                        border.width: 1
                    }

                    contentItem: Label {
                        id: chipLabel
                        text: label
                        color: catalogPanel.catalogCategory === name
                               ? Theme.onAccent : Theme.text
                        font.pixelSize: 13
                        font.weight: catalogPanel.catalogCategory === name
                                     ? Font.DemiBold : Font.Normal
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: catalogPanel.applyCatalogCategory(name)
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
                          && !AppController.recipeLibrary.loading
                text: {
                    if (catalogSearch.text.length > 0 && catalogPanel.catalogCategory.length > 0)
                        return "Aucun résultat dans « " + catalogPanel.catalogCategory
                               + " » pour « " + catalogSearch.text + " »."
                    if (catalogPanel.catalogCategory.length > 0)
                        return "Aucune recette dans « " + catalogPanel.catalogCategory + " »."
                    return "Aucun résultat pour « " + catalogSearch.text + " »."
                }
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 15
            }

            Button {
                Layout.alignment: Qt.AlignHCenter
                visible: AppController.recipeLibraryCount > 0 && catalogView.count === 0
                          && !AppController.recipeLibrary.loading
                          && (catalogSearch.text.length > 0 || catalogPanel.catalogCategory.length > 0)
                flat: true
                implicitHeight: Theme.touchTarget
                contentItem: Label {
                    text: catalogPanel.catalogCategory.length > 0 && catalogSearch.text.length === 0
                          ? "Toutes les catégories" : "Effacer les filtres"
                    color: Theme.accent
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                onClicked: {
                    catalogSearch.text = ""
                    AppController.recipeLibrary.filter = ""
                    catalogPanel.applyCatalogCategory("")
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gap
                Layout.rightMargin: Theme.gap
                visible: (catalogSearch.text.length > 0 || catalogPanel.catalogCategory.length > 0)
                          && catalogView.count > 0
                text: {
                    let s = catalogView.count + " recette"
                            + (catalogView.count > 1 ? "s" : "")
                    if (catalogPanel.catalogCategory.length > 0)
                        s += " · " + catalogPanel.catalogCategory
                    if (catalogSearch.text.length > 0)
                        s += " · « " + catalogSearch.text + " »"
                    return s
                }
                color: Theme.textDim
                font.pixelSize: 12
            }

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Theme.gap
                visible: AppController.recipeLibrary.loading
                         && AppController.recipeLibraryCount > 0
                running: visible
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
                    required property int baseServings

                    width: catalogView.width - catalogView.leftMargin - catalogView.rightMargin
                    height: Math.max(76, libCard.implicitHeight)

                    background: Rectangle {
                        radius: 12
                        color: libCard.pressed ? Theme.surfaceHigh : Theme.surface
                        border.color: Theme.outline
                        border.width: 1
                    }

                    contentItem: RowLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 36
                            Layout.alignment: Qt.AlignVCenter
                            radius: 18
                            color: Theme.accentSoft
                            border.color: Theme.accent
                            border.width: 1

                            Label {
                                anchors.centerIn: parent
                                text: libCard.title.length > 0 ? libCard.title.charAt(0).toUpperCase() : "?"
                                color: Theme.accent
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                Layout.fillWidth: true
                                text: catalogSearch.text.length > 0
                                      ? root.highlightPlain(libCard.title, catalogSearch.text)
                                      : libCard.title
                                textFormat: catalogSearch.text.length > 0
                                            ? Text.StyledText : Text.PlainText
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
                                    if (libCard.baseServings > 0)
                                        parts.push(libCard.baseServings + " pers.")
                                    else if (libCard.servings.length > 0)
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
                    }

                    onClicked: catalogDetail.openFor(libCard.libraryId, libCard.title,
                                                      libCard.category, libCard.baseServings)
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

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 13
            text: "Ensuite : ingrédients en bas de l'écran, étapes via « Préparation »."
        }

        onOpened: {
            recipeNameField.text = ""
            recipeNameField.forceActiveFocus()
        }
        onAccepted: AppController.createRecipe(recipeNameField.text.trim())
    }

    ColoDialog {
        id: catalogDetail
        title: recipeTitle.length > 0 ? recipeTitle : "Recette du catalogue"
        titleMaxLines: 0
        acceptText: "Ajouter à ma bibliothèque"
        showAccept: true

        property string libraryId: ""
        property string recipeTitle: ""
        property string recipeCategory: ""
        property int targetServings: 4
        property int baseServings: 4
        property string instructionsText: libraryId.length > 0
                ? AppController.libraryInstructions(libraryId) : ""

        function openFor(id, title, category, base) {
            libraryId = id
            recipeTitle = title
            recipeCategory = category || ""
            baseServings = base > 0 ? base : 4
            targetServings = AppController.libraryBaseServings(id)
            open()
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(catalogBody.implicitHeight + 4,
                                             catalogDetail.contentMaxHeight)
            clip: true

            ColumnLayout {
                id: catalogBody
                width: catalogDetail.availableWidth
                spacing: 10

        Flow {
            Layout.fillWidth: true
            spacing: 6
            visible: catalogDetail.recipeCategory.length > 0 || catalogDetail.baseServings > 0

            Repeater {
                model: {
                    const chips = []
                    if (catalogDetail.recipeCategory.length > 0)
                        chips.push(catalogDetail.recipeCategory)
                    if (catalogDetail.baseServings > 0)
                        chips.push(catalogDetail.baseServings + " pers.")
                    return chips
                }
                delegate: Rectangle {
                    required property string modelData
                    height: 24
                    width: chipLabel.implicitWidth + 16
                    radius: 12
                    color: Theme.accentSoft

                    Label {
                        id: chipLabel
                        anchors.centerIn: parent
                        text: modelData
                        color: Theme.accent
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        ServingsStepper {
            Layout.fillWidth: true
            value: catalogDetail.targetServings
            baseServings: catalogDetail.baseServings
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

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: AppController.libraryIngredients(catalogDetail.libraryId,
                                                       catalogDetail.targetServings)
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        text: "•"
                        color: Theme.accent
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 1
                    }

                    Label {
                        Layout.fillWidth: true
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

        Label {
            Layout.fillWidth: true
            visible: catalogDetail.instructionsText.length > 0
            text: catalogDetail.instructionsText
            wrapMode: Text.WordWrap
            color: Theme.text
            font.pixelSize: 15
            lineHeight: 1.45
            lineHeightMode: Text.ProportionalHeight
        }

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
            baseServings: AppController.recipeBaseServings(addToListPicker.recipeId)
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
