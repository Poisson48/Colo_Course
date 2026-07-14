import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root
    required property string listName
    // listId is set before push (used to distinguish lists; ItemModel already loaded)
    property string listId: ""

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Liste des articles — branchée sur ItemModel (context property)
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: ItemModel
            spacing: 8
            topMargin: 12
            bottomMargin: 12
            leftMargin: 12
            rightMargin: 12

            delegate: SwipeDelegate {
                id: delegate
                width: listView.width - 24
                implicitHeight: 60

                // Contenu principal
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    // Checkbox (tap pour cocher)
                    CheckBox {
                        checked: model.done
                        onToggled: {
                            ItemModel.toggleDone(model.itemId)
                        }
                    }

                    // Texte de l'article
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 2

                        Text {
                            text: model.name
                            font.pixelSize: 14
                            font.strikeout: model.done
                            color: Material.foreground
                            opacity: model.done ? 0.6 : 1.0
                            Layout.fillWidth: true
                        }

                        Text {
                            text: model.qty
                            font.pixelSize: 12
                            color: Material.foreground
                            opacity: 0.6
                            visible: model.qty !== undefined && model.qty.length > 0
                            Layout.fillWidth: true
                        }
                    }
                }

                // Bouton de suppression (swipe)
                swipe.right: Rectangle {
                    width: parent.width
                    height: parent.height
                    color: Material.backgroundColor
                    anchors.right: parent.right

                    RowLayout {
                        anchors.fill: parent
                        anchors.rightMargin: 12
                        spacing: 8

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "Supprimer"
                            Material.foreground: Material.red
                            onClicked: {
                                ItemModel.removeItem(model.itemId)
                                delegate.swipe.close()
                            }
                        }
                    }
                }
            }
        }

        // Champ d'ajout rapide en bas
        Rectangle {
            Layout.fillWidth: true
            color: Material.backgroundColor
            implicitHeight: contentRow.implicitHeight + 24

            RowLayout {
                id: contentRow
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: "Nom de l'article"
                    Material.accent: Material.primary
                    onAccepted: addItemButton.clicked()
                }

                TextField {
                    id: qtyField
                    Layout.preferredWidth: 80
                    placeholderText: "Qté (opt.)"
                    Material.accent: Material.primary
                    onAccepted: addItemButton.clicked()
                }

                Button {
                    id: addItemButton
                    text: "+"
                    onClicked: {
                        if (nameField.text.length > 0) {
                            ItemModel.addItem(nameField.text, qtyField.text)
                            nameField.text = ""
                            qtyField.text = ""
                            nameField.forceActiveFocus()
                        }
                    }
                }
            }
        }
    }
}
