import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root

    required property string listId
    required property string listTitle

    readonly property string pageTitle: listTitle

    property Component actions: ToolButton {
        width: 92
        height: Theme.touchTarget
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

    ShareSheet { id: shareSheet }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

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
                height: 60
                padding: 0

                background: Rectangle {
                    radius: 12
                    color: row.pressed ? Theme.surfaceHigh : Theme.surface
                    border.color: Theme.outline
                    border.width: 1
                }

                contentItem: RowLayout {
                    spacing: 0

                    CheckBox {
                        Layout.leftMargin: 6
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: Theme.touchTarget
                        implicitHeight: Theme.touchTarget
                        checked: model.done
                        Material.accent: Theme.accent
                        // onToggled et pas onCheckedChanged : ce dernier repart en
                        // boucle quand le modèle se recharge après un merge distant.
                        onToggled: AppController.items.toggleDone(model.itemId)
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

                        Label {
                            Layout.fillWidth: true
                            visible: model.qty && model.qty.length > 0
                            text: model.qty
                            color: Theme.textDim
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }

                    // Repli tactile pour qui ne devine pas le swipe.
                    ToolButton {
                        Layout.rightMargin: 4
                        Layout.alignment: Qt.AlignVCenter
                        width: Theme.touchTarget
                        height: Theme.touchTarget
                        contentItem: Label {
                            text: "✕"
                            color: Theme.textDim
                            font.pixelSize: 15
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: AppController.items.removeItem(model.itemId)
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

                    SwipeDelegate.onClicked: AppController.items.removeItem(model.itemId)
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
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 72
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
                    placeholderText: "Ajouter un article"
                    onAccepted: root.addItem()
                }

                ColoTextField {
                    id: qtyField
                    Layout.preferredWidth: 76
                    placeholderText: "Qté"
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

    function addItem() {
        const name = nameField.text.trim()
        if (name.length === 0)
            return
        AppController.items.addItem(name, qtyField.text.trim())
        nameField.text = ""
        qtyField.text = ""
        nameField.forceActiveFocus()
    }
}
