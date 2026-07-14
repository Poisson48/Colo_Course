import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    title: "Colo_Course"
    width: 400
    height: 700
    Material.primary: "#2196F3"
    Material.accent: "#1976D2"

    // Branchement: AppController.online (false = hors ligne, bandeau visible)
    property bool isOffline: !AppController.online

    // Quand l'utilisateur clique sur une liste, AppController émet listOpened.
    Connections {
        target: AppController
        function onListOpened(listId, title) {
            pageTitle.text = title
            stack.push(listPageComponent, { listId: listId, listName: title })
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Bandeau hors ligne
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: isOffline ? 40 : 0
            color: "#FFC107"
            visible: isOffline

            Layout.margins: 0

            Text {
                anchors.centerIn: parent
                text: "Hors ligne — modifications en attente"
                font.pixelSize: 12
                color: "#333333"
            }

            Behavior on implicitHeight {
                NumberAnimation { duration: 200 }
            }
        }

        // En-tête avec titre et retour
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 56
            color: Material.primary

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 16
                spacing: 12

                Button {
                    id: backButton
                    text: "←"
                    flat: true
                    Material.foreground: "white"
                    visible: stack.depth > 1
                    onClicked: stack.pop()
                }

                Text {
                    id: pageTitle
                    text: "Listes de courses"
                    font.pixelSize: 18
                    font.weight: Font.Medium
                    color: "white"
                    Layout.fillWidth: true
                }
            }
        }

        // Contenu principal (StackView)
        StackView {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true

            Component.onCompleted: {
                stack.push(listsPageComponent)
            }

            Component {
                id: listsPageComponent
                ListsPage {
                    onListSelected: function(listId, listName) {
                        AppController.openList(listId)
                    }
                    onCreateListRequested: function(name) {
                        AppController.createList(name)
                    }
                    onJoinListRequested: function(key) {
                        // stub — implémenté en tâche 4.2 (QR / appairage)
                    }
                }
            }

            Component {
                id: listPageComponent
                ListPage {}
            }

            pushEnter: Transition {
                PropertyAnimation {
                    property: "x"
                    from: width
                    to: 0
                    duration: 200
                }
            }

            pushExit: Transition {
                PropertyAnimation {
                    property: "x"
                    to: -width / 3
                    duration: 200
                }
                PropertyAnimation {
                    property: "opacity"
                    to: 0.5
                    duration: 200
                }
            }

            popEnter: Transition {
                PropertyAnimation {
                    property: "x"
                    from: -width / 3
                    to: 0
                    duration: 200
                }
                PropertyAnimation {
                    property: "opacity"
                    from: 0.5
                    to: 1.0
                    duration: 200
                }
            }

            popExit: Transition {
                PropertyAnimation {
                    property: "x"
                    to: width
                    duration: 200
                }
            }
        }
    }
}
