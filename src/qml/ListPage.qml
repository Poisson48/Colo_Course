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

    readonly property string pageTitle: selectionMode
        ? selectedIds.length + (selectedIds.length > 1 ? " sélectionnés" : " sélectionné")
        : listTitle

    // Retour (bouton Android ou flèche) : défaire l'état courant avant de quitter la
    // page — la sélection d'abord, puis le mode Courses.
    function handleBack() {
        if (selectionMode) {
            selectedIds = []
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

        ToolButton {
            width: 88
            height: Theme.touchTarget
            visible: !root.selectionMode && !root.shoppingMode
            contentItem: Label {
                text: "Partager"
                color: Theme.accent
                font.pixelSize: 14
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: shareSheet.openFor(root.listId, root.listTitle)
        }

        ToolButton {
            width: Theme.touchTarget
            height: Theme.touchTarget
            visible: !root.selectionMode && !root.shoppingMode
            contentItem: Label {
                text: "⋮"
                color: Theme.text
                font.pixelSize: 20
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: pageMenu.popup()
        }
    }

    Menu {
        id: pageMenu

        MenuItem {
            text: "Mode courses"
            onTriggered: root.shoppingMode = true
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
                             ? "Tout est dans le panier 🎉"
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

        ListView {
            id: items
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: AppController.items
            topMargin: Theme.gap
            bottomMargin: Theme.gap
            spacing: 6

            // Ce qui reste à acheter est en haut (tri du modèle) ; une fois coché,
            // l'article descend et s'estompe au lieu de disparaître.
            delegate: SwipeDelegate {
                id: row
                width: items.width - 2 * Theme.gap
                x: Theme.gap
                // Plus haute en mode Courses : on la vise d'une main, en marchant.
                height: root.shoppingMode ? 68 : 60
                padding: 0

                readonly property bool selected: root.isSelected(model.itemId)

                // Le swipe supprime : en sélection il entrerait en conflit avec le
                // geste de sélection, et en mode Courses on ne supprime rien du tout —
                // un geste de travers dans le magasin effacerait l'article des autres.
                swipe.enabled: !root.selectionMode && !root.shoppingMode

                background: Rectangle {
                    radius: 12
                    color: row.selected ? Theme.accentDim
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

                    // Quand l'article a été ajouté, ou coché s'il l'est : l'info était
                    // enterrée dans le dialogue d'édition, elle se lit ici.
                    Label {
                        Layout.rightMargin: root.shoppingMode || root.selectionMode ? 14 : 0
                        Layout.alignment: Qt.AlignVCenter
                        text: model.done ? root.formatShort(model.doneAt)
                                         : root.formatShort(model.created)
                        color: Theme.textDim
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignRight
                    }

                    // Repli tactile pour qui ne devine pas le swipe. Absent en mode
                    // Courses : rien ne doit pouvoir être supprimé d'un doigt distrait.
                    ToolButton {
                        Layout.rightMargin: 4
                        Layout.alignment: Qt.AlignVCenter
                        visible: !root.selectionMode && !root.shoppingMode
                        width: Theme.touchTarget
                        height: Theme.touchTarget
                        contentItem: Label {
                            text: "×"
                            color: Theme.textDim
                            font.pixelSize: 15
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: deleteDialog.openFor([model.itemId], model.name)
                    }
                }

                // Appui long : entrer en sélection multiple — sauf en mode Courses, où
                // la seule action possible est de cocher.
                onPressAndHold: {
                    if (!root.shoppingMode)
                        root.toggleSelection(model.itemId)
                }
                // Mode Courses : toute la ligne est la case à cocher. Sinon, éditer —
                // ou étendre la sélection si elle est déjà commencée.
                onClicked: {
                    if (root.selectionMode)
                        root.toggleSelection(model.itemId)
                    else if (root.shoppingMode)
                        AppController.items.toggleDone(model.itemId)
                    else
                        editDialog.openFor(model)
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
            }
        }

        // État vide de la liste ouverte.
        ColumnLayout {
            visible: items.count === 0
            Layout.alignment: Qt.AlignCenter
            Layout.fillHeight: true
            Layout.leftMargin: 40
            Layout.rightMargin: 40
            spacing: 6

            Item { Layout.fillHeight: true }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: "Liste vide"
                color: Theme.text
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: "Ajoutez un premier article ci-dessous."
                color: Theme.textDim
                font.pixelSize: 14
            }

            Item { Layout.fillHeight: true }
        }

        // Barre d'ajout : collée en bas, au-dessus du clavier (adjustResize).
        // Masquée en sélection (on supprime, on n'ajoute pas) et en mode Courses
        // (le clavier n'a rien à faire là, et la liste doit rester entièrement visible).
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 72
            visible: !root.selectionMode && !root.shoppingMode
            color: Theme.surface

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.outline
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                ColoTextField {
                    id: nameField
                    Layout.fillWidth: true
                    hint: "Ajouter un article"
                    onAccepted: root.addItem()
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
                        color: addButton.enabled ? "#0C1F10" : Theme.textDim
                        font.pixelSize: 24
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: root.addItem()
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

    function addItem() {
        const name = nameField.text.trim()
        if (name.length === 0)
            return
        AppController.items.addItem(name, qtyField.text.trim())
        nameField.text = ""
        qtyField.text = ""
        nameField.forceActiveFocus()
    }

    // Date courte, pour la coller au bout d'une ligne sans l'encombrer : « 14:20 »
    // aujourd'hui, « hier », puis « 3 juil. ».
    function formatShort(ms) {
        if (!ms || ms <= 0)
            return ""
        const d = new Date(ms)
        const now = new Date()
        if (d.toDateString() === now.toDateString())
            return Qt.formatDateTime(d, "HH:mm")
        if (new Date(now.getTime() - 86400000).toDateString() === d.toDateString())
            return "hier"
        return Qt.formatDateTime(d, "d MMM")
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

        function openFor(item) {
            itemId    = item.itemId
            createdMs = item.created
            doneAtMs  = item.doneAt
            editName.text = item.name
            editQty.text  = item.qty
            editNote.text = item.note
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

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 4
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 12
            text: {
                const lines = []
                const added = root.formatStamp(editDialog.createdMs)
                if (added.length > 0)
                    lines.push("Ajouté " + added)
                if (editDialog.doneAtMs > 0)
                    lines.push("Coché " + root.formatStamp(editDialog.doneAtMs))
                return lines.join("\n")
            }
        }

        onAccepted: AppController.items.editItem(editDialog.itemId,
                                                 editName.text.trim(),
                                                 editQty.text.trim(),
                                                 editNote.text.trim())
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
