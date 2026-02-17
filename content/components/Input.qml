import QtQuick
import QtQuick.Controls
import Main

TextField {
    id: control
    placeholderText: "Type something..."
    font: Theme.fontRegular
    color: Theme.foreground
    placeholderTextColor: Theme.mutedFg
    selectionColor: Theme.primary
    selectedTextColor: Theme.primaryFg
    leftPadding: 12
    rightPadding: 12
    topPadding: 10
    bottomPadding: 10
    verticalAlignment: Text.AlignVCenter

    background: Rectangle {
        implicitWidth: 240
        implicitHeight: 40
        color: "transparent"
        radius: Theme.radius
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.ring : Theme.input
        Behavior on border.color { ColorAnimation { duration: 100 } }
    }
}
