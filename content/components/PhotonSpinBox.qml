import QtQuick
import QtQuick.Controls
import Main

SpinBox {
    id: control

    implicitWidth: 140
    implicitHeight: 36
    editable: true

    contentItem: TextInput {
        text: control.textFromValue(control.value, control.locale)
        font: Theme.fontRegular
        color: Theme.foreground
        selectionColor: Theme.accent
        selectedTextColor: Theme.primaryFg
        horizontalAlignment: Qt.AlignLeft
        verticalAlignment: Qt.AlignVCenter
        leftPadding: 12
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }

    up.indicator: Rectangle {
        x: control.width - width - 2
        y: 2
        implicitWidth: 26
        implicitHeight: control.height / 2 - 2
        radius: Theme.radius
        color: control.up.pressed ? Theme.highlight : "transparent"

        Text {
            anchors.centerIn: parent
            text: "+"
            color: control.enabled ? Theme.mutedFg : Theme.border
            font: Theme.fontRegular
        }
    }

    down.indicator: Rectangle {
        x: control.width - width - 2
        y: control.height / 2
        implicitWidth: 26
        implicitHeight: control.height / 2 - 2
        radius: Theme.radius
        color: control.down.pressed ? Theme.highlight : "transparent"

        Text {
            anchors.centerIn: parent
            text: "\u2212"
            color: control.enabled ? Theme.mutedFg : Theme.border
            font: Theme.fontRegular
        }
    }

    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.secondary
        border.color: control.activeFocus ? Theme.accent : Theme.border
        border.width: 1
    }
}
