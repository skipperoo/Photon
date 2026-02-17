import QtQuick
import QtQuick.Controls
import Main

Button {
    id: control
    text: "Button"

    property bool variantOutline: false
    property bool variantDestructive: false

    contentItem: Text {
        text: control.text
        font: Theme.fontMedium
        color: {
            if (control.variantDestructive) return "#ffffff"
            if (control.variantOutline) return Theme.foreground
            return Theme.primaryFg
        }
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: 100
        implicitHeight: 40
        radius: Theme.radius
        border.width: control.variantOutline ? 1 : 0
        border.color: Theme.border
        color: {
            if (!control.enabled) return Theme.mutedFg
            if (control.variantDestructive) {
                return control.down ? Qt.darker(Theme.destructive, 1.2) :
                       control.hovered ? Qt.lighter(Theme.destructive, 1.2) : Theme.destructive
            }
            if (control.variantOutline) {
                return control.down ? Theme.accent :
                       control.hovered ? Theme.accent : "transparent"
            }
            return control.down ? Qt.darker(Theme.primary, 1.1) :
                   control.hovered ? "#e4e4e7" : Theme.primary
        }
        Behavior on color { ColorAnimation { duration: 150 } }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onPressed: (mouse) => mouse.accepted = false
    }
}
