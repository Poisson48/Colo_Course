import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root

    required property string listId
    required property string listTitle

    // Sélection multiple : vide = mode normal. Un appui long sur une ligne y entre.
    property var selectedIds: []
    readonly property bool selectionMode: selectedIds.length > 0

    // Mode Courses : on est dans le magasin, une main sur le caddie. Plus rien à
    // faire que cocher ce qu'on prend — pas d'ajout, pas de suppression, pas
    // d'édition ouverte par mégarde, et toute la ligne devient la case à cocher.
    property bool shoppingMode: false

    // Barre de recherche ouverte. Le filtre vit dans le modèle (C++) : il ne masque
    // que l'affichage, il ne supprime ni ne désynchronise rien.
    property bool searchOpen: false

    // Mode Réorganiser : les poignées de glissement apparaissent, le tap et le swipe
    // sont mis en pause. On y entre par le menu, on en sort par « Terminer ». Garder
    // la poignée hors du repos, c'est ce qui allège la ligne au quotidien.
    property bool reorderMode: false

    readonly property string pageTitle: selectionMode
        ? selectedIds.length + (selectedIds.length > 1 ? " sélectionnés" : " sélectionné")
        : listTitle

    // Dans un rayon, on tient le téléphone sans le toucher pendant des minutes :
    // l'écran ne doit pas s'éteindre au milieu de la liste.
    onShoppingModeChanged: AppController.setKeepScreenOn(shoppingMode)
    // Quitter la page en mode Courses laisserait le drapeau posé, et l'écran allumé
    // pour toujours.
    Component.onDestruction: AppController.setKeepScreenOn(false)

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
        if (reorderMode) {
            reorderMode = false
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

        // En mode Courses ou Réorganiser, une seule sortie possible, bien visible.
        ToolButton {
            width: 92
            height: Theme.touchTarget
            visible: (root.shoppingMode || root.reorderMode) && !root.selectionMode
            contentItem: Label {
                text: "Terminer"
                color: Theme.accent
                font.pixelSize: 14
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: { root.shoppingMode = false; root.reorderMode = false }
        }

        // Le mode du magasin est celui qu'on ouvre le plus souvent : il mérite un
        // bouton, pas une ligne de menu.
        ToolButton {
            width: 90
            height: Theme.touchTarget
            visible: !root.selectionMode && !root.shoppingMode && !root.reorderMode
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
            visible: !root.selectionMode && !root.shoppingMode && !root.reorderMode
            contentItem: Icon {
                name: "menu"
                color: Theme.text
                size: 18
            }
            onClicked: pageMenu.popup()
        }
    }

    // Gestion d'une suggestion de favori (appui long sur une pastille).
    Menu {
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

    Menu {
        id: pageMenu

        MenuItem {
            text: "Rechercher"
            onTriggered: root.searchOpen = true
        }
        // Fin de course : sans ces deux-là, les articles cochés restent barrés à
        // l'écran pour toujours, et il faut les traiter un par un.
        MenuItem {
            text: "Tout remettre à acheter"
            enabled: AppController.items.doneCount > 0
            onTriggered: uncheckDialog.open()
        }
        MenuItem {
            text: "Retirer les articles pris"
            enabled: AppController.items.doneCount > 0
            onTriggered: clearDoneDialog.open()
        }
        MenuItem {
            text: "Réorganiser les articles"
            enabled: AppController.items.count > 1
            onTriggered: root.reorderMode = true
        }
        MenuSeparator {}
        MenuItem {
            text: "Partager la liste"
            onTriggered: shareSheet.openFor(root.listId, root.listTitle)
        }
        MenuItem {
            text: "Renommer la liste"
            onTriggered: renameDialog.open()
        }
        MenuItem {
            text: "Dupliquer la liste"
            onTriggered: duplicateDialog.open()
        }
    }

    ShareSheet { id: shareSheet }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Mode Réorganiser : un rappel discret de ce qu'on fait.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.reorderMode ? 34 : 0
            visible: Layout.preferredHeight > 0
            clip: true
            color: Theme.surfaceHigh
            Behavior on Layout.preferredHeight { NumberAnimation { duration: 140 } }

            Label {
                anchors.centerIn: parent
                text: "Glissez les articles par la poignée pour les réordonner"
                color: Theme.textDim
                font.pixelSize: 12
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width; height: 1; color: Theme.outline
            }
        }

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
                             : AppController.items.doneCount + " sur "
                               + AppController.items.count + " dans le panier")
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

        ListView {
            id: items
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: AppController.items
            topMargin: Theme.gap
            bottomMargin: Theme.gap
            spacing: 6

            // Sections par rayon. Elles n'apparaissent que si les articles sont
            // effectivement classés : qui ne s'en sert pas ne voit aucun en-tête.
            section.property: "aisle"
            section.criteria: ViewSection.FullString
            section.delegate: Item {
                required property string section
                width: items.width
                height: AppController.items.aisleCount > 1 ? 34 : 0
                visible: height > 0

                Label {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.gap + 4
                    anchors.verticalCenter: parent.verticalCenter
                    // Le rayon vide est réel (« non classé »), il lui faut un nom.
                    text: parent.section.length > 0 ? parent.section : "Sans rayon"
                    color: Theme.accent
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                }
            }

            // Ce qui reste à acheter est en haut de son rayon (tri du modèle) ; une
            // fois coché, l'article descend et s'estompe au lieu de disparaître.
            //
            // Le délégué est un conteneur immobile (la place de la ligne dans la vue,
            // et sa zone de dépôt) contenant la ligne elle-même, qui se détache sous le
            // doigt pendant un glissement. Sans cette séparation, la ligne glissée
            // suivrait le conteneur qui bouge sous elle, et le geste sauterait.
            delegate: Item {
                id: wrapper
                width: items.width
                height: root.shoppingMode ? 68 : 60

                // Surtout PAS `required property int index` : déclarer une propriété
                // requise fait basculer le délégué en mode « propriétés requises », et
                // Qt cesse alors d'injecter le contexte du modèle — tous les `model.xxx`
                // de la ligne deviennent introuvables, et la liste s'affiche vide.
                // On recopie donc l'index du contexte dans une propriété ordinaire, que
                // la zone de dépôt peut lire sur la ligne qu'on lui glisse.
                property int rowIndex: index

                readonly property bool dragging: dragHandle.drag.active

                z: dragging ? 2 : 1

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
                height: wrapper.height
                padding: 0

                readonly property bool selected: root.isSelected(model.itemId)

                Drag.active: wrapper.dragging
                Drag.source: wrapper
                Drag.hotSpot.x: width / 2
                Drag.hotSpot.y: height / 2

                opacity: wrapper.dragging ? 0.85 : 1.0

                // Pendant le glissement, la ligne quitte son conteneur pour la vue :
                // elle reste sous le doigt pendant que les autres se réorganisent.
                states: State {
                    when: wrapper.dragging
                    ParentChange { target: row; parent: items }
                }

                // Le swipe supprime : désactivé en sélection (conflit avec le geste), en
                // mode Courses (un geste de travers effacerait l'article des autres) et
                // en réorganisation (le glissement sert à déplacer).
                swipe.enabled: !root.selectionMode && !root.shoppingMode && !root.reorderMode

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
                        // Inerte en réorganisation : on ne fait que déplacer.
                        enabled: !root.reorderMode
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
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        Layout.leftMargin: 4
                        spacing: 1

                        Label {
                            Layout.fillWidth: true
                            text: model.name
                            color: model.done ? Theme.textDim : Theme.text
                            font.pixelSize: 16
                            font.strikeout: model.done
                            elide: Text.ElideRight
                        }

                        // Quantité et description sur une seule ligne : « 2 · sans sucre ».
                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: {
                                const parts = []
                                if (model.qty && model.qty.length > 0)
                                    parts.push(model.qty)
                                if (model.note && model.note.length > 0)
                                    parts.push(model.note)
                                return parts.join(" · ")
                            }
                            color: Theme.textDim
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }

                    // Poignée de déplacement, seulement en mode « Réorganiser » : au repos,
                    // une ligne se limite à case + nom + quantité. La date de l'article et
                    // sa suppression se trouvent en le touchant (dialogue), le glissement
                    // le supprime aussi.
                    MouseArea {
                        id: dragHandle
                        Layout.rightMargin: 2
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: visible ? 34 : 0
                        Layout.preferredHeight: Theme.touchTarget
                        visible: root.reorderMode

                        drag.target: visible ? row : undefined
                        drag.axis: Drag.YAxis
                        cursorShape: Qt.SizeVerCursor

                        Icon {
                            anchors.centerIn: parent
                            name: "grip"
                            color: Theme.textDim
                            size: 16
                        }
                    }
                }

                // Appui long : entrer en sélection multiple — sauf en mode Courses (on
                // ne fait que cocher) ou Réorganiser (on ne fait que glisser).
                onPressAndHold: {
                    if (!root.shoppingMode && !root.reorderMode)
                        root.toggleSelection(model.itemId)
                }
                // Mode Courses : toute la ligne est la case à cocher. Sinon, éditer —
                // ou étendre la sélection si elle est déjà commencée. En réorganisation,
                // le tap ne fait rien (on glisse par la poignée).
                onClicked: {
                    if (root.reorderMode) {
                        return
                    } else if (root.selectionMode) {
                        root.toggleSelection(model.itemId)
                    } else if (root.shoppingMode) {
                        AppController.items.toggleDone(model.itemId)
                        // On coche sans quitter le rayon des yeux : la vibration
                        // confirme le geste à la place du regard.
                        AppController.vibrate()
                    } else {
                        editDialog.openFor(model)
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
            }   // wrapper (conteneur immobile + zone de dépôt)
        }

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

        function openFor(item) {
            itemId    = item.itemId
            createdMs = item.created
            doneAtMs  = item.doneAt
            author    = item.byName
            editName.text = item.name
            editQty.text  = item.qty
            editNote.text = item.note
            editAisle.aisle = item.aisle
            open()
            editName.forceActiveFocus()
            editName.selectAll()
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
                    // L'app sait qui a ajouté quoi (chaque article porte son auteur) :
                    // autant le dire, c'est une liste partagée.
                    lines.push(editDialog.author.length > 0
                               ? "Ajouté par " + editDialog.author + " " + added
                               : "Ajouté " + added)
                }
                if (editDialog.doneAtMs > 0)
                    lines.push("Coché " + root.formatStamp(editDialog.doneAtMs))
                return lines.join("\n")
            }
        }

        // Supprimer depuis le détail : le geste de glissement n'est pas le seul chemin
        // (la croix par ligne a été retirée pour alléger l'affichage).
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
