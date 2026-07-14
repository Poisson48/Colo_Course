import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root
    required property string listName

    // stub — remplacé en 3.2
    property ListModel itemsModel: ListModel {
        ListElement { name: "Lait"; qty: "1L"; done: false; created: 1000 }
        ListElement { name: "Pain"; qty: ""; done: false; created: 1001 }
        ListElement { name: "Fromage"; qty: "200g"; done: true; created: 1002 }
        ListElement { name: "Beurre"; qty: ""; done: true; created: 1003 }
    }

    // Modèle trié : non cochés d'abord, puis par created croissant
    property ListModel sortedItemsModel: ListModel {}

    Component.onCompleted: {
        updateSortedModel()
    }

    Connections {
        target: root.itemsModel
        function onItemsChanged() {
            root.updateSortedModel()
        }
    }

    function updateSortedModel() {
        // Reconstruire le modèle trié
        sortedItemsModel.clear()

        // Extraire tous les items en liste JS
        let items = []
        for (let i = 0; i < itemsModel.count; i++) {
            items.push({
                name: itemsModel.get(i).name,
                qty: itemsModel.get(i).qty,
                done: itemsModel.get(i).done,
                created: itemsModel.get(i).created,
                index: i
            })
        }

        // Trier : non cochés d'abord, puis par created croissant
        items.sort((a, b) => {
            if (a.done !== b.done) {
                return a.done ? 1 : -1  // false (non coché) avant true (coché)
            }
            return a.created - b.created
        })

        // Repeupler le modèle
        for (let item of items) {
            sortedItemsModel.append(item)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Liste des articles
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.sortedItemsModel
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
                            root.itemToggleRequested(model.index)
                            model.done = checked
                            root.updateSortedModel()
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
                            color: model.done ? Material.foreground : Material.foreground
                            opacity: model.done ? 0.6 : 1.0
                            Layout.fillWidth: true
                        }

                        Text {
                            text: model.qty
                            font.pixelSize: 12
                            color: Material.foreground
                            opacity: 0.6
                            visible: model.qty.length > 0
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
                                root.itemDeleteRequested(model.index)
                                root.itemsModel.remove(model.index)
                                root.updateSortedModel()
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
                            root.itemAddRequested(nameField.text, qtyField.text)
                            root.itemsModel.append({
                                name: nameField.text,
                                qty: qtyField.text,
                                done: false,
                                created: Date.now()
                            })
                            root.updateSortedModel()
                            nameField.text = ""
                            qtyField.text = ""
                            nameField.forceActiveFocus()
                        }
                    }
                }
            }
        }
    }

    // Signaux (stubs)
    signal itemToggleRequested(int index)
    signal itemDeleteRequested(int index)
    signal itemAddRequested(string name, string qty)
}
