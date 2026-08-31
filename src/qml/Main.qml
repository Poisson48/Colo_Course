import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    title: "Colo Course"
    width: 400
    height: 780
    color: Theme.background

    // Suit la préférence du système (Theme.dark), au lieu d'imposer le sombre.
    Material.theme: Theme.dark ? Material.Dark : Material.Light
    Material.background: Theme.background
    Material.foreground: Theme.text
    Material.accent: Theme.accent

    readonly property bool offline: !AppController.online

    // « Mes ajouts sont-ils partis ? » — sans réponse, on ne peut que croiser les
    // doigts jusqu'à croiser l'autre personne. En attente tant qu'un relais n'a pas
    // accusé réception ; puis « à jour », brièvement, pour confirmer.
    readonly property bool pending: AppController.pendingChanges > 0

    onPendingChanged: {
        if (!pending && !offline)
            syncedTimer.restart()
    }

    property bool showSynced: false
    Timer {
        id: syncedTimer
        interval: 2200
        onTriggered: window.showSynced = false
        onRunningChanged: if (running) window.showSynced = true
    }

    // Bouton retour Android : Qt le délivre ici comme une demande de fermeture de
    // fenêtre. Sans ce handler, il quittait l'app — depuis une liste ouverte comme
    // depuis un dialogue, ce qui ressemble à un plantage. On l'absorbe tant qu'il
    // reste quelque chose à refermer, et on ne quitte qu'à la racine.
    function handleSystemBack() {
        if (closeTopOverlay())
            return true

        const page = stack.currentItem
        if (closeTopPopup(page))
            return true
        if (page && typeof page.handleBack === "function" && page.handleBack())
            return true
        if (stack.depth > 1) {
            stack.pop()
            return true
        }
        return false
    }

    onClosing: function (close) {
        close.accepted = !handleSystemBack()
    }

    Shortcut {
        sequences: ["Back", "Escape"]
        context: Qt.ApplicationShortcut
        onActivated: handleSystemBack()
    }

    // Un Popup a pour parent visuel l'Overlay, mais reste déclaré dans la page :
    // on le retrouve dans ses `data`. Le dernier ouvert est le plus haut → fermé
    // en premier (un dialogue par-dessus le scanner, par exemple).
    function closeTopPopup(page) {
        if (!page)
            return false
        const kids = page.data
        for (let i = kids.length - 1; i >= 0; --i) {
            const child = kids[i]
            if (child && child.opened === true && typeof child.close === "function") {
                child.close()
                return true
            }
        }
        return false
    }

    // ColoDialog et la plupart des modales sont parentés à Overlay.overlay.
    function closeTopOverlay() {
        if (!window.Overlay || !window.Overlay.overlay)
            return false
        const overlay = window.Overlay.overlay
        for (let i = overlay.children.length - 1; i >= 0; --i) {
            const child = overlay.children[i]
            if (child && child.opened === true && typeof child.close === "function") {
                child.close()
                return true
            }
        }
        return false
    }

    // Barre supérieure : titre de la page courante + actions fournies par la page.
    header: Rectangle {
        color: Theme.surface
        implicitHeight: Math.max(56, headerRow.implicitHeight + 8)

        RowLayout {
            id: headerRow
            anchors.fill: parent
            anchors.leftMargin: 4
            anchors.rightMargin: 4
            spacing: 0

            ToolButton {
                Layout.preferredWidth: Theme.touchTarget
                Layout.preferredHeight: Theme.touchTarget
                visible: stack.depth > 1
                contentItem: Icon {
                    name: "back"
                    color: Theme.text
                    size: 22
                }
                // Même chemin que le bouton retour Android.
                onClicked: handleSystemBack()
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: stack.depth > 1 ? 4 : 12
                spacing: 0

                Label {
                    id: pageTitleLabel
                    Layout.fillWidth: true
                    text: stack.currentItem && stack.currentItem.pageTitle
                          ? stack.currentItem.pageTitle : "Mes listes"
                    color: Theme.text
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                             && !(stack.currentItem && stack.currentItem.pageTitleInContent)
                    text: stack.currentItem && stack.currentItem.pageSubtitle
                          ? stack.currentItem.pageSubtitle : ""
                    color: Theme.textDim
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
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

        // La pile occupe toute la hauteur : les bandeaux (hors ligne, sync, mise à
        // jour) sont en overlay au-dessus, pour ne pas pousser la liste vers le bas.
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

    // Bandeaux en overlay : semi-transparents, lisibles, sans décaler le contenu.
    Column {
        id: bannerOverlay
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        z: 10
        spacing: 0

        // Bandeau hors ligne (SPEC §3.5) : les modifs partent dans l'outbox.
        Rectangle {
            id: offlineBanner
            width: parent.width
            height: window.offline ? 40 : 0
            color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.92)
            clip: true
            visible: height > 0
            Behavior on height { NumberAnimation { duration: 160 } }

            Label {
                anchors.centerIn: parent
                width: parent.width - 16
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                text: window.offline
                      ? (AppController.pendingChanges > 0
                         ? "Hors ligne — " + AppController.pendingChanges
                           + " modification(s) en attente (listes conservées localement)"
                         : "Hors ligne — listes disponibles localement, sync au retour du réseau")
                      : ""
                color: "#1A1400"
                font.pixelSize: 12
            }
        }

        // État de synchronisation. Silencieux quand tout va bien.
        Rectangle {
            width: parent.width
            height: visible ? 26 : 0
            visible: !window.offline && (window.pending || window.showSynced)
            clip: true
            color: window.pending
                   ? Qt.rgba(Theme.surfaceHigh.r, Theme.surfaceHigh.g, Theme.surfaceHigh.b, 0.92)
                   : Qt.rgba(Theme.accentSoft.r, Theme.accentSoft.g, Theme.accentSoft.b, 0.92)

            Label {
                anchors.centerIn: parent
                width: parent.width - 16
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                color: window.pending ? Theme.textDim : Theme.accent
                font.pixelSize: 12
                text: window.pending
                      ? "Envoi de " + AppController.pendingChanges + " modification(s)…"
                      : "Tout est synchronisé"
            }
        }

        // Mise à jour disponible.
        Rectangle {
            id: updateBanner
            width: parent.width
            height: visible ? 56 : 0
            visible: Updater.updateAvailable || Updater.downloading
                     || Updater.readyToInstall
            color: Qt.rgba(Theme.surfaceHigh.r, Theme.surfaceHigh.g, Theme.surfaceHigh.b, 0.95)
            clip: true

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.outline
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 6
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: Theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        text: {
                            if (Updater.downloading)
                                return "Téléchargement…"
                            if (Updater.readyToInstall)
                                return "Version " + Updater.latestVersion + " prête"
                            return "Version " + Updater.latestVersion + " disponible"
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 3
                        radius: 2
                        visible: Updater.downloading
                        color: Theme.outline

                        Rectangle {
                            height: parent.height
                            radius: 2
                            color: Theme.accent
                            width: parent.width * Updater.progress
                            Behavior on width { NumberAnimation { duration: 120 } }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !Updater.downloading
                        elide: Text.ElideRight
                        color: Theme.textDim
                        font.pixelSize: 12
                        text: Updater.readyToInstall
                              ? "Android vous demandera confirmation"
                              : "Vous avez la " + Updater.currentVersion
                    }
                }

                Button {
                    flat: true
                    visible: !Updater.downloading
                    implicitHeight: Theme.touchTarget
                    contentItem: Label {
                        text: Updater.readyToInstall ? "Installer" : "Mettre à jour"
                        color: Theme.accent
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        if (Updater.readyToInstall)
                            Updater.install()
                        else if (Updater.releaseNotes.length > 0)
                            changelogDialog.openPending()
                        else
                            Updater.download()
                    }
                }

                ToolButton {
                    visible: !Updater.downloading
                    implicitWidth: 36
                    implicitHeight: Theme.touchTarget
                    contentItem: Icon {
                        name: "close"
                        color: Theme.textDim
                        size: 16
                    }
                    onClicked: Updater.dismiss()
                }
            }
        }
    }

    Component { id: listsPage; ListsPage {} }
    Component { id: listPage;  ListPage {} }

    // Écran allumé tant que l'app est ouverte (courses en rayon, téléphone posé).
    Component.onCompleted: AppController.setKeepScreenOn(true)
    Component.onDestruction: AppController.setKeepScreenOn(false)

    Connections {
        target: AppController
        function onListOpened(listId, title) {
            stack.push(listPage, { listId: listId, listTitle: title })
        }
        function onToast(message) {
            snackbar.show(message)
        }
    }

    Connections {
        target: Updater
        function onChangelogChanged() {
            if (Updater.hasWhatsNew && !Updater.updateAvailable
                    && !whatsNewDialog.opened && !changelogDialog.opened)
                Qt.callLater(function() { whatsNewDialog.openWhatsNew() })
        }
    }

    ChangelogDialog { id: changelogDialog }
    ChangelogDialog { id: whatsNewDialog }

    function openChangelog() { changelogDialog.openHistory() }

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
