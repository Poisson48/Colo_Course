import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ColoCourse

ApplicationWindow {
    id: window
    visible: true
    title: "Colo Course"
    width: 400
    height: 780
    color: Theme.background

    Material.theme: Material.Dark
    Material.background: Theme.background
    Material.foreground: Theme.text
    Material.accent: Theme.accent

    readonly property bool offline: !AppController.online

    // Barre supérieure : titre de la page courante + actions fournies par la page.
    header: Rectangle {
        color: Theme.surface
        implicitHeight: 56

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4
            anchors.rightMargin: 4
            spacing: 0

            ToolButton {
                Layout.preferredWidth: Theme.touchTarget
                Layout.preferredHeight: Theme.touchTarget
                visible: stack.depth > 1
                contentItem: Text {
                    text: "←"          // ←
                    font.pixelSize: 22
                    color: Theme.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: stack.pop()
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: stack.depth > 1 ? 4 : 12
                text: stack.currentItem && stack.currentItem.pageTitle
                      ? stack.currentItem.pageTitle : "Mes listes"
                color: Theme.text
                font.pixelSize: 20
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            // Chaque page expose ses propres boutons via `property Component actions`.
            Loader {
                Layout.alignment: Qt.AlignVCenter
                sourceComponent: stack.currentItem && stack.currentItem.actions
                                 ? stack.currentItem.actions : null
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.outline
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Bandeau hors ligne (SPEC §3.5) : les modifs partent dans l'outbox.
        Rectangle {
            id: offlineBanner
            Layout.fillWidth: true
            Layout.preferredHeight: window.offline ? 32 : 0
            color: Theme.warning
            clip: true
            visible: Layout.preferredHeight > 0
            Behavior on Layout.preferredHeight { NumberAnimation { duration: 160 } }

            Label {
                anchors.centerIn: parent
                width: parent.width - 16
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: "Hors ligne — les modifications partiront au retour du réseau"
                color: "#1A1400"
                font.pixelSize: 12
            }
        }

        StackView {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            initialItem: listsPage

            pushEnter: Transition { NumberAnimation { property: "x"; from: width; to: 0; duration: 180; easing.type: Easing.OutCubic } }
            pushExit:  Transition { NumberAnimation { property: "opacity"; to: 0.0; duration: 140 } }
            popEnter:  Transition { NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 140 } }
            popExit:   Transition { NumberAnimation { property: "x"; to: width; duration: 180; easing.type: Easing.OutCubic } }
        }
    }

    Component { id: listsPage; ListsPage {} }
    Component { id: listPage;  ListPage {} }

    Connections {
        target: AppController
        function onListOpened(listId, title) {
            stack.push(listPage, { listId: listId, listTitle: title })
        }
        function onToast(message) {
            snackbar.show(message)
        }
    }

    // Snackbar : retour visuel court (lien copié, liste rejointe, erreur…).
    Popup {
        id: snackbar
        y: parent.height - height - 24
        x: 16
        width: parent.width - 32
        padding: 14
        modal: false
        closePolicy: Popup.NoAutoClose

        property string message: ""
        function show(text) {
            message = text
            open()
            hideTimer.restart()
        }

        Timer { id: hideTimer; interval: 2800; onTriggered: snackbar.close() }

        background: Rectangle {
            color: Theme.surfaceHigh
            radius: 12
            border.color: Theme.outline
            border.width: 1
        }

        contentItem: Label {
            text: snackbar.message
            color: Theme.text
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }
    }
}
