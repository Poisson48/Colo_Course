import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Sélecteur « pour N personnes » réutilisable (catalogue, recette, import courses).
RowLayout {
    id: root

    property int value: 4
    property int minimum: 1
    property int maximum: 99

    spacing: 8

    Label {
        text: "Pour"
        color: Theme.textDim
        font.pixelSize: 14
    }

    ToolButton {
        implicitWidth: 40
        implicitHeight: 40
        enabled: root.value > root.minimum
        contentItem: Label {
            text: "−"
            color: Theme.text
            font.pixelSize: 20
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        onClicked: root.value = Math.max(root.minimum, root.value - 1)
    }

    Label {
        text: root.value.toString()
        color: Theme.text
        font.pixelSize: 18
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        Layout.minimumWidth: 28
    }

    ToolButton {
        implicitWidth: 40
        implicitHeight: 40
        enabled: root.value < root.maximum
        contentItem: Label {
            text: "+"
            color: Theme.text
            font.pixelSize: 20
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        onClicked: root.value = Math.min(root.maximum, root.value + 1)
    }

    Label {
        text: "personnes"
        color: Theme.textDim
        font.pixelSize: 14
    }
}
