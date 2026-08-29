import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root

    required property string listId
    required property string listTitle

    // Recette : pas de mode Courses, action « Ajouter à une liste ».
    readonly property bool isRecipe: AppController.isRecipe(listId)

    // Sélection multiple : vide = mode normal. Un appui long sur une ligne y entre.
    property var selectedIds: []
    readonly property bool selectionMode: selectedIds.length > 0

    // Mode Courses : on est dans le magasin, une main sur le caddie. Plus rien à
    // faire que cocher ce qu'on prend — pas d'ajout, pas de suppression, pas
    // d'édition ouverte par mégarde, et toute la ligne devient la case à cocher.
    // Masqué pour les recettes.
    property bool shoppingMode: false

    // Barre de recherche ouverte. Le filtre vit dans le modèle (C++) : il ne masque
    // que l'affichage, il ne supprime ni ne désynchronise rien.
    property bool searchOpen: false

    // Vrai le temps qu'une poignée de déplacement est tenue : fige le défilement de la
    // liste pour que le glissement l'emporte sur écran tactile.
    property bool rowDragging: false

    readonly property string pageTitle: selectionMode
        ? selectedIds.length + (selectedIds.length > 1 ? " sélectionnés" : " sélectionné")
        : listTitle

    // Sous-titre dans la barre (Main.qml) : combien il reste à lire / acheter.
    readonly property string pageSubtitle: {
        if (selectionMode || shoppingMode || searchOpen)
            return ""
        const total = AppController.items.count
        if (total === 0)
            return ""
        const left = total - AppController.items.doneCount
        if (left === 0)
            return "Tout est pris"
        if (left === 1)
            return "1 article restant"
        return left + " articles restants"
    }

    // L'écran reste allumé pour toute l'app (Main.qml), pas seulement en mode Courses.

    function closeSearch() {
        searchOpen = false
        searchField.text = ""
        AppController.items.filter = ""
    }

    // Retour (bouton Android ou flèche) : défaire l'état courant avant de quitter la
    // page — la sélection, puis la recherche, puis le mode Courses.
    function handleBack() {
        if (selectionMode) {
            selectedIds = []
            return true
        }
        if (searchOpen) {
            closeSearch()
            return true
        }
        if (shoppingMode) {
            shoppingMode = false
            return true
        }
        return false
    }

    function isSelected(itemId) {
        return selectedIds.indexOf(itemId) >= 0
    }

    function toggleSelection(itemId) {
        // Réassignation d'un nouveau tableau : muter en place ne notifierait personne.
        const next = selectedIds.slice()
        const at = next.indexOf(itemId)
        if (at >= 0)
            next.splice(at, 1)
        else
            next.push(itemId)
        selectedIds = next
    }

    // Le titre suit le renommage, d'ici ou d'un autre appareil.
    Connections {
        target: AppController
        function onListRenamed(listId, title) {
            if (listId === root.listId)
                root.listTitle = title
        }
    }

    property Component actions: Row {
        spacing: 0

        ToolButton {
            width: 96
            height: Theme.touchTarget
            visible: root.selectionMode
            contentItem: Label {
                text: "Supprimer"
                color: Theme.danger
                font.pixelSize: 14
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: deleteDialog.openFor(root.selectedIds)
        }

        // En mode Courses, une seule sortie possible, bien visible.
        ToolButton {
            width: 92
            height: Theme.touchTarget
            visible: root.shoppingMode && !root.selectionMode
            contentItem: Label {
                text: "Terminer"
                color: Theme.accent
                font.pixelSize: 14
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: root.shoppingMode = false
        }

        // Le mode du magasin est celui qu'on ouvre le plus souvent : il mérite un
        // bouton, pas une ligne de menu. Pas sur une recette.
        ToolButton {
            width: 90
            height: Theme.touchTarget
            visible: !root.selectionMode && !root.shoppingMode && !root.isRecipe
            contentItem: Label {
                text: "Courses"
                color: Theme.accent
                font.pixelSize: 14
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: root.shoppingMode = true
        }

        ToolButton {
            width: Theme.touchTarget
            height: Theme.touchTarget
            visible: !root.selectionMode && !root.shoppingMode
            contentItem: Icon {
                name: "menu"
                color: Theme.text
                size: 18
            }
            onClicked: pageMenu.popup()
        }
    }

    // Gestion d'une suggestion de favori (appui long sur une pastille).
    ColoMenu {
        id: favMenu
        property string favName: ""
        property bool   pinned: false

        MenuItem {
            text: favMenu.pinned ? "Ne plus épingler" : "Épingler en tête"
            onTriggered: AppController.pinFavorite(favMenu.favName, !favMenu.pinned)
        }
        MenuItem {
            text: "Retirer des suggestions"
            onTriggered: AppController.removeFavorite(favMenu.favName)
        }
    }

    ColoMenu {
        id: pageMenu
        objectName: "pageMenu"

        MenuItem {
            text: "Rechercher"
            onTriggered: root.searchOpen = true
        }
        // Ce qui a déjà été pris, et par qui — y compris les articles retirés depuis.
        MenuItem {
            text: "Historique"
            visible: !root.isRecipe
            onTriggered: root.StackView.view.push(historyPageComponent,
                                                  { listId: root.listId })
        }
        // Fin de course : sans ces deux-là, les articles cochés restent barrés à
        // l'écran pour toujours, et il faut les traiter un par un.
        MenuItem {
            text: "Tout remettre à acheter"
            visible: !root.isRecipe
            enabled: AppController.items.doneCount > 0
            onTriggered: uncheckDialog.open()
        }
        MenuItem {
            text: "Retirer les articles pris"
            visible: !root.isRecipe
            enabled: AppController.items.doneCount > 0
            onTriggered: clearDoneDialog.open()
        }
        MenuSeparator { visible: !root.isRecipe }
        // Classement local (par appareil) : un seul mode actif. Changer de mode ne
        // perd jamais l'ordre manuel (`order` reste intact).
        MenuItem {
            text: "Par rayons"
            checkable: true
            checked: AppController.items.sortMode === "aisle"
            onTriggered: AppController.items.sortMode = "aisle"
        }
        MenuItem {
            text: "Manuel (glisser)"
            checkable: true
            checked: AppController.items.sortMode === "manual"
            onTriggered: AppController.items.sortMode = "manual"
        }
        MenuItem {
            text: "Alphabétique"
            checkable: true
            checked: AppController.items.sortMode === "name"
            onTriggered: AppController.items.sortMode = "name"
        }
        MenuItem {
            text: "Date d'ajout"
            checkable: true
            checked: AppController.items.sortMode === "created"
            onTriggered: AppController.items.sortMode = "created"
        }
        MenuSeparator {}
        MenuItem {
            text: "Modifier la préparation"
            visible: root.isRecipe
            onTriggered: prepDialog.open()
        }
        MenuItem {
            text: "Ajouter à une liste…"
            visible: root.isRecipe
            onTriggered: recipeImportPicker.open()
        }
        MenuItem {
            text: "Partager " + (root.isRecipe ? "la recette" : "la liste")
            onTriggered: shareSheet.openFor(root.listId, root.listTitle)
        }
        MenuItem {
            text: root.isRecipe ? "Renommer la recette" : "Renommer la liste"
            onTriggered: renameDialog.open()
        }
        MenuItem {
            text: root.isRecipe ? "Dupliquer la recette" : "Dupliquer la liste"
            onTriggered: duplicateDialog.open()
        }
    }

    ShareSheet { id: shareSheet }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Mode Courses : où en est le caddie, sans avoir à compter les lignes.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.shoppingMode ? 52 : 0
            visible: Layout.preferredHeight > 0
            clip: true
            color: Theme.surface
            Behavior on Layout.preferredHeight { NumberAnimation { duration: 140 } }

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.pad
                anchors.rightMargin: Theme.pad
                anchors.topMargin: 8
                anchors.bottomMargin: 10
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    text: AppController.items.count === 0
                          ? "Liste vide"
                          : (AppController.items.doneCount === AppController.items.count
                             ? "Tout est dans le panier"
                             : (AppController.items.count - AppController.items.doneCount)
                               + " restant" + ((AppController.items.count - AppController.items.doneCount) > 1 ? "s" : "")
                               + " · " + AppController.items.doneCount + " pris")
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 4
                    radius: 2
                    color: Theme.surfaceHigh

                    Rectangle {
                        height: parent.height
                        radius: 2
                        color: Theme.accent
                        width: AppController.items.count > 0
                               ? parent.width * (AppController.items.doneCount
                                                 / AppController.items.count)
                               : 0
                        Behavior on width { NumberAnimation { duration: 180 } }
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.outline
            }
        }

        // Recherche : au-delà d'une vingtaine d'articles, retrouver « harissa » à l'œil
        // devient pénible.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.searchOpen ? 60 : 0
            visible: Layout.preferredHeight > 0
            clip: true
            color: Theme.surface
            Behavior on Layout.preferredHeight { NumberAnimation { duration: 140 } }

            onVisibleChanged: {
                if (visible)
                    searchField.forceActiveFocus()
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                ColoTextField {
                    id: searchField
                    Layout.fillWidth: true
                    hint: "Rechercher un article"
                    onTextChanged: AppController.items.filter = text
                }

                ToolButton {
                    Layout.preferredWidth: Theme.touchTarget
                    Layout.preferredHeight: Theme.touchTarget
                    contentItem: Icon {
                        name: "close"
                        color: Theme.textDim
                        size: 17
                    }
                    onClicked: root.closeSearch()
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.outline
            }
        }

        // Recette : ajuster le nombre de personnes (quantités affichées à l'échelle).
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.isRecipe && !root.shoppingMode ? 52 : 0
            visible: Layout.preferredHeight > 0
            clip: true
            color: Theme.surface
            Behavior on Layout.preferredHeight { NumberAnimation { duration: 140 } }

            ServingsStepper {
                id: recipeServings
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.pad
                value: AppController.recipeTargetServings(root.listId)
                onValueChanged: AppController.setRecipeTargetServings(root.listId, value)
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.outline
            }
        }

        // Préparation (recettes personnelles) — aperçu multiligne.
        Rectangle {
            id: prepBar
            Layout.fillWidth: true
            Layout.preferredHeight: root.isRecipe && !root.shoppingMode
                                    && prepBar.prepText.length > 0
                                    ? Math.min(prepPreview.implicitHeight + Theme.pad * 2, 88) : 0
            visible: Layout.preferredHeight > 0
            clip: true
            color: Theme.surface

            property string prepText: AppController.recipeInstructions(root.listId)

            Label {
                id: prepPreview
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.pad
                anchors.rightMargin: Theme.pad
                text: prepBar.prepText
                color: Theme.textDim
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }

            MouseArea {
                anchors.fill: parent
                onClicked: prepViewDialog.open()
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.outline
            }
        }

        ListView {
            id: items
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: AppController.items
            // La vue ne se fige QUE pendant qu'une poignée est tenue : sinon elle
            // disputerait le geste vertical au glissement. Au repos, elle défile.
            interactive: !root.rowDragging
            topMargin: Theme.gap
            bottomMargin: Theme.gap
            spacing: 6

            // Sections par rayon uniquement en mode aisle. Sinon une seule séquence.
            section.property: AppController.items.sortMode === "aisle" ? "aisle" : ""
            section.criteria: ViewSection.FullString

            // Un changement reçu d'un autre téléphone recharge le modèle en bloc
            // (reset), ce qui renvoie la vue tout en haut. À deux en magasin, chaque
            // cochage de l'autre ferait perdre sa place. On mémorise la position juste
            // avant le reset et on la rétablit juste après (bornée si la liste a
            // raccourci).
            property real savedContentY: 0
            Connections {
                target: AppController.items
                function onModelAboutToBeReset() { items.savedContentY = items.contentY }
                function onModelReset() {
                    Qt.callLater(function() {
                        items.contentY = items.savedContentY
                        items.returnToBounds()
                    })
                }
            }
            section.delegate: Item {
                required property string section
                width: items.width
                height: AppController.items.aisleCount > 1 ? 38 : 0
                visible: height > 0

                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.gap
                    anchors.verticalCenter: parent.verticalCenter
                    height: 26
                    width: Math.min(sectionLabel.implicitWidth + 16, parent.width - 2 * Theme.gap)
                    radius: 13
                    color: Theme.accentSoft

                    Label {
                        id: sectionLabel
                        anchors.centerIn: parent
                        width: parent.width - 12
                        horizontalAlignment: Text.AlignHCenter
                        // Le rayon vide est réel (« non classé »), il lui faut un nom.
                        text: section.length > 0 ? section : "Sans rayon"
                        color: Theme.accent
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        elide: Text.ElideRight
                    }
                }
            }

            // Un article coché ne bouge pas (le tri du modèle ignore l'état pris) :
            // il s'estompe sur place — la liste ne se réorganise pas sous les doigts.
            //
            // Conteneur immobile (place dans la vue + zone de dépôt) contenant la ligne,
            // qui se détache sous le doigt pendant un glissement.
            delegate: Item {
                id: wrapper
                width: items.width
                height: row.height

                // Pas de `required property int index` (couperait le contexte du modèle).
                property int rowIndex: index
                // « Saisie » : posée par la poignée dès l'appui. Fige la vue le temps du
                // glissement (root.rowDragging) pour que le geste l'emporte sur le
                // défilement, et détache la ligne sous le doigt.
                property bool held: false
                z: held ? 2 : 1

                DropArea {
                    anchors.fill: parent
                    onEntered: function (drag) {
                        const source = drag.source
                        if (!source || source === wrapper)
                            return
                        // Franchir une frontière de rayon range l'article dans ce rayon :
                        // c'est le modèle qui en décide (moveItem), pas la vue.
                        AppController.items.moveItem(source.rowIndex, wrapper.rowIndex)
                    }
                }

                SwipeDelegate {
                    id: row
                    width: wrapper.width - 2 * Theme.gap
                    x: Theme.gap
                    // S'adapte au nom enroulé (WordWrap) : une ligne courte reste compacte.
                    height: Math.max(root.shoppingMode ? 72 : 60,
                                     contentItem.implicitHeight + 10)
                    padding: 0

                    readonly property bool selected: root.isSelected(model.itemId)

                    Drag.active: wrapper.held
                    Drag.source: wrapper
                    Drag.hotSpot.x: width / 2
                    Drag.hotSpot.y: height / 2
                    opacity: wrapper.held ? 0.85 : 1.0
                    // Pendant le glissement, la ligne quitte son conteneur pour la vue :
                    // elle reste sous le doigt pendant que les autres se réorganisent.
                    states: State {
                        when: wrapper.held
                        ParentChange { target: row; parent: items }
                    }

                    // Le swipe supprime : désactivé en sélection (conflit) et en mode
                    // Courses (un geste de travers effacerait l'article des autres).
                    swipe.enabled: !root.selectionMode && !root.shoppingMode

                    background: Rectangle {
                        radius: 12
                        color: row.selected ? Theme.accentSoft
                             : (row.pressed ? Theme.surfaceHigh : Theme.surface)
                        border.color: row.selected ? Theme.accent : Theme.outline
                        border.width: 1
                    }

                    contentItem: RowLayout {
                        spacing: 0

                        CheckBox {
                            Layout.leftMargin: 6
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: Theme.touchTarget
                            implicitHeight: Theme.touchTarget
                            // Sur une recette hors sélection : pas de cochage « pris ».
                            visible: root.selectionMode || !root.isRecipe
                            // En mode sélection, la case coche la ligne ; sinon elle
                            // marque l'article comme acheté.
                            checked: root.selectionMode ? row.selected : model.done
                            Material.accent: Theme.accent
                            // onToggled et pas onCheckedChanged : ce dernier repart en
                            // boucle quand le modèle se recharge après un merge distant.
                            onToggled: {
                                if (root.selectionMode)
                                    root.toggleSelection(model.itemId)
                                else
                                    AppController.items.toggleDone(model.itemId)
                            }
                        }

                        ColumnLayout {
                            id: textBlock
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            Layout.leftMargin: 4
                            spacing: 2
                            opacity: model.done && !root.selectionMode ? 0.58 : 1.0
                            Behavior on opacity { NumberAnimation { duration: 160 } }

                            readonly property string filterText: AppController.items.filter
                            readonly property string detail: root.detailLine(model.qty, model.note)

                            Label {
                                Layout.fillWidth: true
                                text: textBlock.filterText.length > 0
                                      ? root.highlightPlain(model.name, textBlock.filterText)
                                      : model.name
                                textFormat: textBlock.filterText.length > 0
                                            ? Text.StyledText : Text.PlainText
                                color: model.done ? Theme.textDim : Theme.text
                                font.pixelSize: root.shoppingMode ? 18 : 16
                                font.strikeout: model.done
                                font.weight: root.shoppingMode ? Font.DemiBold : Font.Normal
                                lineHeight: root.shoppingMode ? 1.35 : 1.28
                                lineHeightMode: Text.ProportionalHeight
                                wrapMode: Text.WordWrap
                            }

                            // Quantité et description : jusqu'à deux lignes, lisibles au doigt.
                            Label {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: textBlock.filterText.length > 0
                                      ? root.highlightPlain(textBlock.detail, textBlock.filterText)
                                      : textBlock.detail
                                textFormat: textBlock.filterText.length > 0
                                            ? Text.StyledText : Text.PlainText
                                color: Theme.textDim
                                font.pixelSize: root.shoppingMode ? 14 : 13
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }

                        // Vignette de la première photo : plus grande, coins arrondis.
                        // Un tap ouvre la photo sans passer par l'édition.
                        Item {
                            id: thumbHost
                            Layout.preferredWidth: model.image.length > 0 ? 48 : 0
                            Layout.preferredHeight: 48
                            Layout.rightMargin: model.image.length > 0 ? 6 : 0
                            Layout.alignment: Qt.AlignVCenter
                            visible: model.image.length > 0

                            Rectangle {
                                anchors.fill: parent
                                radius: 8
                                clip: true
                                color: Theme.surfaceHigh

                                Image {
                                    id: thumbImg
                                    anchors.fill: parent
                                    source: model.image.length > 0
                                            ? "image://itemimg/" + model.image.split(" ")[0]
                                              + "?r=" + AppController.imageRevision
                                            : ""
                                    sourceSize.width: 96
                                    sourceSize.height: 96
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                }
                            }

                            Rectangle {
                                anchors.fill: parent
                                radius: 8
                                color: "transparent"
                                border.color: Theme.outline
                                border.width: thumbImg.status === Image.Ready ? 1 : 0
                            }

                            MouseArea {
                                anchors.fill: parent
                                enabled: thumbImg.status === Image.Ready
                                onClicked: photoViewer.openFor(model.image.split(" ")[0])
                            }
                        }

                        // Poignée de déplacement, toujours visible (hors sélection et
                        // Courses) : on la glisse pour ranger l'article, loin d'un coup,
                        // sans passer par un menu. `held` fige la vue le temps du geste.
                        MouseArea {
                            id: dragHandle
                            Layout.rightMargin: 2
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: visible ? 34 : 0
                            Layout.preferredHeight: Theme.touchTarget
                            visible: !root.selectionMode && !root.shoppingMode
                                     && AppController.items.canReorder
                            drag.target: wrapper.held ? row : undefined
                            drag.axis: Drag.YAxis
                            cursorShape: Qt.SizeVerCursor
                            preventStealing: true
                            onPressed: { wrapper.held = true; root.rowDragging = true }
                            onReleased: { wrapper.held = false; root.rowDragging = false }
                            onCanceled: { wrapper.held = false; root.rowDragging = false }

                            Icon {
                                anchors.centerIn: parent
                                name: "grip"
                                color: Theme.textDim
                                size: 16
                            }
                        }
                    }

                    // Appui long sur la ligne : sélection multiple (sauf en Courses).
                    onPressAndHold: {
                        if (!root.shoppingMode)
                            root.toggleSelection(model.itemId)
                    }
                    // Mode Courses : toute la ligne coche. Sinon : éditer, ou étendre la
                    // sélection si elle est commencée.
                    onClicked: {
                        if (root.selectionMode) {
                            root.toggleSelection(model.itemId)
                        } else if (root.shoppingMode) {
                            AppController.items.toggleDone(model.itemId)
                            // On coche sans quitter le rayon des yeux : la vibration
                            // confirme le geste à la place du regard.
                            AppController.vibrate()
                        } else {
                            readDialog.openFor(model)
                        }
                    }

                    // Glisser vers la gauche : suppression, avec un fond rouge explicite.
                    swipe.right: Rectangle {
                        width: parent.width
                        height: parent.height
                        radius: 12
                        color: Theme.danger

                        Label {
                            anchors.right: parent.right
                            anchors.rightMargin: 20
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Supprimer"
                            color: "white"
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }

                        SwipeDelegate.onClicked: {
                            // La ligne est rouverte : si la confirmation est refusée,
                            // l'article ne doit pas rester en position « glissée ».
                            row.swipe.close()
                            deleteDialog.openFor([model.itemId], model.name)
                        }
                    }
                }   // SwipeDelegate
            }   // wrapper
        }   // ListView

        // État vide. Une liste vide et une recherche sans résultat ne se disent pas
        // pareil : « ajoutez un article » quand on cherche « harissa » serait absurde.
        ColumnLayout {
            visible: items.count === 0
            Layout.alignment: Qt.AlignCenter
            Layout.fillHeight: true
            Layout.leftMargin: 40
            Layout.rightMargin: 40
            spacing: 6

            readonly property bool filtering: AppController.items.filter.length > 0

            Item { Layout.fillHeight: true }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: parent.filtering ? "Aucun résultat" : "Liste vide"
                color: Theme.text
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: parent.filtering
                      ? "Aucun article ne correspond à « " + AppController.items.filter + " »."
                      : "Ajoutez un premier article ci-dessous."
                color: Theme.textDim
                font.pixelSize: 14
            }

            Button {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 4
                visible: parent.filtering
                flat: true
                implicitHeight: Theme.touchTarget
                contentItem: Label {
                    text: "Effacer la recherche"
                    color: Theme.accent
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: root.closeSearch()
            }

            Item { Layout.fillHeight: true }
        }

        // Favoris : articles fréquents, à ajouter d'un tap. Appris à l'usage (le C++
        // les classe par fréquence), masqués tant qu'il n'y en a pas — donc invisibles
        // pour un nouvel utilisateur, puis de plus en plus utiles.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: favBar.visibleBar ? 46 : 0
            visible: implicitHeight > 0
            clip: true
            color: Theme.surface
            Behavior on implicitHeight { NumberAnimation { duration: 120 } }

            property alias visibleBar: favBar.visibleBar

            Rectangle { width: parent.width; height: 1; color: Theme.outline }

            ListView {
                id: favBar
                anchors.fill: parent
                anchors.leftMargin: Theme.gap
                anchors.rightMargin: Theme.gap
                orientation: ListView.Horizontal
                spacing: 8
                clip: true
                model: AppController.favorites

                readonly property bool visibleBar: count > 0 && !root.selectionMode
                                                   && !root.shoppingMode && !root.searchOpen

                delegate: Rectangle {
                    required property var modelData
                    height: 34
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    width: favLabel.implicitWidth + 34
                    radius: 17
                    color: chipMouse.pressed ? Theme.accentSoft : Theme.surfaceHigh
                    border.color: Theme.outline
                    border.width: 1

                    Row {
                        anchors.centerIn: parent
                        spacing: 4

                        Label {
                            text: "＋"
                            color: Theme.accent
                            font.pixelSize: 14
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Label {
                            id: favLabel
                            text: modelData.name
                            color: Theme.text
                            font.pixelSize: 14
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: chipMouse
                        anchors.fill: parent
                        onClicked: root.addFavorite(modelData)
                        // Appui long : gérer la suggestion (épingler / retirer).
                        onPressAndHold: {
                            favMenu.favName = modelData.name
                            favMenu.pinned = modelData.pinned === true
                            favMenu.popup()
                        }
                    }
                }
            }
        }

        // Barre d'ajout : collée en bas, au-dessus du clavier (adjustResize).
        // Masquée en sélection (on supprime, on n'ajoute pas) et en mode Courses
        // (le clavier n'a rien à faire là, et la liste doit rester entièrement visible).
        Rectangle {
            Layout.fillWidth: true
            // La description n'apparaît qu'une fois le nom commencé : c'est à ce
            // moment-là qu'on sait ce qu'on veut (« pq » → « 6 couches épaisses »),
            // et la barre reste sur une ligne le reste du temps.
            implicitHeight: root.composing ? 118 : 72
            visible: !root.selectionMode && !root.shoppingMode
            color: Theme.surface
            clip: true
            Behavior on implicitHeight { NumberAnimation { duration: 120 } }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.outline
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ColoTextField {
                        id: nameField
                        Layout.fillWidth: true
                        hint: "Ajouter un article"
                        onAccepted: root.addItem()
                        // Pré-remplir le rayon d'après les articles déjà classés (« pain »
                        // → Boulangerie), tant que l'utilisateur n'a pas choisi lui-même.
                        // Rien n'est assigné : ce n'est qu'une proposition dans le sélecteur.
                        onTextChanged: {
                            if (root.aisleManual)
                                return
                            const s = AppController.suggestAisle(text.trim())
                            addAisleBox.aisle = s   // "" si inconnu → « Sans rayon »
                        }
                    }

                    ColoTextField {
                        id: qtyField
                        Layout.preferredWidth: 76
                        hint: "Qté"
                        onAccepted: root.addItem()
                    }

                    RoundButton {
                        id: addButton
                        Layout.preferredWidth: 52
                        Layout.preferredHeight: 52
                        enabled: nameField.text.trim().length > 0

                        // Fond explicite : le style Material ignore Material.background
                        // sur un bouton désactivé, et le bouton devient invisible.
                        background: Rectangle {
                            radius: width / 2
                            color: !addButton.enabled ? Theme.surfaceHigh
                                 : (addButton.pressed ? Theme.accentDim : Theme.accent)
                        }

                        contentItem: Label {
                            text: "+"
                            color: addButton.enabled ? Theme.onAccent : Theme.textDim
                            font.pixelSize: 24
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        onClicked: root.addItem()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.rightMargin: 60
                    visible: root.composing
                    spacing: 8

                    ColoTextField {
                        id: addNoteField
                        Layout.fillWidth: true
                        hint: "Description (facultatif)"
                        onAccepted: root.addItem()
                    }

                    AisleBox {
                        id: addAisleBox
                        objectName: "addAisleBox"
                        Layout.preferredWidth: 132
                        // Un choix manuel fige le rayon : on cesse de le pré-remplir.
                        onChosen: root.aisleManual = true
                    }
                }
            }
        }
    }

    // Barre de sélection : rappelle ce qui est sélectionné et comment en sortir.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 60
        visible: root.selectionMode
        color: Theme.surfaceHigh

        Rectangle { width: parent.width; height: 1; color: Theme.outline }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.pad
            anchors.rightMargin: 10
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: root.selectedIds.length + (root.selectedIds.length > 1
                                                 ? " articles sélectionnés"
                                                 : " article sélectionné")
                color: Theme.text
                font.pixelSize: 14
                elide: Text.ElideRight
            }

            Button {
                flat: true
                implicitHeight: Theme.touchTarget
                contentItem: Label {
                    text: "Annuler"
                    color: Theme.textDim
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: root.selectedIds = []
            }

            Button {
                flat: true
                implicitHeight: Theme.touchTarget
                contentItem: Label {
                    text: "Supprimer"
                    color: Theme.danger
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: deleteDialog.openFor(root.selectedIds)
            }
        }
    }

    // Un article en cours de saisie : la ligne de description se déplie.
    readonly property bool composing: nameField.activeFocus || qtyField.activeFocus
                                      || addNoteField.activeFocus
                                      || nameField.text.length > 0

    // L'utilisateur a-t-il choisi le rayon lui-même pour l'article en cours ? Si oui, on
    // cesse de le pré-remplir d'après le nom. Réinitialisé à chaque nouvel article.
    property bool aisleManual: false

    function addItem() {
        const name = nameField.text.trim()
        if (name.length === 0)
            return

        // Deux « Lait » dans une liste partagée, c'est le doublon classique : chacun
        // l'ajoute de son côté. On prévient, on n'interdit pas — deux paquets de
        // pâtes, ça existe.
        const existing = AppController.items.existingName(name)
        if (existing.length > 0) {
            duplicateItemDialog.existingName = existing
            duplicateItemDialog.open()
            return
        }

        commitItem()
    }

    function commitItem() {
        AppController.items.addItem(nameField.text.trim(),
                                    qtyField.text.trim(),
                                    addNoteField.text.trim(),
                                    addAisleBox.aisle)
        nameField.text = ""
        qtyField.text = ""
        addNoteField.text = ""
        // On repart propre : le rayon du prochain article sera pré-rempli d'après son
        // nom. Plus besoin de garder le rayon « collant » — la mémoire fait le travail
        // (« lait », « beurre », « yaourt » retomberont d'eux-mêmes en crèmerie).
        root.aisleManual = false
        addAisleBox.aisle = ""
        nameField.forceActiveFocus()
    }

    // Ajout depuis un favori : la quantité et le rayon mémorisés font gagner la saisie.
    // Déjà présent dans la liste ? On le dit sans rien ajouter (pas de doublon silencieux).
    function addFavorite(fav) {
        const existing = AppController.items.existingName(fav.name)
        if (existing.length > 0) {
            AppController.toast("« " + existing + " » est déjà dans la liste")
            return
        }
        AppController.items.addItem(fav.name, fav.qty, "", fav.aisle)
    }

    function escapeHtml(s) {
        return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    // Surligne le filtre de recherche dans un libellé (StyledText).
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

    // Quantité et note sur une ligne, pour l'affichage et la recherche.
    function detailLine(qty, note) {
        const parts = []
        if (qty && qty.length > 0)
            parts.push(qty)
        if (note && note.length > 0)
            parts.push(note)
        return parts.join(" \u00b7 ")
    }

    // Une date lisible : « aujourd'hui à 18:32 », « hier à 09:05 », sinon « 3 juil. à 14:20 ».
    function formatStamp(ms) {
        if (!ms || ms <= 0)
            return ""
        const d = new Date(ms)
        const now = new Date()
        const sameDay = d.toDateString() === now.toDateString()
        const yesterday = new Date(now.getTime() - 86400000).toDateString() === d.toDateString()
        const time = Qt.formatDateTime(d, "HH:mm")
        if (sameDay)
            return "aujourd'hui à " + time
        if (yesterday)
            return "hier à " + time
        return Qt.formatDateTime(d, "d MMM") + " à " + time
    }

    // --- Dialogues ---

    // Lecture seule : lire nom, description et photos avant d'éditer.
    ColoDialog {
        id: readDialog
        title: "Article"
        acceptText: "Modifier"

        property string itemId: ""
        property string itemName: ""
        property string itemQty: ""
        property string itemNote: ""
        property string itemAisle: ""
        property string itemImage: ""
        property real   createdMs: 0
        property real   doneAtMs: 0
        property string author: ""
        property bool   itemDone: false

        readonly property var photoShas: itemImage.length > 0
            ? itemImage.split(" ").filter(s => s.length > 0) : []

        function openFor(item) {
            itemId    = item.itemId
            itemName  = item.name
            itemQty   = item.qty || ""
            itemNote  = item.note || ""
            itemAisle = item.aisle || ""
            itemImage = item.image || ""
            createdMs = item.created
            doneAtMs  = item.doneAt
            author    = item.byName || ""
            itemDone  = item.done === true
            open()
        }

        Label {
            Layout.fillWidth: true
            text: readDialog.itemName
            color: readDialog.itemDone ? Theme.textDim : Theme.text
            font.pixelSize: 22
            font.weight: Font.DemiBold
            font.strikeout: readDialog.itemDone
            wrapMode: Text.WordWrap
            lineHeight: 1.25
            lineHeightMode: Text.ProportionalHeight
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.detailLine(readDialog.itemQty, readDialog.itemNote)
            color: Theme.textDim
            font.pixelSize: 16
            wrapMode: Text.WordWrap
            lineHeight: 1.35
            lineHeightMode: Text.ProportionalHeight
        }

        Label {
            Layout.fillWidth: true
            visible: readDialog.itemAisle.length > 0
            text: readDialog.itemAisle
            color: Theme.accent
            font.pixelSize: 13
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8
            visible: readDialog.photoShas.length > 0

            Repeater {
                model: readDialog.photoShas
                delegate: Item {
                    width: 80
                    height: 80
                    required property string modelData

                    Rectangle {
                        anchors.fill: parent
                        radius: 8
                        clip: true
                        color: Theme.surfaceHigh

                        Image {
                            id: readThumb
                            anchors.fill: parent
                            source: "image://itemimg/" + modelData
                                    + "?r=" + AppController.imageRevision
                            sourceSize.width: 160
                            sourceSize.height: 160
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: photoViewer.openFor(modelData)
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 4
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 12
            text: {
                const lines = []
                const added = root.formatStamp(readDialog.createdMs)
                if (added.length > 0) {
                    lines.push(readDialog.author.length > 0
                               ? "Ajouté par " + readDialog.author + " " + added
                               : "Ajouté " + added)
                }
                if (readDialog.doneAtMs > 0)
                    lines.push("Coché " + root.formatStamp(readDialog.doneAtMs))
                return lines.join("\n")
            }
        }

        onAccepted: {
            const snap = {
                itemId: readDialog.itemId,
                name: readDialog.itemName,
                qty: readDialog.itemQty,
                note: readDialog.itemNote,
                aisle: readDialog.itemAisle,
                image: readDialog.itemImage,
                created: readDialog.createdMs,
                doneAt: readDialog.doneAtMs,
                byName: readDialog.author,
                done: readDialog.itemDone
            }
            readDialog.close()
            editDialog.openFor(snap)
        }
    }

    ColoDialog {
        id: editDialog
        title: "Modifier l'article"
        acceptText: "Enregistrer"
        acceptEnabled: editName.text.trim().length > 0

        property string itemId: ""
        // real et pas int : un timestamp en millisecondes déborde l'int 32 bits de QML.
        property real   createdMs: 0
        property real   doneAtMs: 0
        property string author: ""
        property string image: ""

        readonly property var photoShas: image.length > 0
            ? image.split(" ").filter(s => s.length > 0) : []

        function openFor(item) {
            itemId    = item.itemId
            createdMs = item.created
            doneAtMs  = item.doneAt
            author    = item.byName
            image     = item.image || ""
            editName.text = item.name
            editQty.text  = item.baseQty !== undefined ? item.baseQty : item.qty
            editNote.text = item.note
            editAisle.aisle = item.aisle
            open()
            editName.forceActiveFocus()
            editName.selectAll()
        }

        function pickPhoto() {
            if (!photoPickers.item)
                photoPickers.active = true
            if (photoPickers.item)
                photoPickers.item.openPhoto(editDialog.itemId)
            else
                AppController.toast("Sélecteur de fichiers indisponible")
        }

        function capturePhoto() {
            // PhotoCapture.qml n'est embarqué que si COLO_HAS_CAMERA (comme ScanPage).
            cameraCapture.source = "PhotoCapture.qml"
            cameraCapture.active = true
            if (cameraCapture.status === Loader.Error)
                AppController.toast("Caméra indisponible sur cet appareil")
        }

        ColoTextField {
            id: editName
            Layout.fillWidth: true
            hint: "Article"
            onAccepted: if (editDialog.acceptEnabled) editDialog.accept()
        }

        ColoTextField {
            id: editQty
            Layout.fillWidth: true
            hint: "Quantité (2, 500 g, 1 pack…)"
            onAccepted: if (editDialog.acceptEnabled) editDialog.accept()
        }

        ColoTextField {
            id: editNote
            Layout.fillWidth: true
            hint: "Description"
            onAccepted: if (editDialog.acceptEnabled) editDialog.accept()
        }

        AisleBox {
            id: editAisle
            Layout.fillWidth: true
        }

        // Photos : vignettes + ajout / retrait, sans page détail séparée.
        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8
            visible: editDialog.photoShas.length > 0

            Repeater {
                model: editDialog.photoShas
                delegate: Item {
                    width: 72
                    height: 72
                    required property string modelData

                    Image {
                        id: thumb
                        anchors.fill: parent
                        source: "image://itemimg/" + modelData
                                + "?r=" + AppController.imageRevision
                        sourceSize.width: 144
                        sourceSize.height: 144
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: thumb.status !== Image.Ready
                        color: Theme.surfaceHigh
                        radius: 8
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: photoViewer.openFor(modelData)
                    }

                    ToolButton {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        width: 28
                        height: 28
                        contentItem: Icon {
                            name: "close"
                            color: Theme.text
                            size: 14
                        }
                        background: Rectangle {
                            radius: 14
                            color: Qt.rgba(0, 0, 0, 0.45)
                        }
                        onClicked: {
                            AppController.removeItemImage(editDialog.itemId, modelData)
                            editDialog.image = AppController.items.itemImage(editDialog.itemId)
                        }
                    }
                }
            }
        }

        // Toujours proposer une photo de plus : caméra ou galerie.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                Layout.fillWidth: true
                flat: true
                implicitHeight: Theme.touchTarget
                contentItem: Label {
                    text: "Prendre une photo"
                    color: Theme.accent
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 10
                    color: parent.pressed ? Theme.surfaceHigh : "transparent"
                    border.color: Theme.outline
                    border.width: 1
                }
                onClicked: editDialog.capturePhoto()
            }

            Button {
                Layout.fillWidth: true
                flat: true
                implicitHeight: Theme.touchTarget
                contentItem: Label {
                    text: "Choisir dans la galerie"
                    color: Theme.accent
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 10
                    color: parent.pressed ? Theme.surfaceHigh : "transparent"
                    border.color: Theme.outline
                    border.width: 1
                }
                onClicked: editDialog.pickPhoto()
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 4
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 12
            text: {
                const lines = []
                const added = root.formatStamp(editDialog.createdMs)
                if (added.length > 0) {
                    lines.push(editDialog.author.length > 0
                               ? "Ajouté par " + editDialog.author + " " + added
                               : "Ajouté " + added)
                }
                if (editDialog.doneAtMs > 0)
                    lines.push("Coché " + root.formatStamp(editDialog.doneAtMs))
                return lines.join("\n")
            }
        }

        Button {
            Layout.fillWidth: true
            Layout.topMargin: 4
            flat: true
            implicitHeight: Theme.touchTarget
            contentItem: Label {
                text: "Supprimer l'article"
                color: Theme.danger
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 10
                color: parent.pressed ? Theme.surfaceHigh : "transparent"
                border.color: Theme.outline
                border.width: 1
            }
            onClicked: {
                const id = editDialog.itemId
                const nm = editName.text.trim()
                editDialog.close()
                deleteDialog.openFor([id], nm)
            }
        }

        onAccepted: AppController.items.editItem(editDialog.itemId,
                                                 editName.text.trim(),
                                                 editQty.text.trim(),
                                                 editNote.text.trim(),
                                                 editAisle.aisle)
    }

    // Sélecteur d'images, chargé au premier besoin seulement.
    Loader {
        id: photoPickers
        active: false
        source: "FilePickers.qml"
    }

    // Capture caméra (PhotoCapture.qml) : chargé dynamiquement ; absent si
    // COLO_HAS_CAMERA est off (desktop).
    Loader {
        id: cameraCapture
        anchors.fill: parent
        z: 20
        active: false
        visible: status === Loader.Ready
        onLoaded: {
            item.itemId = editDialog.itemId
            item.captured.connect(function(path) {
                AppController.setItemImage(editDialog.itemId, "file://" + path)
            })
            item.closeRequested.connect(function() {
                cameraCapture.active = false
                cameraCapture.source = ""
            })
        }
    }

    Connections {
        target: AppController.items
        function onRefreshed() {
            if (editDialog.opened && editDialog.itemId.length > 0)
                editDialog.image = AppController.items.itemImage(editDialog.itemId)
        }
    }

    // Photo en plein écran (depuis le dialogue d'édition).
    Popup {
        id: photoViewer
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: parent.width
        height: parent.height
        modal: true
        padding: 0
        property string sha: ""
        function openFor(shaHex) {
            sha = shaHex
            open()
        }
        background: Rectangle { color: "#CC000000" }
        contentItem: Item {
            Image {
                anchors.centerIn: parent
                width: parent.width
                height: parent.height
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                source: photoViewer.sha.length > 0
                        ? "image://itemimg/" + photoViewer.sha
                          + "?r=" + AppController.imageRevision
                        : ""
            }
            MouseArea {
                anchors.fill: parent
                onClicked: photoViewer.close()
            }
        }
    }

    Component {
        id: historyPageComponent
        HistoryPage {}
    }

    // Recette → liste de courses : choisir la destination, importer les ingrédients.
    ColoDialog {
        id: recipeImportPicker
        title: "Ajouter à une liste"
        showAccept: false

        property var destinations: []
        property int targetServings: 4

        onAboutToShow: {
            destinations = AppController.shoppingLists()
            targetServings = AppController.recipeTargetServings(root.listId)
        }

        Label {
            Layout.fillWidth: true
            visible: recipeImportPicker.destinations.length > 0
            text: "Les ingrédients de « " + root.listTitle
                  + " » seront ajoutés à la liste, à acheter."
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 13
        }

        ServingsStepper {
            Layout.fillWidth: true
            visible: recipeImportPicker.destinations.length > 0
            value: recipeImportPicker.targetServings
            onValueChanged: recipeImportPicker.targetServings = value
        }

        Label {
            Layout.fillWidth: true
            visible: recipeImportPicker.destinations.length === 0
            text: "Aucune liste de courses. Créez-en une d'abord."
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 15
        }

        Repeater {
            model: recipeImportPicker.destinations
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
                    AppController.importListInto(modelData.id, root.listId,
                                                 recipeImportPicker.targetServings)
                    recipeImportPicker.close()
                }
            }
        }
    }

    ColoDialog {
        id: prepViewDialog
        title: "Préparation"
        showAccept: false

        ScrollView {
            id: prepViewScroll
            Layout.fillWidth: true
            Layout.maximumHeight: Math.min(prepViewLabel.implicitHeight + 8,
                                            prepViewDialog.scrollMaxHeight)
            clip: true

            Label {
                id: prepViewLabel
                width: prepViewScroll.availableWidth
                text: AppController.recipeInstructions(root.listId)
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: 14
            }
        }

        Button {
            Layout.fillWidth: true
            flat: true
            text: "Modifier"
            onClicked: {
                prepViewDialog.close()
                prepDialog.open()
            }
        }
    }

    ColoDialog {
        id: prepDialog
        title: "Préparation"
        acceptText: "Enregistrer"

        ScrollView {
            id: prepEditScroll
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(prepField.implicitHeight + 16,
                                             prepDialog.scrollMaxHeight)
            clip: true

            TextArea {
                id: prepField
                width: prepEditScroll.availableWidth
                wrapMode: TextArea.Wrap
                color: Theme.text
                font.pixelSize: 14
                selectByMouse: true
                background: Rectangle {
                    radius: 12
                    color: Theme.surfaceHigh
                    border.color: Theme.outline
                }
            }
        }

        onOpened: prepField.text = AppController.recipeInstructions(root.listId)
        onAccepted: AppController.setRecipeInstructions(root.listId, prepField.text)
    }

    ColoDialog {
        id: duplicateItemDialog
        title: "Déjà dans la liste"
        acceptText: "Ajouter quand même"

        property string existingName: ""

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 14
            text: "« " + duplicateItemDialog.existingName + " » est déjà sur la liste. "
                  + "L'ajouter une seconde fois ?"
        }

        onAccepted: root.commitItem()
    }

    ColoDialog {
        id: uncheckDialog
        title: "Tout remettre à acheter ?"
        acceptText: "Tout décocher"

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 14
            text: "Les " + AppController.items.doneCount
                  + " articles pris repassent à acheter, pour tout le monde. "
                  + "Pratique pour refaire la même liste."
        }

        onAccepted: AppController.items.uncheckAll()
    }

    ColoDialog {
        id: clearDoneDialog
        title: "Retirer les articles pris ?"
        acceptText: "Retirer"
        destructive: true

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 14
            text: AppController.items.doneCount
                  + " article(s) coché(s) seront retirés de la liste, pour tout le monde. "
                  + "Ceux qui restent à acheter ne bougent pas."
        }

        onAccepted: AppController.items.removeDone()
    }

    ColoDialog {
        id: deleteDialog
        title: "Supprimer ?"
        acceptText: "Supprimer"
        destructive: true

        property var    ids: []
        property string itemName: ""

        function openFor(itemIds, name) {
            ids = itemIds.slice()
            itemName = name !== undefined ? name : ""
            open()
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 14
            text: deleteDialog.ids.length === 1 && deleteDialog.itemName.length > 0
                  ? "« " + deleteDialog.itemName + " » sera retiré de la liste, pour tout le monde."
                  : deleteDialog.ids.length + (deleteDialog.ids.length > 1
                        ? " articles seront retirés de la liste, pour tout le monde."
                        : " article sera retiré de la liste, pour tout le monde.")
        }

        onAccepted: {
            AppController.items.removeItems(deleteDialog.ids)
            root.selectedIds = []
        }
    }

    ColoDialog {
        id: renameDialog
        title: "Renommer la liste"
        acceptText: "Renommer"
        acceptEnabled: renameField.text.trim().length > 0

        ColoTextField {
            id: renameField
            Layout.fillWidth: true
            hint: "Nom de la liste"
            onAccepted: if (renameDialog.acceptEnabled) renameDialog.accept()
        }

        onOpened: {
            renameField.text = root.listTitle
            renameField.forceActiveFocus()
            renameField.selectAll()
        }
        onAccepted: AppController.renameList(root.listId, renameField.text.trim())
    }

    ColoDialog {
        id: duplicateDialog
        title: "Dupliquer la liste"
        acceptText: "Dupliquer"
        acceptEnabled: duplicateField.text.trim().length > 0

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

        onOpened: {
            duplicateField.text = root.listTitle + " (copie)"
            duplicateField.forceActiveFocus()
            duplicateField.selectAll()
        }
        onAccepted: AppController.duplicateList(root.listId, duplicateField.text.trim())
    }
}
