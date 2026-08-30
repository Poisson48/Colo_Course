import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Lecture d'une recette : personnes, puis onglets Ingrédients / Préparation.
Item {
    id: root

    required property string listId
    property bool cookMode: false
    property string filterText: ""

    signal ingredientClicked(var item)
    signal editPrepRequested()

    property int contentTab: 0

    readonly property string prepText: AppController.recipeInstructions(root.listId)
    readonly property var prepSteps: {
        const out = []
        if (!prepText || prepText.length === 0)
            return out
        const lines = prepText.split("\n")
        for (let i = 0; i < lines.length; ++i) {
            const line = lines[i].trim()
            if (line.length > 0)
                out.push(line)
        }
        return out
    }

    property int cookStep: 0

    onCookModeChanged: cookStep = 0
    onPrepTextChanged: cookStep = 0

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

    function detailLine(qty, note) {
        const parts = []
        if (qty && qty.length > 0)
            parts.push(qty)
        if (note && note.length > 0)
            parts.push(note)
        return parts.join(" \u00b7 ")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Personnes (toujours visible)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Theme.surface

            ServingsStepper {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.pad
                anchors.rightMargin: Theme.pad
                value: AppController.recipeTargetServings(root.listId)
                baseServings: AppController.recipeBaseServings(root.listId)
                onValueChanged: AppController.setRecipeTargetServings(root.listId, value)
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.outline
            }
        }

        // Mode cuisine : étape courante, tout l'écran (onglets masqués).
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: root.cookMode && root.prepSteps.length > 0
            Layout.preferredHeight: root.cookMode && root.prepSteps.length > 0 ? 0 : 0
            visible: root.cookMode && root.prepSteps.length > 0
            color: Theme.surface

            ColumnLayout {
                id: cookPanel
                anchors.fill: parent
                anchors.margins: Theme.pad
                spacing: 16

                Label {
                    Layout.fillWidth: true
                    text: "Étape " + (root.cookStep + 1) + " / " + root.prepSteps.length
                    color: Theme.textDim
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    text: root.prepSteps.length > root.cookStep
                          ? root.prepSteps[root.cookStep] : ""
                    color: Theme.text
                    font.pixelSize: 22
                    font.weight: Font.Medium
                    wrapMode: Text.WordWrap
                    lineHeight: 1.48
                    lineHeightMode: Text.ProportionalHeight
                    Layout.fillHeight: true
                    verticalAlignment: Text.AlignTop
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        Layout.fillWidth: true
                        flat: true
                        enabled: root.cookStep > 0
                        implicitHeight: 44
                        contentItem: Label {
                            text: "Précédent"
                            color: parent.enabled ? Theme.accent : Theme.textDim
                            horizontalAlignment: Text.AlignHCenter
                        }
                        onClicked: root.cookStep = Math.max(0, root.cookStep - 1)
                    }

                    Button {
                        Layout.fillWidth: true
                        flat: true
                        enabled: root.cookStep < root.prepSteps.length - 1
                        implicitHeight: 44
                        contentItem: Label {
                            text: "Suivant"
                            color: parent.enabled ? Theme.accent : Theme.textDim
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                        }
                        onClicked: root.cookStep = Math.min(root.prepSteps.length - 1,
                                                           root.cookStep + 1)
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

        TabBar {
            id: contentTabBar
            Layout.fillWidth: true
            visible: !root.cookMode
            Material.accent: Theme.accent
            currentIndex: root.contentTab
            onCurrentIndexChanged: root.contentTab = currentIndex

            TabButton {
                text: AppController.items.count === 0
                      ? "Ingrédients"
                      : ("Ingrédients (" + AppController.items.count + ")")
            }
            TabButton {
                text: "Préparation"
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.cookMode
            currentIndex: root.contentTab

            // --- Onglet ingrédients ---
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: recipeScroll
                    objectName: "recipeScroll"
                    anchors.fill: parent
                    clip: true
                    model: AppController.items
                    spacing: 4
                    topMargin: 4
                    bottomMargin: Theme.gap
                    boundsBehavior: Flickable.StopAtBounds

                    property real savedContentY: 0
                    Connections {
                        target: AppController.items
                        function onModelAboutToBeReset() {
                            recipeScroll.savedContentY = recipeScroll.contentY
                        }
                        function onModelReset() {
                            Qt.callLater(function() {
                                recipeScroll.contentY = recipeScroll.savedContentY
                                recipeScroll.returnToBounds()
                            })
                        }
                    }

                    delegate: ItemDelegate {
                        id: ingRow
                        required property string itemId
                        required property string name
                        required property string qty
                        required property string note

                        width: recipeScroll.width - 2 * Theme.gap
                        x: Theme.gap
                        padding: 12

                        background: Rectangle {
                            radius: 10
                            color: ingRow.pressed ? Theme.surfaceHigh : Theme.surface
                            border.color: Theme.outline
                            border.width: 1
                        }

                        contentItem: Row {
                            width: ingRow.availableWidth
                            spacing: 10

                            Label {
                                text: "•"
                                color: Theme.accent
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                                anchors.top: parent.top
                                anchors.topMargin: 2
                            }

                            Column {
                                width: parent.width - 26
                                spacing: 2

                                Label {
                                    width: parent.width
                                    text: root.filterText.length > 0
                                          ? root.highlightPlain(ingRow.name, root.filterText)
                                          : ingRow.name
                                    textFormat: root.filterText.length > 0
                                                ? Text.StyledText : Text.PlainText
                                    color: Theme.text
                                    font.pixelSize: 16
                                    wrapMode: Text.WordWrap
                                }

                                Label {
                                    width: parent.width
                                    visible: text.length > 0
                                    text: {
                                        const d = root.detailLine(ingRow.qty, ingRow.note)
                                        return root.filterText.length > 0
                                               ? root.highlightPlain(d, root.filterText) : d
                                    }
                                    textFormat: root.filterText.length > 0
                                                ? Text.StyledText : Text.PlainText
                                    color: Theme.textDim
                                    font.pixelSize: 13
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 3
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        onClicked: root.ingredientClicked({
                            itemId: ingRow.itemId,
                            name: ingRow.name,
                            qty: ingRow.qty,
                            note: ingRow.note
                        })
                    }
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    width: parent.width - 64
                    visible: AppController.items.count === 0
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: root.filterText.length > 0 ? "Aucun résultat" : "Aucun ingrédient"
                        color: Theme.text
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: root.filterText.length > 0
                              ? "Aucun ingrédient ne correspond à « " + root.filterText + " »."
                              : "Ajoutez le premier ingrédient dans la barre ci-dessous."
                        color: Theme.textDim
                        font.pixelSize: 14
                    }
                }
            }

            // --- Onglet préparation ---
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.pad
                    spacing: 8
                    visible: root.prepText.length > 0

                    RowLayout {
                        Layout.fillWidth: true

                        Item { Layout.fillWidth: true }

                        ToolButton {
                            implicitHeight: 32
                            contentItem: Label {
                                text: "Modifier"
                                color: Theme.textDim
                                font.pixelSize: 13
                            }
                            onClicked: root.editPrepRequested()
                        }
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        Column {
                            id: prepCol
                            width: parent.width
                            spacing: 12

                            Repeater {
                                model: root.prepSteps
                                delegate: RowLayout {
                                    required property int index
                                    required property string modelData
                                    width: prepCol.width
                                    spacing: 8

                                    Label {
                                        text: (index + 1) + "."
                                        color: Theme.accent
                                        font.pixelSize: 15
                                        font.weight: Font.DemiBold
                                        Layout.alignment: Qt.AlignTop
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData
                                        color: Theme.text
                                        font.pixelSize: 15
                                        wrapMode: Text.WordWrap
                                        lineHeight: 1.45
                                        lineHeightMode: Text.ProportionalHeight
                                    }
                                }
                            }
                        }
                    }
                }

                // Pas de préparation
                Item {
                    anchors.fill: parent
                    visible: root.prepText.length === 0

                    Label {
                        anchors.centerIn: parent
                        text: "＋  Ajouter la préparation"
                        color: Theme.accent
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.editPrepRequested()
                    }
                }
            }
        }
    }
}
